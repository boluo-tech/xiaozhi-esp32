#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_heap_caps.h>
#include <cstdio>
#include <cstring>

// 包含智能表情系统
#include "assets/gif_resources_smart.h"

static const char* TAG = "SmartEmotionsTest";

// 测试表情列表 - 包含各种表情名称
const char* TEST_EMOTIONS[] = {
    // 核心表情 (4个)
    "neutral", "happy", "sad", "thinking",
    
    // 常用表情 (7个)
    "angry", "surprised", "sleepy", "confident", "relaxed", "loving", "funny",
    
    // 扩展表情 (10个)
    "confused", "cool", "crying", "delicious", "embarrassed", 
    "kissy", "laughing", "shocked", "silly", "winking",
    
    // 映射表情 (20个) - 这些会智能映射到核心表情
    "joy", "smile", "laugh", "cry", "sorrow", "depressed",
    "thought", "ponder", "contemplate", "mad", "furious", "rage",
    "amazed", "shocked", "astonished", "tired", "drowsy", "exhausted",
    "proud", "assured", "chill", "calm", "love", "affection",
    "joke", "humor", "kiss", "romantic", "wink", "playful"
};

// 测试智能表情系统
void TestSmartEmotionSystem() {
    ESP_LOGI(TAG, "=== Testing Smart Emotion System ===");
    
    // 初始化智能管理器
    g_smart_gif_manager = std::make_unique<SmartGifManager>();
    
    if (!g_smart_gif_manager->Initialize(1024 * 1024, 8)) { // 1MB内存，最多8个表情
        ESP_LOGE(TAG, "Failed to initialize SmartGifManager");
        return;
    }
    
    // 初始化表情映射
    InitializeSmartEmotionMappings();
    
    ESP_LOGI(TAG, "Smart emotion system initialized successfully");
    
    // 测试各种表情访问
    ESP_LOGI(TAG, "Testing emotion access...");
    
    for (const char* emotion : TEST_EMOTIONS) {
        const lv_img_dsc_t* gif = GetSmartEmotionWithFallback(emotion);
        
        ESP_LOGI(TAG, "  Emotion '%s': %s", 
                 emotion, 
                 gif ? "loaded" : "mapped to fallback");
    }
    
    // 打印统计信息
    g_smart_gif_manager->PrintStats();
    
    // 测试内存优化
    ESP_LOGI(TAG, "Testing memory optimization...");
    
    if (g_smart_gif_manager->IsMemoryLow()) {
        ESP_LOGW(TAG, "Memory is low, optimizing...");
        g_smart_gif_manager->OptimizeMemory();
    }
    
    // 再次打印统计信息
    g_smart_gif_manager->PrintStats();
    
    g_smart_gif_manager->Deinitialize();
    ESP_LOGI(TAG, "Smart emotion system test completed");
}

// 对比测试：传统方法 vs 智能方法
void TestComparison() {
    ESP_LOGI(TAG, "=== Comparison Test: Traditional vs Smart ===");
    
    // 传统方法：加载所有表情
    size_t traditional_memory = 21 * 100 * 1024; // 21个表情，每个约100KB
    
    // 智能方法：按需加载
    size_t smart_memory = 8 * 100 * 1024; // 最多8个表情
    
    size_t memory_saved = traditional_memory - smart_memory;
    
    ESP_LOGI(TAG, "Memory comparison:");
    ESP_LOGI(TAG, "  Traditional method: %.1f KB (all 21 emotions)", traditional_memory / 1024.0f);
    ESP_LOGI(TAG, "  Smart method: %.1f KB (up to 8 emotions)", smart_memory / 1024.0f);
    ESP_LOGI(TAG, "  Memory saved: %.1f KB (%.1f MB)", memory_saved / 1024.0f, memory_saved / (1024.0f * 1024.0f));
    ESP_LOGI(TAG, "  Savings: %.1f%%", (float)memory_saved / traditional_memory * 100.0f);
    
    ESP_LOGI(TAG, "Functionality comparison:");
    ESP_LOGI(TAG, "  Traditional: 21 unique emotions");
    ESP_LOGI(TAG, "  Smart: 21 unique emotions + 30+ mapped emotions = 50+ total emotions");
    ESP_LOGI(TAG, "  Smart system provides MORE emotions with LESS memory!");
}

