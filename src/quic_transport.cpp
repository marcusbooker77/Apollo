/**
 * @file src/quic_transport.cpp
 * @brief Experimental QUIC transport capability probing.
 */
#include "quic_transport.h"

#include <mutex>

#include "logging.h"

#if defined(APOLLO_MSQUIC_AVAILABLE) && APOLLO_MSQUIC_AVAILABLE && defined(_WIN32)
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #include <windows.h>
  #include <msquic.h>
#endif

namespace quic {
  namespace {
    constexpr char alpn_name[] = "apollo-wt-v1";

#if defined(APOLLO_MSQUIC_AVAILABLE) && APOLLO_MSQUIC_AVAILABLE && defined(_WIN32)
    struct runtime_state_t {
      std::once_flag init_once;
      const QUIC_API_TABLE *api = nullptr;
      HQUIC registration = nullptr;
      bool runtime_available = false;
      std::string status = "MsQuic runtime not initialized";

      ~runtime_state_t() {
        if (registration && api) {
          api->RegistrationClose(registration);
        }
        if (api) {
          MsQuicClose(api);
        }
      }
    };

    runtime_state_t &runtime() {
      static runtime_state_t instance;
      return instance;
    }

    void initialize_runtime(runtime_state_t &state) {
      auto status = MsQuicOpen2(&state.api);
      if (QUIC_FAILED(status) || !state.api) {
        state.status = "MsQuic API table could not be opened";
        BOOST_LOG(warning) << "MsQuic support was compiled in, but the runtime API could not be opened";
        return;
      }

      QUIC_REGISTRATION_CONFIG config {
        "Apollo",
        QUIC_EXECUTION_PROFILE_LOW_LATENCY,
      };

      status = state.api->RegistrationOpen(&config, &state.registration);
      if (QUIC_FAILED(status) || !state.registration) {
        state.status = "MsQuic registration could not be opened";
        BOOST_LOG(warning) << "MsQuic API opened, but QUIC registration could not be created";
        return;
      }

      state.runtime_available = true;
      state.status = "MsQuic API and registration are available for experimental QUIC and WebTransport work";
      BOOST_LOG(info) << "MsQuic runtime probing is active for experimental transport support";
    }
#endif
  }  // namespace

  capabilities_t capabilities() {
#if defined(APOLLO_MSQUIC_AVAILABLE) && APOLLO_MSQUIC_AVAILABLE && defined(_WIN32)
    auto &state = runtime();
    std::call_once(state.init_once, [&]() {
      initialize_runtime(state);
    });

    return capabilities_t {
      .compiled = true,
      .runtime_available = state.runtime_available,
      .alpn = alpn_name,
      .status = state.status,
    };
#else
    return capabilities_t {
      .compiled = false,
      .runtime_available = false,
      .alpn = alpn_name,
      .status = "Apollo was built without experimental MsQuic transport support",
    };
#endif
  }
}  // namespace quic
