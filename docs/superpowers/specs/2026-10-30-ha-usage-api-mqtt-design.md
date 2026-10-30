# Home Assistant Usage API + MQTT Discovery – Design

## Goal
Expose Cursor/Codex usage and device meta from ESP Usage so Home Assistant (and other clients) query the device once instead of Cursor/Codex APIs.

## Decisions
- HTTP + MQTT Discovery (ESP as MQTT client to existing broker)
- Many individual sensors (flat keys)
- HTTP usage endpoints remain open on LAN (no API key)
- Scope: Cursor + Codex usage + device meta; no Bambu print sensors
- Shared flat sensor registry feeds both HTTP and MQTT

## Architecture
1. **HaSensors** builds a flat key→value list from `UsageSnapshot` (cursor/codex) + device meta.
2. **`GET /api/ha/sensors`** returns all sensors as JSON object.
3. **`GET /api/ha/cursor`**, **`GET /api/ha/codex`**, **`GET /api/ha/device`** return filtered subsets.
4. Existing **`GET /api/usage`** stays nested and unchanged.
5. **HaMqtt** connects to configured broker, publishes state topics and Home Assistant MQTT Discovery configs.
6. Publish on each successful usage refresh and periodically (discovery retain, state update).

## Sensor keys (stable)
### Device
- `ready` (bool), `ip`, `hostname`, `uptime_ms`, `free_heap`, `rssi`, `display_mode`, `display_on`, `refresh_minutes`, `usage_age_ms`, `version`

### Cursor
- `cursor_ok`, `cursor_status`, `cursor_plan`, `cursor_updated`
- `cursor_models_used_percent`, `cursor_models_elapsed_percent`, `cursor_models_reset`
- `cursor_other_used_percent`, `cursor_other_elapsed_percent`, `cursor_other_reset`
- `cursor_ondemand_used_percent`, `cursor_ondemand_elapsed_percent`, `cursor_ondemand_used_amount`, `cursor_ondemand_limit_amount`, `cursor_ondemand_currency`, `cursor_ondemand_reset`
- `cursor_30m_available`, `cursor_30m_partial`, `cursor_30m_calls`, `cursor_30m_tokenized_calls`, `cursor_30m_max_mode_calls`, `cursor_30m_tokens`, `cursor_30m_input_tokens`, `cursor_30m_output_tokens`, `cursor_30m_cache_read_tokens`, `cursor_30m_cache_write_tokens`, `cursor_30m_cost_cents`, `cursor_30m_top_model`, `cursor_30m_top_kind`, `cursor_30m_status`

### Codex
- `codex_ok`, `codex_status`, `codex_plan`, `codex_updated`, `codex_credits`
- `codex_5h_used_percent`, `codex_5h_elapsed_percent`, `codex_5h_reset`
- `codex_weekly_used_percent`, `codex_weekly_elapsed_percent`, `codex_weekly_reset`
- `codex_30m_available`, `codex_30m_ready`, `codex_30m_samples`, `codex_30m_delta_pp`, `codex_30m_status`

Missing numeric values publish as empty / unavailable (HA: empty payload = unavailable when configured).

## MQTT
### Config (NVS / Web UI)
- `mqtt_on`, `mqtt_host`, `mqtt_port` (default 1883), `mqtt_user`, `mqtt_password` (empty keeps), `mqtt_prefix` (default `espusage`), `mqtt_discovery` (default true)

### Topics
- State: `{prefix}/{hostname}/{sensor_key}`
- Discovery: `homeassistant/sensor/{hostname}/{sensor_key}/config` (binary_sensor for bools where appropriate)
- Availability: `{prefix}/{hostname}/status` = `online`/`offline` (LWT offline)

### Client
- Separate `PubSubClient` + `WiFiClient` from Bambu MQTT (no shared instance)
- Plain TCP by default (LAN Mosquitto); optional TLS out of scope for v1
- Reconnect with backoff in `loop()`
- Discovery retained; republish discovery every 30 minutes or on connect

## HTTP examples
```json
GET /api/ha/sensors
{
  "ready": true,
  "cursor_models_used_percent": 42.5,
  "codex_5h_used_percent": 10,
  "ip": "192.168.1.50"
}
```

## Non-goals
- Bambu sensors via HA API
- MQTT TLS / mTLS
- Authenticated HTTP for usage reads
- Individual 5-minute bucket sensors (avoid entity spam)

## Security
- Secrets never in HTTP responses; MQTT password only in NVS / form (empty keeps)
- Status API exposes `mqtt_configured`, `mqtt_connected`, host/port/prefix — not password
