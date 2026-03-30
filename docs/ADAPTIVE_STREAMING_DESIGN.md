# Apollo Adaptive Streaming Suite — Design Document

**Date:** 2026-03-29
**Target:** Hisense 75U7N (MediaTek SoC, WiFi 6E, Android TV) via Moonlight
**Server:** Apollo/Sunshine fork on Windows, RTX 5080 (NVENC)
**Network:** PC wired ethernet, TV on WiFi

## Overview

Six coordinated features that transform Apollo from a static-quality streamer into an intelligent, self-adapting streaming system. Three server-only features work with stock Moonlight. Three client+server features require a custom Moonlight fork (sideloaded via adb).

## Architecture

```
                    Three Timescales

  PREDICTIVE (500ms)     REACTIVE (1-2s)      THERMAL (10-30s)
  ┌─────────────┐      ┌─────────────┐      ┌─────────────┐
  │ F5: WiFi    │      │ F1: Adaptive│      │ F3: Thermal  │
  │ Quality     │─────>│ Bitrate     │─────>│ Aware        │
  │ Signal      │      │             │      │ Encoding     │
  └─────────────┘      └──────┬──────┘      └─────────────┘
        │                     │                     │
        │              ┌──────┴──────┐              │
        │              │ F2: Predict │              │
        └─────────────>│ FEC         │<─────────────┘
                       └─────────────┘
                              │
                       ┌──────┴──────┐
                       │ F3: Frame   │
                       │ Pacing      │
                       └─────────────┘
                              │
                       ┌──────┴──────┐
                       │ F4: Latency │
                       │ Dashboard   │
                       └─────────────┘
                              │
                       ┌──────┴──────┐
                       │ F6: Smart   │
                       │ Reconnect   │
                       └─────────────┘
```

## Implementation Order

| Phase | Feature | Tier | Depends On |
|-------|---------|------|------------|
| 1 | Adaptive Bitrate | Server-only | None |
| 2 | Predictive FEC | Server-only | Phase 1 (shared loss data) |
| 3 | Frame Pacing + Thermal | Server-only | Phase 1 (shared controller) |
| 4 | Latency Dashboard | Client+Server | Phase 1-3 (displays their data) |
| 5 | WiFi Quality Signal | Client+Server | Phase 4 (extends overlay) |
| 6 | Smart Reconnect | Client+Server | All prior phases stable |

---

## Feature 1: Adaptive Bitrate with WiFi-Aware Encoding

### Mechanism

Moonlight sends IDX_LOSS_STATS packets containing packet loss counts and frame timing. Apollo parses these (already implemented in stream.cpp) and feeds them to a new `bitrate_controller` component that computes target bitrate.

NVENC supports dynamic bitrate changes via `NV_ENC_RECONFIGURE_PARAMS` without reinitializing the encoder session.

### Algorithm

```
Every loss_stats packet:
  loss_rate = packets_lost / packets_sent (rolling 2-second window)

  if loss_rate > 5%:
    target_bitrate = current_bitrate * 0.7  (aggressive drop)
    min_clamp to 2 Mbps
  elif loss_rate > 1%:
    target_bitrate = current_bitrate * 0.9  (gentle drop)
  elif loss_rate == 0% for 3+ seconds:
    target_bitrate = current_bitrate * 1.1  (ramp up)
    max_clamp to configured_max_bitrate

  Apply target_bitrate to NVENC encoder
```

### Files

- `src/stream.cpp` — extract loss stats, feed to bitrate controller
- `src/video.cpp` — new `update_bitrate()` method on encode session
- `src/nvenc/nvenc_base.cpp` — implement NVENC reconfigure call
- New: `src/bitrate_controller.h` — rolling stats + decision logic

### Config

```
adaptive_bitrate = enabled
min_bitrate = 2000
max_bitrate = 40000
adaptation_speed = moderate
```

---

## Feature 2: Predictive FEC (Forward Error Correction)

### Mechanism

Uses exponential moving average of loss rate from the same bitrate_controller to dynamically adjust FEC parity ratio per-frame, before FEC encoding in videoBroadcastThread.

### Algorithm

