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
- **支持**: 断点续传

#### 请求示例
```http
GET http://www.jsrc.top:5566/audio/stream?file=xxx.mp3
User-Agent: ESP32-Music-Player/1.0
Accept: */*
Device-Id: 24:6F:28:12:34:56
Board-Type: esp32-s3-touch-lcd-1.85
Range: bytes=0-
```

#### 响应格式
- **内容类型**: 音频流（MP3格式）
- **传输方式**: 流式传输
- **支持格式**: MP3（带ID3标签）

#### 断点续传机制详解

##### 1. 基本概念
断点续传允许客户端从指定位置开始下载文件，而不是从头开始。这对于大文件下载和网络中断恢复非常有用。

##### 2. HTTP Range请求格式
```http
# 从头开始下载
Range: bytes=0-

# 从指定位置开始下载
Range: bytes=1024-

# 下载指定范围
Range: bytes=1024-2047

# 从末尾开始下载（最后1000字节）
Range: bytes=-1000
```

##### 3. 服务器响应
- **状态码 200**: 不支持Range请求，返回完整文件
- **状态码 206**: 支持Range请求，返回部分内容
- **响应头**: `Content-Range: bytes 1024-2047/8192`

##### 3.1 具体HTTP请求响应示例

###### 示例1：从头开始下载
```http
# 客户端请求
GET /audio/stream?file=song.mp3 HTTP/1.1
Host: www.jsrc.top:5566
User-Agent: ESP32-Music-Player/1.0
Accept: */*
Range: bytes=0-

# 服务器响应
HTTP/1.1 206 Partial Content
Content-Type: audio/mpeg
Content-Length: 8192
Content-Range: bytes 0-8191/8192
Accept-Ranges: bytes

[音频数据...]
```

###### 示例2：从中间位置继续下载
```http
# 客户端请求（假设已经下载了4096字节）
GET /audio/stream?file=song.mp3 HTTP/1.1
Host: www.jsrc.top:5566
User-Agent: ESP32-Music-Player/1.0
Accept: */*
Range: bytes=4096-

# 服务器响应
HTTP/1.1 206 Partial Content
Content-Type: audio/mpeg
Content-Length: 4096
Content-Range: bytes 4096-8191/8192
Accept-Ranges: bytes

[剩余音频数据...]
```

###### 示例3：下载指定范围
```http
# 客户端请求（只下载中间部分）
GET /audio/stream?file=song.mp3 HTTP/1.1
Host: www.jsrc.top:5566
User-Agent: ESP32-Music-Player/1.0
Accept: */*
Range: bytes=2048-4095

# 服务器响应
HTTP/1.1 206 Partial Content
Content-Type: audio/mpeg
Content-Length: 2048
Content-Range: bytes 2048-4095/8192
Accept-Ranges: bytes

[指定范围的音频数据...]
```

###### 示例4：服务器不支持Range请求
```http
# 客户端请求
GET /audio/stream?file=song.mp3 HTTP/1.1
Host: www.jsrc.top:5566
User-Agent: ESP32-Music-Player/1.0
Accept: */*
Range: bytes=4096-

# 服务器响应（不支持Range）
HTTP/1.1 200 OK
Content-Type: audio/mpeg
Content-Length: 8192

[完整音频数据...]
```

##### 4. ESP32实现示例

###### 当前实现（简单版本）
```cpp
// 当前代码中的实现
http->SetHeader("Range", "bytes=0-");  // 总是从头开始下载

int status_code = http->GetStatusCode();
if (status_code != 200 && status_code != 206) {
    ESP_LOGE(TAG, "HTTP GET failed with status code: %d", status_code);
    return false;
}
```

