#ifndef PLAYLIST_PLAYER_H
#define PLAYLIST_PLAYER_H

#include <memory>
#include <string>
#include <vector>
#include <functional>
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <esp_log.h>

// 前向声明
class PlaylistManager;
class Esp32Music;
class AudioBufferManager;

// 播放命令
enum class PlayerCommand {
    PLAY_PLAYLIST,
    PLAY_SONG,
    PAUSE,
    RESUME,
    STOP,
    NEXT,
    PREV,
    SEEK,
    EMERGENCY_STOP
};

// 命令结构体
struct PlayerCommandData {
    PlayerCommand cmd;
    std::string data;
    int value;
    
    PlayerCommandData(PlayerCommand c, const std::string& d = "", int v = 0) 
        : cmd(c), data(d), value(v) {}
};

// 播放列表播放器 - 集成播放列表框架和音频播放系统
class PlaylistPlayer {
public:
    // 播放状态
    enum class PlayerState {
        IDLE,           // 空闲
        LOADING,        // 加载中
        PLAYING,        // 播放中
        PAUSED,         // 暂停
        STOPPING,       // 停止中
        ERROR           // 错误
    };
    
    // 播放事件回调
    struct PlayerCallbacks {
        std::function<void(const std::string& song_name)> on_song_start;
        std::function<void(const std::string& song_name)> on_song_end;
        std::function<void(const std::string& song_name, int progress)> on_progress;
        std::function<void(const std::string& error)> on_error;
        std::function<void()> on_playlist_end;
        std::function<void(PlayerState state)> on_state_change;
    };
    
    // 播放配置
    struct PlayerConfig {
        size_t buffer_size;
        size_t preload_count;
        int task_stack_size;
        int task_priority;
        bool enable_lyrics;
        bool enable_preloading;
        
        PlayerConfig() : buffer_size(256 * 1024), preload_count(2), 
                        task_stack_size(4096), task_priority(5),
                        enable_lyrics(true), enable_preloading(true) {}
    };

private:
    // 核心组件
    std::unique_ptr<PlaylistManager> playlist_manager_;
    std::unique_ptr<Esp32Music> music_player_;
    std::unique_ptr<AudioBufferManager> buffer_manager_;
    
    // 播放状态
    PlayerState current_state_;
    std::string current_playlist_query_;
    std::string current_song_name_;
    int current_song_index_;
    int total_songs_;
    
    // 任务和队列
    TaskHandle_t player_task_handle_;
    QueueHandle_t command_queue_;
    SemaphoreHandle_t state_mutex_;
    
    // 回调函数
    PlayerCallbacks callbacks_;
    PlayerConfig config_;
    
    // 播放控制
    std::atomic<bool> is_running_;
    std::atomic<bool> emergency_stop_requested_;
    std::atomic<bool> auto_play_next_;
    
    // 私有方法
    void PlayerTaskLoop();
    void ProcessCommand(const PlayerCommandData& cmd);
    void ProcessPlaylistState();
    void HandleSongStart(const std::string& song_name);
    void HandleSongEnd(const std::string& song_name);
    void HandlePlaylistEnd();
    void HandleError(const std::string& error);
    void SetState(PlayerState new_state);
    void CleanupResources();
    
    // 音频播放集成
    bool StartAudioPlayback(const std::string& song_url, const std::string& song_name);
    bool StopAudioPlayback();
    bool PauseAudioPlayback();
    bool ResumeAudioPlayback();
    void HandleAudioPlaybackEnd();

public:
    PlaylistPlayer();
    ~PlaylistPlayer();
    
    // 初始化和配置
    bool Initialize(const PlayerConfig& config = PlayerConfig());
    void Deinitialize();
    void SetCallbacks(const PlayerCallbacks& callbacks);
    
    // 播放控制
    bool PlayPlaylist(const std::string& query);
    bool PlaySong(const std::string& song_name);
    bool Pause();
    bool Resume();
    bool Stop();
    bool Next();
    bool Previous();
    bool Seek(int position_ms);
    void EmergencyStop();
    
    // 播放列表管理
    bool LoadPlaylist(const std::string& query);
    void SetShuffle(bool enabled);
    void SetRepeat(bool enabled);
    void SetAutoPlayNext(bool enabled);
    void ClearPlaylist();
    
    // 状态查询
    PlayerState GetState() const;
    const std::string& GetCurrentSongName() const;
    int GetCurrentSongIndex() const;
    int GetTotalSongs() const;
    int GetCurrentPosition() const;
    int GetDuration() const;
    bool IsPlaying() const;
    
    // 内存管理
    size_t GetBufferUsage() const;
    size_t GetFreeMemory() const;
    void PrintMemoryStats() const;
    
    // 错误处理
    void Reset();
};

#endif // PLAYLIST_PLAYER_H 