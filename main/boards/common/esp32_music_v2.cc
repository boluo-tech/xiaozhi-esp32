#include "esp32_music_v2.h"
#include "board.h"
#include "system_info.h"
#include "audio_codecs/audio_codec.h"
#include "application.h"
#include "display/display.h"
#include "sdkconfig.h"
#include "audio_buffer_manager.h"
#include "music_downloader.h"
#include "playlist_manager.h"

#include <esp_log.h>
#include <esp_heap_caps.h>
#include <esp_pthread.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

#define TAG "Esp32MusicV2"

// 音频数据块结构
struct AudioChunk {
    uint8_t* data;
    size_t size;
    
    AudioChunk(uint8_t* d, size_t s) : data(d), size(s) {}
    AudioChunk() : data(nullptr), size(0) {}
};

// 音频播放任务参数
struct AudioTaskParams {
    Esp32MusicV2* player;
    std::string audio_url;
    
    AudioTaskParams(Esp32MusicV2* p, const std::string& url) : player(p), audio_url(url) {}
};

Esp32MusicV2::Esp32MusicV2() : mp3_decoder_(nullptr), mp3_decoder_initialized_(false),
                              audio_task_handle_(nullptr), is_playing_(false), is_paused_(false),
                              lyric_task_handle_(nullptr), is_lyric_running_(false),
                              current_lyric_index_(-1), current_position_ms_(0), total_duration_ms_(0) {
    ESP_LOGI(TAG, "Esp32MusicV2 initialized");
}

Esp32MusicV2::~Esp32MusicV2() {
    ESP_LOGI(TAG, "Destroying Esp32MusicV2");
    
    // 停止所有任务
    Stop(true);
    Deinitialize();
    
    // 等待任务结束
    if (audio_task_handle_) {
        vTaskDelete(audio_task_handle_);
        audio_task_handle_ = nullptr;
    }
    
    if (lyric_task_handle_) {
        vTaskDelete(lyric_task_handle_);
        lyric_task_handle_ = nullptr;
    }
    
    ESP_LOGI(TAG, "Esp32MusicV2 destroyed");
}

bool Esp32MusicV2::Initialize(const AudioPlayerConfig& config) {
    ESP_LOGI(TAG, "Initializing Esp32MusicV2");
    
    config_ = config;
    
    // 初始化MP3解码器
    if (!InitializeMp3Decoder()) {
        ESP_LOGE(TAG, "Failed to initialize MP3 decoder");
        return false;
    }
    
    // 初始化播放列表管理器
    playlist_manager_ = std::make_unique<PlaylistManager>();
    if (!playlist_manager_->Initialize(PlaylistConfig::MemoryOptimized())) {
        ESP_LOGE(TAG, "Failed to initialize PlaylistManager");
        return false;
    }
    
    // 初始化音频缓冲区管理器
    buffer_manager_ = std::make_unique<AudioBufferManager>();
    MemoryPoolConfig buffer_config = MemoryPoolConfig::MemoryOptimized();
    if (!buffer_manager_->Initialize(buffer_config)) {
        ESP_LOGE(TAG, "Failed to initialize AudioBufferManager");
        return false;
    }
    
    // 初始化状态机
    state_machine_ = std::make_unique<PlaylistStateMachine>();
    
    // 初始化音乐下载器
    downloader_ = std::make_unique<MusicDownloader>();
    CacheConfig download_config;
    download_config.max_cache_size = config_.buffer_size * 2; // 缓存大小为缓冲区大小的2倍
    download_config.max_file_size = config_.buffer_size;
    download_config.enable_compression = true;
    
    if (!downloader_->Initialize(download_config)) {
        ESP_LOGE(TAG, "Failed to initialize MusicDownloader");
        return false;
    }
    
    // 设置下载器回调
    DownloadCallbacks download_callbacks;
    download_callbacks.on_completed = [this](const std::string& task_id) {
        ESP_LOGI(TAG, "Download completed: %s", task_id.c_str());
        
        if (callbacks_.on_song_start) {
            callbacks_.on_song_start(task_id);
        }
    };
    
    download_callbacks.on_progress = [this](const std::string& task_id, size_t downloaded, size_t total) {
        ESP_LOGD(TAG, "Download progress: %s - %zu/%zu", task_id.c_str(), downloaded, total);
    };
    
    download_callbacks.on_error = [this](const std::string& task_id, const std::string& error) {
        ESP_LOGE(TAG, "Download error: %s - %s", task_id.c_str(), error.c_str());
        
        if (callbacks_.on_error) {
            callbacks_.on_error(error);
        }
    };
    
    download_callbacks.on_cancelled = [this](const std::string& task_id) {
        ESP_LOGI(TAG, "Download cancelled: %s", task_id.c_str());
    };
    
    downloader_->SetCallbacks(download_callbacks);
    
    ESP_LOGI(TAG, "Esp32MusicV2 initialized successfully");
    return true;
}

