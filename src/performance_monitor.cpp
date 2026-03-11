#include "performance_monitor.h"
#include "logging.h"
#include "telemetry.h"

#ifdef _WIN32
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #include <dxgi1_4.h>
  #include <pdh.h>
  #include <windows.h>
  #include <psapi.h>
#endif

namespace perf {
  namespace {
#ifdef _WIN32
    struct gpu_usage_sample_t {
      double usage_percent = 0.0;
      bool available = false;
    };

    struct gpu_memory_sample_t {
      uint64_t used_bytes = 0;
      bool available = false;
    };

    class gpu_sampler_t {
    public:
      ~gpu_sampler_t() {
        if (query_) {
          PdhCloseQuery(query_);
        }
      }

      gpu_usage_sample_t sample_usage_for_pid(DWORD pid) {
        if (!query_ && !initialize()) {
          return {};
        }

        if (PdhCollectQueryData(query_) != ERROR_SUCCESS) {
          return {0.0, true};
        }

        DWORD buffer_size = 0;
        DWORD item_count = 0;
        auto status = PdhGetFormattedCounterArrayW(engine_utilization_counter_, PDH_FMT_DOUBLE, &buffer_size, &item_count, nullptr);
        if (status != ERROR_MORE_DATA || buffer_size == 0) {
          return {0.0, true};
        }

        std::vector<char> buffer(buffer_size);
        auto *items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W *>(buffer.data());
        if (PdhGetFormattedCounterArrayW(engine_utilization_counter_, PDH_FMT_DOUBLE, &buffer_size, &item_count, items) != ERROR_SUCCESS) {
          return {0.0, true};
        }

        const auto pid_fragment = L"pid_" + std::to_wstring(pid) + L"_";
        double total_usage = 0.0;
        for (DWORD item_index = 0; item_index < item_count; ++item_index) {
          if (!items[item_index].szName || items[item_index].FmtValue.CStatus != ERROR_SUCCESS) {
            continue;
          }

          std::wstring instance_name {items[item_index].szName};
          if (instance_name.find(pid_fragment) != std::wstring::npos) {
            total_usage += items[item_index].FmtValue.doubleValue;
          }
        }

        return {std::clamp(total_usage, 0.0, 100.0), true};
      }

    private:
      bool initialize() {
        if (PdhOpenQueryW(nullptr, 0, &query_) != ERROR_SUCCESS) {
          query_ = nullptr;
          if (!warned_unavailable_) {
            BOOST_LOG(warning) << "GPU utilization counters are not available on this system; gpu_usage_available will remain false";
            warned_unavailable_ = true;
          }
          return false;
        }

        if (PdhAddEnglishCounterW(query_, L"\\GPU Engine(*)\\Utilization Percentage", 0, &engine_utilization_counter_) != ERROR_SUCCESS) {
          PdhCloseQuery(query_);
          query_ = nullptr;
          engine_utilization_counter_ = nullptr;
          if (!warned_unavailable_) {
            BOOST_LOG(warning) << "GPU utilization counters are not available on this system; gpu_usage_available will remain false";
            warned_unavailable_ = true;
          }
          return false;
        }

        PdhCollectQueryData(query_);
        return true;
      }

      PDH_HQUERY query_ = nullptr;
      PDH_HCOUNTER engine_utilization_counter_ = nullptr;
      bool warned_unavailable_ = false;
    };

    uint64_t filetime_to_uint64(const FILETIME &filetime) {
      ULARGE_INTEGER value {};
      value.LowPart = filetime.dwLowDateTime;
      value.HighPart = filetime.dwHighDateTime;
      return value.QuadPart;
    }