###### 完整断点续传实现示例
```cpp
class Esp32MusicWithResume : public Esp32Music {
private:
    size_t resume_position_ = 0;
    std::string cache_file_path_;
    
public:
    bool DownloadWithResume(const std::string& song_name) {
        // 1. 检查是否有缓存文件
        cache_file_path_ = "/spiffs/music_cache/" + song_name + ".mp3";
        if (CheckCacheFile(cache_file_path_, resume_position_)) {
            ESP_LOGI(TAG, "Found cached file, resuming from position: %d", resume_position_);
        }
        
        // 2. 构建HTTP请求
        auto http = Board::GetInstance().CreateHttp();
        http->SetHeader("User-Agent", "ESP32-Music-Player/1.0");
        http->SetHeader("Accept", "*/*");
        
        // 3. 设置Range头
        if (resume_position_ > 0) {
            std::string range_header = "bytes=" + std::to_string(resume_position_) + "-";
            http->SetHeader("Range", range_header.c_str());
            ESP_LOGI(TAG, "Requesting range: %s", range_header.c_str());
        } else {
            http->SetHeader("Range", "bytes=0-");
        }
        
        // 4. 发送请求
        if (!http->Open("GET", music_url)) {
            ESP_LOGE(TAG, "Failed to connect to music stream URL");
            return false;
        }
        
        // 5. 检查响应
        int status_code = http->GetStatusCode();
        ESP_LOGI(TAG, "HTTP response status: %d", status_code);
        
        if (status_code == 206) {
            // 部分内容响应
            ESP_LOGI(TAG, "Server supports range requests, resuming download");
            return HandlePartialContent(http);
        } else if (status_code == 200) {
            // 完整内容响应
            ESP_LOGI(TAG, "Server doesn't support range requests, downloading from start");
            resume_position_ = 0;
            return HandleFullContent(http);
        } else {
            ESP_LOGE(TAG, "HTTP request failed: %d", status_code);
            return false;
        }
    }
    
private:
    bool CheckCacheFile(const std::string& path, size_t& file_size) {
        FILE* file = fopen(path.c_str(), "rb");
        if (file) {
            fseek(file, 0, SEEK_END);
            file_size = ftell(file);
            fclose(file);
            return file_size > 0;
        }
        return false;
    }
    
    bool HandlePartialContent(Http* http) {
        // 处理206响应
        const size_t chunk_size = 4096;
        char buffer[chunk_size];
        size_t total_downloaded = resume_position_;
        
        while (is_downloading_) {
            int bytes_read = http->Read(buffer, chunk_size);
            if (bytes_read <= 0) break;
            
            // 保存到缓存文件
            SaveToCache(buffer, bytes_read, total_downloaded);
            
            // 添加到播放缓冲区
            AddToPlayBuffer(buffer, bytes_read);
            
            total_downloaded += bytes_read;
            
            // 每256KB打印进度
            if (total_downloaded % (256 * 1024) == 0) {
                ESP_LOGI(TAG, "Downloaded %d bytes (resumed from %d)", 
                        total_downloaded, resume_position_);
            }
        }
        
        return true;
    }
    
    bool HandleFullContent(Http* http) {
        // 处理200响应（从头开始下载）
        const size_t chunk_size = 4096;
        char buffer[chunk_size];
        size_t total_downloaded = 0;
        
        while (is_downloading_) {
            int bytes_read = http->Read(buffer, chunk_size);
            if (bytes_read <= 0) break;
            
            // 保存到缓存文件
            SaveToCache(buffer, bytes_read, total_downloaded);
            
            // 添加到播放缓冲区
            AddToPlayBuffer(buffer, bytes_read);
            
            total_downloaded += bytes_read;
        }
        
        return true;
    }
    
    void SaveToCache(const char* data, size_t size, size_t offset) {
        FILE* file = fopen(cache_file_path_.c_str(), "rb+");
        if (!file) {
            file = fopen(cache_file_path_.c_str(), "wb");
        }
        
        if (file) {
            fseek(file, offset, SEEK_SET);
            fwrite(data, 1, size, file);
            fclose(file);
        }
    }
    
    void AddToPlayBuffer(const char* data, size_t size) {
        uint8_t* chunk_data = (uint8_t*)heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
        if (chunk_data) {
            memcpy(chunk_data, data, size);
            
            std::lock_guard<std::mutex> lock(buffer_mutex_);
            audio_buffer_.push(AudioChunk(chunk_data, size));
            buffer_size_ += size;
            buffer_cv_.notify_one();
        }
    }
};
```

##### 5. 断点续传的优势

###### 网络中断恢复
```
场景：下载到50%时网络中断
↓
重新连接后，从50%位置继续下载
↓
节省带宽和时间
```