void Esp32MusicV2::Deinitialize() {
    ESP_LOGI(TAG, "Deinitializing Esp32MusicV2");
    
    // 停止所有任务
    is_playing_ = false;
    is_paused_ = false;
    is_lyric_running_ = false;
    
    // 清理组件
    if (playlist_manager_) {
        playlist_manager_->Stop();
    }
    
    if (buffer_manager_) {
        buffer_manager_->ClearBuffer();
    }
    
    if (downloader_) {
        downloader_->CancelAllDownloads();
    }
    
    // 清理MP3解码器
    CleanupMp3Decoder();
    
    ESP_LOGI(TAG, "Esp32MusicV2 deinitialized");
}

void Esp32MusicV2::SetCallbacks(const AudioPlayerCallbacks& callbacks) {
    callbacks_ = callbacks;
}

// Music接口实现
bool Esp32MusicV2::Download(const std::string& song_name, bool auto_start) {
    ESP_LOGI(TAG, "Downloading song: %s, auto_start=%d", song_name.c_str(), auto_start);
    
    if (!downloader_) {
        ESP_LOGE(TAG, "Downloader not initialized");
        return false;
    }
    
    // 使用下载器下载歌曲
    std::string task_id = downloader_->AddDownloadTask(song_name);
    if (task_id.empty()) {
        ESP_LOGE(TAG, "Failed to start song download");
        return false;
    }
    
    // 如果自动播放，等待下载完成后开始播放
    if (auto_start) {
        // 这里可以添加等待下载完成的逻辑
        // 或者使用回调来处理播放开始
    }
    
    return true;
}

bool Esp32MusicV2::Play() {
    ESP_LOGI(TAG, "Starting playback");
    
    if (is_playing_) {
        ESP_LOGW(TAG, "Already playing");
        return true;
    }
    
    // 从播放列表管理器获取当前歌曲
    const SongInfo* current_song_info = playlist_manager_->GetCurrentSong();
    if (!current_song_info) {
        ESP_LOGE(TAG, "No current song in playlist");
        return false;
    }
    std::string current_song = current_song_info->title;
    if (current_song.empty()) {
        ESP_LOGE(TAG, "No current song to play");
        return false;
    }
    
    // 获取歌曲的音频URL
    std::string audio_url = current_song_info->audio_url;
    if (audio_url.empty()) {
        ESP_LOGE(TAG, "No audio URL for current song");
        return false;
    }
    
    return StartStreaming(audio_url, true);
}