```
loss_ema = 0.3 * current_loss_rate + 0.7 * previous_ema

if loss_ema > 10%:   fec_percentage = 50%
elif loss_ema > 5%:  fec_percentage = 30%
elif loss_ema > 1%:  fec_percentage = 20%
elif loss_ema < 0.5% for 5+ seconds: fec_percentage = 10%
else: fec_percentage = 15%

parity_shards = ceil(data_shards * fec_percentage / 100)
clamp parity_shards to [1, data_shards]
```

### Files

- `src/stream.cpp` — replace static fec_percentage with dynamic value
- `src/bitrate_controller.h` — add `get_fec_percentage()` method

### Config

```
adaptive_fec = enabled
min_fec_percentage = 10
max_fec_percentage = 50
```

---

## Feature 3: Frame Pacing + Thermal-Aware Encoding

### Frame Pacing

Server-side jitter buffer that enforces consistent frame dispatch intervals:

```
target_interval = 1000ms / fps
jitter_window = last 30 frame intervals
jitter_score = stddev(jitter_window)

if jitter_score > 2ms:
  pacing_buffer = min(jitter_score * 0.5, 4ms)
else:
  pacing_buffer = 0ms

next_send_time = last_send_time + target_interval + pacing_buffer
sleep_until(next_send_time)
```

### Thermal Inference

Detect TV SoC throttling from server-side signals:

```
Signs of TV thermal throttle:
- IDR requests spike (decoder falling behind)
- Loss rate rises gradually (WiFi chip throttling)
- Both happening simultaneously

Response:
  Step 1: Drop resolution to 1080p (NVENC dynamic resolution)
  Step 2: If still throttling after 10s, drop fps to 30
  Step 3: If stable for 30s, try stepping back up
```

### Files

- `src/stream.cpp` — frame dispatch timer + thermal state tracking
- `src/video.cpp` — dynamic resolution change via NVENC reconfigure
- `src/bitrate_controller.h` — add thermal inference state machine

### Config

```
frame_pacing = enabled
thermal_protection = enabled
thermal_step_down_resolution = 1080
thermal_step_down_fps = 30
thermal_recovery_delay_s = 30
max_pacing_buffer_ms = 4
```

---

## Feature 4: Latency Dashboard Overlay (Custom Moonlight)

### Display

```
┌─────────────────────────┐
│ Apollo Stats             │
│ Encode:   3.2ms  ██     │
│ Network:  8.1ms  ████   │
│ Decode:   5.4ms  ███    │
│ Render:   2.1ms  █      │
│ Total:   18.8ms  ██████ │
│ FPS: 60  Loss: 0.1%     │
│ Bitrate: 28 Mbps        │
│ FEC: 15%  Codec: HEVC   │
│ WiFi: ●●●●○  TV: Cool   │
└─────────────────────────┘
```

Compact mode: `18.8ms | 60fps | 28Mbps | HEVC | ●●●●○`

### Mechanism

Server embeds stats in video frame header padding bytes (stock Moonlight ignores). Client reads and renders overlay.

Toggle: Hold Select + L1 for 2 seconds. Quick tap cycles Full → Compact → Off.

### Files

**Apollo:**
- `src/stream.cpp` — pack stats into video packet header
- `src/nvenc/nvenc_base.cpp` — expose per-frame encode time

**Moonlight fork:**
- `app/src/main/java/com/limelight/binding/video/` — read custom header
- New: `app/src/main/java/com/limelight/overlay/StatsOverlay.java`
- `app/src/main/java/com/limelight/Game.java` — controller combo detection

---

## Feature 5: WiFi Quality Signal + Auto Quality Switching (Custom Moonlight)

### Client-Side WiFi Monitoring

```java
WifiInfo info = wifiManager.getConnectionInfo();
int rssi = info.getRssi();
int linkSpeed = info.getLinkSpeed();

Quality tiers:
  EXCELLENT: rssi > -50, linkSpeed > 400  (5 bars)
  GOOD:      rssi > -60, linkSpeed > 200  (4 bars)
  FAIR:      rssi > -70, linkSpeed > 100  (3 bars)
  POOR:      rssi > -75                   (2 bars)
  CRITICAL:  else                         (1 bar)
```

### Client → Server Signaling

Custom ENet control message `IDX_WIFI_QUALITY`:

