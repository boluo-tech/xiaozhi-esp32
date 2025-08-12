#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_heap_caps.h>
#include <cstdio>
#include <cstring>

// 包含播放列表播放器
#include "boards/common/playlist_player.h"

static const char* TAG = "PlaylistPlayerTest";

// 测试播放列表播放器
void TestPlaylistPlayer() {
    ESP_LOGI(TAG, "=== Testing PlaylistPlayer ===");
    
    // 创建播放列表播放器
    PlaylistPlayer player;
    
    // 设置回调函数
    PlaylistPlayer::PlayerCallbacks callbacks;
    callbacks.on_song_start = [](const std::string& song_name) {
        ESP_LOGI(TAG, "🎵 Song started: %s", song_name.c_str());
    };
    callbacks.on_song_end = [](const std::string& song_name) {
        ESP_LOGI(TAG, "🎵 Song ended: %s", song_name.c_str());
    };
    callbacks.on_progress = [](const std::string& song_name, int progress) {
        ESP_LOGD(TAG, "📊 Progress: %s - %d%%", song_name.c_str(), progress);
    };
    callbacks.on_error = [](const std::string& error) {
        ESP_LOGE(TAG, "❌ Error: %s", error.c_str());
    };
    callbacks.on_playlist_end = []() {
        ESP_LOGI(TAG, "📋 Playlist ended");
    };
    callbacks.on_state_change = [](PlaylistPlayer::PlayerState state) {
        const char* state_names[] = {"IDLE", "LOADING", "PLAYING", "PAUSED", "STOPPING", "ERROR"};
        ESP_LOGI(TAG, "🔄 State changed to: %s", state_names[(int)state]);
    };
    
    player.SetCallbacks(callbacks);
    
    // 初始化播放器
    PlaylistPlayer::PlayerConfig config;
    config.buffer_size = 256 * 1024;  // 256KB
    config.preload_count = 2;
    config.task_stack_size = 4096;
    config.task_priority = 5;
    config.enable_lyrics = true;
    config.enable_preloading = true;
    
    if (!player.Initialize(config)) {
        ESP_LOGE(TAG, "Failed to initialize PlaylistPlayer");
        return;
    }
    
    ESP_LOGI(TAG, "PlaylistPlayer initialized successfully");
    
    // 测试播放列表功能
    ESP_LOGI(TAG, "Testing playlist functionality...");
    
    // 播放播放列表
    if (player.PlayPlaylist("周杰伦热门歌曲")) {
        ESP_LOGI(TAG, "✓ Play playlist command sent successfully");
    } else {
        ESP_LOGE(TAG, "✗ Failed to send play playlist command");
    }
    
    // 等待一段时间
    vTaskDelay(pdMS_TO_TICKS(3000));
    
    // 检查播放状态
    if (player.IsPlaying()) {
        ESP_LOGI(TAG, "✓ Player is playing");
        ESP_LOGI(TAG, "  Current song: %s", player.GetCurrentSongName().c_str());
    } else {
        ESP_LOGW(TAG, "⚠ Player is not playing");
    }
    
    // 测试暂停功能
    ESP_LOGI(TAG, "Testing pause functionality...");
    if (player.Pause()) {
        ESP_LOGI(TAG, "✓ Pause command sent successfully");
    } else {
        ESP_LOGE(TAG, "✗ Failed to send pause command");
    }
    
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    // 测试恢复功能
    ESP_LOGI(TAG, "Testing resume functionality...");
    if (player.Resume()) {
        ESP_LOGI(TAG, "✓ Resume command sent successfully");
    } else {
        ESP_LOGE(TAG, "✗ Failed to send resume command");
    }
    
    vTaskDelay(pdMS_TO_TICKS(3000));
    
    // 测试下一首功能
    ESP_LOGI(TAG, "Testing next song functionality...");
    if (player.Next()) {
        ESP_LOGI(TAG, "✓ Next command sent successfully");
    } else {
        ESP_LOGE(TAG, "✗ Failed to send next command");
    }
    
    vTaskDelay(pdMS_TO_TICKS(3000));
    
    // 测试上一首功能
    ESP_LOGI(TAG, "Testing previous song functionality...");
    if (player.Previous()) {
        ESP_LOGI(TAG, "✓ Previous command sent successfully");
    } else {
        ESP_LOGE(TAG, "✗ Failed to send previous command");
    }
    
    vTaskDelay(pdMS_TO_TICKS(3000));
    
    // 测试停止功能
    ESP_LOGI(TAG, "Testing stop functionality...");
    if (player.Stop()) {
        ESP_LOGI(TAG, "✓ Stop command sent successfully");
    } else {
        ESP_LOGE(TAG, "✗ Failed to send stop command");
    }
    
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    // 测试内存统计
    ESP_LOGI(TAG, "Testing memory statistics...");
    size_t buffer_usage = player.GetBufferUsage();
    size_t free_memory = player.GetFreeMemory();
    
    ESP_LOGI(TAG, "  Buffer usage: %zu bytes (%.1f KB)", buffer_usage, buffer_usage / 1024.0f);
    ESP_LOGI(TAG, "  Free memory: %zu bytes (%.1f KB)", free_memory, free_memory / 1024.0f);
    
    player.PrintMemoryStats();
    
    // 测试播放列表管理功能
    ESP_LOGI(TAG, "Testing playlist management...");
    
    player.SetShuffle(true);
    ESP_LOGI(TAG, "✓ Shuffle enabled");
    
    player.SetRepeat(true);
    ESP_LOGI(TAG, "✓ Repeat enabled");
    
    player.SetAutoPlayNext(true);
    ESP_LOGI(TAG, "✓ Auto play next enabled");
    
    // 测试紧急停止功能
    ESP_LOGI(TAG, "Testing emergency stop functionality...");
    player.EmergencyStop();
    ESP_LOGI(TAG, "✓ Emergency stop executed");
    
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // 测试重置功能
    ESP_LOGI(TAG, "Testing reset functionality...");
    player.Reset();
    ESP_LOGI(TAG, "✓ Player reset");
    
    // 清理
    player.Deinitialize();
    ESP_LOGI(TAG, "PlaylistPlayer test completed");
}