    gpu_memory_sample_t query_process_gpu_memory_bytes() {
      static bool warned_unavailable = false;
      IDXGIFactory1 *factory = nullptr;
      if (FAILED(CreateDXGIFactory1(IID_IDXGIFactory1, reinterpret_cast<void **>(&factory)))) {
        if (!warned_unavailable) {
          BOOST_LOG(warning) << "GPU memory queries are not available on this system; gpu_memory_available will remain false";
          warned_unavailable = true;
        }
        return {};
      }

      uint64_t total_usage = 0;
      bool available = false;
      for (UINT adapter_index = 0;; ++adapter_index) {
        IDXGIAdapter1 *adapter1 = nullptr;
        const auto enum_result = factory->EnumAdapters1(adapter_index, &adapter1);
        if (enum_result == DXGI_ERROR_NOT_FOUND) {
          break;
        }
        if (FAILED(enum_result) || !adapter1) {
          continue;
        }

        DXGI_ADAPTER_DESC1 adapter_desc {};
        if (SUCCEEDED(adapter1->GetDesc1(&adapter_desc)) && !(adapter_desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) {
          IDXGIAdapter3 *adapter3 = nullptr;
          if (SUCCEEDED(adapter1->QueryInterface(IID_IDXGIAdapter3, reinterpret_cast<void **>(&adapter3))) && adapter3) {
            DXGI_QUERY_VIDEO_MEMORY_INFO local_memory_info {};
            if (SUCCEEDED(adapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &local_memory_info))) {
              available = true;
              total_usage += local_memory_info.CurrentUsage;
            }
            adapter3->Release();
          }
        }

        adapter1->Release();
      }

      factory->Release();
      if (!available && !warned_unavailable) {
        BOOST_LOG(warning) << "GPU memory queries are not available on this system; gpu_memory_available will remain false";
        warned_unavailable = true;
      }

      return {total_usage, available};
    }
#endif
  }  // namespace

  performance_monitor_t::performance_monitor_t():
      start_time_(clock_t::now()) {
#ifdef _WIN32
    sampler_running_.store(true, std::memory_order_release);
    sampler_thread_ = std::thread([this]() {
      resource_sampling_loop();
    });
#endif
  }

  performance_monitor_t::~performance_monitor_t() {
    sampler_running_.store(false, std::memory_order_release);
    if (sampler_thread_.joinable()) {
      sampler_thread_.join();
    }
  }

  void performance_monitor_t::resource_sampling_loop() {
#ifdef _WIN32
    telemetry::set_thread_name("apollo-resource-sampler");
    auto process = GetCurrentProcess();
    const auto process_id = GetCurrentProcessId();
    gpu_sampler_t gpu_sampler;

    FILETIME idle_time {};
    FILETIME kernel_time {};
    FILETIME user_time {};
    FILETIME process_creation_time {};
    FILETIME process_exit_time {};
    FILETIME process_kernel_time {};
    FILETIME process_user_time {};

    if (!GetSystemTimes(&idle_time, &kernel_time, &user_time) ||
        !GetProcessTimes(process, &process_creation_time, &process_exit_time, &process_kernel_time, &process_user_time)) {
      return;
    }

    auto previous_system_time = filetime_to_uint64(kernel_time) + filetime_to_uint64(user_time);
    auto previous_process_time = filetime_to_uint64(process_kernel_time) + filetime_to_uint64(process_user_time);

    while (sampler_running_.load(std::memory_order_acquire)) {
      for (int sample_delay_step = 0; sample_delay_step < 10 && sampler_running_.load(std::memory_order_acquire); ++sample_delay_step) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }

      if (!sampler_running_.load(std::memory_order_acquire)) {
        break;
      }

      if (GetSystemTimes(&idle_time, &kernel_time, &user_time) &&
          GetProcessTimes(process, &process_creation_time, &process_exit_time, &process_kernel_time, &process_user_time)) {
        const auto current_system_time = filetime_to_uint64(kernel_time) + filetime_to_uint64(user_time);
        const auto current_process_time = filetime_to_uint64(process_kernel_time) + filetime_to_uint64(process_user_time);

        const auto system_delta = current_system_time - previous_system_time;
        const auto process_delta = current_process_time - previous_process_time;

        if (system_delta > 0) {
          update_cpu_usage(std::clamp(100.0 * static_cast<double>(process_delta) / static_cast<double>(system_delta), 0.0, 100.0));
        }

        previous_system_time = current_system_time;
        previous_process_time = current_process_time;
      }

      PROCESS_MEMORY_COUNTERS_EX process_memory_counters {};
      if (K32GetProcessMemoryInfo(process, reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&process_memory_counters), sizeof(process_memory_counters))) {
        update_memory_usage(process_memory_counters.WorkingSetSize);
      }

      auto gpu_usage_sample = gpu_sampler.sample_usage_for_pid(process_id);
      set_gpu_usage_available(gpu_usage_sample.available);
      update_gpu_usage(gpu_usage_sample.usage_percent);

      auto gpu_memory_sample = query_process_gpu_memory_bytes();
      set_gpu_memory_available(gpu_memory_sample.available);
      update_gpu_memory_usage(gpu_memory_sample.used_bytes);
    }
#endif
  }
}  // namespace perf
