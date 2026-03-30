/**
 * @file src/bitrate_controller.h
 * @brief Header-only adaptive bitrate controller for Apollo streaming.
 */
#pragma once

#include <chrono>
#include <cmath>
#include <algorithm>
#include <atomic>

namespace stream {

  class bitrate_controller_t {
  public:
    struct config_t {
      bool adaptive_bitrate = true;
      bool adaptive_fec = true;
      bool frame_pacing = true;
      bool thermal_protection = true;
      int min_bitrate_kbps = 2000;
      int max_bitrate_kbps = 40000;
      int min_fec_percentage = 10;
      int max_fec_percentage = 50;
      int max_pacing_buffer_ms = 4;
      int thermal_step_down_resolution = 1080;
      int thermal_step_down_fps = 30;
      int thermal_recovery_delay_s = 30;
      int wifi_preemptive_drop_threshold = 2;
    };

    void init(int initial_bitrate_kbps, config_t cfg) {
      _cfg = cfg;
      _current_bitrate_kbps = initial_bitrate_kbps;
      _target_bitrate_kbps = initial_bitrate_kbps;
      _loss_window_idx = 0;
      _loss_window_count = 0;
      _loss_ema = 0.0f;
      _fec_percentage = _cfg.min_fec_percentage + (_cfg.max_fec_percentage - _cfg.min_fec_percentage) / 2;
      _jitter_idx = 0;
      _jitter_count = 0;
      _pacing_buffer_us = 0;
      _idr_requests_last_minute = 0;
      _idr_minute_start = std::chrono::steady_clock::now();
      _thermal_state = 0;
      _resolution_stepped_down = false;
      _fps_stepped_down = false;
      _wifi_quality = 4;
      _prev_wifi_quality = 4;
      _wifi_preemptive_active = false;
      _last_loss_time = std::chrono::steady_clock::now();
      _last_zero_loss_time = std::chrono::steady_clock::now();
      _thermal_last_change = std::chrono::steady_clock::now();
    }

    void on_loss_stats(int packets_sent, int packets_lost) {
      if (!_cfg.adaptive_bitrate && !_cfg.adaptive_fec) {
        return;
      }

      auto now = std::chrono::steady_clock::now();

      // Add to rolling window
      _loss_window[_loss_window_idx] = {packets_sent, packets_lost, now};
      _loss_window_idx = (_loss_window_idx + 1) % LOSS_WINDOW_SIZE;
      if (_loss_window_count < LOSS_WINDOW_SIZE) {
        _loss_window_count++;
      }

      float loss_rate = compute_loss_rate();
      _last_loss_time = now;

      // Track zero-loss streaks
      if (loss_rate == 0.0f) {
        // _last_zero_loss_time stays as-is (it marks start of zero-loss period)
      }
      else {
        _last_zero_loss_time = now;
      }

      // Adaptive bitrate adjustment
      if (_cfg.adaptive_bitrate) {
        if (loss_rate > 0.05f) {
          // Heavy loss: drop 30%
          _target_bitrate_kbps = static_cast<int>(_target_bitrate_kbps * 0.7f);
          _target_bitrate_kbps = std::max(_target_bitrate_kbps, _cfg.min_bitrate_kbps);
        }
        else if (loss_rate > 0.01f) {
          // Moderate loss: drop 10%
          _target_bitrate_kbps = static_cast<int>(_target_bitrate_kbps * 0.9f);
          _target_bitrate_kbps = std::max(_target_bitrate_kbps, _cfg.min_bitrate_kbps);
        }
        else if (loss_rate == 0.0f) {
          auto zero_duration = std::chrono::duration_cast<std::chrono::seconds>(now - _last_zero_loss_time);
          if (zero_duration.count() >= 3) {
            // No loss for 3+ seconds: ramp up 10%
            _target_bitrate_kbps = static_cast<int>(_target_bitrate_kbps * 1.1f);
            _target_bitrate_kbps = std::min(_target_bitrate_kbps, _cfg.max_bitrate_kbps);
          }
        }

        _current_bitrate_kbps = _target_bitrate_kbps;
      }

      // Update loss EMA for FEC
      if (_cfg.adaptive_fec) {
        _loss_ema = 0.3f * loss_rate + 0.7f * _loss_ema;

        // FEC percentage based on EMA thresholds
        if (_loss_ema > 0.10f) {
          _fec_percentage = _cfg.max_fec_percentage;
        }
        else if (_loss_ema > 0.05f) {
          _fec_percentage = _cfg.min_fec_percentage + (_cfg.max_fec_percentage - _cfg.min_fec_percentage) * 3 / 4;
        }
        else if (_loss_ema > 0.02f) {
          _fec_percentage = _cfg.min_fec_percentage + (_cfg.max_fec_percentage - _cfg.min_fec_percentage) / 2;
        }
        else if (_loss_ema > 0.005f) {
          _fec_percentage = _cfg.min_fec_percentage + (_cfg.max_fec_percentage - _cfg.min_fec_percentage) / 4;
        }
        else {
          _fec_percentage = _cfg.min_fec_percentage;
        }
      }

      // Update thermal state
      if (_cfg.thermal_protection) {
        update_thermal();
      }
    }

