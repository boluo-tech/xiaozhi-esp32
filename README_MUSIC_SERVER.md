# ESP32音乐服务器使用说明

## 概述

这是一个支持ESP32音乐播放器的Python Flask服务器，提供音乐搜索、自动播放、列表播放等功能。

## 功能特性

- ✅ 音乐搜索API
- ✅ 音频流下载
- ✅ 歌词下载
- ✅ 自动播放下一首
- ✅ 播放列表支持
- ✅ 播放历史记录
- ✅ 播放统计信息
- ✅ 设备识别支持

## 安装和运行

### 1. 环境要求

- Python 3.7+
- Flask 2.3.3+
- SQLite3

### 2. 安装依赖

```bash
pip install -r requirements.txt
```

### 3. 运行服务器

```bash
python music_server_example.py
```

服务器将在 `http://localhost:5566` 启动。

## API接口说明

### 基础接口

#### 1. 音乐搜索
```http
GET /stream_pcm?song=周杰伦%20稻香
```

#### 2. 音频流下载
```http
GET /audio/stream?file=daoxiang.mp3
```

#### 3. 歌词下载
```http
GET /lyric/stream?file=daoxiang.lrc
```

### 新增接口（自动播放和列表播放）

#### 4. 下一首歌曲
```http
GET /next_song
```

#### 5. 上一首歌曲
```http
GET /previous_song
```

#### 6. 播放列表
```http
GET /playlist?query=周杰伦
```

#### 7. 播放统计
```http
GET /stats
```

#### 8. 健康检查
```http
GET /health
```

## 数据库结构

### music表
| 字段 | 类型 | 说明 |
|------|------|------|
| id | INTEGER | 主键 |
| song_name | TEXT | 歌曲搜索名称 |
| artist | TEXT | 艺术家 |
| title | TEXT | 歌曲标题 |
| audio_url | TEXT | 音频文件URL |
| lyric_url | TEXT | 歌词文件URL |
| play_count | INTEGER | 播放次数 |
| created_at | TIMESTAMP | 创建时间 |

### play_history表
| 字段 | 类型 | 说明 |
|------|------|------|
| id | INTEGER | 主键 |
| song_name | TEXT | 歌曲名称 |
| device_id | TEXT | 设备ID |
| board_type | TEXT | 开发板类型 |
| played_at | TIMESTAMP | 播放时间 |

## 配置说明

### 服务器配置
- **端口**: 5566
- **主机**: 0.0.0.0（允许外部访问）
- **调试模式**: 启用

### 数据库配置
- **数据库文件**: music_database.db
- **自动初始化**: 启动时自动创建表和示例数据

## 示例数据

服务器包含以下示例音乐数据：

### 周杰伦
- 稻香
- 青花瓷
- 夜曲
- 七里香
- 晴天

### 邓紫棋
- 泡沫
- 喜欢你

### 林俊杰
- 江南
- 小酒窝

## 使用示例

### 1. 搜索歌曲
```bash
curl "http://localhost:5566/stream_pcm?song=周杰伦%20稻香"
```

### 2. 获取下一首歌曲
```bash
curl "http://localhost:5566/next_song"
```

### 3. 创建播放列表
```bash
curl "http://localhost:5566/playlist?query=周杰伦"
```

### 4. 查看播放统计
```bash
curl "http://localhost:5566/stats"
```

## 部署说明

### 生产环境部署

1. **使用Gunicorn**
```bash
pip install gunicorn
gunicorn -w 4 -b 0.0.0.0:5566 music_server_example:app
```

2. **使用Nginx反向代理**
```nginx
server {
    listen 80;
    server_name your-domain.com;
    
    location / {
        proxy_pass http://127.0.0.1:5566;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
    }
}
```

3. **使用Docker**
```dockerfile
FROM python:3.9-slim

WORKDIR /app
COPY requirements.txt .
RUN pip install -r requirements.txt

COPY music_server_example.py .
COPY audio_files/ ./audio_files/
COPY lyric_files/ ./lyric_files/

EXPOSE 5566
CMD ["python", "music_server_example.py"]
```

