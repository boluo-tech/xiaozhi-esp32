#include "playlist_manager.h"
#include "playlist_state_machine.h"
#include "audio_buffer_manager.h"
#include "music_downloader.h"
#include <esp_log.h>
#include <esp_timer.h>

static const char* TAG = "PlaylistManager";

PlaylistManager::PlaylistManager() 
    : playlist_task_handle_(nullptr), command_queue_(nullptr), state_mutex_(nullptr),
      current_state_(PlaylistState::IDLE), is_running_(false), emergency_stop_requested_(false) {
    ESP_LOGI(TAG, "PlaylistManager created");
}

PlaylistManager::~PlaylistManager() {
    Deinitialize();
    ESP_LOGI(TAG, "PlaylistManager destroyed");
}

bool PlaylistManager::Initialize(const PlaylistConfig& config) {
    ESP_LOGI(TAG, "Initializing PlaylistManager");
    
    config_ = config;
    
    // 创建状态互斥锁
    state_mutex_ = xSemaphoreCreateMutex();
    if (!state_mutex_) {
        ESP_LOGE(TAG, "Failed to create state mutex");
        return false;
    }
    
    // 创建命令队列
    command_queue_ = xQueueCreate(10, sizeof(std::pair<PlaylistCommand, void*>));
    if (!command_queue_) {
        ESP_LOGE(TAG, "Failed to create command queue");
        vSemaphoreDelete(state_mutex_);
        state_mutex_ = nullptr;
        return false;
    }
    
    // 初始化核心组件
    buffer_manager_ = std::make_unique<AudioBufferManager>();
    state_machine_ = std::make_unique<PlaylistStateMachine>();
    
    // 初始化状态机
    state_machine_->Initialize();
    
    // 设置状态机回调
    StateMachineCallbacks sm_callbacks;
    sm_callbacks.on_state_change = [this](PlaylistState old_state, PlaylistState new_state) {
        HandleStateChange(new_state);
    };
    sm_callbacks.on_error = [this](const std::string& error) {
        HandleError(error);
    };
    state_machine_->SetCallbacks(sm_callbacks);
    
    // 初始化缓冲区管理器
    MemoryPoolConfig buffer_config;
    buffer_config.block_size = 32 * 1024;  // 32KB块
    buffer_config.block_count = 8;         // 8个块
    buffer_config.max_blocks = 16;         // 最大16个块
    buffer_config.timeout_ms = 1000;       // 1秒超时
    
    if (!buffer_manager_->Initialize(buffer_config)) {
        ESP_LOGE(TAG, "Failed to initialize buffer manager");
        return false;
    }
    
    // 创建播放列表任务
    is_running_ = true;
    BaseType_t ret = xTaskCreate([](void* arg) {
        PlaylistManager* manager = static_cast<PlaylistManager*>(arg);
        manager->PlaylistTaskLoop();
    }, "playlist_task", config.task_stack_size, this, config.task_priority, &playlist_task_handle_);
    
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create playlist task: %d", ret);
        is_running_ = false;
        return false;
    }
    
    ESP_LOGI(TAG, "PlaylistManager initialized successfully");
    return true;
}

void PlaylistManager::Deinitialize() {
    ESP_LOGI(TAG, "Deinitializing PlaylistManager");
    
    // 停止任务
    is_running_ = false;
    emergency_stop_requested_ = true;
    
    // 等待任务结束
    if (playlist_task_handle_) {
        vTaskDelete(playlist_task_handle_);
        playlist_task_handle_ = nullptr;
    }
    
    // 清理队列
    if (command_queue_) {
        vQueueDelete(command_queue_);
        command_queue_ = nullptr;
    }
    
    // 清理互斥锁
    if (state_mutex_) {
        vSemaphoreDelete(state_mutex_);
        state_mutex_ = nullptr;
    }
    
    // 清理组件
    if (buffer_manager_) {
        buffer_manager_->Deinitialize();
        buffer_manager_.reset();
    }
    
    if (state_machine_) {
        state_machine_.reset();
    }
    
    if (downloader_) {
        downloader_->Deinitialize();
        downloader_.reset();
    }
    
    // 清理播放列表数据
    current_playlist_.songs.clear();
    current_playlist_.current_index = 0;
    
    ESP_LOGI(TAG, "PlaylistManager deinitialized");
}

void PlaylistManager::SetCallbacks(const PlaylistCallbacks& callbacks) {
    callbacks_ = callbacks;
    ESP_LOGI(TAG, "PlaylistManager callbacks set");
}

