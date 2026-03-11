/**
 * @file performance_monitor.h
 * @brief Lightweight performance monitoring and metrics collection
 * 
 * Provides real-time performance tracking with minimal overhead:
 * - Frame timing and throughput
 * - Encoding latency
 * - Network statistics
 * - Resource usage
 * - HTTP metrics API for external monitoring
 */

#pragma once

#include <atomic>
#include <chrono>
#include <cstdio>
#include <map>
#include <limits>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <algorithm>
#include <numeric>
#include <cmath>

#include "telemetry.h"

namespace perf {

using clock_t = std::chrono::steady_clock;
using time_point_t = clock_t::time_point;
using duration_t = std::chrono::nanoseconds;

/**
 * @brief Simple statistics accumulator
 */
class stats_accumulator_t {
public:
    struct snapshot_t {
        uint64_t count;
        double sum;
        double min;
        double max;
        double variance;
        double stddev;
    };

    void record(double value) {
        std::lock_guard<std::mutex> lock(mutex_);
        count_++;
        sum_ += value;
        sum_squares_ += value * value;
        min_ = std::min(min_, value);
        max_ = std::max(max_, value);
    }
    
    void reset() {
        std::lock_guard<std::mutex> lock(mutex_);
        count_ = 0;
        sum_ = 0.0;
        sum_squares_ = 0.0;
        min_ = std::numeric_limits<double>::max();
        max_ = std::numeric_limits<double>::lowest();
    }
    
    uint64_t count() const { return snapshot().count; }
    double sum() const { return snapshot().sum; }
    double mean() const {
        auto data = snapshot();
        return data.count > 0 ? data.sum / data.count : 0.0;
    }
    double min() const { return snapshot().min; }
    double max() const { return snapshot().max; }
    
    double variance() const {
        return snapshot().variance;
    }
    
    double stddev() const {
        return snapshot().stddev;
    }

    snapshot_t snapshot() const {
        std::lock_guard<std::mutex> lock(mutex_);

        if (count_ == 0) {
            return {0, 0.0, 0.0, 0.0, 0.0, 0.0};
        }

        const double mean_val = sum_ / static_cast<double>(count_);
        const double variance_val = count_ > 1 ? (sum_squares_ / static_cast<double>(count_)) - (mean_val * mean_val) : 0.0;
        return {
            count_,
            sum_,
            min_,
            max_,
            variance_val,
            std::sqrt(std::max(0.0, variance_val)),
        };
    }
    
private:
    mutable std::mutex mutex_;
    uint64_t count_{0};
    double sum_{0.0};
    double sum_squares_{0.0};
    double min_{std::numeric_limits<double>::max()};
    double max_{std::numeric_limits<double>::lowest()};
};

/**
 * @brief RAII timer for automatic duration measurement
 */
class scoped_timer_t {
public:
    scoped_timer_t(stats_accumulator_t& stats)
        : stats_(stats)
        , start_(clock_t::now())
    {}
    
    ~scoped_timer_t() {
        auto duration = clock_t::now() - start_;
        auto microseconds = std::chrono::duration_cast<std::chrono::microseconds>(duration).count();
        stats_.record(static_cast<double>(microseconds));
    }
    
private:
    stats_accumulator_t& stats_;
    time_point_t start_;
};

/**
 * @brief Performance metrics for video streaming
 */
struct video_metrics_t {
    // Frame statistics
    stats_accumulator_t frame_capture_time;   // Time to capture frame (µs)
    stats_accumulator_t frame_encode_time;    // Time to encode frame (µs)
    stats_accumulator_t frame_total_time;     // End-to-end frame time (µs)
    
    // Throughput
    std::atomic<uint64_t> frames_captured{0};
    std::atomic<uint64_t> frames_encoded{0};
    std::atomic<uint64_t> frames_transmitted{0};
    std::atomic<uint64_t> frames_dropped{0};
    
