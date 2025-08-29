#!/usr/bin/env bash
set -euo pipefail

# 用法: ./scripts/make_assets_spiffs.sh <emoji_dir> <out_dir> [label]
# 例子: ./scripts/make_assets_spiffs.sh managed_components/espressif2022__esp_emote_gfx/emoji_normal build/assets A

EMOJI_DIR=${1:-""}
OUT_DIR=${2:-"build/assets"}
LABEL=${3:-"A"}

if [[ -z "$EMOJI_DIR" ]]; then
  echo "用法: $0 <emoji_dir> <out_dir> [label(A|B)]"
  exit 1
fi

mkdir -p "$OUT_DIR"

# 解析IDF spiffsgen.py路径
if command -v idf.py >/dev/null 2>&1; then
  IDF_PY_DIR=$(dirname "$(command -v idf.py)")
  IDF_PATH=$(python3 - <<'PY'
import os, sys
from pathlib import Path
import subprocess
try:
    import idf_component_manager # noqa
except Exception:
    pass
print(os.environ.get('IDF_PATH',''))
PY
)
else
  echo "请先设置 ESP-IDF 环境 (idf.py)"
  exit 1
fi

SPIFFSGEN="$IDF_PATH/components/spiffs/spiffsgen.py"
if [[ ! -f "$SPIFFSGEN" ]]; then
  echo "未找到 spiffsgen.py: $SPIFFSGEN"
  exit 1
fi

# 读取分区表以获取大小（默认为 EchoEar 16m 表）
PT="partitions/v1/16m_echoear.csv"
if [[ ! -f "$PT" ]]; then
  echo "未找到分区表: $PT"
  exit 1
fi

SIZE_HEX=$(awk -F, -v lbl="assets_${LABEL}" 'tolower($1)==tolower(lbl){gsub(/ /,"",$5);print $5}' "$PT")
if [[ -z "$SIZE_HEX" ]]; then
  echo "分区表中未找到 assets_${LABEL}"
  exit 1
fi

# 将如 4000K/5M/0x大小 解析成十进制字节
parse_size(){
  local s="$1"
  if [[ "$s" =~ ^0x[0-9A-Fa-f]+$ ]]; then
    python3 - <<PY
print(int("$s",16))
PY
  elif [[ "$s" =~ [Kk]$ ]]; then
    echo $(( ${s%K} * 1024 ))
  elif [[ "$s" =~ [Mm]$ ]]; then
    echo $(( ${s%M} * 1024 * 1024 ))
  else
    echo "$s"
  fi
}

SIZE_BYTES=$(parse_size "$SIZE_HEX")
OUT_BIN="$OUT_DIR/assets_${LABEL}.bin"

echo "生成 SPIFFS 镜像: $OUT_BIN (大小: ${SIZE_BYTES} bytes)"
python3 "$SPIFFSGEN" "$SIZE_BYTES" "$EMOJI_DIR" "$OUT_BIN" --page-size 256 --obj-name-len 32 --meta-len 4

echo "完成: $OUT_BIN" 