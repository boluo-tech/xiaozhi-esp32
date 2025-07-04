#ifndef ESP32_MUSIC_H
#define ESP32_MUSIC_H

#include "music.h"
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <vector>
#include <memory>
#include <chrono>

// ESP-IDF头文件
#include <esp_heap_caps.h>

// MP3解码器支持
extern "C" {
#include "mp3dec.h"
}

// 音频数据块结构
struct AudioChunk {
    uint8_t* data;
    size_t size;
    
    AudioChunk(uint8_t* d, size_t s) : data(d), size(s) {}
    AudioChunk() : data(nullptr), size(0) {}
    
    // 移动构造函数
    AudioChunk(AudioChunk&& other) noexcept : data(other.data), size(other.size) {
        other.data = nullptr;
        other.size = 0;
    }
    
    // 移动赋值运算符
    AudioChunk& operator=(AudioChunk&& other) noexcept {
        if (this != &other) {
            if (data) {
                heap_caps_free(data);
            }
            data = other.data;
            size = other.size;
            other.data = nullptr;
            other.size = 0;
        }
        return *this;
    }
    
    // 禁用拷贝构造和赋值
    AudioChunk(const AudioChunk&) = delete;
    AudioChunk& operator=(const AudioChunk&) = delete;
    
    ~AudioChunk() {
        if (data) {
            heap_caps_free(data);
        }
    }
};

// 播放列表管理器
class PlaylistManager {
public:
    PlaylistManager();
    ~PlaylistManager();
    
    // 播放列表基本操作
    bool CreatePlaylist(const std::string& query);
    bool LoadPlaylistSong(size_t index);
    void ClearPlaylist();
    
    // 播放列表状态
    bool IsActive() const;
    size_t GetCurrentIndex() const;
    size_t GetTotalSongs() const;
    std::string GetCurrentSongName() const;
    std::optional<PlaylistInfo> GetPlaylistInfo() const;
    
    // 播放控制
    bool PlayNext();
    bool PlayPrevious();
    bool SetLooping(bool looping);
    bool IsLooping() const;
    
    // 歌曲信息获取
    std::optional<PlaylistItem> GetSongInfo(size_t index) const;
    bool HasSongInfo(size_t index) const;
    
private:
    mutable std::mutex mutex_;
    PlaylistInfo playlist_info_;
    std::vector<std::optional<PlaylistItem>> loaded_songs_;  // 按需加载的歌曲信息
    size_t current_index_;
    bool is_looping_;
    
    // 从服务器加载歌曲信息
    bool LoadSongFromServer(size_t index);
    bool ParseSongResponse(const std::string& response, PlaylistItem& item);
};

class Esp32Music : public Music {
public:
    Esp32Music();
    ~Esp32Music() override;
    
    // 基本播放功能
    bool Download(const std::string& song_name) override;
    bool Play() override;
    bool Stop() override;
    std::string GetDownloadResult() override;
    
    // 流式播放功能
    bool StartStreaming(const std::string& music_url) override;
    bool StopStreaming() override;
    size_t GetBufferSize() const override;
    bool IsDownloading() const override;
    
    // 播放模式控制
    bool SetPlayMode(PlayMode mode) override;
    PlayMode GetPlayMode() const override;
    bool EnableAutoNext(bool enable) override;
    bool IsAutoNextEnabled() const override;
    
    // 播放列表功能（按需加载）
    bool CreatePlaylist(const std::string& query) override;
    bool LoadPlaylistSong(size_t index) override;
    bool PlayNext() override;
    bool PlayPrevious() override;
    bool IsPlaylistMode() const override;
    
    // 播放列表状态查询
    bool IsPlaylistActive() const override;
    size_t GetCurrentPlaylistIndex() const override;
    size_t GetPlaylistTotalSongs() const override;
    std::string GetCurrentSongName() const override;
    std::optional<PlaylistInfo> GetPlaylistInfo() const override;
    
    // 播放列表控制
    bool SetPlaylistLooping(bool looping) override;
    bool IsPlaylistLooping() const override;
    void ClearPlaylist() override;
    
private:
    // 基本播放相关
    std::string last_downloaded_data_;
    std::string current_music_url_;
    std::string current_song_name_;
    bool song_name_displayed_;
    
    // 歌词相关
    std::string current_lyric_url_;
    std::vector<std::pair<int, std::string>> lyrics_;
    std::atomic<int> current_lyric_index_;
    std::thread lyric_thread_;
    std::atomic<bool> is_lyric_running_;
    mutable std::mutex lyrics_mutex_;
    
    // 播放状态
    std::atomic<bool> is_playing_;
    std::atomic<bool> is_downloading_;
    
    // 线程相关
    std::thread play_thread_;
    std::thread download_thread_;
    
    // 音频缓冲区
    std::queue<AudioChunk> audio_buffer_;
    mutable std::mutex buffer_mutex_;
    std::condition_variable buffer_cv_;
    size_t buffer_size_;
    
    // MP3解码器
    HMP3Decoder mp3_decoder_;
    MP3FrameInfo mp3_frame_info_;
    bool mp3_decoder_initialized_;
    
    // 播放模式
    PlayMode play_mode_;
    std::atomic<bool> auto_next_enabled_;
    
    // 播放列表管理器
    std::unique_ptr<PlaylistManager> playlist_manager_;
    
    // 播放时间跟踪
    int64_t current_play_time_ms_;
    int64_t last_frame_time_ms_;
    size_t total_frames_decoded_;
    
    // 私有方法
    void DownloadAudioStream(const std::string& music_url);
    void PlayAudioStream();
    void ClearAudioBuffer();
    bool InitializeMp3Decoder();
    void CleanupMp3Decoder();
    void ResetSampleRate();
    size_t SkipId3Tag(uint8_t* data, size_t size);
    
    // 歌词相关方法
    bool DownloadLyrics(const std::string& lyric_url);
    bool ParseLyrics(const std::string& lyric_content);
    void LyricDisplayThread();
    void UpdateLyricDisplay(int64_t current_time_ms);
    
    // 播放结束回调
    void OnPlaybackFinished();
    
    // 服务器请求方法
    bool RequestNextSong();
    bool RequestPreviousSong();
};

#endif // ESP32_MUSIC_H
