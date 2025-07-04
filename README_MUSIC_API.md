# ESP32音乐播放器API文档

## 概述

本文档详细分析了ESP32音乐播放器（`esp32_music.cc` 和 `esp32_music.h`）调用的音乐服务器API接口。

## 配置说明

### 编译时配置

音乐服务器功能支持通过 `menuconfig` 进行配置：

```bash
# 进入配置菜单
idf.py menuconfig

# 导航到：Xiaozhi Assistant -> Music Server Configuration
```

#### 配置选项

| 配置项 | 默认值 | 说明 |
|--------|--------|------|
| `MUSIC_SERVER_ENABLED` | `y` | 启用音乐服务器功能 |
| `MUSIC_SERVER_URL` | `http://www.jsrc.top:5566` | 音乐服务器地址 |
| `MUSIC_SERVER_USER_AGENT` | `ESP32-Music-Player/1.0` | User-Agent字符串 |
| `MUSIC_SERVER_TIMEOUT_MS` | `10000` | 请求超时时间（毫秒） |
| `MUSIC_SERVER_RETRY_COUNT` | `3` | 重试次数 |
| `MUSIC_SERVER_RETRY_DELAY_MS` | `500` | 重试间隔（毫秒） |
| `MUSIC_SERVER_ENABLE_RANGE_REQUESTS` | `y` | 启用断点续传 |
| `MUSIC_SERVER_CHUNK_SIZE` | `4096` | 下载分块大小（字节） |
| `MUSIC_SERVER_MAX_BUFFER_SIZE` | `512` | 最大缓冲区大小（KB） |
| `MUSIC_SERVER_MIN_BUFFER_SIZE` | `64` | 最小缓冲区大小（KB） |

#### 配置示例

```bash
# 禁用音乐服务器
CONFIG_MUSIC_SERVER_ENABLED=n

# 使用自定义服务器
CONFIG_MUSIC_SERVER_URL="http://your-music-server.com:8080"

# 调整缓冲区大小
CONFIG_MUSIC_SERVER_MAX_BUFFER_SIZE=1024
CONFIG_MUSIC_SERVER_MIN_BUFFER_SIZE=128

# 调整下载参数
CONFIG_MUSIC_SERVER_CHUNK_SIZE=8192
CONFIG_MUSIC_SERVER_TIMEOUT_MS=15000
```

### 运行时配置

音乐服务器配置在运行时通过 `MusicServerConfig` 类进行管理：

```cpp
#include "music_server_config.h"

// 获取配置实例
auto& config = MusicServerConfig::GetInstance();

// 检查是否启用
if (config.IsEnabled()) {
    // 获取服务器URL
    std::string url = config.GetServerUrl();
    
    // 构建搜索URL
    std::string search_url = config.BuildSearchUrl("周杰伦 稻香");
    
    // 验证配置
    if (config.IsValid()) {
        // 配置有效，可以开始使用
    }
}
```

## 音乐服务器信息

- **服务器地址**: `http://www.jsrc.top:5566`（可配置）
- **主要API**: `/stream_pcm`（可配置）
- **支持格式**: MP3音频、LRC歌词
- **传输方式**: HTTP流式传输

---

## API接口详细说明

### 请求头详细说明

| 请求头 | 值 | 说明 | 使用场景 |
|--------|----|------|----------|
| `User-Agent` | `ESP32-Music-Player/1.0` | 客户端标识 | 所有请求 |
| `Accept` | `application/json` | 期望JSON响应 | 音乐搜索 |
| `Accept` | `*/*` | 接受任何类型 | 音频流下载 |
| `Accept` | `text/plain` | 期望纯文本 | 歌词下载 |
| `Range` | `bytes=0-` | 断点续传支持 | 音频流下载（可选） |
| `Device-Id` | `24:6F:28:12:34:56` | 设备MAC地址 | 所有请求 |
| `Board-Type` | `esp32-s3-touch-lcd-1.85` | 开发板型号 | 所有请求 |

