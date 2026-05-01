# Code Review Fixes — 2026-05-01

Independent skeptical review of the 18-commit audit train on `master`, plus the in-flight uncommitted `virtual_display.cpp` rework. Three new fix commits land on top of the audit train.

## TL;DR

Most of the prior 18-commit audit train is **substantially correct** — independent verification found `crypto_equal` is real `CRYPTO_memcmp`, `recursive_mutex` on `bitrate_controller` properly guards reentrancy, NVENC reconfigure is correctly disabled rather than racy, smart-reconnect IP fallback is gated default-off, and the `IDX_ENCRYPTED`/`IDX_INPUT_DATA` bounds checks are sound. **But two must-fix issues blocked ship**, and three additional Highs / five Mediums / six Lows were addressed in this round.

## New commits

### `db152fa` — `fix(virtual_display): RAII topology snapshot covers normal+abnormal exit`

**Severity:** Critical (#1) + High (#6)

**Problem:** An in-flight rework of `virtual_display.cpp` had three concrete defects:

1. `restorePhysicalDisplays()` was called only from the normal `proc_t::terminate()` path. On crash, force-kill, SIGINT/SIGTERM, RDP yank, or tray-close, physical displays stayed deactivated and the user was staring at a black machine that needed Win+P or registry recovery.
2. Blanket `SDC_TOPOLOGY_EXTEND` restore was wrong if the user was in clone, internal-only, or custom layout — they would return to a different topology than they left.
3. `SDC_SAVE_TO_DATABASE` was removed from the apply path, so Windows would not re-persist the user's preferred topology either.

**Fix:** Stashed the buggy WIP (`stash@{0}`, kept for comparison) and rebuilt from scratch with a RAII pattern.

- New `display_topology_snapshot_t` class (`src/platform/windows/virtual_display.h`):
  - **ctor:** `GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS)` + `QueryDisplayConfig()` captures the exact path+mode arrays. Capture failure leaves `_valid = false`, dtor becomes a no-op, `BOOST_LOG(warning)` once.
  - **dtor:** `SetDisplayConfig(SDC_APPLY | SDC_USE_SUPPLIED_DISPLAY_CONFIG | SDC_ALLOW_PATH_ORDER_CHANGES | SDC_SAVE_TO_DATABASE)` with the captured arrays. **No `SDC_TOPOLOGY_EXTEND`.**
  - Copy + move both deleted (snapshot is a 1-of-1 RAII guard).
- `topology_snapshot_slot()` returns a function-local `static std::unique_ptr<display_topology_snapshot_t>&`. C++ guarantees the destructor runs on every static-storage unwind path:
  - Normal `terminate()` — explicit `.reset()` in `process.cpp` (belt-and-suspenders) plus static dtor.
  - Exception unwind through `fail_guard` → `terminate()` — explicit `.reset()` runs.
  - SIGINT/SIGTERM via existing `src/main.cpp:300, 317` handlers — both call `proc::proc.terminate()` → `.reset()`.
  - `entry_handler.cpp:79` raises SIGINT, routes through the same path.
  - `std::terminate` / `std::abort` / uncaught — static dtor still runs before exit.
- Pattern chosen so signal handlers don't need editing one-by-one and the snapshot data is decoupled from `proc_t`'s non-trivial destruction order.

**Files touched:** `src/platform/windows/virtual_display.{h,cpp}`, `src/process.cpp`. +146 lines.

### `6d5fc7e` — `fix(security): pin_required UI red banner + audit log; atomic config writes`

**Severity:** Critical (#2) + High (#3)

**Problem A — pin_required UI surfacing too soft:** Tier B (`8417502`) only refused skip-PIN pairing when UPnP was *also* on. With UPnP off, `pin_required=false` on a typical LAN (corporate Wi-Fi, dorm, AirBnB) lets ANY device pair with PIN `0000` and gain `proc::run` shell access. The locale string treated this as a benign description; the only audit trail was `BOOST_LOG(warning)` which most users never read.

**Fix A:**
- `src_assets/common/assets/web/public/assets/locale/en.json`: `pin_required_desc` rewritten to lead with "DANGEROUS WHEN DISABLED". New `pin_required_warning` key with the alert body.
- `src_assets/common/assets/web/configs/tabs/General.vue`: dismissible Bootstrap `alert-danger` renders only when the toggle is off. `pinRequiredOff` computed handles all Checkbox falsy representations (`false`, `0`, `'disabled'`, `'no'`, `'off'`). Bootstrap, not Vuetify (matches the rest of the project's UI stack).
- `src/nvhttp.cpp`: `append_audit_log()` helper writes one line per auto-pair event to `platf::appdata()/audit.log`:
  ```
  <ISO8601 UTC> AUTO_PAIR client=<name> ip=<client_ip> uid=<unique_id>
  ```
  `std::ios::app` maps to POSIX `O_APPEND` and Windows `FILE_APPEND_DATA` — both tear-free for concurrent writers. Serialized with a static `std::mutex`. Open failure logs once via `BOOST_LOG(error)` and never blocks pairing.

**Problem B — atomic file write hardening:** `std::filesystem::rename` is not guaranteed atomic on Windows over an existing target, and the prior `write_file` had no `fsync` between `close()` and `rename` — a power loss in that window could yield a zero-byte file on disk.

**Fix B:** `src/file_handler.cpp`:
- `out.flush()` then `out.close()` to drain CRT buffers.
- **Windows** (`#ifdef _WIN32`): reopen the temp file with `CreateFileW(GENERIC_WRITE, FILE_SHARE_READ, OPEN_EXISTING, FILE_FLAG_WRITE_THROUGH)`, call `FlushFileBuffers` for durable fsync, `CloseHandle`. Then publish via `ReplaceFileW(path, tmp, NULL, REPLACEFILE_WRITE_THROUGH | REPLACEFILE_IGNORE_MERGE_ERRORS, NULL, NULL)` — documented atomic on NTFS. Falls back to `MoveFileExW(MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)` when the target doesn't exist (`ReplaceFileW` requires existing target — this handles fresh `apps.json`).
- **POSIX** (`#else`): keep `std::filesystem::rename`. POSIX `rename(2)` IS atomic for same-filesystem moves; the `.tmp` lives next to the target.
- Any failure: unlink the temp and return `-1`.

**Files touched:** `src/nvhttp.cpp`, `src/file_handler.cpp`, `src_assets/common/assets/web/configs/tabs/General.vue`, `src_assets/common/assets/web/public/assets/locale/en.json`. +151 lines.

### `0692cbc` — `fix(audit): UB cast, wifi-quality init, underflow guard, hysteresis, changelog`

**Severity:** High (#4 #5) + 3 Mediums + 1 Low

| File | Issue | Fix |
|------|-------|-----|
| `src/uuid.h:22` | `std::uniform_int_distribution<uint8_t>` is UB per `[rand.req.genl]` (libc++ static_asserts; MSVC tolerates as ext) | `std::uniform_int_distribution<unsigned int>(0, 255)` + `static_cast<uint8_t>(...)` at the call site |
| `src/bitrate_controller.h:81` | `_wifi_quality_time` left default-constructed in `init()` (epoch); a refactor flipping the threshold comparison would silently break | Initialize to `steady_clock::now()` at end of `init()` |
| `src/stream.cpp:1295` | `(size_t)tagged_cipher_length > payload.size() - hdr_size` — underflow if `payload.size() < hdr_size`. Earlier guard exists but a refactor removing it would silently reintroduce | Three explicit checks: negative reject, `SIZE_MAX - hdr_size` overflow guard, then `cipher_len + hdr_size > payload.size()`. Survives a future refactor that removes the early return |
| `src/bitrate_controller.h:140-184` | Adaptive bitrate pumped on borderline links absent hysteresis | Dual thresholds (`raise = 0.005f` strictly < `lower = 0.01f`), `MIN_LOW_LOSS_STREAK = 3` consecutive samples required to raise, `MIN_DWELL_S = 2s` minimum between any two changes. Heavy-loss step (>0.05) bypasses dwell to protect the link |
| `SECURITY_CHANGELOG.md` | Entry #16 stale ("iterated SHA-256 100k rounds"); commit `b3c0467` upgraded to PBKDF2 600k | Entry corrected. New "Round 2" section header with one-line bullets per audit commit (`f8d38fa` Tier A, `8417502` Tier B, `947fb81` Tier C, `b3c0467` consensus-high, `464059b` adaptive suite, `12e5fd1` CSRF, `78d7162`+`7707a80` pin_required, `861df50` build) |

**Files touched:** `src/uuid.h`, `src/bitrate_controller.h`, `src/stream.cpp`, `SECURITY_CHANGELOG.md`. +99 −11 lines.

## Confirmed-good (independently verified, no change needed)

- `util::crypto_equal` (`src/utility.h:1099`) is `CRYPTO_memcmp`-backed
- `IDX_INPUT_DATA` size guard at `src/stream.cpp:1151` correctly precedes the subtraction at line 1162
- `IDX_ENCRYPTED` size guard at `src/stream.cpp:1277` correctly precedes the subtraction at line 1295 (further hardened in `0692cbc`)
- `bitrate_controller_t recursive_mutex` is held on every public method including the broadcast-thread caller (`record_frame_interval`)
- `nvenc_base::update_bitrate` / `update_resolution` correctly refuse with `BOOST_LOG(warning)` and `return -1` instead of running `nvEncReconfigureEncoder` from the wrong thread
- `crypto::rand` and `rand_alphabet` `std::abort()` on RNG failure — correct call for a network-listening daemon
- `hash_password` v3 = PBKDF2-HMAC-SHA256 600k (`src/httpcommon.cpp`) matches OWASP 2023 guidance; v1/v2 read-compat preserved
- CSRF token rotation works (`confighttp.cpp:240`, `:1407`, etc.); cleared on session expiry, login, save_user_creds
- `SDC_ALLOW_PATH_ORDER_CHANGES` retry path on `SetDisplayConfig` failure is sensible
- `lexically_relative` rewrite of `isChildPath` (`src/confighttp.cpp:605`) handles `..` traversal AND Windows case-insensitivity
- `nvenc_base` move ctor/operator= deletion (`b3c0467`) prevents the dangling `saved_init_params.encodeConfig` pointer the audit identified
- `std::deque` swap in `queue_t` (`thread_safe.h`) — `pop_front()` is O(1)
- `encode_session_t::encoder_type = encoder_type_e::UNKNOWN` default + dispatch switch falls through to error log
- TCP_NODELAY on RTSP socket (`rtsp.cpp:459`) applied with `error_code` overload — no exception on failure
- `pin_required=false` + UPnP-on path correctly returns 503 with no `getservercert` call (`nvhttp.cpp:878-885`)
- `smart_reconnect_legacy_ip_match` defaults to `false` (`config.h:197`, `config.cpp:555`); legacy IP-only fallback genuinely closed

## Outstanding (deferred — need runtime data, not code review)

1. **Build verification** — claim from `861df50` is "100% clean" build under UCRT64/g++. The two specific breakages it fixed (`ack_resolution_step_down` placement, `remote_endpoint_address` deprecation) are mechanically correct in the current tree. Highest residual breakage risk: any caller of `nvenc_base::update_bitrate` / `update_resolution` that previously expected success-on-applied. **Recommend:** run a clean `cmake --build` from scratch and report.
2. **Empirical perf claims** — changelog claims "5-15% throughput" from LTO, "Eliminates ~100KB+ allocation per frame at 60fps" from `concat_and_insert` reuse, "Eliminates RS matrix recomputation 60-240×/sec" from thread_local Reed-Solomon. Need bench numbers, not assertions. **Recommend:** 4K60 H.264 stream with `perf record` / VTune for 60s, compare CPU samples in `concat_and_insert` and Reed-Solomon `init` between `origin/master` and `HEAD`.
3. **Adaptive bitrate end-to-end** — defaults are off and `nvenc_base::update_bitrate` refuses, so even when re-enabled the bitrate computed in `bitrate_controller_t` does not propagate to the encoder. The "Server stats broadcasting" feature ships those values to clients regardless. **Recommend:** verify Moonlight clients don't react adversely to receiving stats that don't match their measured throughput.
4. **Smart-reconnect V1 token fuzz** — claim is the 32-bit token plus AES-GCM keys is enough. **Recommend:** send 1000 reconnect attempts with random `connect_data` and confirm the session is dropped after first decryption failure (not just the bad packet).

## Stash retained for comparison

`stash@{0}` (the buggy in-flight `virtual_display.cpp` rework, -79 +63 lines) is preserved for reference. The new `db152fa` commit fully supersedes it. Drop with:

```bash
git stash drop stash@{0}
```

…whenever you've finished comparing.

## Files referenced

```
src/bitrate_controller.h
src/confighttp.cpp                    (verified, unchanged)
src/crypto.cpp                        (verified, unchanged)
src/file_handler.cpp                  (modified — atomic write)
src/httpcommon.cpp                    (verified, unchanged)
src/main.cpp                          (verified — signal handlers route through terminate())
src/nvenc/nvenc_base.cpp              (verified, unchanged)
src/nvhttp.cpp                        (modified — audit log)
src/platform/windows/virtual_display.cpp  (rebuilt with RAII)
src/platform/windows/virtual_display.h    (rebuilt with RAII)
src/process.cpp                       (modified — snapshot lifecycle)
src/rtsp.cpp                          (verified, unchanged)
src/stream.cpp                        (modified — underflow guard)
src/thread_safe.h                     (verified, unchanged)
src/utility.h                         (verified, unchanged)
src/uuid.h                            (modified — UB cast)
src/video.cpp / video.h               (verified, unchanged)
src_assets/common/assets/web/configs/tabs/General.vue           (modified — UI alert)
src_assets/common/assets/web/public/assets/locale/en.json       (modified — locale strings)
SECURITY_CHANGELOG.md                 (modified — Round 2 section)
```
