#include "playlist_player.h"
#include "playlist_manager.h"
#include "esp32_music.h"
#include "audio_buffer_manager.h"
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

static const char* TAG = "PlaylistPlayer";



PlaylistPlayer::PlaylistPlayer() 
    : current_state_(PlayerState::IDLE), current_song_index_(0), total_songs_(0),
      player_task_handle_(nullptr), command_queue_(nullptr), state_mutex_(nullptr),
      is_running_(false), emergency_stop_requested_(false), auto_play_next_(true) {
}

PlaylistPlayer::~PlaylistPlayer() {
    Deinitialize();
}

bool PlaylistPlayer::Initialize(const PlayerConfig& config) {
    ESP_LOGI(TAG, "Initializing PlaylistPlayer");
    
    config_ = config;
    
    // 创建互斥锁
    state_mutex_ = xSemaphoreCreateMutex();
    if (!state_mutex_) {
        ESP_LOGE(TAG, "Failed to create state mutex");
        return false;
    }
    
    // 创建命令队列
    command_queue_ = xQueueCreate(10, sizeof(PlayerCommandData));
    if (!command_queue_) {
        ESP_LOGE(TAG, "Failed to create command queue");
        vSemaphoreDelete(state_mutex_);
        return false;
    }
    
    // 初始化播放列表管理器
    playlist_manager_ = std::make_unique<PlaylistManager>();
    PlaylistConfig playlist_config;
    playlist_config.buffer_size = config.buffer_size;
    playlist_config.preload_count = config.preload_count;
    playlist_config.task_stack_size = config.task_stack_size;
    playlist_config.task_priority = config.task_priority;
    
    if (!playlist_manager_->Initialize(playlist_config)) {
        ESP_LOGE(TAG, "Failed to initialize PlaylistManager");
        return false;
    }
    
    // 初始化音频播放器
    music_player_ = std::make_unique<Esp32Music>();
    
    // 初始化缓冲区管理器
    buffer_manager_ = std::make_unique<AudioBufferManager>();
    MemoryPoolConfig buffer_config;
    buffer_config.block_size = 32 * 1024;
    buffer_config.block_count = 8;
    buffer_config.max_blocks = 16;
    
    if (!buffer_manager_->Initialize(buffer_config)) {
        ESP_LOGE(TAG, "Failed to initialize AudioBufferManager");
        return false;
    }
    
    // 设置播放列表回调
    PlaylistCallbacks playlist_callbacks;
    playlist_callbacks.on_song_start = [this](const SongInfo& song) {
        HandleSongStart(song.title);
    };
    playlist_callbacks.on_song_end = [this](const SongInfo& song) {
        HandleSongEnd(song.title);
    };
    playlist_callbacks.on_progress = [this](const SongInfo& song, int progress) {
        if (callbacks_.on_progress) {
            callbacks_.on_progress(song.title, progress);
        }
    };
    playlist_callbacks.on_error = [this](const std::string& error) {
        HandleError(error);
    };
    playlist_callbacks.on_playlist_end = [this]() {
        HandlePlaylistEnd();
    };
    playlist_callbacks.on_state_change = [this](PlaylistState state) {
        // 将播放列表状态映射到播放器状态
        switch (state) {
            case PlaylistState::IDLE:
                SetState(PlayerState::IDLE);
                break;
            case PlaylistState::LOADING:
                SetState(PlayerState::LOADING);
                break;
            case PlaylistState::PLAYING:
                SetState(PlayerState::PLAYING);
                break;
            case PlaylistState::PAUSED:
                SetState(PlayerState::PAUSED);
                break;
            case PlaylistState::STOPPING:
                SetState(PlayerState::STOPPING);
                break;
            case PlaylistState::ERROR:
                SetState(PlayerState::ERROR);
                break;
        }
    };
    
    playlist_manager_->SetCallbacks(playlist_callbacks);
    
    // 创建播放任务
    is_running_ = true;
    BaseType_t ret = xTaskCreate([](void* arg) {
        PlaylistPlayer* player = static_cast<PlaylistPlayer*>(arg);
        player->PlayerTaskLoop();
        vTaskDelete(nullptr);
    }, "playlist_player", config.task_stack_size, this, config.task_priority, &player_task_handle_);
    
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create player task");
        is_running_ = false;
        return false;
    }
    
    SetState(PlayerState::IDLE);
    ESP_LOGI(TAG, "PlaylistPlayer initialized successfully");
    return true;
}

