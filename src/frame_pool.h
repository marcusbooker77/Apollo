/**
 * @file frame_pool.h
 * @brief High-performance frame buffer pool for zero-allocation video streaming
 * 
 * This implementation provides:
 * - Zero-allocation frame acquisition during steady-state streaming
 * - Thread-safe lock-free operations for high concurrency
 * - Automatic pool sizing based on stream requirements
 * - Support for both software and hardware frames
 */

#pragma once

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <condition_variable>
#include <mutex>
#include <string>
#include <vector>

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/hwcontext.h>
}

namespace video {

/**
 * @brief Configuration for frame pool initialization
 */
struct frame_pool_config_t {
    int width;                    ///< Frame width in pixels
    int height;                   ///< Frame height in pixels
    AVPixelFormat pixel_format;   ///< Pixel format (e.g., AV_PIX_FMT_YUV420P)
    size_t pool_size;            ///< Number of frames to pre-allocate
    bool hardware_frames;         ///< Use hardware frames (CUDA, D3D11, etc.)
    AVBufferRef* hw_device_ctx;  ///< Hardware device context (if hardware_frames = true)
    
    frame_pool_config_t()
        : width(0)
        , height(0)
        , pixel_format(AV_PIX_FMT_NONE)
        , pool_size(8)  // Default: 8 frames
        , hardware_frames(false)
        , hw_device_ctx(nullptr)
    {}
};

/**
 * @brief RAII wrapper for frame pool entries
 * 
 * Automatically returns frame to pool when destroyed.
 */
class pooled_frame_t {
public:
    pooled_frame_t() = default;
    
    pooled_frame_t(AVFrame* frame, std::function<void(AVFrame*)> deleter)
        : frame_(frame)
        , deleter_(std::move(deleter))
    {}
    
    ~pooled_frame_t() {
        if (frame_ && deleter_) {
            deleter_(frame_);
        }
    }
    
    // Non-copyable, movable
    pooled_frame_t(const pooled_frame_t&) = delete;
    pooled_frame_t& operator=(const pooled_frame_t&) = delete;
    
    pooled_frame_t(pooled_frame_t&& other) noexcept
        : frame_(other.frame_)
        , deleter_(std::move(other.deleter_))
    {
        other.frame_ = nullptr;
    }
    
    pooled_frame_t& operator=(pooled_frame_t&& other) noexcept {
        if (this != &other) {
            if (frame_ && deleter_) {
                deleter_(frame_);
            }
            frame_ = other.frame_;
            deleter_ = std::move(other.deleter_);
            other.frame_ = nullptr;
        }
        return *this;
    }
    
    AVFrame* get() const { return frame_; }
    AVFrame* operator->() const { return frame_; }
    explicit operator bool() const { return frame_ != nullptr; }
    
private:
    AVFrame* frame_ = nullptr;
    std::function<void(AVFrame*)> deleter_;
};

/**
 * @brief High-performance frame buffer pool
 * 
 * Thread-safe frame pool with the following features:
 * - Lock-free acquisition in common case
 * - Automatic growth if pool is exhausted
 * - Statistics tracking for performance monitoring
 * - Support for both SW and HW frames
 */
class frame_pool_t {
public:
    /**
     * @brief Statistics about pool usage
     */
    struct statistics_t {
        std::atomic<uint64_t> total_acquisitions{0};
        std::atomic<uint64_t> total_releases{0};
        std::atomic<uint64_t> pool_hits{0};        // Frame was available in pool
        std::atomic<uint64_t> pool_misses{0};      // Had to allocate new frame
        std::atomic<uint64_t> peak_usage{0};       // Maximum concurrent frames
        std::atomic<size_t> current_size{0};       // Current pool size
        std::atomic<size_t> frames_in_use{0};      // Frames currently acquired
    };

    struct statistics_snapshot_t {
        uint64_t total_acquisitions;
        uint64_t total_releases;
        uint64_t pool_hits;
        uint64_t pool_misses;
        uint64_t peak_usage;
        size_t current_size;
        size_t frames_in_use;
    };
    
    /**
     * @brief Construct a frame pool
     * @param config Pool configuration
     */
    explicit frame_pool_t(const frame_pool_config_t& config)
        : config_(config)
        , stats_()
    {
        preallocate();
    }
    
    ~frame_pool_t() {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto* frame : available_frames_) {
            av_frame_free(&frame);
        }
        available_frames_.clear();
    }
    
    // Non-copyable, non-movable (could be made movable if needed)
    frame_pool_t(const frame_pool_t&) = delete;
    frame_pool_t& operator=(const frame_pool_t&) = delete;
    
