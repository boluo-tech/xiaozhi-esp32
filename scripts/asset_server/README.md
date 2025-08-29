# EchoEar 表情资产热更新服务（MQTT + HTTP）

本文档介绍如何部署 MQTT Broker、启动资产服务（托管 `assets_A.bin`）、生成资产镜像，以及如何通过 MQTT 触发设备更新表情资产。

## 总览
- 流程：托管资产镜像 → 服务端发送 MQTT 指令 → 设备下载写入 `assets_A` → 设置 `asset_slot=A` → 重启生效。
- 固件前提：设备刷入分支 `feature/echoear-asset-hotupdate`，且分区表 `assets_A=6000K`（已更新）。

## 1. 部署 MQTT Broker（任选其一）

- macOS（Homebrew）
```bash
brew install mosquitto
brew services start mosquitto
# 自测
/opt/homebrew/bin/mosquitto_sub -h 127.0.0.1 -p 1883 -t test &
/opt/homebrew/bin/mosquitto_pub -h 127.0.0.1 -p 1883 -t test -m hi
```

- Docker
```bash
docker run -d --name mosquitto -p 1883:1883 eclipse-mosquitto:2
```

> 确保设备与服务端使用同一个 Broker；若 Broker 需鉴权，请在服务端配置 `MQTT_URL` 含用户名密码。

## 2. 部署资产服务（HTTP + MQTT）

- 路径：`scripts/asset_server`
- 安装并启动：
```bash
cd scripts/asset_server
npm install
# 替换为设备实际的 Broker 与订阅主题
PORT=8080 MQTT_URL=mqtt://127.0.0.1:1883 MQTT_TOPIC=<设备订阅主题> node server.js
```
- 接口：
  - 健康检查：GET `/api/health`
  - 上传镜像：POST `/api/upload`（form-data: `file=assets_[A|B].bin`）→ 返回可下载 URL
  - 触发更新：POST `/api/push`（JSON: `{ "url": "http://host/assets/assets_A.bin", "slot":"A" }`）

> 设备订阅主题必须与 `MQTT_TOPIC` 一致，否则设备收不到指令。

## 3. 生成资产镜像（无需编译应用）

- 在项目根目录执行：
```bash
ASSETS_LABEL=A \
ASSETS_DIR=managed_components/espressif2022__esp_emote_gfx/emoji_normal \
ASSETS_OUT=build/assets \
./build_prd.sh -a
```
- 产物：`build/assets/assets_A.bin`
- 上传至资产服务：
```bash
curl -F file=@build/assets/assets_A.bin http://<server>:8080/api/upload
# 返回示例：{"ok":true,"filename":"assets_A.bin","url":"http://<server>:8080/assets/assets_A.bin"}
```

## 4. 触发设备更新

- 方式 A（推荐）：通过资产服务转发 MQTT
```bash
curl -X POST http://<server>:8080/api/push \
  -H 'Content-Type: application/json' \
  -d '{"url":"http://<server>:8080/assets/assets_A.bin","slot":"A"}'
```

- 方式 B：直接发 MQTT（跳过资产服务）
```bash
mosquitto_pub -h <broker_host> -p 1883 -t <设备订阅主题> \
  -m '{"type":"asset_update","url":"http://<server>:8080/assets/assets_A.bin","slot":"A"}'
```

设备侧会：下载镜像 → 擦除/写入 `assets_A` → `asset_slot=A` → 重启 → 新表情生效。

## 5. 设备与分区注意事项
- 分区表：`partitions/v1/16m_echoear.csv` 已将 `assets_A` 调整为 `6000K`，当前暂不启用 `assets_B`。
- 固件：`emote_display.cc` 运行时从 NVS 读取 `asset_slot`（A/B），当前仅使用 A 槽；资产校验放宽（不依赖编译期 checksum）。
- 若后续需要 A/B 双槽切换，可再评估容量分配并恢复 `assets_B` 分区。

## 6. 常见问题
- 服务端日志反复出现 `MQTT error`
  - 检查 `MQTT_URL` 是否正确（主机、端口、鉴权）。
  - 确认 `MQTT_TOPIC` 为设备实际订阅的主题。
- 上传失败
  - 确认本地确实存在 `assets_A.bin`，且使用 form-data 字段名 `file`。
- 设备未更新
  - 设备与服务端是否连接同一 Broker？
  - 资产 URL 是否对设备可达（同局域网或公网）？
  - 设备日志是否有下载/写入/重启过程？
- 生成镜像报容量不足
  - 已将 `assets_A` 提升至 `6000K`；如仍超限，需进一步精简素材或增大分区。

## 7. 参考命令速查
```bash
# 启动 Broker（Homebrew）
brew install mosquitto && brew services start mosquitto

# 启动资产服务
cd scripts/asset_server && npm install
PORT=8080 MQTT_URL=mqtt://127.0.0.1:1883 MQTT_TOPIC=<topic> node server.js

# 生成 & 上传镜像（项目根目录）
ASSETS_LABEL=A ASSETS_DIR=managed_components/espressif2022__esp_emote_gfx/emoji_normal ASSETS_OUT=build/assets ./build_prd.sh -a
curl -F file=@build/assets/assets_A.bin http://localhost:8080/api/upload

# 触发更新
curl -X POST http://localhost:8080/api/push -H 'Content-Type: application/json' -d '{"url":"http://localhost:8080/assets/assets_A.bin","slot":"A"}'
``` 

## 8. 多租户与按客户/按设备更新（重要）

- 资产与版本（建议规范）
  - 路径：`/assets/{customer_id}/{bundle_version}/assets_A.bin`
  - 附带清单 `manifest.json`（version/size/sha256/created_at），便于校验与回滚
- MQTT 主题规范（示例）
  - 单设备命令：`tenants/{customer_id}/devices/{device_id}/commands`
  - 客户广播：`tenants/{customer_id}/broadcast/asset_update`
- 服务端触发
  - 将 `MQTT_URL` 指向生产 Broker，将 `MQTT_TOPIC` 设为设备订阅的主题（上面两类之一）
  - 发送 payload：
```json
{"type":"asset_update","url":"https://cdn.example.com/assets/acme/2025-08-29/assets_A.bin","slot":"A","version":"2025-08-29","sha256":"<hex>","size":6144000,"customer_id":"acme"}
```
- 设备如何知道有更新？
  - 设备连接 MQTT 后会订阅其专属主题；一旦服务端向该主题发布 `type=asset_update` 消息，设备立即收到并执行：下载→写入 `assets_A`→设置 `asset_slot=A`→重启。
  - 无需设备端轮询。
- 设备侧建议增强
  - NVS 记录 `asset_version`，更新后上报结果：`{"type":"asset_update_result","ok":true,"version":"...","device_id":"..."}` 