/**
 * @file network_cache.h
 * @brief High-performance LRU cache for network address classification
 *
 * This cache optimizes the repeated address classification operations
 * in network.cpp, reducing O(n) lookups to O(1) for cached addresses.
 */

#pragma once

#include <atomic>
#include <chrono>
#include <list>
#include <mutex>
#include <shared_mutex>
#include <algorithm>
#include <array>
#include <cstdint>
#include <unordered_map>
#include <string>
#include <string_view>

// Provides net::net_e (PC, LAN, WAN) and net::from_address()
#include "network.h"

namespace net {

struct address_key_t {
    bool is_v6 {};
    std::array<std::uint8_t, 16> bytes {};

    bool operator==(const address_key_t &other) const = default;
};

struct address_key_hash_t {
    std::size_t operator()(const address_key_t &key) const noexcept {
        std::size_t seed = static_cast<std::size_t>(key.is_v6);
        for (std::uint8_t byte : key.bytes) {
            seed = (seed * 131) ^ byte;
        }
        return seed;
    }
};

inline address_key_t make_address_key(const boost::asio::ip::address &address) {
    address_key_t key;
    key.is_v6 = address.is_v6();

    if (key.is_v6) {
        auto bytes = address.to_v6().to_bytes();
        std::copy(bytes.begin(), bytes.end(), key.bytes.begin());
    } else {
        auto bytes = address.to_v4().to_bytes();
        std::copy(bytes.begin(), bytes.end(), key.bytes.begin());
    }

    return key;
}

/**
 * @brief LRU cache entry with expiration support
 */
template<typename ValueType>
struct cache_entry_t {
    ValueType value;
    std::chrono::steady_clock::time_point timestamp;

    cache_entry_t(ValueType v)
        : value(v)
        , timestamp(std::chrono::steady_clock::now())
    {}

    bool is_expired(std::chrono::seconds ttl) const {
        auto now = std::chrono::steady_clock::now();
        return (now - timestamp) > ttl;
    }
};

/**
 * @brief Thread-safe LRU cache with optional TTL
 *
 * Features:
 * - O(1) lookup, insert, and eviction
 * - Thread-safe with read-write lock
 * - Optional time-to-live (TTL) for entries
 * - Configurable size limits
 * - Statistics tracking
 */
template<typename KeyType, typename ValueType, typename HashType = std::hash<KeyType>>
class lru_cache_t {
public:
    struct statistics_t {
        std::atomic<uint64_t> hits{0};
        std::atomic<uint64_t> misses{0};
        std::atomic<uint64_t> evictions{0};
        std::atomic<uint64_t> expired{0};

        double hit_rate() const {
            uint64_t total = hits + misses;
            return total > 0 ? static_cast<double>(hits) / total : 0.0;
        }
    };

    struct statistics_snapshot_t {
        uint64_t hits;
        uint64_t misses;
        uint64_t evictions;
        uint64_t expired;

        double hit_rate() const {
            uint64_t total = hits + misses;
            return total > 0 ? static_cast<double>(hits) / total : 0.0;
        }
    };

    /**
     * @brief Construct LRU cache
     * @param max_size Maximum number of entries
     * @param ttl_seconds Time-to-live in seconds (0 = no expiration)
     */
    explicit lru_cache_t(size_t max_size, int ttl_seconds = 0)
        : max_size_(max_size)
        , ttl_(ttl_seconds)
    {}

    /**
     * @brief Get value from cache
     * @param key Key to lookup
     * @param value Output parameter for value
     * @return true if found and not expired, false otherwise
     */
    bool get(const KeyType& key, ValueType& value) {
        std::shared_lock<std::shared_mutex> read_lock(mutex_);

        auto it = cache_map_.find(key);
        if (it == cache_map_.end()) {
            stats_.misses++;
            return false;
        }

        // Check expiration
        if (ttl_.count() > 0 && it->second->second.is_expired(ttl_)) {
            stats_.expired++;
            // Upgrade to write lock to remove expired entry
            read_lock.unlock();
            std::unique_lock<std::shared_mutex> write_lock(mutex_);

            // Recheck in case another thread removed it
            it = cache_map_.find(key);
            if (it != cache_map_.end()) {
                lru_list_.erase(it->second);
                cache_map_.erase(it);
            }

            stats_.misses++;
            return false;
        }

        value = it->second->second.value;
        stats_.hits++;

        // Update recency opportunistically without blocking other readers.
        read_lock.unlock();
        std::unique_lock<std::shared_mutex> write_lock(mutex_, std::try_to_lock);

        if (write_lock.owns_lock()) {
            // Recheck after lock upgrade
            it = cache_map_.find(key);
            if (it != cache_map_.end()) {
                lru_list_.splice(lru_list_.begin(), lru_list_, it->second);
            }
        }

        return true;
    }

