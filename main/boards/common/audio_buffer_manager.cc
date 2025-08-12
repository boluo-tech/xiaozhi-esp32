#include "audio_buffer_manager.h"
#include <esp_log.h>
#include <algorithm>
#include <memory>

static const char* TAG = "AudioBufferManager";

AudioBufferManager::AudioBufferManager() 
    : initialized_(false), max_preload_size_(1024 * 1024) { // 1MB预加载限制
    ESP_LOGI(TAG, "AudioBufferManager created");
}

AudioBufferManager::~AudioBufferManager() {
    Deinitialize();
    ESP_LOGI(TAG, "AudioBufferManager destroyed");
}

bool AudioBufferManager::Initialize(const MemoryPoolConfig& config) {
    ESP_LOGI(TAG, "Initializing AudioBufferManager with config: block_size=%zu, block_count=%zu", 
             config.block_size, config.block_count);
    
    if (initialized_) {
        ESP_LOGW(TAG, "AudioBufferManager already initialized");
        return true;
    }
    
    config_ = config;
    
    // 创建内存池互斥锁
    pool_mutex_ = xSemaphoreCreateMutex();
    if (!pool_mutex_) {
        ESP_LOGE(TAG, "Failed to create pool mutex");
        return false;
    }
    
    // 创建预加载队列互斥锁
    preload_mutex_ = xSemaphoreCreateMutex();
    if (!preload_mutex_) {
        ESP_LOGE(TAG, "Failed to create preload mutex");
        vSemaphoreDelete(pool_mutex_);
        pool_mutex_ = nullptr;
        return false;
    }
    
    // 初始化内存块
    blocks_.resize(config.block_count);
    for (size_t i = 0; i < config.block_count; ++i) {
        blocks_[i].data = static_cast<uint8_t*>(heap_caps_malloc(config.block_size, MALLOC_CAP_SPIRAM));
        if (!blocks_[i].data) {
            ESP_LOGE(TAG, "Failed to allocate block %zu", i);
            // 清理已分配的内存
            for (size_t j = 0; j < i; ++j) {
                heap_caps_free(blocks_[j].data);
            }
            vSemaphoreDelete(pool_mutex_);
            vSemaphoreDelete(preload_mutex_);
            pool_mutex_ = nullptr;
            preload_mutex_ = nullptr;
            return false;
        }
        
        blocks_[i].size = config.block_size;
        blocks_[i].timestamp = 0;
        blocks_[i].is_used = false;
        
        // 添加到空闲队列
        free_blocks_.push(i);
    }
    
    // 初始化环形缓冲区
    ring_buffer_ = std::make_unique<RingBuffer>(config.block_size * 2); // 2个块大小的环形缓冲区
    if (!ring_buffer_->Initialize()) {
        ESP_LOGE(TAG, "Failed to initialize ring buffer");
        Deinitialize();
        return false;
    }
    
    // 初始化统计信息
    total_allocated_ = 0;
    peak_usage_ = 0;
    total_writes_ = 0;
    total_reads_ = 0;
    
    initialized_ = true;
    
    ESP_LOGI(TAG, "AudioBufferManager initialized successfully with %zu blocks", config.block_count);
    return true;
}

void AudioBufferManager::Deinitialize() {
    if (!initialized_) {
        return;
    }
    
    ESP_LOGI(TAG, "Deinitializing AudioBufferManager");
    
    // 清理预加载队列
    ClearPreloadQueue();
    
    // 释放所有内存块
    ReleaseAllBlocks();
    
    // 清理环形缓冲区
    if (ring_buffer_) {
        ring_buffer_->Deinitialize();
        ring_buffer_.reset();
    }
    
    // 删除互斥锁
    if (pool_mutex_) {
        vSemaphoreDelete(pool_mutex_);
        pool_mutex_ = nullptr;
    }
    
    if (preload_mutex_) {
        vSemaphoreDelete(preload_mutex_);
        preload_mutex_ = nullptr;
    }
    
    // 清理统计信息
    total_allocated_ = 0;
    peak_usage_ = 0;
    total_writes_ = 0;
    total_reads_ = 0;
    
    initialized_ = false;
    
    ESP_LOGI(TAG, "AudioBufferManager deinitialized");
}