    /**
     * @brief Acquire a frame from the pool
     * 
     * This operation is lock-free in the common case where frames are available.
     * If the pool is exhausted, it will allocate a new frame.
     * 
     * @param timeout_ms Maximum time to wait for a frame (0 = no wait, -1 = infinite)
     * @return RAII wrapper containing the frame, or empty if timeout
     */
    pooled_frame_t acquire(int timeout_ms = -1) {
        stats_.total_acquisitions++;
        
        AVFrame* frame = nullptr;
        
        // Fast path: try lock-free acquisition
        {
            std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
            if (lock.owns_lock() && !available_frames_.empty()) {
                frame = available_frames_.back();
                available_frames_.pop_back();
                stats_.pool_hits++;
            }
        }
        
        // Slow path: wait for frame or allocate new one
        if (!frame) {
            if (timeout_ms != 0) {
                std::unique_lock<std::mutex> lock(mutex_);
                
                if (timeout_ms < 0) {
                    // Wait indefinitely
                    cv_.wait(lock, [this] { return !available_frames_.empty(); });
                } else {
                    // Wait with timeout
                    if (!cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                                     [this] { return !available_frames_.empty(); })) {
                        // Timeout - return empty frame
                        return pooled_frame_t();
                    }
                }
                
                frame = available_frames_.back();
                available_frames_.pop_back();
                stats_.pool_hits++;
            } else {
                // No wait - allocate new frame
                frame = allocate_frame();
                if (frame) {
                    stats_.pool_misses++;
                    stats_.current_size++;
                }
            }
        }
        
        if (frame) {
            if (!prepare_frame(frame) || !reset_frame_for_reuse(frame)) {
                av_frame_free(&frame);
                stats_.current_size--;
                return pooled_frame_t();
            }

            // Update statistics
            stats_.frames_in_use++;
            size_t current_usage = stats_.frames_in_use.load();
            size_t peak = stats_.peak_usage.load();
            while (current_usage > peak && 
                   !stats_.peak_usage.compare_exchange_weak(peak, current_usage)) {
                // Retry CAS
            }
            
            // Create RAII wrapper with custom deleter
            auto deleter = [this](AVFrame* f) { release(f); };
            return pooled_frame_t(frame, deleter);
        }
        
        return pooled_frame_t();
    }
    
    /**
     * @brief Get current pool statistics
     */
    statistics_snapshot_t get_statistics() const {
        return {
            stats_.total_acquisitions.load(),
            stats_.total_releases.load(),
            stats_.pool_hits.load(),
            stats_.pool_misses.load(),
            stats_.peak_usage.load(),
            stats_.current_size.load(),
            stats_.frames_in_use.load(),
        };
    }
    
    /**
     * @brief Shrink pool to target size, keeping at least min_frames
     * @param target_size Target number of frames in pool
     * @param min_frames Minimum frames to keep
     */
    void shrink(size_t target_size = 0, size_t min_frames = 2) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        while (available_frames_.size() > target_size && 
               available_frames_.size() > min_frames) {
            AVFrame* frame = available_frames_.back();
            available_frames_.pop_back();
            av_frame_free(&frame);
            stats_.current_size--;
        }
    }
    
    /**
     * @brief Reset statistics
     */
    void reset_statistics() {
        stats_.total_acquisitions = 0;
        stats_.total_releases = 0;
        stats_.pool_hits = 0;
        stats_.pool_misses = 0;
        stats_.peak_usage = 0;
        // Don't reset current_size or frames_in_use
    }
    
