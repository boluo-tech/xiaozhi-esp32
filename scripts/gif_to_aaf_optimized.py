#!/usr/bin/env python3
"""
Optimized AAF (Animated Asset Format) Generator for ESP32 EchoEar
Based on esp_emote_gfx component specifications

Optimizations:
- Lower resolution (configurable, default 64x64)
- Color depth optimization
- No frame skipping (preserve smooth animation)
"""

import os
import sys
import struct
from PIL import Image
import argparse
from typing import List, Tuple, Optional

# AAF Format Constants
AAF_MAGIC = 0x89
AAF_STRING = b"AAF"
FRAME_MAGIC = 0x5A5A

class OptimizedAAFGenerator:
    def __init__(self, target_width=64, target_height=64, quality='high'):
        self.frame_data = []
        self.frame_sizes = []
        self.frame_offsets = []
        self.target_width = target_width
        self.target_height = target_height
        self.quality = quality
        
    def add_gif_frames(self, gif_path: str) -> bool:
        """Add all frames from a GIF file with optimization"""
        try:
            # Open GIF file
            gif = Image.open(gif_path)
            
            # Check if it's actually a GIF
            if gif.format != 'GIF':
                print(f"Warning: {gif_path} is not a GIF file")
                return False
            
            frame_count = 0
            try:
                while True:
                    # Convert frame to RGB (GIF might have transparency)
                    frame = gif.convert('RGB')
                    
                    # Resize frame to target dimensions
                    resized_frame = frame.resize((self.target_width, self.target_height), Image.Resampling.LANCZOS)
                    
                    # Convert to RGB565 format with optimization
                    rgb565_data = self.convert_to_rgb565_optimized(resized_frame)
                    
                    # Add frame magic number (0x5A5A)
                    frame_data = struct.pack('<H', FRAME_MAGIC) + rgb565_data
                    
                    self.frame_data.append(frame_data)
                    self.frame_sizes.append(len(frame_data))
                    frame_count += 1
                    
                    # Move to next frame
                    gif.seek(gif.tell() + 1)
                    
            except EOFError:
                # End of frames
                pass
            
            print(f"✓ Extracted {frame_count} frames from {os.path.basename(gif_path)}")
            print(f"  Resized to: {self.target_width}x{self.target_height}")
            print(f"  Frame size: {self.frame_sizes[0] if self.frame_sizes else 0} bytes")
            return True
            
        except Exception as e:
            print(f"Error processing GIF {gif_path}: {e}")
            return False
    
    def convert_to_rgb565_optimized(self, img: Image.Image) -> bytes:
        """Convert PIL image to RGB565 format with optimization"""
        width, height = img.size
        rgb565_data = bytearray()
        
        # Apply dithering for better color quality at lower bit depth
        if self.quality == 'high':
            img = self.apply_dithering(img)
        
        for y in range(height):
            for x in range(width):
                r, g, b = img.getpixel((x, y))
                
                # Convert to RGB565 (5-6-5 bits)
                r = (r >> 3) & 0x1F
                g = (g >> 2) & 0x3F
                b = (b >> 3) & 0x1F
                
                # Pack into 16-bit value
                rgb565 = (r << 11) | (g << 5) | b
                rgb565_data.extend(struct.pack('<H', rgb565))
        
        return bytes(rgb565_data)
    
    def apply_dithering(self, img: Image.Image) -> Image.Image:
        """Apply Floyd-Steinberg dithering for better color quality"""
        # Simple dithering implementation
        img_array = img.load()
        width, height = img.size
        
        for y in range(height):
            for x in range(width):
                old_pixel = list(img_array[x, y])
                new_pixel = [round(old_pixel[0] / 255) * 255,
                           round(old_pixel[1] / 255) * 255,
                           round(old_pixel[2] / 255) * 255]
                
                error = [old_pixel[i] - new_pixel[i] for i in range(3)]
                
                img_array[x, y] = tuple(new_pixel)
                
                # Distribute error to neighboring pixels
                if x + 1 < width:
                    current_pixel = list(img_array[x + 1, y])
                    for i in range(3):
                        current_pixel[i] = max(0, min(255, current_pixel[i] + error[i] * 7 // 16))
                    img_array[x + 1, y] = tuple(current_pixel)
                    
                if y + 1 < height:
                    current_pixel = list(img_array[x, y + 1])
                    for i in range(3):
                        current_pixel[i] = max(0, min(255, current_pixel[i] + error[i] * 5 // 16))
                    img_array[x, y + 1] = tuple(current_pixel)
                    
                if x + 1 < width and y + 1 < height:
                    current_pixel = list(img_array[x + 1, y + 1])
                    for i in range(3):
                        current_pixel[i] = max(0, min(255, current_pixel[i] + error[i] * 1 // 16))
                    img_array[x + 1, y + 1] = tuple(current_pixel)
                    
                if x - 1 >= 0 and y + 1 < height:
                    current_pixel = list(img_array[x - 1, y + 1])
                    for i in range(3):
                        current_pixel[i] = max(0, min(255, current_pixel[i] + error[i] * 3 // 16))
                    img_array[x - 1, y + 1] = tuple(current_pixel)
        
        return img
    
    def calculate_checksum(self, data: bytes) -> int:
        """Calculate simple checksum of data"""
        return sum(data) & 0xFFFFFFFF
    
    def generate_aaf(self, output_path: str) -> bool:
        """Generate AAF file"""
        try:
            if not self.frame_data:
                print("No frames to process")
                return False
            
            # Calculate offsets
            total_frames = len(self.frame_data)
            asset_table_size = total_frames * 8  # 8 bytes per frame entry
            
            current_offset = 0
            for i in range(total_frames):
                self.frame_offsets.append(current_offset)
                current_offset += self.frame_sizes[i]
            
            # Create asset table
            asset_table = bytearray()
            for i in range(total_frames):
                # Each entry: asset_size (4 bytes) + asset_offset (4 bytes)
                asset_table.extend(struct.pack('<II', self.frame_sizes[i], self.frame_offsets[i]))
            
            # Calculate checksum of asset table + frame data
            all_data = asset_table
            for frame_data in self.frame_data:
                all_data.extend(frame_data)
            
            checksum = self.calculate_checksum(all_data)
            
            # Create AAF header
            header = bytearray()
            header.append(AAF_MAGIC)                    # Magic number (0x89)
            header.extend(AAF_STRING)                   # "AAF" string
            header.extend(struct.pack('<I', total_frames))  # Total frames
            header.extend(struct.pack('<I', checksum))      # Checksum
            header.extend(struct.pack('<I', len(all_data))) # Table + data length
            
            # Write AAF file
            with open(output_path, 'wb') as f:
                f.write(header)        # Write header
                f.write(asset_table)   # Write asset table
                for frame_data in self.frame_data:
                    f.write(frame_data)  # Write frame data
            
            print(f"✓ AAF file generated successfully: {output_path}")
            print(f"  Total frames: {total_frames}")
            print(f"  File size: {os.path.getsize(output_path)} bytes")
            print(f"  Checksum: 0x{checksum:08X}")
            print(f"  Resolution: {self.target_width}x{self.target_height}")
            
            return True
            
        except Exception as e:
            print(f"Error generating AAF file: {e}")
            return False

def main():
    parser = argparse.ArgumentParser(description='Generate optimized AAF format from GIF files')
    parser.add_argument('input_file', help='Input GIF file or directory')
    parser.add_argument('output_file', help='Output AAF file path')
    parser.add_argument('--width', type=int, default=64, help='Target width (default: 64)')
    parser.add_argument('--height', type=int, default=64, help='Target height (default: 64)')
    parser.add_argument('--quality', choices=['low', 'medium', 'high'], default='high', 
                       help='Quality level (default: high)')
    
    args = parser.parse_args()
    
    if not os.path.exists(args.input_file):
        print(f"Input file/directory does not exist: {args.input_file}")
        return 1
    
    # Generate AAF
    generator = OptimizedAAFGenerator(args.width, args.height, args.quality)
    
    if os.path.isdir(args.input_file):
        # Process directory
        import glob
        gif_files = glob.glob(os.path.join(args.input_file, '*.gif'))
        
        if not gif_files:
            print(f"No GIF files found in {args.input_file}")
            return 1
        
        # Sort files by name
        gif_files.sort()
        
        print(f"Found {len(gif_files)} GIF files")
        for gif_file in gif_files:
            print(f"  - {os.path.basename(gif_file)}")
        
        print(f"\nOptimization settings:")
        print(f"  Resolution: {args.width}x{args.height}")
        print(f"  Quality: {args.quality}")
        print(f"  Output: {args.output_file}")
        
        print("\nProcessing GIF files...")
        for gif_file in gif_files:
            if generator.add_gif_frames(gif_file):
                print(f"✓ Processed: {os.path.basename(gif_file)}")
            else:
                print(f"✗ Failed to process: {os.path.basename(gif_file)}")
    
    else:
        # Process single file
        if args.input_file.lower().endswith('.gif'):
            print(f"Processing single GIF file: {args.input_file}")
            print(f"Optimization settings: {args.width}x{args.height}, quality: {args.quality}")
            if generator.add_gif_frames(args.input_file):
                print(f"✓ Processed: {os.path.basename(args.input_file)}")
            else:
                print(f"✗ Failed to process: {args.input_file}")
                return 1
        else:
            print(f"Input file is not a GIF: {args.input_file}")
            return 1
    
    print(f"\nGenerating optimized AAF file: {args.output_file}")
    if generator.generate_aaf(args.output_file):
        print("Optimized AAF generation completed successfully!")
        return 0
    else:
        print("Optimized AAF generation failed!")
        return 1

if __name__ == "__main__":
    sys.exit(main()) 