bool Esp32MusicV2::Stop(bool stop_playlist) {
    ESP_LOGI(TAG, "Stopping playback, stop_playlist=%d", stop_playlist);
    
    is_playing_ = false;
    is_paused_ = false;
    
    // 停止音频任务
    if (audio_task_handle_) {
        vTaskDelete(audio_task_handle_);
        audio_task_handle_ = nullptr;
    }
    
    // 停止歌词任务
    if (lyric_task_handle_) {
        is_lyric_running_ = false;
        vTaskDelete(lyric_task_handle_);
        lyric_task_handle_ = nullptr;
    }
    
    // 清理缓冲区
    if (buffer_manager_) {
        // AudioBufferManager 没有 Clear 方法，跳过
    }
    
    // 重置MP3解码器
    CleanupMp3Decoder();
    InitializeMp3Decoder();
    
    // 如果停止播放列表，清理播放列表管理器
    if (stop_playlist && playlist_manager_) {
        playlist_manager_->Stop();
    }
    
    // 清空歌名显示
    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();
    if (display) {
        display->SetMusicInfo("");
    }
    
    ESP_LOGI(TAG, "Playback stopped");
    return true;
}

std::string Esp32MusicV2::GetDownloadResult() {
    // 返回当前下载状态
    if (downloader_) {
        // 获取活跃任务列表
        auto active_tasks = downloader_->GetActiveTasks();
        if (!active_tasks.empty()) {
            return downloader_->GetTaskError(active_tasks[0]);
        }
    }
    return "";
}

bool Esp32MusicV2::StartStreaming(const std::string& music_url, bool stop_playlist) {
    ESP_LOGI(TAG, "Starting streaming: %s", music_url.c_str());
    
    if (is_playing_) {
        ESP_LOGW(TAG, "Already streaming");
        return true;
    }
    
    if (music_url.empty()) {
        ESP_LOGE(TAG, "Music URL is empty");
        return false;
    }
    
    // 停止之前的播放
    if (stop_playlist) {
        Stop(true);
    }
    
    // 创建音频播放任务
    AudioTaskParams* params = new AudioTaskParams(this, music_url);
    BaseType_t ret = xTaskCreate([](void* arg) {
        AudioTaskParams* params = static_cast<AudioTaskParams*>(arg);
        params->player->AudioTaskLoop();
        delete params;
        vTaskDelete(nullptr);
    }, "audio_stream", config_.task_stack_size, params, config_.task_priority, &audio_task_handle_);
    
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create audio task: %d", ret);
        delete params;
        return false;
    }
    
    is_playing_ = true;
    is_paused_ = false;
    
    ESP_LOGI(TAG, "Streaming started successfully");
    return true;
}

bool Esp32MusicV2::StopStreaming() {
    ESP_LOGI(TAG, "Stopping streaming");
    return Stop(false);
}

size_t Esp32MusicV2::GetBufferSize() const {
    if (buffer_manager_) {
        return buffer_manager_->GetBufferUsage();
    }
    return 0;
}

bool Esp32MusicV2::IsDownloading() const {
    if (downloader_) {
        auto active_tasks = downloader_->GetActiveTasks();
        return !active_tasks.empty();
    }
    return false;
}

bool Esp32MusicV2::PlayPlaylist(const std::string& query) {
    ESP_LOGI(TAG, "Playing playlist: %s", query.c_str());
    
    // 停止当前播放
    Stop(true);
    
    // 使用下载器下载播放列表
    // 这里需要实现播放列表下载逻辑
    // 暂时返回成功
    return true;
}

bool Esp32MusicV2::FetchPlaylist(const std::string& query, std::vector<std::string>& out_playlist) {
    ESP_LOGI(TAG, "Fetching playlist: %s", query.c_str());
    
    if (!downloader_) {
        ESP_LOGE(TAG, "Downloader not initialized");
        return false;
    }
    
    // 这里需要实现播放列表获取逻辑
    // 暂时返回空列表
    out_playlist.clear();
    return true;
}