    // Size statistics
    stats_accumulator_t frame_size_bytes;     // Encoded frame size
    std::atomic<uint64_t> total_bytes_encoded{0};
    
    // Quality metrics
    std::atomic<uint64_t> encoding_errors{0};
    std::atomic<uint64_t> capture_errors{0};
    
    void reset() {
        frame_capture_time.reset();
        frame_encode_time.reset();
        frame_total_time.reset();
        frame_size_bytes.reset();
        
        frames_captured = 0;
        frames_encoded = 0;
        frames_transmitted = 0;
        frames_dropped = 0;
        total_bytes_encoded = 0;
        encoding_errors = 0;
        capture_errors = 0;
    }
};

/**
 * @brief Performance metrics for network transmission
 */
struct network_metrics_t {
    // Latency
    stats_accumulator_t rtt_ms;               // Round-trip time (ms)
    stats_accumulator_t packet_latency_us;   // Per-packet latency (µs)
    
    // Throughput
    std::atomic<uint64_t> packets_sent{0};
    std::atomic<uint64_t> packets_received{0};
    std::atomic<uint64_t> bytes_sent{0};
    std::atomic<uint64_t> bytes_received{0};
    
    // Quality
    std::atomic<uint64_t> packets_lost{0};
    std::atomic<uint64_t> packets_retransmitted{0};
    std::atomic<uint64_t> packets_out_of_order{0};
    
    // Congestion
    stats_accumulator_t send_queue_depth;
    stats_accumulator_t receive_queue_depth;
    
    void reset() {
        rtt_ms.reset();
        packet_latency_us.reset();
        send_queue_depth.reset();
        receive_queue_depth.reset();
        
        packets_sent = 0;
        packets_received = 0;
        bytes_sent = 0;
        bytes_received = 0;
        packets_lost = 0;
        packets_retransmitted = 0;
        packets_out_of_order = 0;
    }
};

/**
 * @brief System resource usage metrics
 */
struct resource_metrics_t {
    std::atomic<double> cpu_usage_percent{0.0};
    std::atomic<uint64_t> memory_used_bytes{0};
    std::atomic<uint64_t> gpu_memory_used_bytes{0};
    std::atomic<double> gpu_usage_percent{0.0};
    std::atomic<bool> gpu_usage_available{false};
    std::atomic<bool> gpu_memory_available{false};
    
    void reset() {
        cpu_usage_percent = 0.0;
        memory_used_bytes = 0;
        gpu_memory_used_bytes = 0;
        gpu_usage_percent = 0.0;
        gpu_usage_available = false;
        gpu_memory_available = false;
    }
};

/**
 * @brief Session lifecycle metrics
 */
struct session_metrics_t {
    std::atomic<uint64_t> active_sessions{0};
    std::atomic<uint64_t> sessions_started{0};
    std::atomic<uint64_t> sessions_ended{0};

    void reset() {
        active_sessions = 0;
        sessions_started = 0;
        sessions_ended = 0;
    }
};

/**
 * @brief Centralized performance monitor
 */
class performance_monitor_t {
public:
    static performance_monitor_t& instance() {
        static performance_monitor_t instance;
        return instance;
    }
    
    // Video metrics
    void record_frame_capture(uint64_t duration_us) {
        video_.frame_capture_time.record(static_cast<double>(duration_us));
        video_.frames_captured++;
    }
    
    void record_frame_encode(uint64_t duration_us, size_t frame_size) {
        video_.frame_encode_time.record(static_cast<double>(duration_us));
        video_.frame_size_bytes.record(static_cast<double>(frame_size));
        video_.frames_encoded++;
        video_.total_bytes_encoded += frame_size;
    }
    
    void record_frame_total(uint64_t duration_us) {
        video_.frame_total_time.record(static_cast<double>(duration_us));
    }
    
    void record_frame_transmitted() {
        video_.frames_transmitted++;
    }
    
    void record_frame_dropped() {
        video_.frames_dropped++;
    }
    
    void record_encoding_error() {
        video_.encoding_errors++;
    }
    