void PlaylistPlayer::Deinitialize() {
    ESP_LOGI(TAG, "Deinitializing PlaylistPlayer");
    
    is_running_ = false;
    emergency_stop_requested_ = true;
    
    // 停止音频播放
    if (music_player_) {
        music_player_->EmergencyStop();
    }
    
    // 等待任务结束
    if (player_task_handle_) {
        vTaskDelete(player_task_handle_);
        player_task_handle_ = nullptr;
    }
    
    // 清理组件
    if (playlist_manager_) {
        playlist_manager_->Deinitialize();
        playlist_manager_.reset();
    }
    
    if (buffer_manager_) {
        buffer_manager_->Deinitialize();
        buffer_manager_.reset();
    }
    
    music_player_.reset();
    
    // 清理队列和互斥锁
    if (command_queue_) {
        vQueueDelete(command_queue_);
        command_queue_ = nullptr;
    }
    
    if (state_mutex_) {
        vSemaphoreDelete(state_mutex_);
        state_mutex_ = nullptr;
    }
    
    SetState(PlayerState::IDLE);
    ESP_LOGI(TAG, "PlaylistPlayer deinitialized");
}

void PlaylistPlayer::SetCallbacks(const PlayerCallbacks& callbacks) {
    callbacks_ = callbacks;
}