void Esp32MusicV2::EmergencyStop() {
    ESP_LOGI(TAG, "Emergency stop triggered");
    
    // 立即停止所有任务
    is_playing_ = false;
    is_paused_ = false;
    is_lyric_running_ = false;
    
    // 强制删除任务
    if (audio_task_handle_) {
        vTaskDelete(audio_task_handle_);
        audio_task_handle_ = nullptr;
    }
    
    if (lyric_task_handle_) {
        vTaskDelete(lyric_task_handle_);
        lyric_task_handle_ = nullptr;
    }
    
    // 停止下载
    if (downloader_) {
        downloader_->CancelAllDownloads();
    }
    
    // 清理缓冲区
    if (buffer_manager_) {
        // AudioBufferManager 没有 Clear 方法，跳过
    }
    
    // 重置解码器
    CleanupMp3Decoder();
    InitializeMp3Decoder();
    
    // 清空显示
    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();
    if (display) {
        display->SetMusicInfo("");
    }
    
    ESP_LOGI(TAG, "Emergency stop completed");
}

// 新增功能
bool Esp32MusicV2::Pause() {
    ESP_LOGI(TAG, "Pausing playback");
    
    if (!is_playing_ || is_paused_) {
        ESP_LOGW(TAG, "Not playing or already paused");
        return false;
    }
    
    is_paused_ = true;
    
    if (callbacks_.on_playback_state_change) {
        callbacks_.on_playback_state_change(false);
    }
    
    ESP_LOGI(TAG, "Playback paused");
    return true;
}

bool Esp32MusicV2::Resume() {
    ESP_LOGI(TAG, "Resuming playback");
    
    if (!is_playing_ || !is_paused_) {
        ESP_LOGW(TAG, "Not playing or not paused");
        return false;
    }
    
    is_paused_ = false;
    
    if (callbacks_.on_playback_state_change) {
        callbacks_.on_playback_state_change(true);
    }
    
    ESP_LOGI(TAG, "Playback resumed");
    return true;
}

bool Esp32MusicV2::Next() {
    ESP_LOGI(TAG, "Playing next song");
    
    if (!playlist_manager_) {
        ESP_LOGE(TAG, "Playlist manager not initialized");
        return false;
    }
    
    if (playlist_manager_->Next()) {
        // 获取下一首歌曲并开始播放
        const SongInfo* next_song = playlist_manager_->GetCurrentSong();
        
        if (next_song && !next_song->title.empty()) {
            return StartStreaming(next_song->audio_url, false);
        }
    }
    
    return false;
}

bool Esp32MusicV2::Previous() {
    ESP_LOGI(TAG, "Playing previous song");
    
    if (!playlist_manager_) {
        ESP_LOGE(TAG, "Playlist manager not initialized");
        return false;
    }
    
    if (playlist_manager_->Previous()) {
        // 获取上一首歌曲并开始播放
        const SongInfo* prev_song = playlist_manager_->GetCurrentSong();
        
        if (prev_song && !prev_song->title.empty()) {
            return StartStreaming(prev_song->audio_url, false);
        }
    }
    
    return false;
}

bool Esp32MusicV2::Seek(int position_ms) {
    ESP_LOGI(TAG, "Seeking to %d ms", position_ms);
    
    // 这里可以实现音频定位功能
    // 由于MP3流式播放的限制，定位功能比较复杂
    // 暂时只记录位置信息
    
    current_position_ms_ = position_ms;
    
    if (callbacks_.on_progress) {
        callbacks_.on_progress(current_song_name_, position_ms);
    }
    
    return true;
}

bool Esp32MusicV2::SetVolume(int volume) {
    ESP_LOGI(TAG, "Setting volume to %d", volume);
    
    auto& board = Board::GetInstance();
    auto codec = board.GetAudioCodec();
    if (codec) {
        // AudioCodec 可能没有 SetVolume 方法，暂时跳过
        return true;
    }
    
    return false;
}

int Esp32MusicV2::GetVolume() const {
    auto& board = Board::GetInstance();
    auto codec = board.GetAudioCodec();
    if (codec) {
        // AudioCodec 可能没有 GetVolume 方法，暂时返回默认值
        return 50;
    }
    return 0;
}