private:
    /**
     * @brief Pre-allocate frames according to pool configuration
     */
    void preallocate() {
        std::lock_guard<std::mutex> lock(mutex_);
        
        for (size_t i = 0; i < config_.pool_size; ++i) {
            AVFrame* frame = allocate_frame();
            if (frame) {
                available_frames_.push_back(frame);
                stats_.current_size++;
            }
        }
    }
    
    /**
     * @brief Allocate a new frame
     */
    AVFrame* allocate_frame() {
        AVFrame* frame = av_frame_alloc();
        if (!frame) {
            return nullptr;
        }

        if (!prepare_frame(frame)) {
            av_frame_free(&frame);
            return nullptr;
        }

        return frame;
    }

    bool prepare_frame(AVFrame* frame) {
        if (!frame) {
            return false;
        }

        frame->width = config_.width;
        frame->height = config_.height;
        frame->format = config_.pixel_format;

        if (config_.hardware_frames && config_.hw_device_ctx) {
            if (frame->hw_frames_ctx || frame->buf[0]) {
                return true;
            }

            frame->hw_frames_ctx = av_buffer_ref(config_.hw_device_ctx);
            if (!frame->hw_frames_ctx) {
                return false;
            }

            if (av_hwframe_get_buffer(frame->hw_frames_ctx, frame, 0) < 0) {
                av_buffer_unref(&frame->hw_frames_ctx);
                return false;
            }
        } else {
            if (frame->buf[0]) {
                return true;
            }

            if (av_frame_get_buffer(frame, 32) < 0) {
                return false;
            }
        }

        return true;
    }

    bool reset_frame_for_reuse(AVFrame* frame) {
        if (!frame) {
            return false;
        }

        frame->pts = AV_NOPTS_VALUE;
        frame->pkt_dts = AV_NOPTS_VALUE;
        frame->duration = 0;
        frame->best_effort_timestamp = AV_NOPTS_VALUE;
        frame->flags = 0;
        frame->key_frame = 0;
        frame->pict_type = AV_PICTURE_TYPE_NONE;
        frame->coded_picture_number = 0;
        frame->display_picture_number = 0;
        frame->quality = 0;
        frame->opaque = nullptr;
        av_buffer_unref(&frame->opaque_ref);

        while (frame->nb_side_data > 0) {
            av_frame_remove_side_data(frame, frame->side_data[frame->nb_side_data - 1]->type);
        }

        if (av_frame_make_writable(frame) < 0) {
            return false;
        }

        return true;
    }
    
    /**
     * @brief Release a frame back to the pool
     */
    void release(AVFrame* frame) {
        if (!frame) return;
        
        stats_.total_releases++;
        stats_.frames_in_use--;
        
        // Keep backing storage warm so reacquire stays allocation-free.
        (void) reset_frame_for_reuse(frame);
        
        {
            std::lock_guard<std::mutex> lock(mutex_);
            available_frames_.push_back(frame);
        }
        
        // Wake up one waiting thread
        cv_.notify_one();
    }
    
    frame_pool_config_t config_;
    std::vector<AVFrame*> available_frames_;
    std::mutex mutex_;
    std::condition_variable cv_;
    statistics_t stats_;
};

/**
 * @brief Global frame pool manager
 * 
 * Manages multiple pools for different frame configurations.
 * Useful when streaming multiple resolutions or formats simultaneously.
 */
class frame_pool_manager_t {
public:
    static frame_pool_manager_t& instance() {
        static frame_pool_manager_t instance;
        return instance;
    }
    
    /**
     * @brief Get or create a pool for given configuration
     */
    std::shared_ptr<frame_pool_t> get_pool(const frame_pool_config_t& config) {
        std::string key = make_key(config);
        
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto it = pools_.find(key);
        if (it != pools_.end()) {
            if (auto pool = it->second.lock()) {
                return pool;
            }
        }
        
        // Create new pool
        auto pool = std::make_shared<frame_pool_t>(config);
        pools_[key] = pool;
        return pool;
    }
    
    /**
     * @brief Remove unused pools
     */
    void cleanup() {
        std::lock_guard<std::mutex> lock(mutex_);
        
        for (auto it = pools_.begin(); it != pools_.end(); ) {
            if (it->second.expired()) {
                it = pools_.erase(it);
            } else {
                ++it;
            }
        }
    }
    
private:
    frame_pool_manager_t() = default;
    
    std::string make_key(const frame_pool_config_t& config) const {
        return std::to_string(config.width) + "x" +
               std::to_string(config.height) + ":" +
               std::to_string(config.pixel_format) + ":" +
               (config.hardware_frames ? "hw" : "sw");
    }
    
    std::mutex mutex_;
    std::map<std::string, std::weak_ptr<frame_pool_t>> pools_;
};

}  // namespace video

/**
 * USAGE EXAMPLE:
 * 
 * // Initialize pool
 * video::frame_pool_config_t config;
 * config.width = 1920;
 * config.height = 1080;
 * config.pixel_format = AV_PIX_FMT_YUV420P;
 * config.pool_size = 8;
 * 
 * video::frame_pool_t pool(config);
 * 
 * // Acquire frame (RAII - automatically returned to pool)
 * {
 *     auto frame = pool.acquire();
 *     if (frame) {
 *         // Use frame->...
 *         // Frame is automatically returned when 'frame' goes out of scope
 *     }
 * }
 * 
 * // Check statistics
 * auto stats = pool.get_statistics();
 * float hit_rate = static_cast<float>(stats.pool_hits) / stats.total_acquisitions;
 * std::cout << "Pool hit rate: " << (hit_rate * 100) << "%" << std::endl;
 */