    void on_idr_request() {
      if (!_cfg.thermal_protection) {
        return;
      }

      auto now = std::chrono::steady_clock::now();
      auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - _idr_minute_start);

      if (elapsed.count() >= 60) {
        // Reset the minute window
        _idr_requests_last_minute = 0;
        _idr_minute_start = now;
      }

      _idr_requests_last_minute++;
    }

    void on_wifi_quality(int quality, int rssi, int link_speed_mbps) {
      (void) rssi;
      (void) link_speed_mbps;

      auto now = std::chrono::steady_clock::now();
      _prev_wifi_quality = _wifi_quality;
      _wifi_quality = quality;

      // Check for rapid quality tier drop
      auto time_since_last = std::chrono::duration_cast<std::chrono::seconds>(now - _wifi_quality_time);
      if (time_since_last.count() <= 2 &&
          (_prev_wifi_quality - _wifi_quality) >= _cfg.wifi_preemptive_drop_threshold) {
        _wifi_preemptive_active = true;
      }
      else if (_wifi_quality >= 3) {
        // Quality recovered to GOOD or better
        _wifi_preemptive_active = false;
      }

      _wifi_quality_time = now;
    }

    int get_target_bitrate_kbps() const {
      int bitrate = _target_bitrate_kbps;

      // Apply WiFi preemptive drop
      if (_wifi_preemptive_active) {
        bitrate = static_cast<int>(bitrate * 0.6f);
        bitrate = std::max(bitrate, _cfg.min_bitrate_kbps);
      }

      return bitrate;
    }

    int get_fec_percentage() const {
      return _fec_percentage;
    }

    int get_pacing_buffer_us() const {
      if (!_cfg.frame_pacing || _jitter_count < 2) {
        return 0;
      }

      float stddev = compute_jitter_stddev();
      if (stddev > 2000.0f) {
        int max_buf = _cfg.max_pacing_buffer_ms * 1000;
        return std::min(static_cast<int>(stddev * 0.5f), max_buf);
      }

      return 0;
    }

    int get_thermal_state() const {
      return _thermal_state;
    }

    int get_thermal_resolution() const {
      if (_thermal_state >= 2 && !_resolution_stepped_down) {
        return _cfg.thermal_step_down_resolution;
      }
      return 0;
    }

    int get_thermal_fps() const {
      if (_thermal_state >= 2 && _resolution_stepped_down) {
        auto now = std::chrono::steady_clock::now();
        auto since_step = std::chrono::duration_cast<std::chrono::seconds>(now - _thermal_step_down_time);
        if (since_step.count() >= 10 && _thermal_state >= 2) {
          return _cfg.thermal_step_down_fps;
        }
      }
      return 0;
    }

    void record_frame_interval(std::chrono::microseconds interval) {
      _jitter_window[_jitter_idx] = interval;
      _jitter_idx = (_jitter_idx + 1) % JITTER_WINDOW_SIZE;
      if (_jitter_count < JITTER_WINDOW_SIZE) {
        _jitter_count++;
      }
    }

  private:
    config_t _cfg;

    // Bitrate state
    int _current_bitrate_kbps = 20000;
    int _target_bitrate_kbps = 20000;

    // Loss tracking (rolling 2-second window)
    static constexpr int LOSS_WINDOW_SIZE = 20;
    struct loss_sample_t {
      int sent = 0;
      int lost = 0;
      std::chrono::steady_clock::time_point time;
    };
    loss_sample_t _loss_window[LOSS_WINDOW_SIZE] = {};
    int _loss_window_idx = 0;
    int _loss_window_count = 0;
    std::chrono::steady_clock::time_point _last_loss_time;
    std::chrono::steady_clock::time_point _last_zero_loss_time;

