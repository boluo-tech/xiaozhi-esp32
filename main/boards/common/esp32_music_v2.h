#ifndef ESP32_MUSIC_V2_H
#define ESP32_MUSIC_V2_H

#include <memory>
#include <atomic>
#include <string>
#include <vector>
#include <functional>

#include "music.h"
#include "playlist_manager.h"
#include "audio_buffer_manager.h"
#include "playlist_state_machine.h"
#include "music_downloader.h"

// MP3解码器支持
extern "C" {
#include "mp3dec.h"
}

// 音频播放器配置
struct AudioPlayerConfig {
    size_t buffer_size;          // 音频缓冲区大小
    size_t preload_count;        // 预加载歌曲数量
    int task_stack_size;         // 任务栈大小
    int task_priority;           // 任务优先级
    uint32_t command_timeout_ms; // 命令超时时间
    bool enable_lyrics;          // 是否启用歌词显示
    bool enable_cache;           // 是否启用缓存
    bool enable_preload;         // 是否启用预加载
    
    AudioPlayerConfig() : buffer_size(256 * 1024), preload_count(2),
                         task_stack_size(4096), task_priority(5),
                         command_timeout_ms(5000), enable_lyrics(true),
                         enable_cache(true), enable_preload(true) {}
};

// 播放事件回调
struct AudioPlayerCallbacks {
    std::function<void(const std::string&)> on_song_start;
    std::function<void(const std::string&)> on_song_end;
    std::function<void(const std::string&, int)> on_progress;
    std::function<void(const std::string&)> on_error;
    std::function<void()> on_playlist_end;
    std::function<void(const std::string&)> on_lyric_update;
    std::function<void(bool)> on_playback_state_change;
};

class Esp32MusicV2 : public Music {
private:
    // 核心组件
    std::unique_ptr<PlaylistManager> playlist_manager_;
    std::unique_ptr<AudioBufferManager> buffer_manager_;
    std::unique_ptr<PlaylistStateMachine> state_machine_;
    std::unique_ptr<MusicDownloader> downloader_;
    
    // MP3解码器
    HMP3Decoder mp3_decoder_;
    MP3FrameInfo mp3_frame_info_;
    bool mp3_decoder_initialized_;
    
    // 音频播放任务
    TaskHandle_t audio_task_handle_;
    std::atomic<bool> is_playing_;
    std::atomic<bool> is_paused_;
    
    // 歌词显示
    TaskHandle_t lyric_task_handle_;
    std::atomic<bool> is_lyric_running_;
    std::vector<std::pair<int, std::string>> lyrics_;
    std::atomic<int> current_lyric_index_;
    
    // 配置和回调
    AudioPlayerConfig config_;
    AudioPlayerCallbacks callbacks_;
    
    // 状态管理
    std::string current_song_name_;
    std::string current_artist_;
    int current_position_ms_;
    int total_duration_ms_;
    
    // 私有方法
    void AudioTaskLoop();
    void LyricTaskLoop();
    bool InitializeMp3Decoder();
    void CleanupMp3Decoder();
    bool DecodeMp3Frame(const uint8_t* data, size_t size, int16_t* pcm_buffer);
    void UpdateLyricDisplay(int position_ms);
    void HandlePlaylistEvent(const PlaylistCallbacks& playlist_callbacks);
    void HandleDownloadEvent(const DownloadCallbacks& download_callbacks);
    
public:
    Esp32MusicV2();
    ~Esp32MusicV2();
    
    // 初始化和配置
    bool Initialize(const AudioPlayerConfig& config = AudioPlayerConfig());
    void Deinitialize();
    void SetCallbacks(const AudioPlayerCallbacks& callbacks);
    
    // Music接口实现
    virtual bool Download(const std::string& song_name, bool auto_start = true) override;
    virtual bool Play() override;
    virtual bool Stop(bool stop_playlist = true) override;
    virtual std::string GetDownloadResult() override;
    virtual bool StartStreaming(const std::string& music_url, bool stop_playlist = true) override;
    virtual bool StopStreaming() override;
    virtual size_t GetBufferSize() const override;
    virtual bool IsDownloading() const override;
    virtual bool PlayPlaylist(const std::string& query) override;
    virtual bool FetchPlaylist(const std::string& query, std::vector<std::string>& out_playlist) override;
    virtual void EmergencyStop() override;
    
    // 新增功能
    bool Pause();
    bool Resume();
    bool Next();
    bool Previous();
    bool Seek(int position_ms);
    bool SetVolume(int volume);
    int GetVolume() const;
    
    // 播放列表管理
    bool LoadPlaylist(const std::string& query);
    void SetShuffle(bool enabled);
    void SetRepeat(bool enabled);
    void ClearPlaylist();
    
    // 歌词管理
    bool LoadLyrics(const std::string& lyric_url);
    void EnableLyrics(bool enabled);
    bool IsLyricsEnabled() const;
    
    // 缓存管理
    bool ClearCache();
    size_t GetCacheSize() const;
    bool IsCacheEnabled() const;
    
    // 状态查询
    bool IsPlaying() const;
    bool IsPaused() const;
    const std::string& GetCurrentSong() const;
    const std::string& GetCurrentArtist() const;
    int GetCurrentPosition() const;
    int GetTotalDuration() const;
    float GetProgress() const;
    
    // 内存管理
    size_t GetBufferUsage() const;
    size_t GetFreeMemory() const;
    void PrintMemoryStats() const;
    
    // 性能监控
    void PrintPerformanceStats() const;
    void ResetStats();
    
    // 调试功能
    void EnableDebugLog(bool enabled);
    void DumpState() const;
};

#endif // ESP32_MUSIC_V2_H 