### 安全配置

1. **HTTPS支持**
```python
# 在app.run()中添加SSL证书
app.run(host='0.0.0.0', port=5566, ssl_context='adhoc')
```

2. **访问控制**
```python
# 添加API密钥验证
def require_api_key(f):
    @wraps(f)
    def decorated_function(*args, **kwargs):
        api_key = request.headers.get('X-API-Key')
        if api_key != 'your-secret-key':
            return jsonify({'error': 'Invalid API key'}), 401
        return f(*args, **kwargs)
    return decorated_function
```

## 监控和日志

### 日志配置
```python
import logging
from logging.handlers import RotatingFileHandler

# 文件日志
file_handler = RotatingFileHandler('music_server.log', maxBytes=10240, backupCount=10)
file_handler.setFormatter(logging.Formatter(
    '%(asctime)s %(levelname)s: %(message)s [in %(pathname)s:%(lineno)d]'
))
file_handler.setLevel(logging.INFO)
app.logger.addHandler(file_handler)
```

### 性能监控
```python
import time
from functools import wraps

def monitor_performance(f):
    @wraps(f)
    def decorated_function(*args, **kwargs):
        start_time = time.time()
        result = f(*args, **kwargs)
        end_time = time.time()
        app.logger.info(f'{f.__name__} took {end_time - start_time:.2f} seconds')
        return result
    return decorated_function
```

## 故障排除

### 常见问题

1. **端口被占用**
```bash
# 查看端口占用
netstat -tulpn | grep 5566

# 杀死占用进程
kill -9 <PID>
```

2. **数据库文件权限**
```bash
# 确保数据库文件可写
chmod 666 music_database.db
```

3. **音频文件不存在**
```bash
# 创建音频文件目录
mkdir -p audio_files lyric_files

# 添加示例音频文件
# 注意：实际部署时需要提供真实的音频文件
```

### 调试模式

启用调试模式查看详细错误信息：
```python
app.run(host='0.0.0.0', port=5566, debug=True)
```

## 扩展功能

### 1. 添加新歌曲
```python
def add_song(song_name, artist, title, audio_url, lyric_url):
    conn = sqlite3.connect(DB_PATH)
    cursor = conn.cursor()
    cursor.execute('''
        INSERT INTO music (song_name, artist, title, audio_url, lyric_url)
        VALUES (?, ?, ?, ?, ?)
    ''', (song_name, artist, title, audio_url, lyric_url))
    conn.commit()
    conn.close()
```

### 2. 自定义推荐算法
```python
def get_personalized_recommendations(device_id, limit=10):
    """基于用户播放历史的个性化推荐"""
    conn = sqlite3.connect(DB_PATH)
    cursor = conn.cursor()
    cursor.execute('''
        SELECT m.song_name, m.artist, m.title, m.audio_url, m.lyric_url
        FROM music m
        JOIN play_history ph ON m.song_name = ph.song_name
        WHERE ph.device_id = ?
        GROUP BY m.song_name
        ORDER BY COUNT(*) DESC
        LIMIT ?
    ''', (device_id, limit))
    
    songs = []
    for row in cursor.fetchall():
        songs.append({
            'song_name': row[0],
            'artist': row[1],
            'title': row[2],
            'audio_url': row[3],
            'lyric_url': row[4]
        })
    conn.close()
    return songs
```

### 3. 缓存支持
```python
from functools import lru_cache

@lru_cache(maxsize=128)
def get_cached_songs(query):
    """缓存搜索结果"""
    return search_songs(query)
```

## 联系信息

- **QQ交流群**: 826072986
- **项目地址**: https://github.com/boluo-tech/xiaozhi-esp32
- **作者**: 小智开源音乐固件团队

## 更新日志

- **v1.4.0**: 新增自动播放和列表播放功能
- **v1.3.0**: 添加播放历史和统计功能
- **v1.2.0**: 优化搜索和流式传输
- **v1.1.0**: 添加歌词支持
- **v1.0.0**: 基础音乐搜索和播放功能 