#include <algorithm>
#include <chrono>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <map>
#include <random>
#include <shared_mutex>
#include <string>
#include <thread>
#include <vector>

#include "network_cache.h"
#include "sync.h"

namespace {

using clock_t = std::chrono::steady_clock;

template<typename Fn>
double time_ms(Fn &&fn) {
  const auto start = clock_t::now();
  fn();
  const auto elapsed = clock_t::now() - start;
  return std::chrono::duration<double, std::milli>(elapsed).count();
}

void benchmark_cache_hits() {
  net::lru_cache_t<std::string, int> cache(4096, 300);
  std::vector<std::string> keys;
  keys.reserve(4096);

  for (int i = 0; i < 4096; ++i) {
    keys.emplace_back("192.168.1." + std::to_string(i % 255));
    cache.put(keys.back(), i);
  }

  constexpr int iterations = 500000;
  volatile int sink = 0;
  const double elapsed = time_ms([&]() {
    for (int i = 0; i < iterations; ++i) {
      int value = 0;
      cache.get(keys[i % keys.size()], value);
      sink += value;
    }
  });

  const auto stats = cache.get_statistics();
  std::cout << "cache_hits_ms=" << std::fixed << std::setprecision(2) << elapsed
            << " hit_rate=" << std::setprecision(4) << stats.hit_rate()
            << " sink=" << sink << '\n';
}

void benchmark_session_lookup() {
  sync_util::sync_t<std::map<int, int>, std::shared_mutex> sessions;
  {
    auto lock = sessions.unique_lock();
    for (int i = 0; i < 50000; ++i) {
      sessions->emplace(i, i * 2);
    }
  }

  constexpr int iterations = 300000;
  volatile int sink = 0;
  const double elapsed = time_ms([&]() {
    for (int i = 0; i < iterations; ++i) {
      auto lock = sessions.shared_lock();
      auto it = sessions->find(i % 50000);
      sink += it != sessions->end() ? it->second : 0;
    }
  });

  std::cout << "session_lookup_ms=" << std::fixed << std::setprecision(2) << elapsed
            << " sink=" << sink << '\n';
}

void copy_plane_linewise(
  std::uint8_t *dst,
  const std::uint8_t *src,
  int dst_linesize,
  int src_linesize,
  std::size_t line_width,
  int lines
) {
  for (int line = 0; line < lines; ++line) {
    std::memcpy(dst + (line * dst_linesize), src + (line * src_linesize), line_width);
  }
}

void benchmark_plane_copy() {
  constexpr int width = 2560;
  constexpr int height = 1440;
  constexpr int stride = width;
  std::vector<std::uint8_t> src(stride * height, 7);
  std::vector<std::uint8_t> dst(stride * height, 0);

  constexpr int iterations = 400;
  const double bulk = time_ms([&]() {
    for (int i = 0; i < iterations; ++i) {
      std::memcpy(dst.data(), src.data(), src.size());
    }
  });

  const double linewise = time_ms([&]() {
    for (int i = 0; i < iterations; ++i) {
      copy_plane_linewise(dst.data(), src.data(), stride, stride, width, height);
    }
  });

  std::cout << "plane_copy_bulk_ms=" << std::fixed << std::setprecision(2) << bulk
            << " plane_copy_linewise_ms=" << linewise << '\n';
}

}  // namespace

int main() {
  std::cout << "apollo_benchmarks\n";
  benchmark_cache_hits();
  benchmark_session_lookup();
  benchmark_plane_copy();
  return 0;
}