```
Payload: 4 bytes
  [0]   quality tier (0-4)
  [1]   rssi (signed byte, dBm)
  [2-3] link_speed (uint16, Mbps)
```

### Server Response

```
Quality drop 2+ tiers in 2s → preemptive 40% bitrate drop
POOR → conservative ramp-up bias
EXCELLENT → aggressive ramp-up bias
```

### Files

**Apollo:**
- `src/stream.cpp` — IDX_WIFI_QUALITY handler
- `src/bitrate_controller.h` — preemptive drop, bias modes

**Moonlight fork:**
- New: `app/src/main/java/com/limelight/wifi/WifiMonitor.java`
- `app/src/main/java/com/limelight/nvstream/NvConnection.java` — send messages
- `app/src/main/java/com/limelight/overlay/StatsOverlay.java` — WiFi indicator

### Config

```
# Apollo
wifi_quality_signaling = enabled
preemptive_drop_threshold = 2

# Moonlight
wifi_monitor_interval_ms = 500
send_wifi_quality = true
```

---

## Feature 6: Smart Reconnect (Custom Moonlight)

### Server — Session Suspension

```
on peer_disconnect:
  if smart_reconnect_enabled:
    session->state = SUSPENDED
    session->suspend_time = now()
    // Stop encoding, keep game running, keep crypto keys
    // Start 30s timeout timer

on new_peer_connect:
  if connect_data matches suspended session:
    session->state = RUNNING
    session->control.peer = new_peer
    request_idr_frame()  // client can decode immediately
    resume streaming
```

### Client — Freeze and Reconnect

```
on connection_lost:
  Hold last decoded frame on screen
  Show pulsing "Reconnecting..." overlay

  for attempt in 1..6:
    wait(500ms * attempt)  // backoff
    if wifi_available:
      try reconnect with same session credentials
      if success: resume decoding, hide overlay, return

  // Failed — show "Connection lost" dialog
```

### Security

- Same AES-GCM keys (no re-pairing)
- connect_data validation (32-bit random from RTSP)
- 30s timeout (auto-cleanup)
- Max 2 suspended sessions

### Files

**Apollo:**
- `src/stream.cpp` — SUSPENDED state, suspend timer, session resume matching

**Moonlight fork:**
- `app/src/main/java/com/limelight/nvstream/NvConnection.java` — reconnection loop
- `app/src/main/java/com/limelight/binding/video/MediaCodecDecoderRenderer.java` — freeze frame
- New: `app/src/main/java/com/limelight/overlay/ReconnectOverlay.java`

### Config

```
# Apollo
smart_reconnect = enabled
smart_reconnect_timeout_s = 30
max_suspended_sessions = 2

# Moonlight
smart_reconnect = true
max_reconnect_attempts = 6
reconnect_backoff_ms = 500
```

---

## Graceful Degradation Matrix

| Server | Client | Features Available |
|--------|--------|--------------------|
| Apollo | Stock Moonlight | F1 + F2 + F3 (full server-side suite) |
| Apollo | Custom Moonlight | F1 + F2 + F3 + F4 + F5 + F6 (all features) |
| Stock Sunshine | Custom Moonlight | Client overlay shows local stats only, reconnect attempts fail gracefully |

---

## Decision Log

| # | Decision | Alternatives | Rationale |
|---|----------|-------------|-----------|
| 1 | Server-side adaptive bitrate via loss stats | Client requests, fixed presets | No client changes, reuses protocol |
| 2 | FEC co-located with bitrate controller | Separate controller | Same loss data, simpler |
| 3 | Thermal inference from IDR/loss patterns | Client CPU temp reporting | Works with stock Moonlight |
| 4 | Stats in video frame header padding | Separate control messages | Zero traffic, frame-synchronized |
| 5 | Android WifiManager polling | Passive only | Direct measurement, predictive |
| 6 | Session suspension with connect_data match | Full re-pair | Reuses protocol, no crypto redo |
| 7 | HEVC preferred over AV1 | Auto-negotiate | MediaTek handles HEVC better |
| 8 | Three-timescale response | Single algorithm | Different problems, different speeds |
| 9 | Custom Moonlight fork via adb sideload | Stock only | Android TV supports sideload |
| 10 | Graceful degradation for all features | Require custom client | Mixed environments work |