    void record_capture_error() {
        video_.capture_errors++;
    }
    
    // Network metrics
    void record_rtt(uint32_t rtt_ms) {
        network_.rtt_ms.record(static_cast<double>(rtt_ms));
    }
    
    void record_packet_sent(size_t size) {
        network_.packets_sent++;
        network_.bytes_sent += size;
    }

    void record_packet_batch_sent(size_t packet_count, size_t total_bytes) {
        network_.packets_sent += packet_count;
        network_.bytes_sent += total_bytes;
    }

    void record_packet_latency(uint64_t duration_us) {
        network_.packet_latency_us.record(static_cast<double>(duration_us));
    }
    
    void record_packet_received(size_t size) {
        network_.packets_received++;
        network_.bytes_received += size;
    }
    
    void record_packet_lost(uint64_t count = 1) {
        network_.packets_lost += count;
    }
    
    void record_packet_retransmitted() {
        network_.packets_retransmitted++;
    }
    
    void record_send_queue_depth(size_t depth) {
        network_.send_queue_depth.record(static_cast<double>(depth));
    }
    
    // Resource metrics
    void update_cpu_usage(double percent) {
        resources_.cpu_usage_percent = percent;
        telemetry::plot_cpu_usage(percent);
    }
    
    void update_memory_usage(uint64_t bytes) {
        resources_.memory_used_bytes = bytes;
        telemetry::plot_memory_usage_bytes(bytes);
    }
    
    void update_gpu_usage(double percent) {
        resources_.gpu_usage_percent = percent;
        telemetry::plot_gpu_usage(percent);
    }
    
    void update_gpu_memory_usage(uint64_t bytes) {
        resources_.gpu_memory_used_bytes = bytes;
        telemetry::plot_gpu_memory_usage_bytes(bytes);
    }

    void set_gpu_usage_available(bool available) {
        resources_.gpu_usage_available = available;
    }

    void set_gpu_memory_available(bool available) {
        resources_.gpu_memory_available = available;
    }

    // Session metrics
    void record_session_started() {
        sessions_.active_sessions++;
        sessions_.sessions_started++;
        telemetry::plot_active_sessions(sessions_.active_sessions.load());
    }

    void record_session_ended() {
        auto active = sessions_.active_sessions.load();
        while (active > 0 && !sessions_.active_sessions.compare_exchange_weak(active, active - 1)) {}
        sessions_.sessions_ended++;
        telemetry::plot_active_sessions(sessions_.active_sessions.load());
    }
    
    // Getters
    const video_metrics_t& video_metrics() const { return video_; }
    const network_metrics_t& network_metrics() const { return network_; }
    const resource_metrics_t& resource_metrics() const { return resources_; }
    const session_metrics_t& session_metrics() const { return sessions_; }
    
    // Reset all metrics
    void reset() {
        video_.reset();
        network_.reset();
        resources_.reset();
        sessions_.reset();
        start_time_ = clock_t::now();
    }
    
    // Get uptime in seconds
    double uptime_seconds() const {
        auto duration = clock_t::now() - start_time_;
        return std::chrono::duration<double>(duration).count();
    }
    