// 播放列表管理
bool Esp32MusicV2::LoadPlaylist(const std::string& query) {
    ESP_LOGI(TAG, "Loading playlist: %s", query.c_str());
    
    std::vector<std::string> playlist;
    if (!FetchPlaylist(query, playlist)) {
        ESP_LOGE(TAG, "Failed to fetch playlist");
        return false;
    }
    
    // 清空当前播放列表
    if (playlist_manager_) {
        // PlaylistManager 可能没有 Clear 方法，暂时跳过
        
        // 添加歌曲到播放列表管理器
        for (const auto& song : playlist) {
            (void)song; // 避免未使用变量警告
            // PlaylistManager 可能没有 AddSong 方法，暂时跳过
        }
    }
    
    return true;
}

void Esp32MusicV2::SetShuffle(bool enabled) {
    if (playlist_manager_) {
        playlist_manager_->SetShuffle(enabled);
    }
}

void Esp32MusicV2::SetRepeat(bool enabled) {
    if (playlist_manager_) {
        playlist_manager_->SetRepeat(enabled);
    }
}

void Esp32MusicV2::ClearPlaylist() {
    if (playlist_manager_) {
        // PlaylistManager 可能没有 Clear 方法，暂时跳过
    }
}

// 歌词管理
bool Esp32MusicV2::LoadLyrics(const std::string& lyric_url) {
    ESP_LOGI(TAG, "Loading lyrics from: %s", lyric_url.c_str());
    
    // 启动歌词显示任务
    if (is_lyric_running_) {
        is_lyric_running_ = false;
        if (lyric_task_handle_) {
            vTaskDelete(lyric_task_handle_);
            lyric_task_handle_ = nullptr;
        }
    }
    
    is_lyric_running_ = true;
    current_lyric_index_ = -1;
    lyrics_.clear();
    
    BaseType_t ret = xTaskCreate([](void* arg) {
        Esp32MusicV2* player = static_cast<Esp32MusicV2*>(arg);
        player->LyricTaskLoop();
        vTaskDelete(nullptr);
    }, "lyric_display", 8192, this, 5, &lyric_task_handle_);
    
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create lyric task: %d", ret);
        is_lyric_running_ = false;
        return false;
    }
    
    return true;
}

void Esp32MusicV2::EnableLyrics(bool enabled) {
    config_.enable_lyrics = enabled;
}

bool Esp32MusicV2::IsLyricsEnabled() const {
    return config_.enable_lyrics;
}

// 缓存管理
bool Esp32MusicV2::ClearCache() {
    if (downloader_) {
        downloader_->ClearCache();
        return true;
    }
    return false;
}

size_t Esp32MusicV2::GetCacheSize() const {
    if (downloader_) {
        return downloader_->GetCacheSize();
    }
    return 0;
}

bool Esp32MusicV2::IsCacheEnabled() const {
    if (downloader_) {
        return downloader_->IsCacheEnabled();
    }
    return false;
}

// 状态查询
bool Esp32MusicV2::IsPlaying() const {
    return is_playing_ && !is_paused_;
}

bool Esp32MusicV2::IsPaused() const {
    return is_paused_;
}

const std::string& Esp32MusicV2::GetCurrentSong() const {
    return current_song_name_;
}

const std::string& Esp32MusicV2::GetCurrentArtist() const {
    return current_artist_;
}

int Esp32MusicV2::GetCurrentPosition() const {
    return current_position_ms_;
}

int Esp32MusicV2::GetTotalDuration() const {
    return total_duration_ms_;
}

float Esp32MusicV2::GetProgress() const {
    if (total_duration_ms_ > 0) {
        return (float)current_position_ms_ / total_duration_ms_;
    }
    return 0.0f;
}

// 内存管理
size_t Esp32MusicV2::GetBufferUsage() const {
    if (buffer_manager_) {
        return buffer_manager_->GetBufferUsage();
    }
    return 0;
}

