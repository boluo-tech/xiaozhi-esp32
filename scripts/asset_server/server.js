#!/usr/bin/env node
'use strict'

const path = require('path')
const fs = require('fs')
const express = require('express')
const multer = require('multer')
const mqtt = require('mqtt')
require('dotenv').config()

const PORT = process.env.PORT || 8080
const MQTT_URL = process.env.MQTT_URL || 'mqtt://localhost:1883'
const MQTT_TOPIC = process.env.MQTT_TOPIC || 'xiaozhi/asset_update'
const PUBLIC_DIR = process.env.PUBLIC_DIR || path.join(__dirname, 'public')

fs.mkdirSync(PUBLIC_DIR, { recursive: true })

// MQTT client
const client = mqtt.connect(MQTT_URL)
client.on('connect', () => {
  console.log(`[MQTT] connected: ${MQTT_URL}`)
})
client.on('error', (err) => {
  console.error('[MQTT] error:', err.message)
})

const app = express()
app.use(express.json())
app.use('/assets', express.static(PUBLIC_DIR, { fallthrough: false, etag: true, maxAge: '1y' }))

// Upload handling
const storage = multer.diskStorage({
  destination: (req, file, cb) => cb(null, PUBLIC_DIR),
  filename: (req, file, cb) => {
    // enforce filename: assets_A.bin or assets_B.bin
    const name = file.originalname
    if (!/^assets_[ABab]\.bin$/.test(name)) return cb(new Error('Invalid filename. Expect assets_A.bin or assets_B.bin'))
    cb(null, name)
  }
})
const upload = multer({ storage })

// Upload endpoint
app.post('/api/upload', upload.single('file'), (req, res) => {
  if (!req.file) return res.status(400).json({ ok: false, error: 'file required' })
  const url = `${req.protocol}://${req.get('host')}/assets/${req.file.filename}`
  return res.json({ ok: true, filename: req.file.filename, url })
})

// Publish asset_update to MQTT
// body: { url: string, slot?: 'A'|'B' }
app.post('/api/push', async (req, res) => {
  const { url, slot } = req.body || {}
  if (typeof url !== 'string' || !url.startsWith('http')) {
    return res.status(400).json({ ok: false, error: 'invalid url' })
  }
  const payload = slot && (slot === 'A' || slot === 'B')
    ? { type: 'asset_update', url, slot }
    : { type: 'asset_update', url }
  try {
    client.publish(MQTT_TOPIC, JSON.stringify(payload), { qos: 1 }, (err) => {
      if (err) return res.status(500).json({ ok: false, error: err.message })
      return res.json({ ok: true, topic: MQTT_TOPIC, payload })
    })
  } catch (e) {
    return res.status(500).json({ ok: false, error: e.message })
  }
})

app.get('/api/health', (req, res) => res.json({ ok: true }))

app.listen(PORT, () => {
  console.log(`[HTTP] listening on http://localhost:${PORT}`)
  console.log(`[HTTP] static assets: GET /assets/* from ${PUBLIC_DIR}`)
  console.log(`[API ] upload: POST /api/upload (form-data: file=assets_[A|B].bin)`)  
  console.log(`[API ] push:   POST /api/push   (json: {url, slot?: 'A'|'B'})`)
}) 