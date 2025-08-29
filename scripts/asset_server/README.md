# EchoEar Asset Server

Simple server to host `assets_A.bin` / `assets_B.bin` and publish MQTT `asset_update` commands.

## Setup

```bash
cd scripts/asset_server
npm install
cp .env.example .env   # edit MQTT_URL / MQTT_TOPIC if needed
npm run start
```

Default:
- HTTP: http://localhost:8080
- Static assets: GET /assets/*
- Upload: POST /api/upload (form-data: file=assets_[A|B].bin)
- Push: POST /api/push (json: {url, slot?: 'A'|'B'})

## Generate assets bin
Use project script:
```bash
ASSETS_LABEL=B ASSETS_DIR=managed_components/espressif2022__esp_emote_gfx/emoji_normal ASSETS_OUT=build/assets ./build_prd.sh -a
# then upload
curl -F file=@build/assets/assets_B.bin http://localhost:8080/api/upload
```

## Trigger device update
```bash
curl -X POST http://localhost:8080/api/push \
  -H 'Content-Type: application/json' \
  -d '{"url":"http://<server>/assets/assets_B.bin","slot":"B"}'
```

Devices will download to selected slot, set active slot in NVS, and reboot. 