bool PlaylistPlayer::PlayPlaylist(const std::string& query) {
    ESP_LOGI(TAG, "Playing playlist: %s", query.c_str());
    
    PlayerCommandData cmd(PlayerCommand::PLAY_PLAYLIST, query);
    if (xQueueSend(command_queue_, &cmd, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to send play playlist command");
        return false;
    }
    
    return true;
}

bool PlaylistPlayer::PlaySong(const std::string& song_name) {
    ESP_LOGI(TAG, "Playing song: %s", song_name.c_str());
    
    PlayerCommandData cmd(PlayerCommand::PLAY_SONG, song_name);
    if (xQueueSend(command_queue_, &cmd, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to send play song command");
        return false;
    }
    
    return true;
}

bool PlaylistPlayer::Pause() {
    ESP_LOGI(TAG, "Pausing playback");
    
    PlayerCommandData cmd(PlayerCommand::PAUSE);
    if (xQueueSend(command_queue_, &cmd, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to send pause command");
        return false;
    }
    
    return true;
}

bool PlaylistPlayer::Resume() {
    ESP_LOGI(TAG, "Resuming playback");
    
    PlayerCommandData cmd(PlayerCommand::RESUME);
    if (xQueueSend(command_queue_, &cmd, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to send resume command");
        return false;
    }
    
    return true;
}

bool PlaylistPlayer::Stop() {
    ESP_LOGI(TAG, "Stopping playback");
    
    PlayerCommandData cmd(PlayerCommand::STOP);
    if (xQueueSend(command_queue_, &cmd, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to send stop command");
        return false;
    }
    
    return true;
}

bool PlaylistPlayer::Next() {
    ESP_LOGI(TAG, "Playing next song");
    
    PlayerCommandData cmd(PlayerCommand::NEXT);
    if (xQueueSend(command_queue_, &cmd, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to send next command");
        return false;
    }
    
    return true;
}

bool PlaylistPlayer::Previous() {
    ESP_LOGI(TAG, "Playing previous song");
    
    PlayerCommandData cmd(PlayerCommand::PREV);
    if (xQueueSend(command_queue_, &cmd, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to send previous command");
        return false;
    }
    
    return true;
}

bool PlaylistPlayer::Seek(int position_ms) {
    ESP_LOGI(TAG, "Seeking to position: %d ms", position_ms);
    
    PlayerCommandData cmd(PlayerCommand::SEEK, "", position_ms);
    if (xQueueSend(command_queue_, &cmd, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to send seek command");
        return false;
    }
    
    return true;
}

void PlaylistPlayer::EmergencyStop() {
    ESP_LOGI(TAG, "Emergency stop requested");
    
    emergency_stop_requested_ = true;
    
    PlayerCommandData cmd(PlayerCommand::EMERGENCY_STOP);
    xQueueSend(command_queue_, &cmd, 0); // 不等待，立即发送
    
    // 立即停止音频播放
    if (music_player_) {
        music_player_->EmergencyStop();
    }
}

void PlaylistPlayer::PlayerTaskLoop() {
    ESP_LOGI(TAG, "Player task started");
    
    while (is_running_ && !emergency_stop_requested_) {
        PlayerCommandData cmd(PlayerCommand::STOP);
        
        // 等待命令
        if (xQueueReceive(command_queue_, &cmd, pdMS_TO_TICKS(100)) == pdTRUE) {
            ProcessCommand(cmd);
        }
        
        // 处理播放状态
        ProcessPlaylistState();
        
        // 短暂延时
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    
    ESP_LOGI(TAG, "Player task ended");
}

void PlaylistPlayer::ProcessCommand(const PlayerCommandData& cmd) {
    switch (cmd.cmd) {
        case PlayerCommand::PLAY_PLAYLIST:
            if (playlist_manager_) {
                playlist_manager_->PlayPlaylist(cmd.data);
            }
            break;
            
        case PlayerCommand::PLAY_SONG:
            if (playlist_manager_) {
                playlist_manager_->PlaySong(cmd.data);
            }
            break;
            
        case PlayerCommand::PAUSE:
            if (playlist_manager_) {
                playlist_manager_->Pause();
            }
            if (music_player_) {
                PauseAudioPlayback();
            }
            break;
            
        case PlayerCommand::RESUME:
            if (playlist_manager_) {
                playlist_manager_->Resume();
            }
            if (music_player_) {
                ResumeAudioPlayback();
            }
            break;
            
        case PlayerCommand::STOP:
            if (playlist_manager_) {
                playlist_manager_->Stop();
            }
            if (music_player_) {
                StopAudioPlayback();
            }
            break;
            
        case PlayerCommand::NEXT:
            if (playlist_manager_) {
                playlist_manager_->Next();
            }
            break;
            
        case PlayerCommand::PREV:
            if (playlist_manager_) {
                playlist_manager_->Previous();
            }
            break;
            
        case PlayerCommand::SEEK:
            if (playlist_manager_) {
                playlist_manager_->Seek(cmd.value);
            }
            break;
            
        case PlayerCommand::EMERGENCY_STOP:
            if (playlist_manager_) {
                playlist_manager_->EmergencyStop();
            }
            if (music_player_) {
                music_player_->EmergencyStop();
            }
            SetState(PlayerState::IDLE);
            break;
    }
}

void PlaylistPlayer::ProcessPlaylistState() {
    if (!playlist_manager_) return;
    
    // 检查播放列表状态并同步音频播放
    PlaylistState playlist_state = playlist_manager_->GetState();
    
    switch (playlist_state) {
        case PlaylistState::PLAYING:
            // 确保音频播放器正在播放
            if (music_player_ && !music_player_->IsPlaying()) {
                // 获取当前歌曲信息并开始播放
                const SongInfo* current_song = playlist_manager_->GetCurrentSong();
                if (current_song) {
                    StartAudioPlayback(current_song->audio_url, current_song->title);
                }
            }
            break;
            
        case PlaylistState::PAUSED:
            // 暂停音频播放
            if (music_player_ && music_player_->IsPlaying()) {
                PauseAudioPlayback();
            }
            break;
            
        case PlaylistState::STOPPING:
            // 停止音频播放
            if (music_player_) {
                StopAudioPlayback();
            }
            break;
            
        default:
            break;
    }
}

void PlaylistPlayer::HandleSongStart(const std::string& song_name) {
    ESP_LOGI(TAG, "Song started: %s", song_name.c_str());
    
    current_song_name_ = song_name;
    
    if (callbacks_.on_song_start) {
        callbacks_.on_song_start(song_name);
    }
}

void PlaylistPlayer::HandleSongEnd(const std::string& song_name) {
    ESP_LOGI(TAG, "Song ended: %s", song_name.c_str());
    
    if (callbacks_.on_song_end) {
        callbacks_.on_song_end(song_name);
    }
    
    // 自动播放下一首
    if (auto_play_next_) {
        Next();
    }
}

void PlaylistPlayer::HandlePlaylistEnd() {
    ESP_LOGI(TAG, "Playlist ended");
    
    if (callbacks_.on_playlist_end) {
        callbacks_.on_playlist_end();
    }
    
    SetState(PlayerState::IDLE);
}

void PlaylistPlayer::HandleError(const std::string& error) {
    ESP_LOGE(TAG, "Playlist error: %s", error.c_str());
    
    if (callbacks_.on_error) {
        callbacks_.on_error(error);
    }
    
    SetState(PlayerState::ERROR);
}

void PlaylistPlayer::SetState(PlayerState new_state) {
    if (xSemaphoreTake(state_mutex_, pdMS_TO_TICKS(100)) == pdTRUE) {
        PlayerState old_state = current_state_;
        current_state_ = new_state;
        xSemaphoreGive(state_mutex_);
        
        if (old_state != new_state) {
            ESP_LOGI(TAG, "Player state changed: %d -> %d", (int)old_state, (int)new_state);
            
            if (callbacks_.on_state_change) {
                callbacks_.on_state_change(new_state);
            }
        }
    }
}

bool PlaylistPlayer::StartAudioPlayback(const std::string& song_url, const std::string& song_name) {
    ESP_LOGI(TAG, "Starting audio playback: %s", song_name.c_str());
    
    if (!music_player_) {
        ESP_LOGE(TAG, "Music player not initialized");
        return false;
    }
    
    // 使用现有的音频播放系统
    return music_player_->StartStreaming(song_url, false);
}

bool PlaylistPlayer::StopAudioPlayback() {
    ESP_LOGI(TAG, "Stopping audio playback");
    
    if (!music_player_) {
        return false;
    }
    
    return music_player_->StopStreaming();
}

bool PlaylistPlayer::PauseAudioPlayback() {
    ESP_LOGI(TAG, "Pausing audio playback");
    
    if (!music_player_) {
        return false;
    }
    
    // 注意：现有的Esp32Music可能没有直接的暂停方法
    // 这里需要根据实际实现来调整
    return true;
}

bool PlaylistPlayer::ResumeAudioPlayback() {
    ESP_LOGI(TAG, "Resuming audio playback");
    
    if (!music_player_) {
        return false;
    }
    
    // 注意：现有的Esp32Music可能没有直接的恢复方法
    // 这里需要根据实际实现来调整
    return true;
}

void PlaylistPlayer::HandleAudioPlaybackEnd() {
    ESP_LOGI(TAG, "Audio playback ended");
    
    // 通知播放列表管理器歌曲播放结束
    if (playlist_manager_) {
        // 这里可能需要添加一个方法来通知播放结束
    }
}

// 状态查询方法
PlaylistPlayer::PlayerState PlaylistPlayer::GetState() const {
    if (xSemaphoreTake(state_mutex_, pdMS_TO_TICKS(100)) == pdTRUE) {
        PlayerState state = current_state_;
        xSemaphoreGive(state_mutex_);
        return state;
    }
    return PlayerState::ERROR;
}

const std::string& PlaylistPlayer::GetCurrentSongName() const {
    return current_song_name_;
}

bool PlaylistPlayer::IsPlaying() const {
    return GetState() == PlayerState::PLAYING;
}

// 内存管理方法
size_t PlaylistPlayer::GetBufferUsage() const {
    if (buffer_manager_) {
        return buffer_manager_->GetBufferUsage();
    }
    return 0;
}

size_t PlaylistPlayer::GetFreeMemory() const {
    if (buffer_manager_) {
        return buffer_manager_->GetFreeBlockCount() * 32 * 1024; // 估算
    }
    return 0;
}

void PlaylistPlayer::PrintMemoryStats() const {
    if (buffer_manager_) {
        buffer_manager_->PrintMemoryStats();
    }
}

// 其他方法的简单实现
void PlaylistPlayer::SetShuffle(bool enabled) {
    if (playlist_manager_) {
        playlist_manager_->SetShuffle(enabled);
    }
}

void PlaylistPlayer::SetRepeat(bool enabled) {
    if (playlist_manager_) {
        playlist_manager_->SetRepeat(enabled);
    }
}

void PlaylistPlayer::SetAutoPlayNext(bool enabled) {
    auto_play_next_ = enabled;
}

void PlaylistPlayer::ClearPlaylist() {
    if (playlist_manager_) {
        playlist_manager_->ClearPlaylist();
    }
}

void PlaylistPlayer::Reset() {
    ESP_LOGI(TAG, "Resetting playlist player");
    
    if (playlist_manager_) {
        playlist_manager_->Reset();
    }
    
    SetState(PlayerState::IDLE);
    emergency_stop_requested_ = false;
} 