    /**
     * @brief Generate metrics report as JSON
     */
    std::string to_json() const {
        auto uptime = uptime_seconds();
        const auto capture = video_.frame_capture_time.snapshot();
        const auto encode = video_.frame_encode_time.snapshot();
        const auto total = video_.frame_total_time.snapshot();
        const auto frame_size = video_.frame_size_bytes.snapshot();
        const auto rtt = network_.rtt_ms.snapshot();
        const auto packets_sent = network_.packets_sent.load();
        const auto packets_received = network_.packets_received.load();
        const auto bytes_sent = network_.bytes_sent.load();
        const auto bytes_received = network_.bytes_received.load();
        const auto packets_lost = network_.packets_lost.load();
        const auto packets_retransmitted = network_.packets_retransmitted.load();
        
        char buffer[4864];
        snprintf(buffer, sizeof(buffer),
            "{\n"
            "  \"uptime_seconds\": %.2f,\n"
            "  \"sessions\": {\n"
            "    \"active\": %llu,\n"
            "    \"started_total\": %llu,\n"
            "    \"ended_total\": %llu\n"
            "  },\n"
            "  \"video\": {\n"
            "    \"frames_captured\": %llu,\n"
            "    \"frames_encoded\": %llu,\n"
            "    \"frames_transmitted\": %llu,\n"
            "    \"frames_dropped\": %llu,\n"
            "    \"fps\": %.2f,\n"
            "    \"capture_time_us\": {\"mean\": %.2f, \"min\": %.2f, \"max\": %.2f, \"stddev\": %.2f},\n"
            "    \"encode_time_us\": {\"mean\": %.2f, \"min\": %.2f, \"max\": %.2f, \"stddev\": %.2f},\n"
            "    \"total_time_us\": {\"mean\": %.2f, \"min\": %.2f, \"max\": %.2f, \"stddev\": %.2f},\n"
            "    \"frame_size_bytes\": {\"mean\": %.2f, \"min\": %.2f, \"max\": %.2f},\n"
            "    \"bitrate_mbps\": %.2f,\n"
            "    \"encoding_errors\": %llu,\n"
            "    \"capture_errors\": %llu\n"
            "  },\n"
            "  \"network\": {\n"
            "    \"packets_sent\": %llu,\n"
            "    \"packets_received\": %llu,\n"
            "    \"bytes_sent\": %llu,\n"
            "    \"bytes_received\": %llu,\n"
            "    \"packets_lost\": %llu,\n"
            "    \"packet_loss_rate\": %.4f,\n"
            "    \"packets_retransmitted\": %llu,\n"
            "    \"rtt_ms\": {\"mean\": %.2f, \"min\": %.2f, \"max\": %.2f},\n"
            "    \"send_throughput_mbps\": %.2f,\n"
            "    \"receive_throughput_mbps\": %.2f\n"
            "  },\n"
            "  \"resources\": {\n"
            "    \"cpu_usage_percent\": %.2f,\n"
            "    \"memory_used_mb\": %.2f,\n"
            "    \"gpu_usage_available\": %s,\n"
            "    \"gpu_usage_percent\": %.2f,\n"
            "    \"gpu_memory_available\": %s,\n"
            "    \"gpu_memory_used_mb\": %.2f\n"
            "  }\n"
            "}",
            uptime,
            sessions_.active_sessions.load(),
            sessions_.sessions_started.load(),
            sessions_.sessions_ended.load(),
            video_.frames_captured.load(),
            video_.frames_encoded.load(),
            video_.frames_transmitted.load(),
            video_.frames_dropped.load(),
            uptime > 0 ? video_.frames_encoded.load() / uptime : 0.0,
            capture.count > 0 ? capture.sum / capture.count : 0.0,
            capture.min,
            capture.max,
            capture.stddev,
            encode.count > 0 ? encode.sum / encode.count : 0.0,
            encode.min,
            encode.max,
            encode.stddev,
            total.count > 0 ? total.sum / total.count : 0.0,
            total.min,
            total.max,
            total.stddev,
            frame_size.count > 0 ? frame_size.sum / frame_size.count : 0.0,
            frame_size.min,
            frame_size.max,
            uptime > 0 ? (video_.total_bytes_encoded.load() * 8.0 / 1000000.0 / uptime) : 0.0,
            video_.encoding_errors.load(),
            video_.capture_errors.load(),
            packets_sent,
            packets_received,
            bytes_sent,
            bytes_received,
            packets_lost,
            packets_sent > 0 ?
                static_cast<double>(packets_lost) / packets_sent : 0.0,
            packets_retransmitted,
            rtt.count > 0 ? rtt.sum / rtt.count : 0.0,
            rtt.min,
            rtt.max,
            uptime > 0 ? (bytes_sent * 8.0 / 1000000.0 / uptime) : 0.0,
            uptime > 0 ? (bytes_received * 8.0 / 1000000.0 / uptime) : 0.0,
            resources_.cpu_usage_percent.load(),
            resources_.memory_used_bytes.load() / (1024.0 * 1024.0),
            resources_.gpu_usage_available.load() ? "true" : "false",
            resources_.gpu_usage_percent.load(),
            resources_.gpu_memory_available.load() ? "true" : "false",
            resources_.gpu_memory_used_bytes.load() / (1024.0 * 1024.0)
        );
        
        return std::string(buffer);
    }
    