AudioBlock* AudioBufferManager::AllocateBlock(size_t size, uint32_t timeout_ms) {
    if (!initialized_ || !pool_mutex_) {
        return nullptr;
    }
    
    if (xSemaphoreTake(pool_mutex_, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        ESP_LOGW(TAG, "Failed to acquire pool mutex for allocation");
        return nullptr;
    }
    
    AudioBlock* block = nullptr;
    
    if (!free_blocks_.empty()) {
        size_t block_index = free_blocks_.front();
        free_blocks_.pop();
        
        blocks_[block_index].is_used = true;
        blocks_[block_index].timestamp = esp_timer_get_time() / 1000; // 毫秒时间戳
        blocks_[block_index].size = std::min(size, config_.block_size);
        
        block = &blocks_[block_index];
        total_allocated_++;
        
        // 更新峰值使用量
        size_t current_usage = config_.block_count - free_blocks_.size();
        if (current_usage > peak_usage_) {
            peak_usage_ = current_usage;
        }
        
        ESP_LOGD(TAG, "Allocated block %zu, size: %zu, total_allocated: %zu", 
                 block_index, size, total_allocated_.load());
    } else {
        ESP_LOGW(TAG, "No free blocks available for allocation");
    }
    
    xSemaphoreGive(pool_mutex_);
    return block;
}

bool AudioBufferManager::ReleaseBlock(AudioBlock* block) {
    if (!initialized_ || !pool_mutex_ || !block) {
        return false;
    }
    
    if (xSemaphoreTake(pool_mutex_, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "Failed to acquire pool mutex for release");
        return false;
    }
    
    bool released = false;
    
    // 查找对应的块索引
    for (size_t i = 0; i < blocks_.size(); ++i) {
        if (&blocks_[i] == block) {
            if (blocks_[i].is_used) {
                blocks_[i].is_used = false;
                blocks_[i].size = 0;
                blocks_[i].timestamp = 0;
                
                free_blocks_.push(i);
                total_allocated_--;
                
                released = true;
                ESP_LOGD(TAG, "Released block %zu, total_allocated: %zu", i, total_allocated_.load());
            }
            break;
        }
    }
    
    xSemaphoreGive(pool_mutex_);
    return released;
}

void AudioBufferManager::ReleaseAllBlocks() {
    if (!initialized_ || !pool_mutex_) {
        return;
    }
    
    if (xSemaphoreTake(pool_mutex_, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "Failed to acquire pool mutex for release all");
        return;
    }
    
    // 重置所有块
    for (size_t i = 0; i < blocks_.size(); ++i) {
        blocks_[i].is_used = false;
        blocks_[i].size = 0;
        blocks_[i].timestamp = 0;
    }
    
    // 清空并重新填充空闲队列
    while (!free_blocks_.empty()) {
        free_blocks_.pop();
    }
    
    for (size_t i = 0; i < blocks_.size(); ++i) {
        free_blocks_.push(i);
    }
    
    total_allocated_ = 0;
    
    ESP_LOGI(TAG, "Released all blocks");
    xSemaphoreGive(pool_mutex_);
}

size_t AudioBufferManager::WriteToBuffer(const uint8_t* data, size_t len) {
    if (!initialized_ || !ring_buffer_) {
        return 0;
    }
    
    size_t written = ring_buffer_->Write(data, len);
    total_writes_ += written;
    
    ESP_LOGD(TAG, "Wrote %zu bytes to ring buffer, total_writes: %zu", written, total_writes_.load());
    return written;
}

size_t AudioBufferManager::ReadFromBuffer(uint8_t* data, size_t len) {
    if (!initialized_ || !ring_buffer_) {
        return 0;
    }
    
    size_t read = ring_buffer_->Read(data, len);
    total_reads_ += read;
    
    ESP_LOGD(TAG, "Read %zu bytes from ring buffer, total_reads: %zu", read, total_reads_.load());
    return read;
}

void AudioBufferManager::ClearBuffer() {
    if (ring_buffer_) {
        ring_buffer_->Clear();
        ESP_LOGI(TAG, "Ring buffer cleared");
    }
}

bool AudioBufferManager::AddToPreloadQueue(const AudioBlock& block) {
    if (!initialized_ || !preload_mutex_) {
        return false;
    }
    
    if (xSemaphoreTake(preload_mutex_, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "Failed to acquire preload mutex");
        return false;
    }
    
    bool added = false;
    
    // 检查预加载队列大小限制
    size_t current_size = preload_queue_.size() * config_.block_size;
    if (current_size + block.size <= max_preload_size_) {
        preload_queue_.push(block);
        added = true;
        ESP_LOGD(TAG, "Added block to preload queue, size: %zu, queue_size: %zu", 
                 block.size, preload_queue_.size());
    } else {
        ESP_LOGW(TAG, "Preload queue full, cannot add block");
    }
    
    xSemaphoreGive(preload_mutex_);
    return added;
}

AudioBlock* AudioBufferManager::GetFromPreloadQueue() {
    if (!initialized_ || !preload_mutex_) {
        return nullptr;
    }
    
    if (xSemaphoreTake(preload_mutex_, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "Failed to acquire preload mutex");
        return nullptr;
    }
    
    AudioBlock* block = nullptr;
    
    if (!preload_queue_.empty()) {
        block = new AudioBlock(preload_queue_.front());
        preload_queue_.pop();
        ESP_LOGD(TAG, "Retrieved block from preload queue, queue_size: %zu", preload_queue_.size());
    }
    
    xSemaphoreGive(preload_mutex_);
    return block;
}

void AudioBufferManager::ClearPreloadQueue() {
    if (!initialized_ || !preload_mutex_) {
        return;
    }
    
    if (xSemaphoreTake(preload_mutex_, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "Failed to acquire preload mutex for clear");
        return;
    }
    
    while (!preload_queue_.empty()) {
        preload_queue_.pop();
    }
    
    ESP_LOGI(TAG, "Preload queue cleared");
    xSemaphoreGive(preload_mutex_);
}

size_t AudioBufferManager::GetFreeBlockCount() const {
    if (!initialized_ || !pool_mutex_) {
        return 0;
    }
    
    if (xSemaphoreTake(pool_mutex_, pdMS_TO_TICKS(100)) != pdTRUE) {
        return 0;
    }
    
    size_t free_count = free_blocks_.size();
    xSemaphoreGive(pool_mutex_);
    return free_count;
}

size_t AudioBufferManager::GetUsedBlockCount() const {
    if (!initialized_) {
        return 0;
    }
    
    return config_.block_count - GetFreeBlockCount();
}

size_t AudioBufferManager::GetBufferUsage() const {
    if (!ring_buffer_) {
        return 0;
    }
    
    return ring_buffer_->GetAvailableData();
}

size_t AudioBufferManager::GetPreloadQueueSize() const {
    if (!initialized_ || !preload_mutex_) {
        return 0;
    }
    
    if (xSemaphoreTake(preload_mutex_, pdMS_TO_TICKS(100)) != pdTRUE) {
        return 0;
    }
    
    size_t queue_size = preload_queue_.size();
    xSemaphoreGive(preload_mutex_);
    return queue_size;
}

bool AudioBufferManager::IsMemoryLow() const {
    if (!initialized_) {
        return false;
    }
    
    size_t free_blocks = GetFreeBlockCount();
    size_t total_blocks = config_.block_count;
    
    // 当空闲块少于20%时认为内存不足
    return (free_blocks * 100 / total_blocks) < 20;
}

void AudioBufferManager::Defragment() {
    if (!initialized_ || !pool_mutex_) {
        return;
    }
    
    ESP_LOGI(TAG, "Starting memory defragmentation");
    
    if (xSemaphoreTake(pool_mutex_, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGW(TAG, "Failed to acquire pool mutex for defragmentation");
        return;
    }
    
    // 简单的碎片整理：重新排列已使用的块
    std::vector<size_t> used_indices;
    for (size_t i = 0; i < blocks_.size(); ++i) {
        if (blocks_[i].is_used) {
            used_indices.push_back(i);
        }
    }
    
    // 将已使用的块移到前面
    for (size_t i = 0; i < used_indices.size(); ++i) {
        if (used_indices[i] != i) {
            std::swap(blocks_[i], blocks_[used_indices[i]]);
        }
    }
    
    // 重新构建空闲队列
    while (!free_blocks_.empty()) {
        free_blocks_.pop();
    }
    
    for (size_t i = used_indices.size(); i < blocks_.size(); ++i) {
        free_blocks_.push(i);
    }
    
    ESP_LOGI(TAG, "Memory defragmentation completed");
    xSemaphoreGive(pool_mutex_);
}

void AudioBufferManager::PrintMemoryStats() const {
    if (!initialized_) {
        ESP_LOGI(TAG, "AudioBufferManager not initialized");
        return;
    }
    
    size_t free_blocks = GetFreeBlockCount();
    size_t used_blocks = GetUsedBlockCount();
    size_t buffer_usage = GetBufferUsage();
    size_t preload_size = GetPreloadQueueSize();
    
    ESP_LOGI(TAG, "=== AudioBufferManager Memory Stats ===");
    ESP_LOGI(TAG, "Total blocks: %zu", config_.block_count);
    ESP_LOGI(TAG, "Free blocks: %zu", free_blocks);
    ESP_LOGI(TAG, "Used blocks: %zu", used_blocks);
    ESP_LOGI(TAG, "Block size: %zu bytes", config_.block_size);
    ESP_LOGI(TAG, "Total allocated: %zu", total_allocated_.load());
    ESP_LOGI(TAG, "Peak usage: %zu", peak_usage_.load());
    ESP_LOGI(TAG, "Buffer usage: %zu bytes", buffer_usage);
    ESP_LOGI(TAG, "Preload queue size: %zu", preload_size);
    ESP_LOGI(TAG, "Total writes: %zu", total_writes_.load());
    ESP_LOGI(TAG, "Total reads: %zu", total_reads_.load());
    ESP_LOGI(TAG, "Memory low: %s", IsMemoryLow() ? "YES" : "NO");
    ESP_LOGI(TAG, "=====================================");
}

void AudioBufferManager::OptimizeForStreaming() {
    ESP_LOGI(TAG, "Optimizing for streaming mode");
    // 可以在这里调整缓冲区大小和预加载策略
}

void AudioBufferManager::OptimizeForPlayback() {
    ESP_LOGI(TAG, "Optimizing for playback mode");
    // 可以在这里调整缓冲区大小和预加载策略
} 