void PlaylistManager::PlaylistTaskLoop() {
    ESP_LOGI(TAG, "Playlist task started");
    
    while (is_running_) {
        // 检查紧急停止请求
        if (emergency_stop_requested_.load()) {
            ESP_LOGI(TAG, "Emergency stop requested, cleaning up");
            CleanupResources();
            break;
        }
        
        // 处理命令队列
        std::pair<PlaylistCommand, void*> command_data;
        if (xQueueReceive(command_queue_, &command_data, pdMS_TO_TICKS(100)) == pdTRUE) {
            ProcessCommand(command_data.first, command_data.second);
        }
        
        // 根据当前状态执行相应操作
        PlaylistState current_state = state_machine_->GetCurrentState();
        switch (current_state) {
            case PlaylistState::IDLE:
                // 空闲状态，等待命令
                vTaskDelay(pdMS_TO_TICKS(50));
                break;
                
            case PlaylistState::LOADING:
                // 加载状态，处理加载逻辑
                HandleLoadingState();
                break;
                
            case PlaylistState::PLAYING:
                // 播放状态，处理播放逻辑
                HandlePlayingState();
                break;
                
            case PlaylistState::PAUSED:
                // 暂停状态，等待恢复命令
                vTaskDelay(pdMS_TO_TICKS(100));
                break;
                
            case PlaylistState::STOPPING:
                // 停止状态，清理资源
                HandleStoppingState();
                break;
                
            case PlaylistState::ERROR:
                // 错误状态，等待重置
                vTaskDelay(pdMS_TO_TICKS(200));
                break;
        }
    }
    
    ESP_LOGI(TAG, "Playlist task ended");
    vTaskDelete(nullptr);
}

void PlaylistManager::ProcessCommand(PlaylistCommand cmd, void* data) {
    ESP_LOGI(TAG, "Processing command: %d", static_cast<int>(cmd));
    
    switch (cmd) {
        case PlaylistCommand::PLAY:
            HandlePlayCommand();
            break;
            
        case PlaylistCommand::PAUSE:
            HandlePauseCommand();
            break;
            
        case PlaylistCommand::STOP:
            HandleStopCommand();
            break;
            
        case PlaylistCommand::NEXT:
            HandleNextCommand();
            break;
            
        case PlaylistCommand::PREV:
            HandlePrevCommand();
            break;
            
        case PlaylistCommand::SEEK:
            if (data) {
                int position = *static_cast<int*>(data);
                HandleSeekCommand(position);
            }
            break;
            
        case PlaylistCommand::EMERGENCY_STOP:
            HandleEmergencyStopCommand();
            break;
    }
}

void PlaylistManager::HandlePlayCommand() {
    ESP_LOGI(TAG, "Handling play command");
    
    if (state_machine_->ProcessEvent(PlaylistEvent::PLAY_REQUESTED, "User play request")) {
        ESP_LOGI(TAG, "Play command processed successfully");
    } else {
        ESP_LOGW(TAG, "Failed to process play command");
    }
}

void PlaylistManager::HandlePauseCommand() {
    ESP_LOGI(TAG, "Handling pause command");
    
    if (state_machine_->ProcessEvent(PlaylistEvent::PAUSE_REQUESTED, "User pause request")) {
        ESP_LOGI(TAG, "Pause command processed successfully");
    } else {
        ESP_LOGW(TAG, "Failed to process pause command");
    }
}

void PlaylistManager::HandleStopCommand() {
    ESP_LOGI(TAG, "Handling stop command");
    
    if (state_machine_->ProcessEvent(PlaylistEvent::STOP_REQUESTED, "User stop request")) {
        ESP_LOGI(TAG, "Stop command processed successfully");
    } else {
        ESP_LOGW(TAG, "Failed to process stop command");
    }
}

void PlaylistManager::HandleNextCommand() {
    ESP_LOGI(TAG, "Handling next command");
    
    if (state_machine_->ProcessEvent(PlaylistEvent::NEXT_REQUESTED, "User next request")) {
        ESP_LOGI(TAG, "Next command processed successfully");
    } else {
        ESP_LOGW(TAG, "Failed to process next command");
    }
}

void PlaylistManager::HandlePrevCommand() {
    ESP_LOGI(TAG, "Handling previous command");
    
    if (state_machine_->ProcessEvent(PlaylistEvent::PREV_REQUESTED, "User previous request")) {
        ESP_LOGI(TAG, "Previous command processed successfully");
    } else {
        ESP_LOGW(TAG, "Failed to process previous command");
    }
}