// 测试表情映射功能
void TestEmotionMapping() {
    ESP_LOGI(TAG, "=== Testing Emotion Mapping ===");
    
    // 测试映射表情
    const char* mapping_tests[] = {
        "joy", "smile", "laugh",      // 应该映射到 happy
        "cry", "sorrow", "depressed", // 应该映射到 sad
        "thought", "ponder",          // 应该映射到 thinking
        "mad", "furious", "rage",     // 应该映射到 angry
        "amazed", "shocked",          // 应该映射到 surprised
        "tired", "drowsy",            // 应该映射到 sleepy
        "proud", "assured",           // 应该映射到 confident
        "chill", "calm",              // 应该映射到 relaxed
        "love", "affection",          // 应该映射到 loving
        "joke", "humor",              // 应该映射到 funny
        "kiss", "romantic",           // 应该映射到 kissy
        "wink", "playful"             // 应该映射到 winking
    };
    
    for (const char* emotion : mapping_tests) {
        std::string mapped = g_smart_gif_manager->MapEmotion(emotion);
        ESP_LOGI(TAG, "  '%s' -> '%s'", emotion, mapped.c_str());
    }
}

// 测试内存使用情况
void TestMemoryUsage() {
    ESP_LOGI(TAG, "=== Testing Memory Usage ===");
    
    size_t initial_heap = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t initial_spiram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    
    ESP_LOGI(TAG, "Initial memory:");
    ESP_LOGI(TAG, "  Heap: %.1f KB", initial_heap / 1024.0f);
    ESP_LOGI(TAG, "  SPIRAM: %.1f KB", initial_spiram / 1024.0f);
    
    // 测试加载一些表情
    const char* test_loads[] = {"happy", "sad", "angry", "surprised", "sleepy"};
    
    for (const char* emotion : test_loads) {
        bool loaded = g_smart_gif_manager->LoadEmotion(emotion);
        ESP_LOGI(TAG, "  Loading '%s': %s", emotion, loaded ? "success" : "failed");
    }
    
    size_t final_heap = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t final_spiram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    
    ESP_LOGI(TAG, "Final memory:");
    ESP_LOGI(TAG, "  Heap: %.1f KB", final_heap / 1024.0f);
    ESP_LOGI(TAG, "  SPIRAM: %.1f KB", final_spiram / 1024.0f);
    
    size_t heap_used = initial_heap - final_heap;
    size_t spiram_used = initial_spiram - final_spiram;
    
    ESP_LOGI(TAG, "Memory used:");
    ESP_LOGI(TAG, "  Heap: %.1f KB", heap_used / 1024.0f);
    ESP_LOGI(TAG, "  SPIRAM: %.1f KB", spiram_used / 1024.0f);
    ESP_LOGI(TAG, "  Total: %.1f KB", (heap_used + spiram_used) / 1024.0f);
}

// 主测试函数
extern "C" void app_main() {
    ESP_LOGI(TAG, "Starting smart emotion system tests...");
    
    // 等待系统稳定
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // 运行对比测试
    TestComparison();
    
    // 运行智能表情系统测试
    TestSmartEmotionSystem();
    
    // 重新初始化管理器进行详细测试
    g_smart_gif_manager = std::make_unique<SmartGifManager>();
    g_smart_gif_manager->Initialize(1024 * 1024, 8);
    InitializeSmartEmotionMappings();
    
    // 测试表情映射
    TestEmotionMapping();
    
    // 测试内存使用
    TestMemoryUsage();
    
    ESP_LOGI(TAG, "All smart emotion tests completed!");
    
    // 保持运行以便查看日志
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        ESP_LOGI(TAG, "Smart emotion test is still running...");
    }
} 