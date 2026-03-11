/**
 * @file src/sync.h
 * @brief Declarations for synchronization utilities.
 */
#pragma once

// standard includes
#include <array>
#include <mutex>
#include <shared_mutex>
#include <utility>

namespace sync_util {

  template<class T, class M = std::mutex>
  class sync_t {
  public:
    using value_t = T;
    using mutex_t = M;

    /**
     * @brief Acquire exclusive lock (original behavior)
     * @return RAII lock guard that releases on destruction
     */
    std::lock_guard<mutex_t> lock() {
      return std::lock_guard {_lock};
    }

    /**
     * @brief Acquire shared lock for read operations
     *
     * OPTIMIZATION: Added to support read-write lock optimization.
     *
     * When sync_t is instantiated with std::shared_mutex, this method allows
     * multiple threads to hold shared locks simultaneously for read-only operations,
     * significantly reducing lock contention.
     *
     * Usage:
     *   sync_util::sync_t<std::map<K, V>, std::shared_mutex> data;
     *   auto read_lock = data.shared_lock();  // Multiple threads can hold this
     *   auto value = data->find(key);         // Read-only access
     *
     * @return RAII shared lock that releases on destruction
     * @note Only use for read-only operations. Use unique_lock() for writes.
     */
    std::shared_lock<mutex_t> shared_lock() {
      return std::shared_lock {_lock};
    }

    /**
     * @brief Acquire unique lock for write operations
     *
     * OPTIMIZATION: Added to support read-write lock optimization.
     *
     * When sync_t is instantiated with std::shared_mutex, this method provides
     * exclusive access for write operations, blocking all other threads.
     *
     * Usage:
     *   sync_util::sync_t<std::map<K, V>, std::shared_mutex> data;
     *   auto write_lock = data.unique_lock();  // Exclusive access
     *   data->insert({key, value});            // Write operation
     *
     * @return RAII unique lock that releases on destruction
     * @note Use this for all write operations. Multiple threads cannot hold this simultaneously.
     */
    std::unique_lock<mutex_t> unique_lock() {
      return std::unique_lock {_lock};
    }

    template<class... Args>
    sync_t(Args &&...args):
        raw {std::forward<Args>(args)...} {
    }

    sync_t &operator=(sync_t &&other) noexcept {
      std::lock(_lock, other._lock);

      raw = std::move(other.raw);

      _lock.unlock();
      other._lock.unlock();

      return *this;
    }

    sync_t &operator=(sync_t &other) noexcept {
      std::lock(_lock, other._lock);

      raw = other.raw;

      _lock.unlock();
      other._lock.unlock();

      return *this;
    }

    template<class V>
    sync_t &operator=(V &&val) {
      auto lg = lock();

      raw = val;

      return *this;
    }

    sync_t &operator=(const value_t &val) noexcept {
      auto lg = lock();

      raw = val;

      return *this;
    }

    sync_t &operator=(value_t &&val) noexcept {
      auto lg = lock();

      raw = std::move(val);

      return *this;
    }

    value_t *operator->() {
      return &raw;
    }

    value_t &operator*() {
      return raw;
    }

    const value_t &operator*() const {
      return raw;
    }

    value_t raw;

  private:
    mutex_t _lock;
  };

}  // namespace sync_util
