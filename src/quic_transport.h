/**
 * @file src/quic_transport.h
 * @brief Experimental QUIC transport capability probing.
 */
#pragma once

#include <string>

namespace quic {
  struct capabilities_t {
    bool compiled = false;
    bool runtime_available = false;
    std::string alpn;
    std::string status;
  };

  capabilities_t capabilities();
}  // namespace quic
