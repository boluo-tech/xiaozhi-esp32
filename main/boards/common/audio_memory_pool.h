#ifndef AUDIO_MEMORY_POOL_H
#define AUDIO_MEMORY_POOL_H

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <esp_heap_caps.h>
#include <vector>
#include <queue>

// 音频内存块结构
struct AudioBlock {
    uint8_t* data;
    size_t size;
    bool in_use;
    uint32_t timestamp;
    
    AudioBlock() : data(nullptr), size(0), in_use(false), timestamp(0) {}
};

// 内存池配置
struct MemoryPoolConfig {
    size_t block_size;      // 每个块的大小
    size_t block_count;     // 块的数量
    size_t max_blocks;      // 最大块数
    uint32_t timeout_ms;    // 获取块超时时间
};

class AudioMemoryPool {
private:
    std::vector<AudioBlock> blocks_;
    std::queue<size_t> free_blocks_;
    SemaphoreHandle_t mutex_;
    MemoryPoolConfig config_;
    bool initialized_;
    
    // 内存统计
    size_t total_allocated_;
    size_t peak_usage_;
    
public:
    AudioMemoryPool();
    ~AudioMemoryPool();
    
    // 初始化和配置
    bool Initialize(const MemoryPoolConfig& config);
    void Deinitialize();
    
    // 内存块管理
    AudioBlock* AllocateBlock(size_t size, uint32_t timeout_ms = 0);
    bool ReleaseBlock(AudioBlock* block);
    void ReleaseAllBlocks();
    
    // 统计信息
    size_t GetFreeBlockCount() const;
    size_t GetUsedBlockCount() const;
    size_t GetTotalAllocated() const { return total_allocated_; }
    size_t GetPeakUsage() const { return peak_usage_; }
    
    // 内存碎片整理
    void Defragment();
    
    // 内存使用情况检查
    bool IsMemoryLow() const;
    void PrintMemoryStats() const;
};

#endif // AUDIO_MEMORY_POOL_H 