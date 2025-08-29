#!/usr/bin/env bash
set -euo pipefail

# 独立流程：GIF→AAF→assets_A.bin（无需编译应用）
# 用法：
#  scripts/build_assets_bin.sh \
#    --gifs <gif_dir|zip> \
#    --emoji-dir managed_components/espressif2022__esp_emote_gfx/emoji_normal \
#    --out build/assets/assets_A.bin \
#    [--width 480 --height 480 --fps 20]

GIF_INPUT=""
EMOJI_DIR="managed_components/espressif2022__esp_emote_gfx/emoji_normal"
OUT_BIN="build/assets/assets_A.bin"
WIDTH=480
HEIGHT=480
FPS=20
LABEL=A

while [[ $# -gt 0 ]]; do
  case "$1" in
    --gifs) GIF_INPUT="$2"; shift 2;;
    --emoji-dir) EMOJI_DIR="$2"; shift 2;;
    --out) OUT_BIN="$2"; shift 2;;
    --width) WIDTH="$2"; shift 2;;
    --height) HEIGHT="$2"; shift 2;;
    --fps) FPS="$2"; shift 2;;
    --label) LABEL="$2"; shift 2;;
    *) echo "未知参数: $1"; exit 1;;
  esac
done

if [[ -z "$GIF_INPUT" ]]; then
  echo "用法: $0 --gifs <gif_dir|zip> [--emoji-dir <dir>] [--out <file>] [--width W --height H --fps N]"
  exit 1
fi

ROOT_DIR=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT_DIR"

# 1) 准备临时目录
WORK_DIR=$(mktemp -d)
trap 'rm -rf "$WORK_DIR"' EXIT
GIF_DIR="$WORK_DIR/gifs"
AAF_DIR="$WORK_DIR/aaf"
STAGE_DIR="$WORK_DIR/stage"
mkdir -p "$GIF_DIR" "$AAF_DIR" "$STAGE_DIR"

# 2) 解压或拷贝 GIF
if [[ -f "$GIF_INPUT" && "$GIF_INPUT" == *.zip ]]; then
  unzip -q "$GIF_INPUT" -d "$GIF_DIR"
else
  rsync -a --exclude='.*' "$GIF_INPUT"/ "$GIF_DIR"/
fi

# 3) 生成 AAF（调用已存在的优化脚本）
python3 scripts/convert_optimized.py \
  --input "$GIF_DIR" \
  --output "$AAF_DIR" \
  --width "$WIDTH" --height "$HEIGHT" --fps "$FPS"

# 4) 将 AAF 映射到官方命名并补齐依赖资源
# 复制已有官方资源文件（字体与图标）
cp -f "$EMOJI_DIR"/KaiTi.ttf "$STAGE_DIR"/ 2>/dev/null || true
for icon in icon_Battery.bin icon_WiFi_failed.bin icon_mic.bin icon_speaker_zzz.bin icon_wifi.bin; do
  [[ -f "$EMOJI_DIR/$icon" ]] && cp -f "$EMOJI_DIR/$icon" "$STAGE_DIR/$icon"
done

# 简单映射：按文件名英文化（若你的 AAF 用中文名，可在此处做映射表）
# 这里只做直接按已统一英文名拷贝（如 happy.aaf / sad.aaf 等）
shopt -s nullglob
for name in angry happy idle listen sad shocked thinking cool surprised funny crying confused sleepy yawn curious silly dizzy enjoy; do
  [[ -f "$AAF_DIR/$name.aaf" ]] && cp -f "$AAF_DIR/$name.aaf" "$STAGE_DIR/$name.aaf"
done
shopt -u nullglob

# 5) 调用 spiffsgen.py 生成 assets_A.bin
if [[ -z "${IDF_PATH:-}" ]]; then
  echo "请先设置 ESP-IDF 环境 (export IDF_PATH=...)"
  exit 1
fi
SPIFFSGEN="$IDF_PATH/components/spiffs/spiffsgen.py"
if [[ ! -f "$SPIFFSGEN" ]]; then
  echo "未找到 spiffsgen.py: $SPIFFSGEN"; exit 1
fi

# 从分区表读取大小
PT=partitions/v1/16m_echoear.csv
SIZE_HEX=$(awk -F, -v lbl="assets_${LABEL}" 'tolower($1)==tolower(lbl){gsub(/ /,"",$5);print $5}' "$PT")
parse_size(){
  local s="$1"
  if [[ "$s" =~ ^0x[0-9A-Fa-f]+$ ]]; then python3 - <<PY
print(int("$s",16))
PY
  elif [[ "$s" =~ [Kk]$ ]]; then echo $(( ${s%K} * 1024 ));
  elif [[ "$s" =~ [Mm]$ ]]; then echo $(( ${s%M} * 1024 * 1024 ));
  else echo "$s"; fi
}
SIZE_BYTES=$(parse_size "$SIZE_HEX")
mkdir -p "$(dirname "$OUT_BIN")"
python3 "$SPIFFSGEN" "$SIZE_BYTES" "$STAGE_DIR" "$OUT_BIN" --page-size 256 --obj-name-len 32 --meta-len 4

echo "OK: 生成完成 -> $OUT_BIN" 