### 1. 音乐搜索API

#### 接口信息
- **URL**: `http://www.jsrc.top:5566/stream_pcm`
- **方法**: `GET`
- **参数**: `song` - 歌曲名称（URL编码）

#### 请求示例
```http
GET http://www.jsrc.top:5566/stream_pcm?song=周杰伦%20稻香
User-Agent: ESP32-Music-Player/1.0
Accept: application/json
Device-Id: 24:6F:28:12:34:56
Board-Type: esp32-s3-touch-lcd-1.85
```

#### 响应格式
```json
{
  "artist": "周杰伦",
  "title": "稻香",
  "audio_url": "/audio/stream?file=xxx.mp3",
  "lyric_url": "/lyric/stream?file=xxx.lrc"
}
```

#### 响应字段说明
- `artist`: 艺术家名称
- `title`: 歌曲标题
- `audio_url`: 音频流下载路径
- `lyric_url`: 歌词文件下载路径

---

### 2. 音频流下载API

#### 接口信息
- **URL**: `http://www.jsrc.top:5566{audio_url}`
- **方法**: `GET`

#### 请求示例
```http
GET http://www.jsrc.top:5566/audio/stream?file=xxx.mp3
User-Agent: ESP32-Music-Player/1.0
Accept: */*
Range: bytes=0-
Device-Id: 24:6F:28:12:34:56
Board-Type: esp32-s3-touch-lcd-1.85
```

#### 响应格式
- **Content-Type**: `audio/mpeg`
- **Content-Length**: 音频文件大小（字节）
- **Accept-Ranges**: `bytes`（支持断点续传）

---

### 3. 歌词下载API

#### 接口信息
- **URL**: `http://www.jsrc.top:5566{lyric_url}`
- **方法**: `GET`

#### 请求示例
```http
GET http://www.jsrc.top:5566/lyric/stream?file=xxx.lrc
User-Agent: ESP32-Music-Player/1.0
Accept: text/plain
Device-Id: 24:6F:28:12:34:56
Board-Type: esp32-s3-touch-lcd-1.85
```

#### 响应格式
- **Content-Type**: `text/plain; charset=utf-8`
- **内容**: LRC格式歌词文件

---

## 新增API接口（自动播放和列表播放）

### 4. 下一首歌曲API

#### 接口信息
- **URL**: `http://www.jsrc.top:5566/next_song`
- **方法**: `GET`
- **功能**: 获取下一首推荐歌曲

#### 请求示例
```http
GET http://www.jsrc.top:5566/next_song
User-Agent: ESP32-Music-Player/1.0
Accept: application/json
Device-Id: 24:6F:28:12:34:56
Board-Type: esp32-s3-touch-lcd-1.85
```

#### 响应格式
```json
{
  "song_name": "周杰伦 青花瓷",
  "artist": "周杰伦",
  "title": "青花瓷",
  "audio_url": "/audio/stream?file=qinghuaci.mp3",
  "lyric_url": "/lyric/stream?file=qinghuaci.lrc"
}
```

#### 响应字段说明
- `song_name`: 歌曲搜索名称
- `artist`: 艺术家名称
- `title`: 歌曲标题
- `audio_url`: 音频流下载路径
- `lyric_url`: 歌词文件下载路径

---

### 5. 上一首歌曲API

#### 接口信息
- **URL**: `http://www.jsrc.top:5566/previous_song`
- **方法**: `GET`
- **功能**: 获取上一首推荐歌曲

#### 请求示例
```http
GET http://www.jsrc.top:5566/previous_song
User-Agent: ESP32-Music-Player/1.0
Accept: application/json
Device-Id: 24:6F:28:12:34:56
Board-Type: esp32-s3-touch-lcd-1.85
```

#### 响应格式
```json
{
  "song_name": "周杰伦 稻香",
  "artist": "周杰伦",
  "title": "稻香",
  "audio_url": "/audio/stream?file=daoxiang.mp3",
  "lyric_url": "/lyric/stream?file=daoxiang.lrc"
}
```