void PlaylistManager::HandleSeekCommand(int position) {
    ESP_LOGI(TAG, "Handling seek command to position %d", position);
    
    if (state_machine_->ProcessEvent(PlaylistEvent::SEEK_REQUESTED, "User seek request")) {
        ESP_LOGI(TAG, "Seek command processed successfully");
    } else {
        ESP_LOGW(TAG, "Failed to process seek command");
    }
}

void PlaylistManager::HandleEmergencyStopCommand() {
    ESP_LOGI(TAG, "Handling emergency stop command");
    
    emergency_stop_requested_ = true;
    
    if (state_machine_->ProcessEvent(PlaylistEvent::EMERGENCY_STOP, "Emergency stop")) {
        ESP_LOGI(TAG, "Emergency stop command processed successfully");
    } else {
        ESP_LOGW(TAG, "Failed to process emergency stop command");
    }
}

void PlaylistManager::HandleLoadingState() {
    // 这里实现加载逻辑
    ESP_LOGD(TAG, "Handling loading state");
    
    // 模拟加载完成
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // 加载完成后切换到播放状态
    if (state_machine_->ProcessEvent(PlaylistEvent::LOAD_COMPLETED, "Loading completed")) {
        ESP_LOGI(TAG, "Loading completed, switching to playing state");
    }
}

void PlaylistManager::HandlePlayingState() {
    // 这里实现播放逻辑
    ESP_LOGD(TAG, "Handling playing state");
    
    // 模拟播放进度
    vTaskDelay(pdMS_TO_TICKS(50));
    
    // 检查播放是否完成
    static int play_counter = 0;
    play_counter++;
    
    if (play_counter > 100) { // 模拟播放5秒后完成
        play_counter = 0;
        if (state_machine_->ProcessEvent(PlaylistEvent::PLAY_COMPLETED, "Play completed")) {
            ESP_LOGI(TAG, "Play completed, switching to loading state for next song");
        }
    }
}

void PlaylistManager::HandleStoppingState() {
    ESP_LOGI(TAG, "Handling stopping state");
    
    // 清理资源
    CleanupResources();
    
    // 切换到空闲状态
    if (state_machine_->ProcessEvent(PlaylistEvent::RESET_REQUESTED, "Stop completed")) {
        ESP_LOGI(TAG, "Stop completed, switching to idle state");
    }
}

void PlaylistManager::HandleStateChange(PlaylistState new_state) {
    ESP_LOGI(TAG, "State changed to: %d", static_cast<int>(new_state));
    
    // 调用状态变化回调
    if (callbacks_.on_state_change) {
        callbacks_.on_state_change(new_state);
    }
}

void PlaylistManager::HandleError(const std::string& error) {
    ESP_LOGE(TAG, "Playlist error: %s", error.c_str());
    
    // 调用错误回调
    if (callbacks_.on_error) {
        callbacks_.on_error(error);
    }
}

void PlaylistManager::CleanupResources() {
    ESP_LOGI(TAG, "Cleaning up resources");
    
    // 清理缓冲区
    if (buffer_manager_) {
        buffer_manager_->ClearBuffer();
        buffer_manager_->ReleaseAllBlocks();
    }
    
    // 取消所有下载
    if (downloader_) {
        downloader_->CancelAllDownloads();
    }
}

// 播放控制接口实现
bool PlaylistManager::PlayPlaylist(const std::string& query) {
    ESP_LOGI(TAG, "PlayPlaylist called with query: %s", query.c_str());
    
    // 这里应该实现歌单加载逻辑
    // 暂时返回成功
    return true;
}

bool PlaylistManager::PlaySong(const std::string& song_id) {
    ESP_LOGI(TAG, "PlaySong called with song_id: %s", song_id.c_str());
    
    // 发送播放命令
    std::pair<PlaylistCommand, void*> command_data(PlaylistCommand::PLAY, nullptr);
    if (xQueueSend(command_queue_, &command_data, pdMS_TO_TICKS(1000)) == pdTRUE) {
        return true;
    }
    
    return false;
}

bool PlaylistManager::Pause() {
    ESP_LOGI(TAG, "Pause called");
    
    std::pair<PlaylistCommand, void*> command_data(PlaylistCommand::PAUSE, nullptr);
    if (xQueueSend(command_queue_, &command_data, pdMS_TO_TICKS(1000)) == pdTRUE) {
        return true;
    }
    
    return false;
}

