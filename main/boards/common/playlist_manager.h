#ifndef PLAYLIST_MANAGER_H
#define PLAYLIST_MANAGER_H

#include "playlist_state_machine.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <esp_log.h>
#include <atomic>
#include <vector>
#include <string>
#include <memory>
#include <functional>

// 前向声明
class AudioBufferManager;
class PlaylistStateMachine;
class MusicDownloader;

// 播放状态枚举已在 playlist_state_machine.h 中定义

// 播放控制命令
enum class PlaylistCommand {
    PLAY,           // 播放
    PAUSE,          // 暂停
    STOP,           // 停止
    NEXT,           // 下一首
    PREV,           // 上一首
    SEEK,           // 跳转
    EMERGENCY_STOP  // 紧急停止
};

// 歌曲信息结构
struct SongInfo {
    std::string id;
    std::string title;
    std::string artist;
    std::string album;
    std::string audio_url;
    std::string lyric_url;
    int duration_ms;
    std::string cover_url;
    
    SongInfo() : duration_ms(0) {}
};

// 播放列表信息
struct PlaylistInfo {
    std::string id;
    std::string name;
    std::string description;
    std::vector<SongInfo> songs;
    int current_index;
    bool shuffle;
    bool repeat;
    
    PlaylistInfo() : current_index(0), shuffle(false), repeat(false) {}
};

// 播放事件回调
struct PlaylistCallbacks {
    std::function<void(const SongInfo&)> on_song_start;
    std::function<void(const SongInfo&)> on_song_end;
    std::function<void(const SongInfo&, int)> on_progress;
    std::function<void(const std::string&)> on_error;
    std::function<void()> on_playlist_end;
    std::function<void(PlaylistState)> on_state_change;
};

// 配置结构体
struct PlaylistConfig {
    size_t buffer_size;
    size_t preload_count;
    int task_stack_size;
    int task_priority;
    uint32_t command_timeout_ms;
    
    PlaylistConfig() : buffer_size(256 * 1024), preload_count(2), 
                       task_stack_size(4096), task_priority(5), 
                       command_timeout_ms(5000) {}
    
    // 内存优化配置
    static PlaylistConfig MemoryOptimized() {
        PlaylistConfig config;
        config.buffer_size = 128 * 1024;      // 减少到128KB
        config.preload_count = 1;             // 只预加载1首
        config.task_stack_size = 3072;        // 减少栈大小
        return config;
    }
    
    // 最小内存配置
    static PlaylistConfig MinimalMemory() {
        PlaylistConfig config;
        config.buffer_size = 64 * 1024;       // 减少到64KB
        config.preload_count = 0;             // 不预加载
        config.task_stack_size = 2048;        // 最小栈大小
        return config;
    }
};

class PlaylistManager {
private:
    // 核心组件
    std::unique_ptr<AudioBufferManager> buffer_manager_;
    std::unique_ptr<PlaylistStateMachine> state_machine_;
    std::unique_ptr<MusicDownloader> downloader_;
    
    // 播放列表数据
    PlaylistInfo current_playlist_;
    
    // 任务和队列
    TaskHandle_t playlist_task_handle_;
    QueueHandle_t command_queue_;
    SemaphoreHandle_t state_mutex_;
    
    // 状态管理
    std::atomic<PlaylistState> current_state_;
    std::atomic<bool> is_running_;
    std::atomic<bool> emergency_stop_requested_;
    
    // 回调函数
    PlaylistCallbacks callbacks_;
    
    // 配置参数
    PlaylistConfig config_;
    
    // 私有方法
    void PlaylistTaskLoop();
    void ProcessCommand(PlaylistCommand cmd, void* data = nullptr);
    void HandleStateChange(PlaylistState new_state);
    void PreloadNextSongs();
    void CleanupResources();
    
    // 命令处理方法
    void HandlePlayCommand();
    void HandlePauseCommand();
    void HandleStopCommand();
    void HandleNextCommand();
    void HandlePrevCommand();
    void HandleSeekCommand(int position);
    void HandleEmergencyStopCommand();
    
    // 状态处理方法
    void HandleLoadingState();
    void HandlePlayingState();
    void HandleStoppingState();
    
public:
    PlaylistManager();
    ~PlaylistManager();
    
    // 初始化和配置
    bool Initialize(const PlaylistConfig& config = PlaylistConfig());
    void Deinitialize();
    void SetCallbacks(const PlaylistCallbacks& callbacks);
    
    // 播放控制
    bool PlayPlaylist(const std::string& query);
    bool PlaySong(const std::string& song_id);
    bool Pause();
    bool Resume();
    bool Stop();
    bool Next();
    bool Previous();
    bool Seek(int position_ms);
    void EmergencyStop();
    
    // 状态查询
    PlaylistState GetState() const;
    const PlaylistInfo& GetCurrentPlaylist() const;
    const SongInfo* GetCurrentSong() const;
    int GetCurrentPosition() const;
    int GetDuration() const;
    bool IsPlaying() const;
    
    // 播放列表管理
    bool LoadPlaylist(const std::string& query);
    void SetShuffle(bool enabled);
    void SetRepeat(bool enabled);
    void ClearPlaylist();
    
    // 错误处理
    void HandleError(const std::string& error);
    void Reset();
    
    // 内存管理
    size_t GetBufferUsage() const;
    size_t GetFreeMemory() const;
    void PrintMemoryStats() const;
    
    // 内存优化方法
    bool EnableMemoryOptimization();
    bool EnableMinimalMemoryMode();
    void SetDynamicMemoryAdjustment(bool enabled);
    bool IsMemoryLow() const;
    void ForceGarbageCollection();
    
    // 内存监控
    struct MemoryStats {
        size_t total_used;
        size_t buffer_used;
        size_t pool_used;
        size_t free_memory;
        float usage_percentage;
    };
    MemoryStats GetMemoryStats() const;
};

#endif // PLAYLIST_MANAGER_H 