#ifndef AUDIO_BUFFER_MANAGER_H
#define AUDIO_BUFFER_MANAGER_H

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <esp_heap_caps.h>
#include <vector>
#include <queue>
#include <atomic>
#include <cstring>
#include <memory>

// 音频数据块
struct AudioBlock {
    uint8_t* data;
    size_t size;
    uint32_t timestamp;
    bool is_used;
    
    AudioBlock() : data(nullptr), size(0), timestamp(0), is_used(false) {}
};

// 内存池配置
struct MemoryPoolConfig {
    size_t block_size;      // 每个块的大小
    size_t block_count;     // 块的数量
    size_t max_blocks;      // 最大块数
    uint32_t timeout_ms;    // 获取块超时时间
    
    MemoryPoolConfig() : block_size(32 * 1024), block_count(8), 
                        max_blocks(16), timeout_ms(1000) {}
    
    // 内存优化配置
    static MemoryPoolConfig MemoryOptimized() {
        MemoryPoolConfig config;
        config.block_size = 16 * 1024;        // 减少块大小到16KB
        config.block_count = 4;               // 减少块数量到4个
        config.max_blocks = 8;                // 减少最大块数到8个
        return config;
    }
    
    // 最小内存配置
    static MemoryPoolConfig MinimalMemory() {
        MemoryPoolConfig config;
        config.block_size = 8 * 1024;         // 减少块大小到8KB
        config.block_count = 2;               // 减少块数量到2个
        config.max_blocks = 4;                // 减少最大块数到4个
        return config;
    }
};

// 环形缓冲区
class RingBuffer {
private:
    uint8_t* buffer_;
    size_t size_;
    size_t read_pos_;
    size_t write_pos_;
    size_t data_size_;
    SemaphoreHandle_t mutex_;
    
public:
    RingBuffer(size_t size);
    ~RingBuffer();
    
    bool Initialize();
    void Deinitialize();
    
    size_t Write(const uint8_t* data, size_t len);
    size_t Read(uint8_t* data, size_t len);
    size_t GetAvailableData() const;
    size_t GetFreeSpace() const;
    void Clear();
    bool IsEmpty() const;
    bool IsFull() const;
};

// 音频缓冲区管理器
class AudioBufferManager {
private:
    // 内存池
    std::vector<AudioBlock> blocks_;
    std::queue<size_t> free_blocks_;
    SemaphoreHandle_t pool_mutex_;
    MemoryPoolConfig config_;
    bool initialized_;
    
    // 环形缓冲区
    std::unique_ptr<RingBuffer> ring_buffer_;
    
    // 统计信息
    std::atomic<size_t> total_allocated_;
    std::atomic<size_t> peak_usage_;
    std::atomic<size_t> total_writes_;
    std::atomic<size_t> total_reads_;
    
    // 预加载管理
    std::queue<AudioBlock> preload_queue_;
    SemaphoreHandle_t preload_mutex_;
    size_t max_preload_size_;
    
public:
    AudioBufferManager();
    ~AudioBufferManager();
    
    // 初始化和配置
    bool Initialize(const MemoryPoolConfig& config = MemoryPoolConfig());
    void Deinitialize();
    
    // 内存块管理
    AudioBlock* AllocateBlock(size_t size, uint32_t timeout_ms = 0);
    bool ReleaseBlock(AudioBlock* block);
    void ReleaseAllBlocks();
    
    // 环形缓冲区操作
    size_t WriteToBuffer(const uint8_t* data, size_t len);
    size_t ReadFromBuffer(uint8_t* data, size_t len);
    void ClearBuffer();
    
    // 预加载管理
    bool AddToPreloadQueue(const AudioBlock& block);
    AudioBlock* GetFromPreloadQueue();
    void ClearPreloadQueue();
    
    // 统计信息
    size_t GetFreeBlockCount() const;
    size_t GetUsedBlockCount() const;
    size_t GetTotalAllocated() const { return total_allocated_; }
    size_t GetPeakUsage() const { return peak_usage_; }
    size_t GetBufferUsage() const;
    size_t GetPreloadQueueSize() const;
    
    // 内存管理
    bool IsMemoryLow() const;
    void Defragment();
    void PrintMemoryStats() const;
    
    // 性能优化
    void OptimizeForStreaming();
    void OptimizeForPlayback();
};

#endif // AUDIO_BUFFER_MANAGER_H 