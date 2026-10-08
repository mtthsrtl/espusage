# Bambu A1 Print Screen – Design

## Goal
Separate physical display screens for AI usage and Bambu Lab A1 print status. No mixed content. Switch via open HTTP API. Boot always starts on usage.

## Screens
- **usage**: existing Cursor/Codex dashboard only
- **print**: full-screen printer status only (state, %, remaining time, nozzle/bed temps, filename, layer, filament/AMS)

## API
- `GET /api/view` → `{ok, screen, bambu_enabled, bambu_mode, mqtt_connected}`
- `GET /api/view?screen=usage|print|toggle` → switch (no api_key)
- Boot default: `usage` (not persisted)
- Leaving print disconnects MQTT immediately; entering print connects MQTT

## Config (Web UI → NVS)
- `bambu_on`, `bambu_mode` (`local`|`cloud`), `bambu_host`, `bambu_serial`
- Local: `bambu_code` (access code)
- Cloud: `bambu_user`, `bambu_token`, `bambu_region` (`global`|`cn`)
- Status exposes only configured/stored flags, never secrets

## Transport
- MQTT/TLS :8883 for both modes
- Local: host=IP, user=`bblp`, password=access code, insecure TLS
- Cloud: `us.mqtt.bambulab.com` / `cn.mqtt.bambulab.com`, user=`u_{id}`, password=token
- Topics: `device/{serial}/report` + `.../request` with `pushall`

## Non-goals
- Print control / start-stop
- Camera stream
- Mixing printer widgets into usage layouts