    /**
     * @brief Generate Prometheus-compatible metrics
     */
    std::string to_prometheus() const {
        std::string output;
        auto uptime = uptime_seconds();
        const auto encode = video_.frame_encode_time.snapshot();
        const auto rtt = network_.rtt_ms.snapshot();
        
        // Video metrics
        output += "# HELP apollo_frames_captured_total Total frames captured\n";
        output += "# TYPE apollo_frames_captured_total counter\n";
        output += "apollo_frames_captured_total " + std::to_string(video_.frames_captured.load()) + "\n\n";
        
        output += "# HELP apollo_frames_encoded_total Total frames encoded\n";
        output += "# TYPE apollo_frames_encoded_total counter\n";
        output += "apollo_frames_encoded_total " + std::to_string(video_.frames_encoded.load()) + "\n\n";
        
        output += "# HELP apollo_encode_time_us_mean Mean encoding time in microseconds\n";
        output += "# TYPE apollo_encode_time_us_mean gauge\n";
        output += "apollo_encode_time_us_mean " + std::to_string(encode.count > 0 ? encode.sum / encode.count : 0.0) + "\n\n";
        
        output += "# HELP apollo_fps Frames per second\n";
        output += "# TYPE apollo_fps gauge\n";
        output += "apollo_fps " + std::to_string(uptime > 0 ? video_.frames_encoded.load() / uptime : 0.0) + "\n\n";

        output += "# HELP apollo_active_sessions Current active streaming sessions\n";
        output += "# TYPE apollo_active_sessions gauge\n";
        output += "apollo_active_sessions " + std::to_string(sessions_.active_sessions.load()) + "\n\n";

        output += "# HELP apollo_sessions_started_total Total streaming sessions started\n";
        output += "# TYPE apollo_sessions_started_total counter\n";
        output += "apollo_sessions_started_total " + std::to_string(sessions_.sessions_started.load()) + "\n\n";

        output += "# HELP apollo_sessions_ended_total Total streaming sessions ended\n";
        output += "# TYPE apollo_sessions_ended_total counter\n";
        output += "apollo_sessions_ended_total " + std::to_string(sessions_.sessions_ended.load()) + "\n\n";
        
        // Network metrics
        output += "# HELP apollo_packets_sent_total Total packets sent\n";
        output += "# TYPE apollo_packets_sent_total counter\n";
        output += "apollo_packets_sent_total " + std::to_string(network_.packets_sent.load()) + "\n\n";
        
        output += "# HELP apollo_packets_lost_total Total packets lost\n";
        output += "# TYPE apollo_packets_lost_total counter\n";
        output += "apollo_packets_lost_total " + std::to_string(network_.packets_lost.load()) + "\n\n";
        
        output += "# HELP apollo_rtt_ms_mean Mean round-trip time in milliseconds\n";
        output += "# TYPE apollo_rtt_ms_mean gauge\n";
        output += "apollo_rtt_ms_mean " + std::to_string(rtt.count > 0 ? rtt.sum / rtt.count : 0.0) + "\n\n";
        
        // Resource metrics
        output += "# HELP apollo_cpu_usage_percent CPU usage percentage\n";
        output += "# TYPE apollo_cpu_usage_percent gauge\n";
        output += "apollo_cpu_usage_percent " + std::to_string(resources_.cpu_usage_percent.load()) + "\n\n";
        
        output += "# HELP apollo_memory_used_bytes Memory used in bytes\n";
        output += "# TYPE apollo_memory_used_bytes gauge\n";
        output += "apollo_memory_used_bytes " + std::to_string(resources_.memory_used_bytes.load()) + "\n\n";

        output += "# HELP apollo_gpu_usage_available Whether GPU utilization sampling is available on this system\n";
        output += "# TYPE apollo_gpu_usage_available gauge\n";
        output += "apollo_gpu_usage_available " + std::to_string(resources_.gpu_usage_available.load() ? 1 : 0) + "\n\n";

        output += "# HELP apollo_gpu_usage_percent GPU usage percentage\n";
        output += "# TYPE apollo_gpu_usage_percent gauge\n";
        output += "apollo_gpu_usage_percent " + std::to_string(resources_.gpu_usage_percent.load()) + "\n\n";

        output += "# HELP apollo_gpu_memory_available Whether GPU memory sampling is available on this system\n";
        output += "# TYPE apollo_gpu_memory_available gauge\n";
        output += "apollo_gpu_memory_available " + std::to_string(resources_.gpu_memory_available.load() ? 1 : 0) + "\n\n";

        output += "# HELP apollo_gpu_memory_used_bytes GPU memory used in bytes\n";
        output += "# TYPE apollo_gpu_memory_used_bytes gauge\n";
        output += "apollo_gpu_memory_used_bytes " + std::to_string(resources_.gpu_memory_used_bytes.load()) + "\n\n";
        
        return output;
    }
    
private:
    performance_monitor_t();
    ~performance_monitor_t();

