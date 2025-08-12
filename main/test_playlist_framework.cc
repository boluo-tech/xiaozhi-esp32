#include "playlist_manager.h"
#include "playlist_state_machine.h"
#include "audio_buffer_manager.h"
#include <esp_log.h>
#include <esp_timer.h>

static const char* TAG = "TestPlaylistFramework";

// 测试回调函数
void OnSongStart(const SongInfo& song) {
    ESP_LOGI(TAG, "=== Song Started ===");
    ESP_LOGI(TAG, "Title: %s", song.title.c_str());
    ESP_LOGI(TAG, "Artist: %s", song.artist.c_str());
    ESP_LOGI(TAG, "Duration: %d ms", song.duration_ms);
}

void OnSongEnd(const SongInfo& song) {
    ESP_LOGI(TAG, "=== Song Ended ===");
    ESP_LOGI(TAG, "Title: %s", song.title.c_str());
}

void OnProgress(const SongInfo& song, int position) {
    ESP_LOGD(TAG, "Progress: %s - %d ms", song.title.c_str(), position);
}

void OnError(const std::string& error) {
    ESP_LOGE(TAG, "=== Playlist Error ===");
    ESP_LOGE(TAG, "Error: %s", error.c_str());
}

void OnPlaylistEnd() {
    ESP_LOGI(TAG, "=== Playlist Ended ===");
}

void OnStateChange(PlaylistState state) {
    const char* state_names[] = {"IDLE", "LOADING", "PLAYING", "PAUSED", "STOPPING", "ERROR"};
    ESP_LOGI(TAG, "=== State Changed ===");
    ESP_LOGI(TAG, "New State: %s", state_names[static_cast<int>(state)]);
}

// 测试状态机
void TestStateMachine() {
    ESP_LOGI(TAG, "=== Testing State Machine ===");
    
    auto* state_machine = StateMachineFactory::CreateDefaultStateMachine();
    
    // 验证状态机
    if (!state_machine->ValidateStateMachine()) {
        ESP_LOGE(TAG, "State machine validation failed!");
        return;
    }
    
    ESP_LOGI(TAG, "State machine validation passed");
    
    // 测试状态转换
    ESP_LOGI(TAG, "Testing state transitions...");
    
    // 从IDLE开始
    ESP_LOGI(TAG, "Current state: %d", static_cast<int>(state_machine->GetCurrentState()));
    
    // 请求播放
    if (state_machine->ProcessEvent(PlaylistEvent::PLAY_REQUESTED, "Test play")) {
        ESP_LOGI(TAG, "Play request processed, new state: %d", 
                 static_cast<int>(state_machine->GetCurrentState()));
    }
    
    // 加载完成
    if (state_machine->ProcessEvent(PlaylistEvent::LOAD_COMPLETED, "Test load complete")) {
        ESP_LOGI(TAG, "Load completed, new state: %d", 
                 static_cast<int>(state_machine->GetCurrentState()));
    }
    
    // 暂停
    if (state_machine->ProcessEvent(PlaylistEvent::PAUSE_REQUESTED, "Test pause")) {
        ESP_LOGI(TAG, "Pause request processed, new state: %d", 
                 static_cast<int>(state_machine->GetCurrentState()));
    }
    
    // 恢复播放
    if (state_machine->ProcessEvent(PlaylistEvent::PLAY_REQUESTED, "Test resume")) {
        ESP_LOGI(TAG, "Resume request processed, new state: %d", 
                 static_cast<int>(state_machine->GetCurrentState()));
    }
    
    // 播放完成
    if (state_machine->ProcessEvent(PlaylistEvent::PLAY_COMPLETED, "Test play complete")) {
        ESP_LOGI(TAG, "Play completed, new state: %d", 
                 static_cast<int>(state_machine->GetCurrentState()));
    }
    
    // 停止
    if (state_machine->ProcessEvent(PlaylistEvent::STOP_REQUESTED, "Test stop")) {
        ESP_LOGI(TAG, "Stop request processed, new state: %d", 
                 static_cast<int>(state_machine->GetCurrentState()));
    }
    
    // 重置
    if (state_machine->ProcessEvent(PlaylistEvent::RESET_REQUESTED, "Test reset")) {
        ESP_LOGI(TAG, "Reset request processed, new state: %d", 
                 static_cast<int>(state_machine->GetCurrentState()));
    }
    
    // 打印状态历史
    state_machine->PrintStateHistory();
    
    delete state_machine;
    ESP_LOGI(TAG, "State machine test completed");
}

