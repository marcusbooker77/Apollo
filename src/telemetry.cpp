/**
 * @file src/telemetry.cpp
 * @brief Optional profiling and tracing integrations.
 */
#include "telemetry.h"

#include <cstdlib>
#include <mutex>
#include <string>
#include <utility>

#include "logging.h"

#if defined(APOLLO_OPENTELEMETRY_AVAILABLE) && APOLLO_OPENTELEMETRY_AVAILABLE
  #include <opentelemetry/exporters/otlp/otlp_http_exporter_factory.h>
  #include <opentelemetry/sdk/resource/resource.h>
  #include <opentelemetry/sdk/trace/batch_span_processor_factory.h>
  #include <opentelemetry/sdk/trace/tracer_provider_factory.h>
  #include <opentelemetry/trace/provider.h>
  #include <opentelemetry/trace/scope.h>
#endif

namespace telemetry {
  namespace {
    struct telemetry_state_t {
      std::once_flag initialized_once;
      bool initialized = false;
      bool opentelemetry_active = false;
      std::string opentelemetry_endpoint;

#if defined(APOLLO_OPENTELEMETRY_AVAILABLE) && APOLLO_OPENTELEMETRY_AVAILABLE
      opentelemetry::nostd::shared_ptr<opentelemetry::trace::Tracer> tracer;
#endif
    };

    telemetry_state_t &state() {
      static telemetry_state_t instance;
      return instance;
    }

#if defined(APOLLO_OPENTELEMETRY_AVAILABLE) && APOLLO_OPENTELEMETRY_AVAILABLE
    void initialize_opentelemetry(telemetry_state_t &telemetry_state) {
      const char *endpoint_env = std::getenv("APOLLO_OTEL_EXPORTER_OTLP_ENDPOINT");
      if (!endpoint_env || !*endpoint_env) {
        endpoint_env = std::getenv("OTEL_EXPORTER_OTLP_ENDPOINT");
      }
      if (!endpoint_env || !*endpoint_env) {
        BOOST_LOG(info) << "OpenTelemetry support compiled in, but no OTLP endpoint was configured; trace export remains inactive";
        return;
      }

      using namespace opentelemetry;
      namespace otlp = exporter::otlp;
      namespace resource = sdk::resource;
      namespace sdktrace = sdk::trace;
      namespace trace = opentelemetry::trace;

      otlp::OtlpHttpExporterOptions exporter_options;
      exporter_options.url = endpoint_env;

      auto exporter = otlp::OtlpHttpExporterFactory::Create(exporter_options);
      auto processor = sdktrace::BatchSpanProcessorFactory::Create(std::move(exporter));

      auto service_name = std::getenv("APOLLO_OTEL_SERVICE_NAME");
      auto environment = std::getenv("APOLLO_OTEL_ENVIRONMENT");
      resource::ResourceAttributes attributes {
        {"service.name", std::string {service_name && *service_name ? service_name : PROJECT_NAME}},
        {"service.version", std::string {PROJECT_VERSION}},
        {"deployment.environment", std::string {environment && *environment ? environment : "local"}},
      };

      auto provider = sdktrace::TracerProviderFactory::Create(
        std::move(processor),
        resource::Resource::Create(attributes)
      );

      trace::Provider::SetTracerProvider(provider);
      telemetry_state.tracer = provider->GetTracer("apollo", PROJECT_VERSION);
      telemetry_state.opentelemetry_active = static_cast<bool>(telemetry_state.tracer);
      telemetry_state.opentelemetry_endpoint = exporter_options.url;

      if (telemetry_state.opentelemetry_active) {
        BOOST_LOG(info) << "OpenTelemetry trace export enabled at " << telemetry_state.opentelemetry_endpoint;
      }
    }
#endif
  }  // namespace

  struct scoped_span_t::impl_t {
#if defined(APOLLO_OPENTELEMETRY_AVAILABLE) && APOLLO_OPENTELEMETRY_AVAILABLE
    opentelemetry::nostd::shared_ptr<opentelemetry::trace::Span> span;
    std::unique_ptr<opentelemetry::trace::Scope> scope;
#endif
  };

  scoped_span_t::scoped_span_t(std::string_view name) {
    initialize();
    impl_ = std::make_unique<impl_t>();

#if defined(APOLLO_OPENTELEMETRY_AVAILABLE) && APOLLO_OPENTELEMETRY_AVAILABLE
    auto &telemetry_state = state();
    if (telemetry_state.opentelemetry_active && telemetry_state.tracer) {
      impl_->span = telemetry_state.tracer->StartSpan(std::string {name});
      impl_->scope = std::make_unique<opentelemetry::trace::Scope>(impl_->span);
    }
#else
    (void) name;
#endif
  }

  scoped_span_t::~scoped_span_t() {
#if defined(APOLLO_OPENTELEMETRY_AVAILABLE) && APOLLO_OPENTELEMETRY_AVAILABLE
    if (impl_ && impl_->span) {
      impl_->span->End();
    }
#endif
  }

