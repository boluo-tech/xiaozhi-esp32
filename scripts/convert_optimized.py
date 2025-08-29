#!/usr/bin/env python3
"""
Batch convert GIF files to optimized AAF format
"""

import os
import sys
from gif_to_aaf_optimized import OptimizedAAFGenerator

def convert_optimized():
    """Convert all GIF files with optimization"""
    
    # 从命令行参数获取设置
    input_dir = "xiaohei_gif"
    output_dir = "xiaohei_aaf_optimized"
    target_width = 32
    target_height = 32
    quality = 'high'
    
    # 解析命令行参数
    i = 1
    while i < len(sys.argv):
        if sys.argv[i] == '-i' and i + 1 < len(sys.argv):
            input_dir = sys.argv[i + 1]
            i += 2
        elif sys.argv[i] == '-o' and i + 1 < len(sys.argv):
            output_dir = sys.argv[i + 1]
            i += 2
        elif sys.argv[i] == '-w' and i + 1 < len(sys.argv):
            target_width = int(sys.argv[i + 1])
            i += 2
        elif sys.argv[i] == '--height' and i + 1 < len(sys.argv):
            target_height = int(sys.argv[i + 1])
            i += 2
        elif sys.argv[i] == '-q' and i + 1 < len(sys.argv):
            quality = sys.argv[i + 1]
            i += 2
        else:
            i += 1
    
    # Check if input directory exists
    if not os.path.exists(input_dir):
        print(f"Error: Input directory '{input_dir}' does not exist!")
        return False
    
    # Create output directory
    if not os.path.exists(output_dir):
        os.makedirs(output_dir)
        print(f"Created output directory: {output_dir}")
    
    # Get all GIF files
    gif_files = []
    for file in os.listdir(input_dir):
        if file.lower().endswith('.gif'):
            gif_files.append(os.path.join(input_dir, file))
    
    if not gif_files:
        print(f"No GIF files found in {input_dir}")
        return False
    
    # Sort files by name
    gif_files.sort()
    
    print(f"Found {len(gif_files)} GIF files to convert")
    print(f"Optimization settings:")
    print(f"  Resolution: {target_width}x{target_height}")
    print(f"  Quality: {quality}")
    print(f"  Output directory: {output_dir}")
    print("=" * 60)
    
    # Convert each GIF file
    success_count = 0
    total_original_size = 0
    total_optimized_size = 0
    
    for gif_file in gif_files:
        gif_name = os.path.basename(gif_file)
        aaf_name = gif_name.replace('.gif', '.aaf')
        output_path = os.path.join(output_dir, aaf_name)
        
        print(f"\nConverting: {gif_name}")
        print("-" * 40)
        
        # Get original file size
        original_size = os.path.getsize(gif_file)
        total_original_size += original_size
        
        # Create new generator for each file
        generator = OptimizedAAFGenerator(target_width, target_height, quality)
        
        # Process GIF file
        if generator.add_gif_frames(gif_file):
            # Generate AAF file
            if generator.generate_aaf(output_path):
                optimized_size = os.path.getsize(output_path)
                total_optimized_size += optimized_size
                
                # Calculate compression ratio
                compression_ratio = (1 - optimized_size / original_size) * 100
                
                print(f"✓ Successfully converted: {gif_name} -> {aaf_name}")
                print(f"  Original size: {original_size:,} bytes")
                print(f"  Optimized size: {optimized_size:,} bytes")
                print(f"  Compression: {compression_ratio:.1f}%")
                
                success_count += 1
                
                # Verify the generated file
                verify_aaf_file(output_path)
            else:
                print(f"✗ Failed to generate AAF: {aaf_name}")
        else:
            print(f"✗ Failed to process GIF: {gif_name}")
    
    print("\n" + "=" * 60)
    print(f"Conversion completed!")
    print(f"Successfully converted: {success_count}/{len(gif_files)} files")
    print(f"Total original size: {total_original_size:,} bytes")
    print(f"Total optimized size: {total_optimized_size:,} bytes")
    print(f"Overall compression: {(1 - total_optimized_size / total_original_size) * 100:.1f}%")
    print(f"Output directory: {output_dir}")
    
    return success_count == len(gif_files)

def verify_aaf_file(aaf_path):
    """Verify the generated AAF file structure"""
    try:
        with open(aaf_path, 'rb') as f:
            data = f.read()
        
        if len(data) < 16:
            print("  ✗ File too small")
            return False
        
        # Check magic number
        if data[0] != 0x89:
            print("  ✗ Wrong magic number")
            return False
        
        # Check AAF string
        if data[1:4] != b'AAF':
            print("  ✗ Wrong AAF string")
            return False
        
        # Check frame count
        frame_count = int.from_bytes(data[4:8], 'little')
        print(f"  ✓ Frame count: {frame_count}")
        
        # Check for frame magic numbers
        frame_magic_count = data.count(b'\x5a\x5a')
        print(f"  ✓ Frame magic numbers: {frame_magic_count}")
        
        # Check file size
        file_size = len(data)
        print(f"  ✓ File size: {file_size:,} bytes")
        
        return True
        
    except Exception as e:
        print(f"  ✗ Verification error: {e}")
        return False

if __name__ == "__main__":
    print("GIF to Optimized AAF Batch Converter")
    print("Converting GIF files with resolution and quality optimization")
    print("=" * 60)
    
    if convert_optimized():
        print("\n🎉 All optimizations completed successfully!")
        sys.exit(0)
    else:
        print("\n❌ Some optimizations failed!")
        sys.exit(1) 