// 测试播放列表播放器的集成功能
void TestPlaylistPlayerIntegration() {
    ESP_LOGI(TAG, "=== Testing PlaylistPlayer Integration ===");
    
    PlaylistPlayer player;
    
    // 设置简单的回调
    PlaylistPlayer::PlayerCallbacks callbacks;
    callbacks.on_song_start = [](const std::string& song_name) {
        ESP_LOGI(TAG, "🎵 Playing: %s", song_name.c_str());
    };
    callbacks.on_state_change = [](PlaylistPlayer::PlayerState state) {
        const char* states[] = {"IDLE", "LOADING", "PLAYING", "PAUSED", "STOPPING", "ERROR"};
        ESP_LOGI(TAG, "🔄 State: %s", states[(int)state]);
    };
    
    player.SetCallbacks(callbacks);
    
    // 使用最小配置初始化
    PlaylistPlayer::PlayerConfig config = PlaylistPlayer::PlayerConfig();
    config.buffer_size = 128 * 1024;  // 较小的缓冲区
    config.preload_count = 1;          // 只预加载1首
    
    if (!player.Initialize(config)) {
        ESP_LOGE(TAG, "Failed to initialize player with minimal config");
        return;
    }
    
    ESP_LOGI(TAG, "Player initialized with minimal config");
    
    // 测试播放单个歌曲
    ESP_LOGI(TAG, "Testing single song playback...");
    if (player.PlaySong("稻香")) {
        ESP_LOGI(TAG, "✓ Single song play command sent");
    }
    
    vTaskDelay(pdMS_TO_TICKS(5000));
    
    // 检查状态
    auto state = player.GetState();
    ESP_LOGI(TAG, "Current state: %d", (int)state);
    
    // 测试播放列表
    ESP_LOGI(TAG, "Testing playlist playback...");
    if (player.PlayPlaylist("华语流行")) {
        ESP_LOGI(TAG, "✓ Playlist play command sent");
    }
    
    vTaskDelay(pdMS_TO_TICKS(5000));
    
    // 测试控制功能
    ESP_LOGI(TAG, "Testing control functions...");
    
    player.Pause();
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    player.Resume();
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    player.Next();
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    player.Stop();
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // 清理
    player.Deinitialize();
    ESP_LOGI(TAG, "Integration test completed");
}

// 测试内存优化功能
void TestPlaylistPlayerMemoryOptimization() {
    ESP_LOGI(TAG, "=== Testing PlaylistPlayer Memory Optimization ===");
    
    // 获取初始内存状态
    size_t initial_heap = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t initial_spiram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    
    ESP_LOGI(TAG, "Initial memory:");
    ESP_LOGI(TAG, "  Heap: %.1f KB", initial_heap / 1024.0f);
    ESP_LOGI(TAG, "  SPIRAM: %.1f KB", initial_spiram / 1024.0f);
    
    PlaylistPlayer player;
    
    // 使用内存优化配置
    PlaylistPlayer::PlayerConfig config;
    config.buffer_size = 64 * 1024;   // 较小的缓冲区
    config.preload_count = 0;          // 不预加载
    config.task_stack_size = 2048;     // 较小的栈
    config.enable_preloading = false;  // 禁用预加载
    
    if (!player.Initialize(config)) {
        ESP_LOGE(TAG, "Failed to initialize player with memory optimization");
        return;
    }
    
    ESP_LOGI(TAG, "Player initialized with memory optimization");
    
    // 测试播放功能
    player.PlayPlaylist("轻音乐");
    vTaskDelay(pdMS_TO_TICKS(3000));
    
    // 获取内存使用情况
    size_t buffer_usage = player.GetBufferUsage();
    size_t free_memory = player.GetFreeMemory();
    
    ESP_LOGI(TAG, "Memory usage during playback:");
    ESP_LOGI(TAG, "  Buffer usage: %.1f KB", buffer_usage / 1024.0f);
    ESP_LOGI(TAG, "  Free memory: %.1f KB", free_memory / 1024.0f);
    
    // 获取最终内存状态
    size_t final_heap = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t final_spiram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    
    size_t heap_used = initial_heap - final_heap;
    size_t spiram_used = initial_spiram - final_spiram;
    
    ESP_LOGI(TAG, "Memory used:");
    ESP_LOGI(TAG, "  Heap: %.1f KB", heap_used / 1024.0f);
    ESP_LOGI(TAG, "  SPIRAM: %.1f KB", spiram_used / 1024.0f);
    ESP_LOGI(TAG, "  Total: %.1f KB", (heap_used + spiram_used) / 1024.0f);
    
    player.Deinitialize();
    ESP_LOGI(TAG, "Memory optimization test completed");
}

// 测试函数 - 供外部调用
void RunPlaylistPlayerTests() {
    ESP_LOGI(TAG, "Starting PlaylistPlayer tests...");
    
    // 等待系统稳定
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // 运行基本功能测试
    TestPlaylistPlayer();
    
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    // 运行集成测试
    TestPlaylistPlayerIntegration();
    
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    // 运行内存优化测试
    TestPlaylistPlayerMemoryOptimization();
    
    ESP_LOGI(TAG, "All PlaylistPlayer tests completed!");
} 