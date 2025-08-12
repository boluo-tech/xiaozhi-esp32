#include "audio_buffer_manager.h"
#include <esp_log.h>
#include <cstring>

static const char* TAG = "RingBuffer";

RingBuffer::RingBuffer(size_t size) 
    : size_(size), read_pos_(0), write_pos_(0), data_size_(0) {
    ESP_LOGI(TAG, "Creating ring buffer with size %zu", size);
}

RingBuffer::~RingBuffer() {
    Deinitialize();
    ESP_LOGI(TAG, "Ring buffer destroyed");
}

bool RingBuffer::Initialize() {
    if (size_ == 0) {
        ESP_LOGE(TAG, "Invalid buffer size: 0");
        return false;
    }
    
    // 分配缓冲区内存
    buffer_ = static_cast<uint8_t*>(heap_caps_malloc(size_, MALLOC_CAP_SPIRAM));
    if (!buffer_) {
        ESP_LOGE(TAG, "Failed to allocate ring buffer memory");
        return false;
    }
    
    // 创建互斥锁
    mutex_ = xSemaphoreCreateMutex();
    if (!mutex_) {
        ESP_LOGE(TAG, "Failed to create ring buffer mutex");
        heap_caps_free(buffer_);
        buffer_ = nullptr;
        return false;
    }
    
    // 初始化状态
    read_pos_ = 0;
    write_pos_ = 0;
    data_size_ = 0;
    
    ESP_LOGI(TAG, "Ring buffer initialized successfully");
    return true;
}

void RingBuffer::Deinitialize() {
    if (mutex_) {
        vSemaphoreDelete(mutex_);
        mutex_ = nullptr;
    }
    
    if (buffer_) {
        heap_caps_free(buffer_);
        buffer_ = nullptr;
    }
    
    read_pos_ = 0;
    write_pos_ = 0;
    data_size_ = 0;
    
    ESP_LOGI(TAG, "Ring buffer deinitialized");
}

size_t RingBuffer::Write(const uint8_t* data, size_t len) {
    if (!buffer_ || !mutex_ || !data || len == 0) {
        return 0;
    }
    
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "Failed to acquire ring buffer mutex for write");
        return 0;
    }
    
    size_t written = 0;
    size_t available_space = size_ - data_size_;
    
    if (available_space == 0) {
        ESP_LOGW(TAG, "Ring buffer is full, cannot write");
        xSemaphoreGive(mutex_);
        return 0;
    }
    
    // 限制写入长度
    size_t to_write = std::min(len, available_space);
    
    // 写入数据
    for (size_t i = 0; i < to_write; ++i) {
        buffer_[write_pos_] = data[i];
        write_pos_ = (write_pos_ + 1) % size_;
        written++;
    }
    
    data_size_ += written;
    
    ESP_LOGD(TAG, "Wrote %zu bytes, buffer usage: %zu/%zu", 
             written, data_size_, size_);
    
    xSemaphoreGive(mutex_);
    return written;
}

size_t RingBuffer::Read(uint8_t* data, size_t len) {
    if (!buffer_ || !mutex_ || !data || len == 0) {
        return 0;
    }
    
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "Failed to acquire ring buffer mutex for read");
        return 0;
    }
    
    size_t read = 0;
    
    if (data_size_ == 0) {
        ESP_LOGD(TAG, "Ring buffer is empty, nothing to read");
        xSemaphoreGive(mutex_);
        return 0;
    }
    
    // 限制读取长度
    size_t to_read = std::min(len, data_size_);
    
    // 读取数据
    for (size_t i = 0; i < to_read; ++i) {
        data[i] = buffer_[read_pos_];
        read_pos_ = (read_pos_ + 1) % size_;
        read++;
    }
    
    data_size_ -= read;
    
    ESP_LOGD(TAG, "Read %zu bytes, buffer usage: %zu/%zu", 
             read, data_size_, size_);
    
    xSemaphoreGive(mutex_);
    return read;
}

size_t RingBuffer::GetAvailableData() const {
    if (!buffer_ || !mutex_) {
        return 0;
    }
    
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) != pdTRUE) {
        return 0;
    }
    
    size_t available = data_size_;
    xSemaphoreGive(mutex_);
    return available;
}

size_t RingBuffer::GetFreeSpace() const {
    if (!buffer_ || !mutex_) {
        return 0;
    }
    
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) != pdTRUE) {
        return 0;
    }
    
    size_t free_space = size_ - data_size_;
    xSemaphoreGive(mutex_);
    return free_space;
}

void RingBuffer::Clear() {
    if (!buffer_ || !mutex_) {
        return;
    }
    
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "Failed to acquire ring buffer mutex for clear");
        return;
    }
    
    read_pos_ = 0;
    write_pos_ = 0;
    data_size_ = 0;
    
    ESP_LOGI(TAG, "Ring buffer cleared");
    xSemaphoreGive(mutex_);
}

bool RingBuffer::IsEmpty() const {
    if (!buffer_ || !mutex_) {
        return true;
    }
    
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) != pdTRUE) {
        return true;
    }
    
    bool empty = (data_size_ == 0);
    xSemaphoreGive(mutex_);
    return empty;
}

bool RingBuffer::IsFull() const {
    if (!buffer_ || !mutex_) {
        return false;
    }
    
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) != pdTRUE) {
        return false;
    }
    
    bool full = (data_size_ == size_);
    xSemaphoreGive(mutex_);
    return full;
} 