---

### 6. 播放列表API

#### 接口信息
- **URL**: `http://www.jsrc.top:5566/playlist`
- **方法**: `GET`
- **参数**: `query` - 播放列表查询条件（URL编码）
- **功能**: 根据查询条件获取播放列表

#### 请求示例
```http
GET http://www.jsrc.top:5566/playlist?query=周杰伦
User-Agent: ESP32-Music-Player/1.0
Accept: application/json
Device-Id: 24:6F:28:12:34:56
Board-Type: esp32-s3-touch-lcd-1.85
```

#### 响应格式
```json
{
  "query": "周杰伦",
  "total": 15,
  "songs": [
    {
      "song_name": "周杰伦 稻香",
      "artist": "周杰伦",
      "title": "稻香",
      "audio_url": "/audio/stream?file=daoxiang.mp3",
      "lyric_url": "/lyric/stream?file=daoxiang.lrc"
    },
    {
      "song_name": "周杰伦 青花瓷",
      "artist": "周杰伦",
      "title": "青花瓷",
      "audio_url": "/audio/stream?file=qinghuaci.mp3",
      "lyric_url": "/lyric/stream?file=qinghuaci.lrc"
    },
    {
      "song_name": "周杰伦 夜曲",
      "artist": "周杰伦",
      "title": "夜曲",
      "audio_url": "/audio/stream?file=yequ.mp3",
      "lyric_url": "/lyric/stream?file=yequ.lrc"
    }
  ]
}
```

#### 响应字段说明
- `query`: 原始查询条件
- `total`: 播放列表总歌曲数
- `songs`: 歌曲数组
  - `song_name`: 歌曲搜索名称
  - `artist`: 艺术家名称
  - `title`: 歌曲标题
  - `audio_url`: 音频流下载路径
  - `lyric_url`: 歌词文件下载路径

---

## 错误处理

### HTTP状态码
- `200 OK`: 请求成功
- `400 Bad Request`: 请求参数错误
- `404 Not Found`: 资源未找到
- `500 Internal Server Error`: 服务器内部错误

### 错误响应格式
```json
{
  "error": true,
  "code": 404,
  "message": "歌曲未找到",
  "details": "没有找到匹配的歌曲"
}
```

---

## 重试机制

### 音乐搜索重试
- **最大重试次数**: 3次
- **重试间隔**: 500ms
- **重试条件**: HTTP错误、网络超时

```cpp
const int max_retries = 3;
int retry_count = 0;
bool success = false;

while (retry_count < max_retries && !success) {
    // 重试逻辑
    retry_count++;
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
}
```

### 音频流下载重试
- **最大重试次数**: 3次
- **重试间隔**: 1000ms
- **重试条件**: 网络中断、数据读取失败

### 歌词下载重试
- **最大重试次数**: 3次
- **重试间隔**: 500ms
- **重试条件**: HTTP错误、网络超时、数据读取失败