bool PlaylistManager::Resume() {
    ESP_LOGI(TAG, "Resume called");
    
    std::pair<PlaylistCommand, void*> command_data(PlaylistCommand::PLAY, nullptr);
    if (xQueueSend(command_queue_, &command_data, pdMS_TO_TICKS(1000)) == pdTRUE) {
        return true;
    }
    
    return false;
}

bool PlaylistManager::Stop() {
    ESP_LOGI(TAG, "Stop called");
    
    std::pair<PlaylistCommand, void*> command_data(PlaylistCommand::STOP, nullptr);
    if (xQueueSend(command_queue_, &command_data, pdMS_TO_TICKS(1000)) == pdTRUE) {
        return true;
    }
    
    return false;
}

bool PlaylistManager::Next() {
    ESP_LOGI(TAG, "Next called");
    
    std::pair<PlaylistCommand, void*> command_data(PlaylistCommand::NEXT, nullptr);
    if (xQueueSend(command_queue_, &command_data, pdMS_TO_TICKS(1000)) == pdTRUE) {
        return true;
    }
    
    return false;
}

bool PlaylistManager::Previous() {
    ESP_LOGI(TAG, "Previous called");
    
    std::pair<PlaylistCommand, void*> command_data(PlaylistCommand::PREV, nullptr);
    if (xQueueSend(command_queue_, &command_data, pdMS_TO_TICKS(1000)) == pdTRUE) {
        return true;
    }
    
    return false;
}

bool PlaylistManager::Seek(int position_ms) {
    ESP_LOGI(TAG, "Seek called to position: %d ms", position_ms);
    
    int* position_ptr = new int(position_ms);
    std::pair<PlaylistCommand, void*> command_data(PlaylistCommand::SEEK, position_ptr);
    if (xQueueSend(command_queue_, &command_data, pdMS_TO_TICKS(1000)) == pdTRUE) {
        return true;
    }
    
    delete position_ptr;
    return false;
}

void PlaylistManager::EmergencyStop() {
    ESP_LOGI(TAG, "EmergencyStop called");
    
    std::pair<PlaylistCommand, void*> command_data(PlaylistCommand::EMERGENCY_STOP, nullptr);
    xQueueSend(command_queue_, &command_data, 0); // 不等待，立即发送
}

// 状态查询接口
PlaylistState PlaylistManager::GetState() const {
    if (state_machine_) {
        return state_machine_->GetCurrentState();
    }
    return PlaylistState::ERROR;
}

const PlaylistInfo& PlaylistManager::GetCurrentPlaylist() const {
    return current_playlist_;
}

const SongInfo* PlaylistManager::GetCurrentSong() const {
    if (current_playlist_.current_index >= 0 && 
        current_playlist_.current_index < static_cast<int>(current_playlist_.songs.size())) {
        return &current_playlist_.songs[current_playlist_.current_index];
    }
    return nullptr;
}

int PlaylistManager::GetCurrentPosition() const {
    // 这里应该返回当前播放位置
    return 0;
}

int PlaylistManager::GetDuration() const {
    const SongInfo* song = GetCurrentSong();
    return song ? song->duration_ms : 0;
}

bool PlaylistManager::IsPlaying() const {
    return state_machine_ && state_machine_->IsInState(PlaylistState::PLAYING);
}

// 内存管理接口
size_t PlaylistManager::GetBufferUsage() const {
    if (buffer_manager_) {
        return buffer_manager_->GetBufferUsage();
    }
    return 0;
}

size_t PlaylistManager::GetFreeMemory() const {
    if (buffer_manager_) {
        return buffer_manager_->GetFreeBlockCount() * buffer_manager_->GetTotalAllocated();
    }
    return 0;
}

void PlaylistManager::PrintMemoryStats() const {
    if (buffer_manager_) {
        buffer_manager_->PrintMemoryStats();
    }
}

// 其他接口的简单实现
bool PlaylistManager::LoadPlaylist(const std::string& query) {
    ESP_LOGI(TAG, "LoadPlaylist called with query: %s", query.c_str());
    return true;
}

void PlaylistManager::SetShuffle(bool enabled) {
    current_playlist_.shuffle = enabled;
    ESP_LOGI(TAG, "Shuffle set to: %s", enabled ? "true" : "false");
}

