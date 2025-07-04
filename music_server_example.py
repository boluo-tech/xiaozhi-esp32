#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
ESP32音乐服务器示例
支持自动播放和列表播放功能

作者：小智开源音乐固件
QQ交流群：826072986
"""

from flask import Flask, request, jsonify, send_file
import json
import random
import sqlite3
import os
from urllib.parse import unquote
import logging

# 配置日志
logging.basicConfig(level=logging.INFO)
logger = logging.getLogger(__name__)

app = Flask(__name__)

# 数据库配置
DB_PATH = "music_database.db"

# 音乐数据库示例数据
MUSIC_DATABASE = {
    "周杰伦": [
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
        },
        {
            "song_name": "周杰伦 七里香",
            "artist": "周杰伦",
            "title": "七里香",
            "audio_url": "/audio/stream?file=qilixiang.mp3",
            "lyric_url": "/lyric/stream?file=qilixiang.lrc"
        },
        {
            "song_name": "周杰伦 晴天",
            "artist": "周杰伦",
            "title": "晴天",
            "audio_url": "/audio/stream?file=qingtian.mp3",
            "lyric_url": "/lyric/stream?file=qingtian.lrc"
        }
    ],
    "邓紫棋": [
        {
            "song_name": "邓紫棋 泡沫",
            "artist": "邓紫棋",
            "title": "泡沫",
            "audio_url": "/audio/stream?file=paomo.mp3",
            "lyric_url": "/lyric/stream?file=paomo.lrc"
        },
        {
            "song_name": "邓紫棋 喜欢你",
            "artist": "邓紫棋",
            "title": "喜欢你",
            "audio_url": "/audio/stream?file=xihuanni.mp3",
            "lyric_url": "/lyric/stream?file=xihuanni.lrc"
        }
    ],
    "林俊杰": [
        {
            "song_name": "林俊杰 江南",
            "artist": "林俊杰",
            "title": "江南",
            "audio_url": "/audio/stream?file=jiangnan.mp3",
            "lyric_url": "/lyric/stream?file=jiangnan.lrc"
        },
        {
            "song_name": "林俊杰 小酒窝",
            "artist": "林俊杰",
            "title": "小酒窝",
            "audio_url": "/audio/stream?file=xiaojiuwo.mp3",
            "lyric_url": "/lyric/stream?file=xiaojiuwo.lrc"
        }
    ]
}

# 全局播放历史记录
play_history = []
current_playlist = []

def init_database():
    """初始化数据库"""
    conn = sqlite3.connect(DB_PATH)
    cursor = conn.cursor()
    
    # 创建音乐表
    cursor.execute('''
        CREATE TABLE IF NOT EXISTS music (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            song_name TEXT NOT NULL,
            artist TEXT NOT NULL,
            title TEXT NOT NULL,
            audio_url TEXT NOT NULL,
            lyric_url TEXT NOT NULL,
            play_count INTEGER DEFAULT 0,
            created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
        )
    ''')
    
    # 创建播放历史表
    cursor.execute('''
        CREATE TABLE IF NOT EXISTS play_history (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            song_name TEXT NOT NULL,
            device_id TEXT,
            board_type TEXT,
            played_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
        )
    ''')
    
    # 插入示例数据
    for artist, songs in MUSIC_DATABASE.items():
        for song in songs:
            cursor.execute('''
                INSERT OR IGNORE INTO music (song_name, artist, title, audio_url, lyric_url)
                VALUES (?, ?, ?, ?, ?)
            ''', (song['song_name'], song['artist'], song['title'], song['audio_url'], song['lyric_url']))
    
    conn.commit()
    conn.close()
    logger.info("数据库初始化完成")

def get_all_songs():
    """获取所有歌曲"""
    conn = sqlite3.connect(DB_PATH)
    cursor = conn.cursor()
    cursor.execute('SELECT song_name, artist, title, audio_url, lyric_url FROM music')
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

def search_songs(query):
    """搜索歌曲"""
    conn = sqlite3.connect(DB_PATH)
    cursor = conn.cursor()
    cursor.execute('''
        SELECT song_name, artist, title, audio_url, lyric_url 
        FROM music 
        WHERE song_name LIKE ? OR artist LIKE ? OR title LIKE ?
    ''', (f'%{query}%', f'%{query}%', f'%{query}%'))
    
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

def get_random_song():
    """获取随机歌曲"""
    songs = get_all_songs()
    if songs:
        return random.choice(songs)
    return None

def record_play_history(song_name, device_id=None, board_type=None):
    """记录播放历史"""
    conn = sqlite3.connect(DB_PATH)
    cursor = conn.cursor()
    
    # 记录播放历史
    cursor.execute('''
        INSERT INTO play_history (song_name, device_id, board_type)
        VALUES (?, ?, ?)
    ''', (song_name, device_id, board_type))
    
    # 更新播放次数
    cursor.execute('''
        UPDATE music SET play_count = play_count + 1
        WHERE song_name = ?
    ''', (song_name,))
    
    conn.commit()
    conn.close()

def get_recommended_songs(limit=10):
    """获取推荐歌曲（基于播放次数）"""
    conn = sqlite3.connect(DB_PATH)
    cursor = conn.cursor()
    cursor.execute('''
        SELECT song_name, artist, title, audio_url, lyric_url 
        FROM music 
        ORDER BY play_count DESC 
        LIMIT ?
    ''', (limit,))
    
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

@app.route('/stream_pcm', methods=['GET'])
def search_music():
    """音乐搜索API"""
    song = request.args.get('song', '')
    if not song:
        return jsonify({'error': True, 'message': '缺少歌曲参数'}), 400
    
    # URL解码
    song = unquote(song)
    logger.info(f"搜索歌曲: {song}")
    
    # 搜索歌曲
    songs = search_songs(song)
    
    if songs:
        # 返回第一首匹配的歌曲
        result = songs[0]
        
        # 记录播放历史
        device_id = request.headers.get('Device-Id')
        board_type = request.headers.get('Board-Type')
        record_play_history(result['song_name'], device_id, board_type)
        
        logger.info(f"找到歌曲: {result['title']} - {result['artist']}")
        return jsonify(result)
    else:
        logger.warning(f"未找到歌曲: {song}")
        return jsonify({'error': True, 'message': f'没有找到歌曲: {song}'}), 404

@app.route('/next_song', methods=['GET'])
def get_next_song():
    """获取下一首歌曲API"""
    logger.info("请求下一首歌曲")
    
    # 获取推荐歌曲
    recommended = get_recommended_songs(20)
    
    if recommended:
        # 随机选择一首推荐歌曲
        next_song = random.choice(recommended)
        
        # 记录播放历史
        device_id = request.headers.get('Device-Id')
        board_type = request.headers.get('Board-Type')
        record_play_history(next_song['song_name'], device_id, board_type)
        
        logger.info(f"下一首歌曲: {next_song['title']} - {next_song['artist']}")
        return jsonify(next_song)
    else:
        logger.warning("没有可推荐的歌曲")
        return jsonify({'error': True, 'message': '没有可推荐的歌曲'}), 404

@app.route('/previous_song', methods=['GET'])
def get_previous_song():
    """获取上一首歌曲API"""
    logger.info("请求上一首歌曲")
    
    # 获取播放历史
    conn = sqlite3.connect(DB_PATH)
    cursor = conn.cursor()
    cursor.execute('''
        SELECT song_name FROM play_history 
        ORDER BY played_at DESC 
        LIMIT 10
    ''')
    
    history = [row[0] for row in cursor.fetchall()]
    conn.close()
    
    if len(history) > 1:
        # 返回倒数第二首播放的歌曲
        prev_song_name = history[1]
        songs = search_songs(prev_song_name)
        
        if songs:
            prev_song = songs[0]
            
            # 记录播放历史
            device_id = request.headers.get('Device-Id')
            board_type = request.headers.get('Board-Type')
            record_play_history(prev_song['song_name'], device_id, board_type)
            
            logger.info(f"上一首歌曲: {prev_song['title']} - {prev_song['artist']}")
            return jsonify(prev_song)
    
    # 如果没有历史记录，返回随机歌曲
    random_song = get_random_song()
    if random_song:
        logger.info(f"随机歌曲: {random_song['title']} - {random_song['artist']}")
        return jsonify(random_song)
    
    logger.warning("没有可播放的歌曲")
    return jsonify({'error': True, 'message': '没有可播放的歌曲'}), 404

@app.route('/playlist', methods=['GET'])
def get_playlist():
    """获取播放列表API"""
    query = request.args.get('query', '')
    if not query:
        return jsonify({'error': True, 'message': '缺少查询参数'}), 400
    
    # URL解码
    query = unquote(query)
    logger.info(f"创建播放列表: {query}")
    
    # 搜索歌曲
    songs = search_songs(query)
    
    if songs:
        # 限制播放列表大小
        songs = songs[:20]
        
        result = {
            'query': query,
            'total': len(songs),
            'songs': songs
        }
        
        logger.info(f"播放列表创建成功，包含 {len(songs)} 首歌曲")
        return jsonify(result)
    else:
        logger.warning(f"未找到匹配的歌曲: {query}")
        return jsonify({
            'query': query,
            'total': 0,
            'songs': []
        })

@app.route('/audio/stream', methods=['GET'])
def stream_audio():
    """音频流下载API"""
    file_param = request.args.get('file', '')
    if not file_param:
        return jsonify({'error': True, 'message': '缺少文件参数'}), 400
    
    # 这里应该返回实际的音频文件
    # 示例中返回一个模拟的音频文件
    audio_path = f"audio_files/{file_param}"
    
    if os.path.exists(audio_path):
        return send_file(audio_path, mimetype='audio/mpeg')
    else:
        logger.warning(f"音频文件不存在: {audio_path}")
        return jsonify({'error': True, 'message': '音频文件不存在'}), 404

@app.route('/lyric/stream', methods=['GET'])
def stream_lyric():
    """歌词下载API"""
    file_param = request.args.get('file', '')
    if not file_param:
        return jsonify({'error': True, 'message': '缺少文件参数'}), 400
    
    # 这里应该返回实际的歌词文件
    # 示例中返回一个模拟的歌词文件
    lyric_path = f"lyric_files/{file_param}"
    
    if os.path.exists(lyric_path):
        return send_file(lyric_path, mimetype='text/plain; charset=utf-8')
    else:
        logger.warning(f"歌词文件不存在: {lyric_path}")
        return jsonify({'error': True, 'message': '歌词文件不存在'}), 404

@app.route('/stats', methods=['GET'])
def get_stats():
    """获取播放统计信息"""
    conn = sqlite3.connect(DB_PATH)
    cursor = conn.cursor()
    
    # 总歌曲数
    cursor.execute('SELECT COUNT(*) FROM music')
    total_songs = cursor.fetchone()[0]
    
    # 总播放次数
    cursor.execute('SELECT COUNT(*) FROM play_history')
    total_plays = cursor.fetchone()[0]
    
    # 热门歌曲
    cursor.execute('''
        SELECT song_name, artist, title, play_count 
        FROM music 
        ORDER BY play_count DESC 
        LIMIT 10
    ''')
    popular_songs = []
    for row in cursor.fetchall():
        popular_songs.append({
            'song_name': row[0],
            'artist': row[1],
            'title': row[2],
            'play_count': row[3]
        })
    
    conn.close()
    
    stats = {
        'total_songs': total_songs,
        'total_plays': total_plays,
        'popular_songs': popular_songs
    }
    
    return jsonify(stats)

@app.route('/health', methods=['GET'])
def health_check():
    """健康检查API"""
    return jsonify({
        'status': 'healthy',
        'version': '1.4.0',
        'features': [
            'music_search',
            'audio_streaming',
            'lyric_download',
            'auto_play',
            'playlist_support'
        ]
    })

@app.errorhandler(404)
def not_found(error):
    return jsonify({'error': True, 'message': '接口不存在'}), 404

@app.errorhandler(500)
def internal_error(error):
    return jsonify({'error': True, 'message': '服务器内部错误'}), 500

if __name__ == '__main__':
    # 初始化数据库
    init_database()
    
    # 创建必要的目录
    os.makedirs('audio_files', exist_ok=True)
    os.makedirs('lyric_files', exist_ok=True)
    
    logger.info("ESP32音乐服务器启动中...")
    logger.info("支持的功能:")
    logger.info("- 音乐搜索: GET /stream_pcm?song=歌曲名")
    logger.info("- 下一首歌曲: GET /next_song")
    logger.info("- 上一首歌曲: GET /previous_song")
    logger.info("- 播放列表: GET /playlist?query=查询条件")
    logger.info("- 音频流: GET /audio/stream?file=文件名")
    logger.info("- 歌词下载: GET /lyric/stream?file=文件名")
    logger.info("- 播放统计: GET /stats")
    logger.info("- 健康检查: GET /health")
    
    # 启动服务器
    app.run(host='0.0.0.0', port=5566, debug=True) 