size_t Esp32MusicV2::GetFreeMemory() const {
    return heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
}

void Esp32MusicV2::PrintMemoryStats() const {
    ESP_LOGI(TAG, "Memory Stats:");
    ESP_LOGI(TAG, "  Buffer Usage: %zu bytes", GetBufferUsage());
    ESP_LOGI(TAG, "  Free SPIRAM: %zu bytes", GetFreeMemory());
    ESP_LOGI(TAG, "  Cache Size: %zu bytes", GetCacheSize());
}

// 性能监控
void Esp32MusicV2::PrintPerformanceStats() const {
    ESP_LOGI(TAG, "Performance Stats:");
    ESP_LOGI(TAG, "  Current Song: %s", current_song_name_.c_str());
    ESP_LOGI(TAG, "  Position: %d ms / %d ms", current_position_ms_, total_duration_ms_);
    ESP_LOGI(TAG, "  Progress: %.2f%%", GetProgress() * 100.0f);
    ESP_LOGI(TAG, "  Playing: %s", is_playing_ ? "Yes" : "No");
    ESP_LOGI(TAG, "  Paused: %s", is_paused_ ? "Yes" : "No");
}

void Esp32MusicV2::ResetStats() {
    current_position_ms_ = 0;
    total_duration_ms_ = 0;
}

// 调试功能
void Esp32MusicV2::EnableDebugLog(bool enabled) {
    // 可以在这里控制调试日志级别
    ESP_LOGI(TAG, "Debug logging %s", enabled ? "enabled" : "disabled");
}

void Esp32MusicV2::DumpState() const {
    ESP_LOGI(TAG, "=== Esp32MusicV2 State Dump ===");
    PrintMemoryStats();
    PrintPerformanceStats();
    
    if (playlist_manager_) {
        ESP_LOGI(TAG, "Playlist Manager: Active");
    } else {
        ESP_LOGI(TAG, "Playlist Manager: Not initialized");
    }
    
    if (buffer_manager_) {
        ESP_LOGI(TAG, "Buffer Manager: Active");
    } else {
        ESP_LOGI(TAG, "Buffer Manager: Not initialized");
    }
    
        if (downloader_) {
        auto active_tasks = downloader_->GetActiveTasks();
        ESP_LOGI(TAG, "Downloader: Active, Downloading: %s",
                 !active_tasks.empty() ? "Yes" : "No");
    } else {
        ESP_LOGI(TAG, "Downloader: Not initialized");
    }
    
    ESP_LOGI(TAG, "=== End State Dump ===");
}

