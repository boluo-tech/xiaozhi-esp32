#ifndef MUSIC_H
#define MUSIC_H

#include <string>
#include <vector>
#include <optional>
#include <chrono>

// 播放模式枚举
enum class PlayMode {
    SINGLE,     // 单曲播放
    AUTO_NEXT,  // 自动播放下一首
    PLAYLIST    // 列表播放
};

// 播放列表项结构
struct PlaylistItem {
    std::string song_name;
    std::string artist;
    std::string title;
    std::string audio_url;
    std::string lyric_url;
    
    PlaylistItem() = default;
    PlaylistItem(const std::string& name) : song_name(name) {}
};

// 播放列表信息结构
struct PlaylistInfo {
    std::string query;           // 查询条件
    std::string playlist_id;     // 播放列表ID
    size_t total_songs;          // 总歌曲数
    bool is_looping;             // 是否循环播放
    std::chrono::steady_clock::time_point created_at;
    
    PlaylistInfo() : total_songs(0), is_looping(false) {}
};

class Music {
public:
    virtual ~Music() = default;  // 添加虚析构函数
    
    virtual bool Download(const std::string& song_name) = 0;
    virtual bool Play() = 0;
    virtual bool Stop() = 0;
    virtual std::string GetDownloadResult() = 0;
    
    // 新增流式播放相关方法
    virtual bool StartStreaming(const std::string& music_url) = 0;
    virtual bool StopStreaming() = 0;  // 停止流式播放
    virtual size_t GetBufferSize() const = 0;
    virtual bool IsDownloading() const = 0;
    
    // 新增自动播放和列表播放方法
    virtual bool SetPlayMode(PlayMode mode) = 0;
    virtual PlayMode GetPlayMode() const = 0;
    virtual bool EnableAutoNext(bool enable) = 0;
    virtual bool IsAutoNextEnabled() const = 0;
    
    // 播放列表相关方法（按需加载设计）
    virtual bool CreatePlaylist(const std::string& query) = 0;  // 创建播放列表，只获取基本信息
    virtual bool LoadPlaylistSong(size_t index) = 0;            // 加载指定索引的歌曲信息
    virtual bool PlayNext() = 0;                                // 播放下一首
    virtual bool PlayPrevious() = 0;                            // 播放上一首
    virtual bool IsPlaylistMode() const = 0;                    // 是否处于播放列表模式
    
    // 播放列表状态查询
    virtual bool IsPlaylistActive() const = 0;                  // 播放列表是否激活
    virtual size_t GetCurrentPlaylistIndex() const = 0;         // 当前播放索引
    virtual size_t GetPlaylistTotalSongs() const = 0;           // 播放列表总歌曲数
    virtual std::string GetCurrentSongName() const = 0;         // 当前歌曲名称
    virtual std::optional<PlaylistInfo> GetPlaylistInfo() const = 0; // 获取播放列表信息
    
    // 播放列表控制
    virtual bool SetPlaylistLooping(bool looping) = 0;          // 设置循环播放
    virtual bool IsPlaylistLooping() const = 0;                 // 是否循环播放
    virtual void ClearPlaylist() = 0;                           // 清空播放列表
};

#endif // MUSIC_H 