    // FEC state
    float _loss_ema = 0.0f;
    int _fec_percentage = 20;

    // Frame pacing state
    static constexpr int JITTER_WINDOW_SIZE = 30;
    std::chrono::microseconds _jitter_window[JITTER_WINDOW_SIZE] = {};
    int _jitter_idx = 0;
    int _jitter_count = 0;
    int _pacing_buffer_us = 0;

    // Thermal state
    int _idr_requests_last_minute = 0;
    std::chrono::steady_clock::time_point _idr_minute_start;
    int _thermal_state = 0;
    std::chrono::steady_clock::time_point _thermal_step_down_time;
    std::chrono::steady_clock::time_point _thermal_last_change;
    bool _resolution_stepped_down = false;
    bool _fps_stepped_down = false;

    // WiFi quality (from custom client)
    int _wifi_quality = 4;
    int _prev_wifi_quality = 4;
    std::chrono::steady_clock::time_point _wifi_quality_time;
    bool _wifi_preemptive_active = false;

    float compute_loss_rate() const {
      if (_loss_window_count == 0) {
        return 0.0f;
      }

      auto now = std::chrono::steady_clock::now();
      int total_sent = 0;
      int total_lost = 0;

      for (int i = 0; i < _loss_window_count; i++) {
        auto age = std::chrono::duration_cast<std::chrono::seconds>(now - _loss_window[i].time);
        // Only consider samples within 2-second window
        if (age.count() <= 2) {
          total_sent += _loss_window[i].sent;
          total_lost += _loss_window[i].lost;
        }
      }

      if (total_sent == 0) {
        return 0.0f;
      }

      return static_cast<float>(total_lost) / static_cast<float>(total_sent);
    }

    float compute_jitter_stddev() const {
      if (_jitter_count < 2) {
        return 0.0f;
      }

      // Compute mean
      double sum = 0.0;
      for (int i = 0; i < _jitter_count; i++) {
        sum += static_cast<double>(_jitter_window[i].count());
      }
      double mean = sum / _jitter_count;

      // Compute variance
      double var_sum = 0.0;
      for (int i = 0; i < _jitter_count; i++) {
        double diff = static_cast<double>(_jitter_window[i].count()) - mean;
        var_sum += diff * diff;
      }
      double variance = var_sum / (_jitter_count - 1);

      return static_cast<float>(std::sqrt(variance));
    }

    void update_thermal() {
      auto now = std::chrono::steady_clock::now();

      // Reset IDR counter if minute elapsed
      auto idr_elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - _idr_minute_start);
      if (idr_elapsed.count() >= 60) {
        _idr_requests_last_minute = 0;
        _idr_minute_start = now;
      }

      bool idr_hot = _idr_requests_last_minute > 5;
      bool loss_trending_up = _loss_ema > 0.02f;

      int prev_state = _thermal_state;

      if (idr_hot && loss_trending_up) {
        // Both signals: move toward hot
        if (_thermal_state < 2) {
          _thermal_state++;
          _thermal_last_change = now;
          if (_thermal_state == 2) {
            _thermal_step_down_time = now;
          }
        }
      }
      else if (idr_hot || loss_trending_up) {
        // One signal: at least warm
        if (_thermal_state < 1) {
          _thermal_state = 1;
          _thermal_last_change = now;
        }
      }

      // Recovery: if stable for recovery_delay, step back down
      auto since_change = std::chrono::duration_cast<std::chrono::seconds>(now - _thermal_last_change);
      if (!idr_hot && !loss_trending_up && since_change.count() >= _cfg.thermal_recovery_delay_s) {
        if (_thermal_state > 0) {
          _thermal_state--;
          _thermal_last_change = now;

          if (_thermal_state < 2) {
            _fps_stepped_down = false;
          }
          if (_thermal_state == 0) {
            _resolution_stepped_down = false;
          }
        }
      }

      // Track step-down state transitions
      if (_thermal_state >= 2 && prev_state < 2) {
        _resolution_stepped_down = false;
        _fps_stepped_down = false;
      }

      // Mark resolution as stepped down if it was requested
      if (_thermal_state >= 2 && !_resolution_stepped_down) {
        // The caller will read get_thermal_resolution() and apply it,
        // then should set _resolution_stepped_down via the step-down acknowledgment.
        // For simplicity, we mark it on the next update cycle after the state hits 2.
        _resolution_stepped_down = true;
        _thermal_step_down_time = now;
      }
    }
  };

}  // namespace stream
