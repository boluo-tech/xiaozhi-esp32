#!/usr/bin/env python3
"""
正确的 AAF 转换脚本
将 GIF 文件转换为标准的 AAF 格式
"""

import os
import sys
from PIL import Image
import struct

def gif_to_aaf(gif_path, output_path):
    """将 GIF 文件转换为 AAF 格式"""
    
    # 打开 GIF 文件
    with Image.open(gif_path) as img:
        # 获取所有帧
        frames = []
        try:
            while True:
                # 转换为 RGB 模式
                frame = img.convert('RGB')
                # 转换为 4-bit 索引色
                frame = frame.quantize(colors=16, method=2)
                frames.append(frame)
                img.seek(img.tell() + 1)
        except EOFError:
            pass
    
    if not frames:
        print(f"错误：无法从 {gif_path} 提取帧")
        return False
    
    print(f"提取了 {len(frames)} 帧")
    
    # 准备数据
    frame_data = []
    frame_sizes = []
    frame_offsets = []
    current_offset = 0
    
    for frame in frames:
        # 转换为原始字节数据
        frame_bytes = frame.tobytes()
        frame_size = len(frame_bytes) + 2  # +2 for 0x5A5A header
        frame_sizes.append(frame_size)
        frame_offsets.append(current_offset)
        
        # 准备帧数据
        frame_data.append(b'\x5A\x5A' + frame_bytes)
        current_offset += frame_size
    
    # 计算表长度
    table_len = len(frames) * 8
    
    # 计算校验和
    checksum_data = bytearray()
    for i in range(len(frames)):
        checksum_data.extend(struct.pack('<I', frame_sizes[i]))
        checksum_data.extend(struct.pack('<I', frame_offsets[i]))
    
    for frame_bytes in frame_data:
        checksum_data.extend(frame_bytes)
    
    checksum = sum(checksum_data) & 0xFFFFFFFF
    
    # 创建 AAF 文件
    with open(output_path, 'wb') as f:
        # 写入文件头
        f.write(b'\x89')  # 魔数
        f.write(b'AAF')   # 格式字符串
        
        # 写入帧数（4字节，小端序）
        f.write(struct.pack('<I', len(frames)))
        
        # 写入校验和
        f.write(struct.pack('<I', checksum))
        
        # 写入表长度
        f.write(struct.pack('<I', table_len))
        
        # 写入资产表
        for i in range(len(frames)):
            f.write(struct.pack('<I', frame_sizes[i]))  # 大小
            f.write(struct.pack('<I', frame_offsets[i]))  # 偏移
        
        # 写入帧数据
        for frame_bytes in frame_data:
            f.write(frame_bytes)
    
    print(f"成功创建 AAF 文件：{output_path}")
    return True

def main():
    if len(sys.argv) != 3:
        print("用法: python3 fix_aaf_converter.py <gif_file> <output_aaf>")
        sys.exit(1)
    
    gif_file = sys.argv[1]
    output_file = sys.argv[2]
    
    if not os.path.exists(gif_file):
        print(f"错误：文件 {gif_file} 不存在")
        sys.exit(1)
    
    if gif_to_aaf(gif_file, output_file):
        print("转换完成！")
    else:
        print("转换失败！")
        sys.exit(1)

if __name__ == "__main__":
    main() 