  void scoped_span_t::set_attribute(std::string_view key, std::string_view value) {
#if defined(APOLLO_OPENTELEMETRY_AVAILABLE) && APOLLO_OPENTELEMETRY_AVAILABLE
    if (impl_ && impl_->span) {
      impl_->span->SetAttribute(std::string {key}, std::string {value});
    }
#else
    (void) key;
    (void) value;
#endif
  }

  void scoped_span_t::set_attribute(std::string_view key, int64_t value) {
#if defined(APOLLO_OPENTELEMETRY_AVAILABLE) && APOLLO_OPENTELEMETRY_AVAILABLE
    if (impl_ && impl_->span) {
      impl_->span->SetAttribute(std::string {key}, value);
    }
#else
    (void) key;
    (void) value;
#endif
  }

  void scoped_span_t::set_attribute(std::string_view key, uint64_t value) {
#if defined(APOLLO_OPENTELEMETRY_AVAILABLE) && APOLLO_OPENTELEMETRY_AVAILABLE
    if (impl_ && impl_->span) {
      impl_->span->SetAttribute(std::string {key}, static_cast<int64_t>(value));
    }
#else
    (void) key;
    (void) value;
#endif
  }

  void scoped_span_t::set_attribute(std::string_view key, double value) {
#if defined(APOLLO_OPENTELEMETRY_AVAILABLE) && APOLLO_OPENTELEMETRY_AVAILABLE
    if (impl_ && impl_->span) {
      impl_->span->SetAttribute(std::string {key}, value);
    }
#else
    (void) key;
    (void) value;
#endif
  }

  void scoped_span_t::set_attribute(std::string_view key, bool value) {
#if defined(APOLLO_OPENTELEMETRY_AVAILABLE) && APOLLO_OPENTELEMETRY_AVAILABLE
    if (impl_ && impl_->span) {
      impl_->span->SetAttribute(std::string {key}, value);
    }
#else
    (void) key;
    (void) value;
#endif
  }

  void initialize() {
    auto &telemetry_state = state();
    std::call_once(telemetry_state.initialized_once, [&]() {
      telemetry_state.initialized = true;
#if defined(APOLLO_OPENTELEMETRY_AVAILABLE) && APOLLO_OPENTELEMETRY_AVAILABLE
      initialize_opentelemetry(telemetry_state);
#endif
    });
  }

  void shutdown() {
    auto &telemetry_state = state();

#if defined(APOLLO_OPENTELEMETRY_AVAILABLE) && APOLLO_OPENTELEMETRY_AVAILABLE
    if (telemetry_state.opentelemetry_active) {
      BOOST_LOG(info) << "OpenTelemetry trace export shutting down";
    }
#endif

    telemetry_state.opentelemetry_active = false;
  }

  void set_thread_name(const char *name) {
#if defined(APOLLO_TRACY_AVAILABLE) && APOLLO_TRACY_AVAILABLE
    tracy::SetThreadName(name);
#else
    (void) name;
#endif
  }

  capabilities_t capabilities() {
    initialize();

    const auto &telemetry_state = state();
    return capabilities_t {
      .tracy_compiled = static_cast<bool>(APOLLO_TRACY_AVAILABLE),
      .tracy_active = static_cast<bool>(APOLLO_TRACY_AVAILABLE),
      .opentelemetry_compiled = static_cast<bool>(APOLLO_OPENTELEMETRY_AVAILABLE),
      .opentelemetry_active = telemetry_state.opentelemetry_active,
      .web_experiments_enabled = static_cast<bool>(APOLLO_WEB_EXPERIMENTS),
      .opentelemetry_endpoint = telemetry_state.opentelemetry_endpoint,
    };
  }

  void plot_cpu_usage(double percent) {
#if defined(APOLLO_TRACY_AVAILABLE) && APOLLO_TRACY_AVAILABLE
    TracyPlot("apollo.cpu_usage_percent", percent);
#else
    (void) percent;
#endif
  }

  void plot_memory_usage_bytes(uint64_t bytes) {
#if defined(APOLLO_TRACY_AVAILABLE) && APOLLO_TRACY_AVAILABLE
    TracyPlot("apollo.memory_used_bytes", static_cast<double>(bytes));
#else
    (void) bytes;
#endif
  }

  void plot_gpu_usage(double percent) {
#if defined(APOLLO_TRACY_AVAILABLE) && APOLLO_TRACY_AVAILABLE
    TracyPlot("apollo.gpu_usage_percent", percent);
#else
    (void) percent;
#endif
  }

  void plot_gpu_memory_usage_bytes(uint64_t bytes) {
#if defined(APOLLO_TRACY_AVAILABLE) && APOLLO_TRACY_AVAILABLE
    TracyPlot("apollo.gpu_memory_used_bytes", static_cast<double>(bytes));
#else
    (void) bytes;
#endif
  }

  void plot_active_sessions(uint64_t count) {
#if defined(APOLLO_TRACY_AVAILABLE) && APOLLO_TRACY_AVAILABLE
    TracyPlot("apollo.active_sessions", static_cast<double>(count));
#else
    (void) count;
#endif
  }
}  // namespace telemetry