```cpp
const int max_retries = 3;
int retry_count = 0;
bool success = false;
std::string lyric_content;
std::string current_url = lyric_url;
int redirect_count = 0;
const int max_redirects = 5;  // 最多允许5次重定向

while (retry_count < max_retries && !success && redirect_count < max_redirects) {
    if (retry_count > 0) {
        ESP_LOGI(TAG, "Retrying lyric download (attempt %d of %d)", retry_count + 1, max_retries);
        // 重试前暂停一下
        std::this_thread::sleep_for(std::chrono::milliseconds(config.GetRetryDelayMs()));
    }
    
    // 使用Board提供的HTTP客户端
    auto http = Board::GetInstance().CreateHttp();
    if (!http) {
        ESP_LOGE(TAG, "Failed to create HTTP client for lyric download");
        retry_count++;
        continue;
    }
    
    // 设置请求头
    http->SetHeader("User-Agent", config.GetUserAgent().c_str());
    http->SetHeader("Accept", "text/plain");
    http->SetHeader("Device-Id", SystemInfo::GetMacAddress().c_str());
    http->SetHeader("Board-Type", BOARD_NAME);
    
    // 打开GET连接
    ESP_LOGI(TAG, "小智开源音乐固件qq交流群:826072986");
    if (!http->Open("GET", current_url)) {
        ESP_LOGE(TAG, "Failed to open HTTP connection for lyrics");
        delete http;
        retry_count++;
        continue;
    }
    
    // 检查HTTP状态码
    int status_code = http->GetStatusCode();
    ESP_LOGI(TAG, "Lyric download HTTP status code: %d", status_code);
    
    // 处理重定向 - 由于Http类没有GetHeader方法，我们只能根据状态码判断
    if (status_code == 301 || status_code == 302 || status_code == 303 || status_code == 307 || status_code == 308) {
        // 由于无法获取Location头，只能报告重定向但无法继续
        ESP_LOGW(TAG, "Received redirect status %d but cannot follow redirect (no GetHeader method)", status_code);
        http->Close();
        delete http;
        retry_count++;
        continue;
    }
    
    // 非200系列状态码视为错误
    if (status_code < 200 || status_code >= 300) {
        ESP_LOGE(TAG, "HTTP GET failed with status code: %d", status_code);
        http->Close();
        delete http;
        retry_count++;
        continue;
    }
    
    // 读取响应
    lyric_content.clear();
    char buffer[1024];
    int bytes_read;
    bool read_error = false;
    int total_read = 0;
    
    // 由于无法获取Content-Length和Content-Type头，我们不知道预期大小和内容类型
    ESP_LOGD(TAG, "Starting to read lyric content");
    
    while (true) {
        bytes_read = http->Read(buffer, sizeof(buffer) - 1);
        // ESP_LOGD(TAG, "Lyric HTTP read returned %d bytes", bytes_read); // 注释掉以减少日志输出
        
        if (bytes_read > 0) {
            buffer[bytes_read] = '\0';
            lyric_content += buffer;
            total_read += bytes_read;
            
            // 定期打印下载进度 - 改为DEBUG级别减少输出
            if (total_read % 4096 == 0) {
                ESP_LOGD(TAG, "Downloaded %d bytes so far", total_read);
            }
        } else if (bytes_read == 0) {
            // 正常结束，没有更多数据
            ESP_LOGD(TAG, "Lyric download completed, total bytes: %d", total_read);
            success = true;
            break;
        } else {
            // bytes_read < 0，可能是ESP-IDF的已知问题
            // 如果已经读取到了一些数据，则认为下载成功
            if (!lyric_content.empty()) {
                ESP_LOGW(TAG, "HTTP read returned %d, but we have data (%d bytes), continuing", bytes_read, lyric_content.length());
                success = true;
                break;
            } else {
                ESP_LOGE(TAG, "Failed to read lyric data: error code %d", bytes_read);
                read_error = true;
                break;
            }
        }
    }
    
    http->Close();
    delete http;
    
    if (read_error) {
        retry_count++;
        continue;
    }
    
    // 如果成功读取数据，跳出重试循环
    if (success) {
        break;
    }
}

// 检查是否超过了最大重试次数
if (retry_count >= max_retries) {
    ESP_LOGE(TAG, "Failed to download lyrics after %d attempts", max_retries);
    return false;
}

// 记录前几个字节的数据，帮助调试
if (!lyric_content.empty()) {
    size_t preview_size = std::min(lyric_content.size(), size_t(50));
    std::string preview = lyric_content.substr(0, preview_size);
    ESP_LOGD(TAG, "Lyric content preview (%d bytes): %s", lyric_content.length(), preview.c_str());
} else {
    ESP_LOGE(TAG, "Failed to download lyrics or lyrics are empty");
    return false;
}

ESP_LOGI(TAG, "Lyrics downloaded successfully, size: %d bytes", lyric_content.length());
return ParseLyrics(lyric_content);
```