###### 大文件下载优化
```
文件大小：10MB
网络速度：1MB/s
预计时间：10秒

如果中断在5秒时：
- 传统方式：重新下载10MB，需要10秒
- 断点续传：继续下载5MB，需要5秒
```

###### 多设备同步
```
设备A：下载了前30%
设备B：可以从30%位置开始下载
设备C：可以从60%位置开始下载
```

##### 6. 实际使用场景

###### 场景1：网络不稳定
```
用户：播放周杰伦的稻香
↓
开始下载：0% → 25% → 网络中断
↓
网络恢复：从25%位置继续下载
↓
播放体验：无缝继续
```

###### 场景2：设备重启
```
播放中：设备意外重启
↓
重启后：检查缓存文件
↓
发现缓存：从缓存位置继续下载
↓
用户体验：快速恢复播放
```

###### 场景3：切换网络
```
WiFi下载：0% → 40%
切换到4G：从40%位置继续
↓
节省流量：只下载剩余60%
```

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
- **内容类型**: 文本（LRC歌词格式）
- **编码**: UTF-8
- **格式**: `[mm:ss.xx]歌词文本`

---

## 完整的API调用流程

### 步骤1: 搜索音乐
```
用户请求: "播放周杰伦的稻香"
↓
构建URL: http://www.jsrc.top:5566/stream_pcm?song=周杰伦%20稻香
↓
发送GET请求
↓
解析响应JSON，获取audio_url和lyric_url
```

### 步骤2: 并行下载
```
音频流下载: GET http://www.jsrc.top:5566/audio/stream?file=xxx.mp3
歌词下载:   GET http://www.jsrc.top:5566/lyric/stream?file=xxx.lrc
```

### 步骤3: 流式播放
```
音频数据 → MP3解码 → PCM数据 → 音频输出
歌词数据 → LRC解析 → 时间同步 → 屏幕显示
```

---

## URL编码处理

### URL编码函数
```cpp
static std::string url_encode(const std::string& str) {
    std::string encoded;
    char hex[4];
    
    for (size_t i = 0; i < str.length(); i++) {
        unsigned char c = str[i];
        
        if ((c >= 'A' && c <= 'Z') ||
            (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~') {
            encoded += c;
        } else if (c == ' ') {
            encoded += '+';  // 空格编码为'+'
        } else {
            snprintf(hex, sizeof(hex), "%%%02X", c);
            encoded += hex;
        }
    }
    return encoded;
}
```

### 编码示例
- `周杰伦 稻香` → `周杰伦+稻香`
- `Hello World` → `Hello+World`
- `特殊字符@#$` → `%E7%89%B9%E6%AE%8A%E5%AD%97%E7%AC%A6%40%23%24`

---

## 错误处理和重试机制

### HTTP状态码处理
```cpp
int status_code = http->GetStatusCode();
if (status_code != 200) {
    ESP_LOGE(TAG, "HTTP GET failed with status code: %d", status_code);
    return false;
}
```

### 歌词下载重试
- **最大重试次数**: 3次
- **重试间隔**: 500ms
- **重试条件**: HTTP错误、网络超时、数据读取失败

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

---

## 注意事项

1. **网络连接**: 需要稳定的网络连接
2. **内存管理**: 注意缓冲区大小，避免内存溢出
3. **编码处理**: 确保UTF-8编码正确处理中文字符
4. **错误处理**: 实现适当的错误处理和重试机制
5. **线程安全**: 多线程环境下注意数据同步

---

## 相关文件

- `main/boards/common/esp32_music.cc` - 音乐播放器实现
- `main/boards/common/esp32_music.h` - 音乐播放器头文件
- `main/boards/common/music.h` - 音乐播放器基类

---

## 更新日志

- **v1.0**: 初始版本，支持基本的音乐播放功能
- **v1.1**: 添加歌词显示功能
- **v1.2**: 优化流式播放和缓冲区管理
- **v1.3**: 添加错误处理和重试机制

---

## 联系方式

- **QQ交流群**: 826072986
- **项目地址**: https://github.com/boluo-tech/xiaozhi-esp32
- **音乐服务器**: http://www.jsrc.top:5566 