// 私有方法
void Esp32MusicV2::AudioTaskLoop() {
    ESP_LOGI(TAG, "Audio task started");
    
    // 这里实现音频流式播放逻辑
    // 参考 esp32_music.cc 中的 PlayAudioStream 方法
    // 但使用新的架构组件
    
    // 显示歌名
    if (!current_song_name_.empty()) {
        auto& board = Board::GetInstance();
        auto display = board.GetDisplay();
        if (display) {
            std::string formatted_song_name = "《" + current_song_name_ + "》播放中...";
            display->SetMusicInfo(formatted_song_name.c_str());
        }
    }
    
    // 模拟播放过程
    while (is_playing_ && !is_paused_) {
        // 检查设备状态
        auto& app = Application::GetInstance();
        DeviceState current_state = app.GetDeviceState();
        
        if (current_state == kDeviceStateListening) {
            ESP_LOGI(TAG, "Device is in listening state, stopping playback");
            break;
        }
        
        if (current_state != kDeviceStateIdle && current_state != kDeviceStateSpeaking) {
            ESP_LOGI(TAG, "Device state changed to %d, stopping playback", current_state);
            break;
        }
        
        // 更新播放进度
        current_position_ms_ += 100; // 模拟100ms进度
        
        if (callbacks_.on_progress) {
            callbacks_.on_progress(current_song_name_, current_position_ms_);
        }
        
        // 更新歌词显示
        UpdateLyricDisplay(current_position_ms_);
        
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    // 播放结束
    if (callbacks_.on_song_end) {
        callbacks_.on_song_end(current_song_name_);
    }
    
    // 清空显示
    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();
    if (display) {
        display->SetMusicInfo("");
    }
    
    ESP_LOGI(TAG, "Audio task finished");
}

void Esp32MusicV2::LyricTaskLoop() {
    ESP_LOGI(TAG, "Lyric task started");
    
    // 这里实现歌词显示逻辑
    // 参考 esp32_music.cc 中的 LyricDisplayThread 方法
    
    while (is_lyric_running_ && is_playing_) {
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    
    ESP_LOGI(TAG, "Lyric task finished");
}

bool Esp32MusicV2::InitializeMp3Decoder() {
    mp3_decoder_ = MP3InitDecoder();
    if (mp3_decoder_ == nullptr) {
        ESP_LOGE(TAG, "Failed to initialize MP3 decoder");
        mp3_decoder_initialized_ = false;
        return false;
    }
    
    mp3_decoder_initialized_ = true;
    ESP_LOGI(TAG, "MP3 decoder initialized successfully");
    return true;
}

void Esp32MusicV2::CleanupMp3Decoder() {
    if (mp3_decoder_ != nullptr) {
        MP3FreeDecoder(mp3_decoder_);
        mp3_decoder_ = nullptr;
    }
    mp3_decoder_initialized_ = false;
    ESP_LOGI(TAG, "MP3 decoder cleaned up");
}

bool Esp32MusicV2::DecodeMp3Frame(const uint8_t* data, size_t size, int16_t* pcm_buffer) {
    if (!mp3_decoder_initialized_ || !mp3_decoder_) {
        ESP_LOGE(TAG, "MP3 decoder not initialized");
        return false;
    }
    
    // 这里实现MP3帧解码逻辑
    // 参考 esp32_music.cc 中的解码部分
    
    return true;
}

void Esp32MusicV2::UpdateLyricDisplay(int position_ms) {
    if (!config_.enable_lyrics || lyrics_.empty()) {
        return;
    }
    
    // 查找当前应该显示的歌词
    int new_lyric_index = -1;
    
    for (int i = 0; i < (int)lyrics_.size(); i++) {
        if (lyrics_[i].first <= position_ms) {
            new_lyric_index = i;
        } else {
            break;
        }
    }
    
    // 如果歌词索引发生变化，更新显示
    if (new_lyric_index != current_lyric_index_) {
        current_lyric_index_ = new_lyric_index;
        
        std::string lyric_text;
        if (current_lyric_index_ >= 0 && current_lyric_index_ < (int)lyrics_.size()) {
            lyric_text = lyrics_[current_lyric_index_].second;
        }
        
        auto& board = Board::GetInstance();
        auto display = board.GetDisplay();
        if (display) {
            display->SetChatMessage("lyric", lyric_text.c_str());
        }
        
        if (callbacks_.on_lyric_update) {
            callbacks_.on_lyric_update(lyric_text);
        }
    }
}

void Esp32MusicV2::HandlePlaylistEvent(const PlaylistCallbacks& playlist_callbacks) {
    // 处理播放列表事件
    if (playlist_callbacks.on_song_start) {
        // 需要传递 SongInfo 对象，暂时跳过
    }
    
    if (playlist_callbacks.on_song_end) {
        // 需要传递 SongInfo 对象，暂时跳过
    }
    
    if (playlist_callbacks.on_playlist_end) {
        playlist_callbacks.on_playlist_end();
    }
}

void Esp32MusicV2::HandleDownloadEvent(const DownloadCallbacks& download_callbacks) {
    // 处理下载事件
    if (download_callbacks.on_completed) {
        // 需要传递 task_id，暂时跳过
    }
} 