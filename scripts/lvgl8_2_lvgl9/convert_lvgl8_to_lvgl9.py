import os
import re
import sys
import argparse

def convert_file(input_path, output_path):
    """
    将lvgl8的.c文件转换为lvgl9格式
    
    转换规则:
    1. 将".header.cf = LV_IMG_CF_RAW_CHROMA_KEYED,"替换为".header.cf = LV_COLOR_FORMAT_RAW,"
    2. 删除".header.always_zero = 0,"和".header.reserved = 0,"
    """
    print(f"正在处理文件: {input_path}")
    try:
        with open(input_path, 'r', encoding='utf-8', errors='ignore') as f:
            content = f.read()
        
        # 检查原始内容
        original_size = len(content)
        print(f"  原始文件大小: {original_size} 字节")
        
        # 替换 .header.cf 值
        content = content.replace(".header.cf = LV_IMG_CF_RAW_CHROMA_KEYED,", 
                                ".header.cf = LV_COLOR_FORMAT_RAW,")
        
        # 删除 .header.always_zero = 0, 和 .header.reserved = 0,
        content = re.sub(r'\.header\.always_zero\s*=\s*0,\s*', '', content)
        content = re.sub(r'\.header\.reserved\s*=\s*0,\s*', '', content)
        
        # 检查是否有实际变化
        new_size = len(content)
        print(f"  处理后文件大小: {new_size} 字节")
        print(f"  变化: {original_size - new_size} 字节")
        
        # 写入转换后的内容到输出文件
        with open(output_path, 'w', encoding='utf-8') as f:
            f.write(content)
        
        print(f"  已保存到: {output_path}")
    except Exception as e:
        print(f"  处理文件时出错: {str(e)}")

def main():
    # 解析命令行参数
    parser = argparse.ArgumentParser(description='Convert LVGL8 image files to LVGL9 format')
    parser.add_argument('input_dir', nargs='?', help='Input directory containing .c files (default: ./input)')
    args = parser.parse_args()
    
    # 获取当前目录
    current_dir = os.getcwd()
    print(f"当前工作目录: {current_dir}")
    
    # 输入和输出目录
    if args.input_dir:
        input_dir = args.input_dir
        # 如果提供的是相对路径，转换为绝对路径
        if not os.path.isabs(input_dir):
            input_dir = os.path.abspath(input_dir)
    else:
        input_dir = os.path.join(current_dir, "input")
    
    output_dir = os.path.join(current_dir, "output")   # 根目录
    
    print(f"输入目录: {input_dir}")
    print(f"输出目录: {output_dir}")
    
    # 确保输入目录存在
    if not os.path.isdir(input_dir):
        print(f"错误: 输入目录 '{input_dir}' 不存在")
        return
    
    # 确保输出目录存在
    if not os.path.isdir(output_dir):
        os.makedirs(output_dir)
    else:
        # 清空输出目录中的所有文件
        print("清空输出目录...")
        for filename in os.listdir(output_dir):
            file_path = os.path.join(output_dir, filename)
            try:
                if os.path.isfile(file_path):
                    os.unlink(file_path)
                elif os.path.isdir(file_path):
                    import shutil
                    shutil.rmtree(file_path)
            except Exception as e:
                print(f"删除文件/目录时出错: {file_path} - {str(e)}")
    
    # 列出输入目录中的文件
    files = os.listdir(input_dir)
    print(f"输入目录中的文件: {files}")
    
    # 处理输入目录中的所有.c文件
    processed_files = 0
    for filename in files:
        if filename.endswith('.c'):
            input_path = os.path.join(input_dir, filename)
            output_path = os.path.join(output_dir, filename)
            
            convert_file(input_path, output_path)
            processed_files += 1
    
    print(f"\n转换完成! 共处理了 {processed_files} 个文件")

if __name__ == "__main__":
    main() 