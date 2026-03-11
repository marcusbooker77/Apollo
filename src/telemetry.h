/**
 * @file src/telemetry.h
 * @brief Optional profiling and tracing integrations.
 */
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#if defined(APOLLO_TRACY_AVAILABLE) && APOLLO_TRACY_AVAILABLE
  #include <tracy/Tracy.hpp>
  #define APOLLO_TRACY_SCOPE(name_literal) ZoneScopedN(name_literal)
  #define APOLLO_TRACY_FRAME_MARK(name_literal) FrameMarkNamed(name_literal)
#else
  #define APOLLO_TRACY_SCOPE(name_literal) do {} while (0)
  #define APOLLO_TRACY_FRAME_MARK(name_literal) do {} while (0)
#endif

namespace telemetry {
  struct capabilities_t {
    bool tracy_compiled = false;
    bool tracy_active = false;
    bool opentelemetry_compiled = false;
    bool opentelemetry_active = false;
    bool web_experiments_enabled = false;
    std::string opentelemetry_endpoint;
  };

  class scoped_span_t {
  public:
    explicit scoped_span_t(std::string_view name);
    ~scoped_span_t();

    scoped_span_t(const scoped_span_t &) = delete;
    scoped_span_t &operator=(const scoped_span_t &) = delete;

    void set_attribute(std::string_view key, std::string_view value);
    void set_attribute(std::string_view key, int64_t value);
    void set_attribute(std::string_view key, uint64_t value);
    void set_attribute(std::string_view key, double value);
    void set_attribute(std::string_view key, bool value);

  private:
    struct impl_t;
    std::unique_ptr<impl_t> impl_;
  };

  void initialize();
  void shutdown();
  void set_thread_name(const char *name);
  capabilities_t capabilities();

  void plot_cpu_usage(double percent);
  void plot_memory_usage_bytes(uint64_t bytes);
  void plot_gpu_usage(double percent);
  void plot_gpu_memory_usage_bytes(uint64_t bytes);
  void plot_active_sessions(uint64_t count);
}  // namespace telemetry