    void resource_sampling_loop();

    video_metrics_t video_;
    network_metrics_t network_;
    resource_metrics_t resources_;
    session_metrics_t sessions_;
    time_point_t start_time_;
    std::atomic<bool> sampler_running_{false};
    std::thread sampler_thread_;
};

// Convenience macros for timing
#define PERF_TIMER(stats_var) \
    perf::scoped_timer_t PERF_TIMER_##__LINE__(stats_var)

}  // namespace perf

/**
 * USAGE EXAMPLES:
 * 
 * // 1. Record frame capture timing
 * auto start = std::chrono::steady_clock::now();
 * capture_frame(frame);
 * auto duration = std::chrono::steady_clock::now() - start;
 * auto us = std::chrono::duration_cast<std::chrono::microseconds>(duration).count();
 * perf::performance_monitor_t::instance().record_frame_capture(us);
 * 
 * // 2. Using scoped timer
 * {
 *     PERF_TIMER(perf::performance_monitor_t::instance().video_metrics().frame_encode_time);
 *     encode_frame(frame);  // Automatically timed
 * }
 * 
 * // 3. HTTP endpoint for metrics
 * void handle_metrics_request(http::response& res) {
 *     auto& monitor = perf::performance_monitor_t::instance();
 *     
 *     // JSON format
 *     res.set_header("Content-Type", "application/json");
 *     res.write(monitor.to_json());
 *     
 *     // Or Prometheus format
 *     res.set_header("Content-Type", "text/plain; version=0.0.4");
 *     res.write(monitor.to_prometheus());
 * }
 * 
 * // 4. Periodic stats logging
 * void log_performance_stats() {
 *     auto& monitor = perf::performance_monitor_t::instance();
 *     auto video = monitor.video_metrics();
 *     auto network = monitor.network_metrics();
 *     
 *     BOOST_LOG(info) << "Performance: "
 *                     << "FPS=" << (monitor.uptime_seconds() > 0 ? 
 *                                   video.frames_encoded / monitor.uptime_seconds() : 0)
 *                     << " Encode=" << video.frame_encode_time.mean() << "µs"
 *                     << " RTT=" << network.rtt_ms.mean() << "ms"
 *                     << " Loss=" << (network.packets_sent > 0 ?
 *                                     100.0 * network.packets_lost / network.packets_sent : 0) << "%";
 * }
 */
