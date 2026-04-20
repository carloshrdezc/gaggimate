# Flashing the ESP32 Display

This page describes how to build and flash the GaggiMate display firmware and web UI to the ESP32.

## Prerequisites

- PlatformIO CLI (`pio`)
- Node.js and npm (for building the web UI)
- ESP32 connected via USB

## One-Command Flash

The fastest way to build and flash everything:

```bash
./flash.sh <port>
```

Example:
```bash
./flash.sh COM4
```

This runs all steps automatically:
1. Build web UI (`npm run build`)
2. Sync `web/dist/` → `data/w/` (SPIFFS source) + gzip assets
3. Build firmware
4. Upload firmware
5. Build SPIFFS
6. Upload SPIFFS

## Manual Steps

If you prefer to run steps manually, or if the web UI is not updating after flashing:

### 1. Build Web UI

```bash
cd web
npm run build
cd ..
```

### 2. Sync to SPIFFS Source

**Critical**: The SPIFFS filesystem is built from `data/w/`, not `web/dist/`. After every web build, you must sync:

```bash
rm -rf data/w
mkdir -p data/w
cp -R web/dist/* data/w/
gzip -f data/w/assets/*.js
gzip -f data/w/assets/*.css
gzip -f data/w/*.html
```

### 3. Build and Upload Firmware

```bash
pio run -e display -t upload --upload-port <port>
```

### 4. Build and Upload SPIFFS

```bash
pio run -e display -t buildfs
pio run -e display -t uploadfs --upload-port <port>
```

## Troubleshooting

### Web UI not updating after flash

This usually means `data/w/` was not synced from `web/dist/` before building SPIFFS. Repeat **step 2** and rebuild SPIFFS (**step 4**).

### Browser caching old web UI

After flashing, hard refresh the browser: `Ctrl+Shift+R` (or `Cmd+Shift+R` on Mac).

### Find the correct COM port

```bash
pio device list
```