void PlaylistManager::SetRepeat(bool enabled) {
    current_playlist_.repeat = enabled;
    ESP_LOGI(TAG, "Repeat set to: %s", enabled ? "true" : "false");
}

void PlaylistManager::ClearPlaylist() {
    current_playlist_.songs.clear();
    current_playlist_.current_index = 0;
    ESP_LOGI(TAG, "Playlist cleared");
}

void PlaylistManager::Reset() {
    ESP_LOGI(TAG, "Reset called");
    if (state_machine_) {
        state_machine_->ProcessEvent(PlaylistEvent::RESET_REQUESTED, "Reset requested");
    }
}

void PlaylistManager::PreloadNextSongs() {
    ESP_LOGD(TAG, "PreloadNextSongs called");
    // 这里实现预加载逻辑
}

// 内存优化方法实现
bool PlaylistManager::EnableMemoryOptimization() {
    ESP_LOGI(TAG, "Enabling memory optimization mode");
    
    // 停止当前播放
    if (current_state_ == PlaylistState::PLAYING) {
        Pause();
    }
    
    // 清理资源
    CleanupResources();
    
    // 重新初始化为优化配置
    PlaylistConfig optimized_config = PlaylistConfig::MemoryOptimized();
    if (!Initialize(optimized_config)) {
        ESP_LOGE(TAG, "Failed to initialize with memory optimization");
        return false;
    }
    
    ESP_LOGI(TAG, "Memory optimization enabled successfully");
    return true;
}

bool PlaylistManager::EnableMinimalMemoryMode() {
    ESP_LOGI(TAG, "Enabling minimal memory mode");
    
    // 停止当前播放
    if (current_state_ == PlaylistState::PLAYING) {
        Pause();
    }
    
    // 清理资源
    CleanupResources();
    
    // 重新初始化为最小内存配置
    PlaylistConfig minimal_config = PlaylistConfig::MinimalMemory();
    if (!Initialize(minimal_config)) {
        ESP_LOGE(TAG, "Failed to initialize with minimal memory mode");
        return false;
    }
    
    ESP_LOGI(TAG, "Minimal memory mode enabled successfully");
    return true;
}

void PlaylistManager::SetDynamicMemoryAdjustment(bool enabled) {
    ESP_LOGI(TAG, "Dynamic memory adjustment %s", enabled ? "enabled" : "disabled");
    // 这里可以实现动态内存调整逻辑
}

bool PlaylistManager::IsMemoryLow() const {
    if (!buffer_manager_) {
        return true;
    }
    
    // 检查内存池使用率
    size_t free_blocks = buffer_manager_->GetFreeBlockCount();
    size_t total_blocks = buffer_manager_->GetUsedBlockCount() + free_blocks;
    
    if (total_blocks == 0) {
        return true;
    }
    
    float usage_ratio = (float)(total_blocks - free_blocks) / total_blocks;
    return usage_ratio > 0.8f; // 80%以上使用率认为内存不足
}

void PlaylistManager::ForceGarbageCollection() {
    ESP_LOGI(TAG, "Forcing garbage collection");
    
    if (buffer_manager_) {
        buffer_manager_->Defragment();
        buffer_manager_->ClearPreloadQueue();
    }
    
    // 清理播放列表数据
    if (current_playlist_.songs.size() > 10) {
        // 只保留当前歌曲和前后几首
        int current_index = current_playlist_.current_index;
        std::vector<SongInfo> optimized_songs;
        
        for (int i = std::max(0, current_index - 2); 
             i < std::min((int)current_playlist_.songs.size(), current_index + 3); 
             i++) {
            optimized_songs.push_back(current_playlist_.songs[i]);
        }
        
        current_playlist_.songs = optimized_songs;
        current_playlist_.current_index = std::min(2, current_index);
    }
}

PlaylistManager::MemoryStats PlaylistManager::GetMemoryStats() const {
    MemoryStats stats = {};
    
    if (buffer_manager_) {
        stats.buffer_used = buffer_manager_->GetBufferUsage();
        stats.pool_used = buffer_manager_->GetTotalAllocated();
        stats.total_used = stats.buffer_used + stats.pool_used;
        
        // 估算总可用内存 (假设PSRAM为8MB)
        size_t total_memory = 8 * 1024 * 1024; // 8MB
        stats.free_memory = total_memory - stats.total_used;
        stats.usage_percentage = (float)stats.total_used / total_memory * 100.0f;
    }
    
    return stats;
} 