---

## 数据流处理

### 音频流处理
- **分块大小**: 4KB每块
- **缓冲区大小**: 最大512KB，最小64KB
- **解码方式**: 实时MP3解码
- **播放方式**: 流式播放，边下载边播放

### 歌词流处理
- **格式解析**: LRC格式 `[mm:ss.xx]歌词文本`
- **时间同步**: 基于播放时间戳
- **更新频率**: 50ms
- **显示方式**: 实时屏幕显示

---

## 技术特性

### 1. 流式传输
- 支持大文件流式下载
- 边下载边播放，无需等待完整下载
- 支持断点续传

### 2. 多线程处理
- 下载线程：负责音频数据下载
- 播放线程：负责音频解码和播放
- 歌词线程：负责歌词下载和显示

### 3. 缓冲区管理
- 智能缓冲区管理
- 防止内存溢出
- 动态调整缓冲区大小

### 4. 格式支持
- **音频格式**: MP3（带ID3标签）
- **歌词格式**: LRC
- **编码格式**: UTF-8

### 5. 自动播放功能
- **自动播放下一首**: 播放完一首歌后自动请求下一首
- **播放模式切换**: 支持单曲、自动播放、列表播放三种模式
- **播放列表管理**: 支持按艺术家、专辑等条件创建播放列表

---

## 使用示例

### 基本使用
```cpp
// 创建音乐播放器
Esp32Music music_player;

// 下载并播放音乐
if (music_player.Download("周杰伦 稻香")) {
    music_player.Play();
}

// 停止播放
music_player.Stop();
```

### 流式播放
```cpp
// 开始流式播放
music_player.StartStreaming("http://www.jsrc.top:5566/audio/stream?file=xxx.mp3");

// 停止流式播放
music_player.StopStreaming();
```

### 自动播放功能
```cpp
// 启用自动播放下一首
music_player.EnableAutoNext(true);

// 设置播放模式为自动播放
music_player.SetPlayMode(PlayMode::AUTO_NEXT);

// 播放歌曲（播放完后会自动播放下一首）
music_player.Download("随机歌曲");
```

### 列表播放功能
```cpp
// 创建播放列表
music_player.CreatePlaylist("周杰伦");

// 设置播放模式为列表播放
music_player.SetPlayMode(PlayMode::PLAYLIST);

// 启用自动播放下一首
music_player.EnableAutoNext(true);

// 开始播放第一首歌曲
if (music_player.GetPlaylistSize() > 0) {
    music_player.Download(music_player.GetCurrentSongName());
}

// 手动播放下一首
music_player.PlayNext();

// 手动播放上一首
music_player.PlayPrevious();
```

---

## 注意事项

1. **网络连接**: 需要稳定的网络连接
2. **内存管理**: 注意缓冲区大小，避免内存溢出
3. **编码处理**: 确保UTF-8编码正确处理中文字符
4. **错误处理**: 实现适当的错误处理和重试机制
5. **线程安全**: 多线程环境下注意数据同步
6. **自动播放**: 确保在合适的时机启用/禁用自动播放功能
7. **播放列表**: 播放列表为空时需要进行相应处理

---

## 相关文件

- `main/boards/common/esp32_music.cc` - 音乐播放器实现
- `main/boards/common/esp32_music.h` - 音乐播放器头文件
- `main/boards/common/music.h` - 音乐播放器基类
- `main/mcp_server.cc` - MCP工具定义

---

## 更新日志

- **v1.0**: 初始版本，支持基本的音乐播放功能
- **v1.1**: 添加歌词显示功能
- **v1.2**: 优化流式播放和缓冲区管理
- **v1.3**: 添加错误处理和重试机制
- **v1.4**: 新增自动播放和列表播放功能

---

## 联系方式

- **QQ交流群**: 826072986
- **项目地址**: https://github.com/boluo-tech/xiaozhi-esp32
- **音乐服务器**: http://www.jsrc.top:5566 