// 测试缓冲区管理器
void TestBufferManager() {
    ESP_LOGI(TAG, "=== Testing Buffer Manager ===");
    
    AudioBufferManager buffer_manager;
    
    // 初始化
    MemoryPoolConfig config;
    config.block_size = 16 * 1024;  // 16KB
    config.block_count = 4;         // 4个块
    config.max_blocks = 8;          // 最大8个块
    config.timeout_ms = 1000;       // 1秒超时
    
    if (!buffer_manager.Initialize(config)) {
        ESP_LOGE(TAG, "Failed to initialize buffer manager");
        return;
    }
    
    ESP_LOGI(TAG, "Buffer manager initialized successfully");
    
    // 测试内存块分配
    ESP_LOGI(TAG, "Testing block allocation...");
    
    AudioBlock* block1 = buffer_manager.AllocateBlock(8192); // 8KB
    if (block1) {
        ESP_LOGI(TAG, "Block 1 allocated successfully");
        ESP_LOGI(TAG, "Block size: %zu", block1->size);
        ESP_LOGI(TAG, "Free blocks: %zu", buffer_manager.GetFreeBlockCount());
    }
    
    AudioBlock* block2 = buffer_manager.AllocateBlock(16384); // 16KB
    if (block2) {
        ESP_LOGI(TAG, "Block 2 allocated successfully");
        ESP_LOGI(TAG, "Block size: %zu", block2->size);
        ESP_LOGI(TAG, "Free blocks: %zu", buffer_manager.GetFreeBlockCount());
    }
    
    // 测试环形缓冲区
    ESP_LOGI(TAG, "Testing ring buffer...");
    
    uint8_t test_data[1024];
    for (int i = 0; i < 1024; ++i) {
        test_data[i] = i % 256;
    }
    
    size_t written = buffer_manager.WriteToBuffer(test_data, 1024);
    ESP_LOGI(TAG, "Written to buffer: %zu bytes", written);
    
    uint8_t read_data[1024];
    size_t read = buffer_manager.ReadFromBuffer(read_data, 1024);
    ESP_LOGI(TAG, "Read from buffer: %zu bytes", read);
    
    // 验证数据
    bool data_correct = true;
    for (int i = 0; i < 1024; ++i) {
        if (read_data[i] != test_data[i]) {
            data_correct = false;
            break;
        }
    }
    
    if (data_correct) {
        ESP_LOGI(TAG, "Ring buffer data verification passed");
    } else {
        ESP_LOGE(TAG, "Ring buffer data verification failed");
    }
    
    // 释放内存块
    if (block1) {
        buffer_manager.ReleaseBlock(block1);
        ESP_LOGI(TAG, "Block 1 released");
    }
    
    if (block2) {
        buffer_manager.ReleaseBlock(block2);
        ESP_LOGI(TAG, "Block 2 released");
    }
    
    // 打印内存统计
    buffer_manager.PrintMemoryStats();
    
    ESP_LOGI(TAG, "Buffer manager test completed");
}

// 测试播放列表管理器
void TestPlaylistManager() {
    ESP_LOGI(TAG, "=== Testing PlaylistManager ===");
    
    // 使用内存优化配置
    PlaylistConfig config = PlaylistConfig::MemoryOptimized();
    ESP_LOGI(TAG, "Using memory optimized config: buffer=%zuKB, preload=%zu, stack=%dKB", 
             config.buffer_size/1024, config.preload_count, config.task_stack_size/1024);
    
    PlaylistManager manager;
    
    // 设置回调
    PlaylistCallbacks callbacks;
    callbacks.on_song_start = [](const SongInfo& song) {
        ESP_LOGI(TAG, "Song started: %s - %s", song.title.c_str(), song.artist.c_str());
    };
    callbacks.on_error = [](const std::string& error) {
        ESP_LOGE(TAG, "Playlist error: %s", error.c_str());
    };
    callbacks.on_state_change = [](PlaylistState state) {
        ESP_LOGI(TAG, "State changed to: %d", (int)state);
    };
    
    manager.SetCallbacks(callbacks);
    
    // 初始化
    if (!manager.Initialize(config)) {
        ESP_LOGE(TAG, "Failed to initialize PlaylistManager");
        return;
    }
    
    // 测试内存统计
    auto stats = manager.GetMemoryStats();
    ESP_LOGI(TAG, "Memory stats: used=%zuKB, buffer=%zuKB, pool=%zuKB, free=%zuKB, usage=%.1f%%",
             stats.total_used/1024, stats.buffer_used/1024, stats.pool_used/1024, 
             stats.free_memory/1024, stats.usage_percentage);
    
    // 测试播放控制
    ESP_LOGI(TAG, "Testing playback controls...");
    manager.PlayPlaylist("test playlist");
    
    // 等待一段时间
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // 检查内存使用情况
    if (manager.IsMemoryLow()) {
        ESP_LOGW(TAG, "Memory is low, enabling minimal mode");
        manager.EnableMinimalMemoryMode();
    }
    
    // 再次检查内存统计
    stats = manager.GetMemoryStats();
    ESP_LOGI(TAG, "After optimization - Memory stats: used=%zuKB, usage=%.1f%%",
             stats.total_used/1024, stats.usage_percentage);
    
    // 测试垃圾回收
    ESP_LOGI(TAG, "Testing garbage collection...");
    manager.ForceGarbageCollection();
    
    // 最终内存统计
    stats = manager.GetMemoryStats();
    ESP_LOGI(TAG, "Final memory stats: used=%zuKB, usage=%.1f%%",
             stats.total_used/1024, stats.usage_percentage);
    
    manager.Deinitialize();
    ESP_LOGI(TAG, "PlaylistManager test completed");
}

// 主测试函数
void TestPlaylistFramework() {
    ESP_LOGI(TAG, "==========================================");
    ESP_LOGI(TAG, "Starting Playlist Framework Tests");
    ESP_LOGI(TAG, "==========================================");
    
    // 测试状态机
    TestStateMachine();
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // 测试缓冲区管理器
    TestBufferManager();
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // 测试播放列表管理器
    TestPlaylistManager();
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    ESP_LOGI(TAG, "==========================================");
    ESP_LOGI(TAG, "All Playlist Framework Tests Completed");
    ESP_LOGI(TAG, "==========================================");
}

// 导出测试函数供外部调用
extern "C" {
    void test_playlist_framework() {
        TestPlaylistFramework();
    }
} 