    /**
     * @brief Insert or update value in cache
     * @param key Key to insert
     * @param value Value to store
     */
    void put(const KeyType& key, const ValueType& value) {
        std::unique_lock<std::shared_mutex> lock(mutex_);

        auto it = cache_map_.find(key);
        if (it != cache_map_.end()) {
            // Update existing entry and move to front
            it->second->second = cache_entry_t<ValueType>(value);
            lru_list_.splice(lru_list_.begin(), lru_list_, it->second);
            return;
        }

        // Evict if at capacity
        if (cache_map_.size() >= max_size_) {
            auto last = lru_list_.back();
            cache_map_.erase(last.first);
            lru_list_.pop_back();
            stats_.evictions++;
        }

        // Insert new entry at front
        lru_list_.emplace_front(key, cache_entry_t<ValueType>(value));
        cache_map_[key] = lru_list_.begin();
    }

    /**
     * @brief Clear all entries
     */
    void clear() {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        cache_map_.clear();
        lru_list_.clear();
    }

    /**
     * @brief Get cache statistics
     */
    statistics_snapshot_t get_statistics() const {
        return {
            stats_.hits.load(),
            stats_.misses.load(),
            stats_.evictions.load(),
            stats_.expired.load(),
        };
    }

    /**
     * @brief Reset statistics counters
     */
    void reset_statistics() {
        stats_.hits = 0;
        stats_.misses = 0;
        stats_.evictions = 0;
        stats_.expired = 0;
    }

    /**
     * @brief Get current size
     */
    size_t size() const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        return cache_map_.size();
    }

private:
    using list_item_t = std::pair<KeyType, cache_entry_t<ValueType>>;
    using list_iterator_t = typename std::list<list_item_t>::iterator;

    size_t max_size_;
    std::chrono::seconds ttl_;
    mutable std::shared_mutex mutex_;
    std::list<list_item_t> lru_list_;
    std::unordered_map<KeyType, list_iterator_t, HashType> cache_map_;
    statistics_t stats_;
};

/**
 * @brief Specialized cache for network address classification
 */
class address_classification_cache_t {
public:
    /**
     * @brief Construct with default size (4096 entries) and 5-minute TTL
     */
    address_classification_cache_t()
        : cache_(4096, 300)  // 4K entries, 5-minute TTL
    {}

    /**
     * @brief Construct with custom parameters
     * @param max_entries Maximum cache size
     * @param ttl_seconds Time-to-live in seconds
     */
    address_classification_cache_t(size_t max_entries, int ttl_seconds)
        : cache_(max_entries, ttl_seconds)
    {}

    /**
     * @brief Classify address with caching
     * @param address_str Address string (IPv4 or IPv6)
     * @param classifier Function to classify address if not cached
     * @return Network type (PC, LAN, or WAN)
     */
    template<typename ClassifierFunc>
    net_e classify(const boost::asio::ip::address &address, ClassifierFunc&& classifier) {
        auto key = make_address_key(address);
        net_e result;

        // Try cache first
        if (cache_.get(key, result)) {
            return result;
        }

        // Cache miss - call classifier and cache result
        result = classifier(address);
        cache_.put(key, result);

        return result;
    }

    /**
     * @brief Get cache statistics
     */
    auto get_statistics() const {
        return cache_.get_statistics();
    }

    /**
     * @brief Clear cache
     */
    void clear() {
        cache_.clear();
    }

private:
    lru_cache_t<address_key_t, net_e, address_key_hash_t> cache_;
};

}  // namespace net

/**
 * USAGE EXAMPLE - Integration with existing network.cpp:
 *
 * // In network.cpp, add global cache:
 * namespace net {
 *     static address_classification_cache_t addr_cache(4096, 300);
 * }
 *
 * // Modify from_address() function:
 * net_e from_address(const std::string_view &view) {
 *     return addr_cache.classify(view, [](const std::string_view& addr) {
 *         // Original classification logic here
 *         auto addr_obj = normalize_address(ip::make_address(addr));
 *
 *         if (addr_obj.is_v6()) {
 *             for (auto &range : pc_ips_v6) {
 *                 if (range.hosts().find(addr_obj.to_v6()) != range.hosts().end()) {
 *                     return PC;
 *                 }
 *             }
 *             for (auto &range : lan_ips_v6) {
 *                 if (range.hosts().find(addr_obj.to_v6()) != range.hosts().end()) {
 *                     return LAN;
 *                 }
 *             }
 *         } else {
 *             for (auto &range : pc_ips_v4) {
 *                 if (range.hosts().find(addr_obj.to_v4()) != range.hosts().end()) {
 *                     return PC;
 *                 }
 *             }
 *             for (auto &range : lan_ips_v4) {
 *                 if (range.hosts().find(addr_obj.to_v4()) != range.hosts().end()) {
 *                     return LAN;
 *                 }
 *             }
 *         }
 *         return WAN;
 *     });
 * }
 *
 * // Print cache stats periodically:
 * void print_network_cache_stats() {
 *     auto stats = addr_cache.get_statistics();
 *     BOOST_LOG(info) << "Address cache hit rate: "
 *                     << (stats.hit_rate() * 100) << "%";
 * }
 */
