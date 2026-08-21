# ESP Usage for GUITION 4848S040

Native, standalone firmware for the 4-inch GUITION ESP32-S3 4848S040 (480×480). It replaces ESPHome/EspControl and provides a touch-first dark usage dashboard, local configuration portal, NVS persistence, modular HTTPS providers, OTA updates, and diagnostics.

## What is included

- Modern 480×480 LVGL dashboard with Codex and Cursor cards, progress bars, status colors, and tap-to-open detail overlays
- ST7701S RGB panel, GT911 touch, 150 Hz PWM backlight, octal PSRAM, and 16 MB flash configuration
- Wi-Fi station mode plus setup/recovery AP (`ESPUsage-Setup` or `ESPUsage-Recovery`)
- Local web configuration at `http://espusage.local/` or the IP shown on screen
- Credentials saved only at runtime in ESP32 NVS; no secrets are compiled into the firmware or returned by diagnostics
- Browser-upload OTA page, dual OTA partitions, `/api/health`, and redacted `/api/status`
- Separate transport, Codex adapter, and Cursor provider modules

The display pinout is based on the working [EspControl 4848S040 hardware definition](https://github.com/jtenniswood/espcontrol/blob/main/devices/guition-esp32-s3-4848s040/device/device.yaml): RGB data pins, GPIO 39/48/47 panel control, GPIO 18/17/16/21 timing, GPIO 19/45 GT911, and GPIO 38 backlight. The Arduino_GFX timings use the field-tested 10/8/50 horizontal and 10/8/20 vertical porch/pulse values.

## Important: first installation from EspControl

Do **not** install this firmware through EspControl's existing web updater. EspControl and ESP Usage can use different partition tables and OTA metadata. An application-only upload may boot-loop or leave no valid fallback image. The first switch must be done once over USB-C; subsequent ESP Usage updates can use the built-in web updater.

### Windows USB-C flash (exact steps)

1. Install [Visual Studio Code](https://code.visualstudio.com/) and its [PlatformIO IDE extension](https://platformio.org/install/ide?install=vscode).
2. Download/clone this repository and open its folder in VS Code. Create your private local configuration once:

   ```powershell
   Copy-Item platformio.ini.example platformio.ini
   ```

   `platformio.ini` is ignored by Git. COM ports and optional local Wi-Fi build values therefore remain only on your computer.
3. Connect the 4848S040 to the computer using a **USB-C data cable**. Disconnect other serial ESP boards.
4. Let Windows finish installing the USB serial driver. In Device Manager, note the new COM port (for example `COM7`).
5. In `platformio.ini`, add this line below `upload_speed` if automatic port detection chooses incorrectly:

   ```ini
   upload_port = COM7
   ```

6. Start the first upload with PlatformIO's **Upload** task (bottom status bar arrow), or open the PlatformIO terminal in the repository and run:

   ```powershell
   pio run -e guition-4848s040 -t upload
   ```

7. If the connection times out: hold **BOOT**, briefly press **RESET**, release **BOOT** after one second, and run Upload again. Some enclosures expose only reset; in that case unplug USB, hold BOOT while reconnecting, then release it.
8. Wait for `SUCCESS`. Power-cycle the panel once if the screen remains blank after the automatic reset.
9. Join the Wi-Fi network **ESPUsage-Setup**, browse to `http://192.168.4.1/`, enter your Wi-Fi settings, and save. After restart, open the IP shown on the display or `http://espusage.local/`.

The USB upload writes the bootloader, partition table, OTA metadata, and application at their correct offsets. Existing EspControl configuration is erased/ignored; keep a backup if you may want to return to it.

### Later OTA updates

1. Build a new image using PlatformIO **Build**, or:

   ```powershell
   pio run -e guition-4848s040
   ```

2. Open `http://espusage.local/`, go to **Firmware update**, and select:

   ```text
   .pio/build/guition-4848s040/firmware.bin
   ```

3. Click **Install OTA** and keep the panel powered until it restarts. Never upload `bootloader.bin` or `partitions.bin` in the OTA form.

If OTA is interrupted, the ESP32 bootloader retains the previously valid OTA slot. USB recovery remains available.

## Usage data sources and limitations

### Codex

OpenAI documents where users can view Codex limits and credits (Codex Settings → Usage), but does not document a consumer REST endpoint for retrieving the five-hour/weekly ChatGPT Codex gauges. See [Using Codex with your ChatGPT plan](https://help.openai.com/en/articles/11369540) and the [Codex rate card](https://help.openai.com/en/articles/20001106).

For that reason this project deliberately does **not** embed a scraped ChatGPT endpoint, browser cookie, or undocumented session flow. Configure a trusted HTTPS adapter that returns:

```json
{
  "plan": "Plus",
  "credits": 42.5,
  "primary": { "used_percent": 63, "reset_text": "resets in 2h 38m" },
  "secondary": { "used_percent": 27, "reset_text": "resets Monday" }
}
```

The adapter URL and optional bearer token are runtime-only NVS values. Any future direct ChatGPT endpoint must be treated as **undocumented/unsupported** until OpenAI publishes it.

### Cursor

Cursor officially documents an [Admin API](https://cursor.com/docs/account/teams/admin-api) for team usage. It requires a Team/Enterprise admin API key. For an individual account, this firmware can instead use the session/auth token with `GET https://cursor.com/api/usage-summary`. That endpoint and cookie flow are **undocumented and unsupported by Cursor** and may change without notice. The token is stored only in ESP32 NVS.

The display maps the currently observed response fields as follows:

- Cursor Models: `individualUsage.plan.totalPercentUsed` (falls back to the larger available sub-limit)
- Auto Models: `individualUsage.plan.autoPercentUsed`
- API Usage: `individualUsage.plan.apiPercentUsed`

All three Cursor cards use `billingCycleEnd` for the remaining reset time. The implementation is isolated in `src/providers/CursorProvider.cpp` so a future schema change does not affect the display, storage, or Codex provider.

## Security notes

- `.gitignore` excludes the local `platformio.ini`, common secret files, and build output. `platformio.ini.example` contains placeholders only. There are no credentials or tokens in this repository.
- The web portal is HTTP on the local LAN. Use a trusted home/office network; the setup AP is intended only for initial provisioning.
- Provider traffic requires an `https://` URL. The current small-device client encrypts transport but does not yet pin/validate a CA certificate (`setInsecure()`); this limitation is explicit in `HttpJson.cpp`. Do not expose the device to an untrusted network.
- Diagnostics expose booleans such as `codex_configured`, never credential values.
- For production hardening, enable ESP32 NVS encryption and Secure Boot/Flash Encryption during provisioning.

## Build

```powershell
pio run -e guition-4848s040
```

Tested with PlatformIO `espressif32@6.12.0`, Arduino-ESP32 2.0.17, Arduino_GFX 1.6.0, LVGL 8.4.0, TAMC_GT911 1.0.2, and ArduinoJson 7.4.2.

## HTTP endpoints

| Endpoint | Method | Purpose |
|---|---:|---|
| `/` | GET | Configuration and OTA UI |
| `/api/health` | GET | Minimal liveness response |
| `/api/status` | GET | Redacted runtime/debug status |
| `/api/config` | POST | Save settings to NVS and restart |
| `/api/ota` | POST | Upload an application `firmware.bin` |

## License

MIT. Hardware pin/timing attribution is retained above; no EspControl/ESPHome source code is included.

