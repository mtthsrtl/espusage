# ESP Usage for GUITION 4848S040

Native, standalone firmware for the 4-inch GUITION ESP32-S3 4848S040 (480×480). It replaces ESPHome/EspControl and provides a touch-first dark usage dashboard, local configuration portal, NVS persistence, modular HTTPS providers, OTA updates, and diagnostics.

## What is included

- Modern 480×480 LVGL dashboard with Cursor/Codex provider icons, selectable framed or flat design, individually selectable rows, wide progress bars, reset-period pace markers, and status colors
- Touch operation: short tap switches the whole dashboard between `USED` and `REMAINING`; long press opens details for the selected limit or 30-minute row
- Six 5-minute activity buckets for Cursor tokens/calls and Codex weekly-limit changes
- ST7701S RGB panel, GT911 touch, 150 Hz PWM backlight, octal PSRAM, and 16 MB flash configuration
- Wi-Fi station mode plus automatic setup/recovery AP (`ESPUsage-Setup`)
- Browser-based Wi-Fi scan, network selection, password entry, and NVS-backed reset/reconfiguration
- Local web configuration at `http://espusage.local/` or the IP shown on screen
- Credentials saved only at runtime in ESP32 NVS; no secrets are compiled into the firmware or returned by diagnostics
- Browser-upload OTA page, dual OTA partitions, `/api/health`, redacted `/api/status`, credential-free live `/api/usage`, and live `/api/touch` diagnostics
- Separate transport, Codex adapter, and Cursor provider modules

The display pinout is based on the working [EspControl 4848S040 hardware definition](https://github.com/jtenniswood/espcontrol/blob/main/devices/guition-esp32-s3-4848s040/device/device.yaml): RGB data pins, GPIO 39/48/47 panel control, GPIO 18/17/16/21 timing, GPIO 19/45 GT911, and GPIO 38 backlight. The Arduino_GFX timings use the field-tested 10/8/50 horizontal and 10/8/20 vertical porch/pulse values and the deliberate EspControl compromise of a 10 MHz pixel clock.

Touch follows the same board-specific setup as EspControl: GT911 is polled every 16 ms on SDA GPIO19/SCL GPIO45 at the ESPHome default of 50 kHz, with no reset or interrupt GPIO assigned. Register selection and data reads use separate I²C transactions, and the ready flag is acknowledged before the current point buffer is read, matching ESPHome's GT911 driver. The firmware probes both supported addresses (`0x5D` and `0x14`), logs a full I²C scan when neither responds, and retries automatically after communication loss. GPIO41/42 are not touch pins on this board. EspControl's separate GSL3680 component is used by other Guition models and is not the touchscreen configuration for the 4848S040.

The normal LAN web UI contains a live **Touch diagnostics** panel. It refreshes every two seconds and shows the I²C scan, detected controller/address, LVGL callback and GT911 poll counters, state-read errors, raw/display coordinates, and the separate down/up/tap/toggle counters. This makes serial access optional when diagnosing Touch after an OTA update.

The 4848S040 can leave its RGB/touch peripherals in an unusable state after a software reset. Matching EspControl's board workaround, ESP Usage detects OTA and settings restarts and immediately enters a 100 ms deep sleep to force a clean hardware reset. An OTA update therefore produces two short boot sequences before the web portal becomes available again.

## Important: first installation from EspControl

Do **not** install this firmware through EspControl's existing web updater. EspControl and ESP Usage can use different partition tables and OTA metadata. An application-only upload may boot-loop or leave no valid fallback image. The first switch must be done once over USB-C; subsequent ESP Usage updates can use the built-in web updater.

### Windows USB-C flash (exact steps)

1. Install [Visual Studio Code](https://code.visualstudio.com/) and its [PlatformIO IDE extension](https://platformio.org/install/ide?install=vscode).
2. Download/clone this repository and open its folder in VS Code. Create your private local configuration once:

   ```powershell
   Copy-Item platformio.ini.example platformio.ini
   ```

   `platformio.ini` is ignored by Git. Wi-Fi credentials are never build flags and are entered only through the device setup portal.
3. Connect the 4848S040 to the computer using a **USB-C data cable**. Disconnect other serial ESP boards.
4. Let Windows finish installing the USB serial driver. In Device Manager, note the new COM port (for example `COM7`).
5. In `platformio.ini`, add this line below `upload_speed` if automatic port detection chooses incorrectly:

   ```ini
   upload_port = COM7
   ```

6. Start the first upload with PlatformIO's **Upload** task (bottom status bar arrow), or open the PlatformIO terminal in the repository and run:

   ```powershell
   & "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e guition-4848s040 -t upload
   ```

   For the requested Windows port `COM8`, the exact command is:

   ```powershell
   & "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e guition-4848s040 -t upload --upload-port COM8
   ```

7. If the connection times out: hold **BOOT**, briefly press **RESET**, release **BOOT** after one second, and run Upload again. Some enclosures expose only reset; in that case unplug USB, hold BOOT while reconnecting, then release it.
8. Wait for `SUCCESS`. Power-cycle the panel once if the screen remains blank after the automatic reset.
9. Join the Wi-Fi network **ESPUsage-Setup** and browse to `http://192.168.4.1/`. Click **Scan again**, select your Wi-Fi network, enter its password, and choose **Save WiFi & restart**. After restart, open the IP shown on the display or `http://espusage.local/`.

If the saved network cannot be reached after three connection attempts, the device automatically starts **ESPUsage-Setup** again. The same Wi-Fi scanner is available later under **Settings / WiFi** in the normal LAN web UI. **Delete WiFi configuration** removes only the Wi-Fi credentials from NVS and restarts the setup AP; provider and display settings remain intact.

The USB upload writes the bootloader, partition table, OTA metadata, and application at their correct offsets. Existing EspControl configuration is erased/ignored; keep a backup if you may want to return to it.

### Later OTA updates

1. Build a new image using PlatformIO **Build**, or:

   ```powershell
   & "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e guition-4848s040
   ```

2. Open `http://espusage.local/`, go to **Firmware update**, and select:

   ```text
   .pio/build/guition-4848s040/firmware.bin
   ```

3. Click **Install OTA** and keep the panel powered until it restarts. Never upload `bootloader.bin` or `partitions.bin` in the OTA form.

If OTA is interrupted, the ESP32 bootloader retains the previously valid OTA slot. USB recovery remains available.

## Usage data sources and limitations

### Codex

OpenAI documents where users can view Codex limits and credits (Codex Settings → Usage), but does not document a public consumer REST endpoint for retrieving the five-hour/weekly ChatGPT Codex gauges. See [Using Codex with your ChatGPT plan](https://help.openai.com/en/articles/11369540) and the [Codex rate card](https://help.openai.com/en/articles/20001106).

The web UI can use the Codex app OAuth credentials stored locally in `%USERPROFILE%\.codex\auth.json`. Copy `tokens.access_token` into **Codex access_token** and, when present, copy `tokens.account_id` into **ChatGPT account_id**. With the adapter URL left empty, the firmware sends these values to the same unofficial read-only usage route used by the local companion widget. Access tokens expire; this ESP32 firmware deliberately does not store a refresh token or perform an OAuth refresh. Replace the access token in the web UI when the display reports HTTP 401.

This direct route is **undocumented and unsupported** and may stop working. As an alternative, configure a trusted HTTPS adapter URL that returns:

```json
{
  "plan": "Plus",
  "credits": 42.5,
  "primary": { "used_percent": 63, "elapsed_percent": 48, "reset_text": "resets in 2h 38m" },
  "secondary": { "used_percent": 27, "elapsed_percent": 71, "reset_text": "resets Monday" }
}
```

The adapter URL and optional bearer token are runtime-only NVS values.

The Codex section shows the weekly limit plus an optional **Last 30 min** row. The latter is calculated locally from changes to the weekly percentage and displays their total in percentage points, for example `+7.00 PP` when the used weekly value rises from 20% to 27%. The first successful request establishes a `COLLECTING 1/2` baseline; the value appears after the second successful measurement. Six five-minute buckets remain in RAM and are cleared after reboot or a detected weekly reset. The Codex five-hour row remains removed.

The firmware deliberately does not use OpenAI's [organization Usage API](https://developers.openai.com/api/reference/resources/admin/subresources/organization/subresources/usage). That API requires an organization admin key and measures OpenAI API organization usage; it does not represent the consumer Codex/ChatGPT subscription gauges shown here.

### Cursor

Cursor officially documents an [Admin API](https://docs.cursor.com/en/account/teams/admin-api) for team usage. It requires a Team/Enterprise admin API key. For an individual account, this firmware can instead use the session/auth token with `GET https://cursor.com/api/usage-summary`. On Windows, `E:\Cursor_Usage` reads it from `%APPDATA%\Cursor\User\globalStorage\state.vscdb`, table `ItemTable`, key `cursorAuth/accessToken`. The firmware accepts either that raw JWT or an existing `sub::JWT`/`WorkosCursorSessionToken` value. This endpoint and cookie flow are **undocumented and unsupported by Cursor** and may change without notice. The token is stored only in ESP32 NVS.

For **Last 30 min**, the firmware also reads the personal dashboard's undocumented `POST /api/dashboard/get-filtered-usage-events` route. It processes timestamp, model, call kind/Max Mode, input/output/cache-read/cache-write tokens, and cost when present. Results include total tokens, calls, token split, cost, top model, and six 5-minute buckets. Pages are read in groups of 50 and capped at 500 events; reaching the cap or losing a later page marks the result as partial. If only some events expose tokens, a `+` is shown after the token total; if none do, the six bars fall back to call counts. The web UI states the token coverage explicitly.

The display maps the currently observed response fields as follows:

- Cursor Models: `individualUsage.plan.autoPercentUsed`
- Other Models: `individualUsage.plan.apiPercentUsed`
- On Demand: percentage calculated from `individualUsage.onDemand.used` and `.limit` (with `teamUsage.onDemand` as a fallback)

All three Cursor limits use `billingCycleStart` and `billingCycleEnd` for the remaining reset time and white elapsed-period marker. Codex uses the weekly rate-limit window's duration and reset time for the same marker; adapters can supply `elapsed_percent`. Under **Display and status**, choose **Panels** for framed provider sections or **Flat** for open sections with a neutral divider, wider bars, and larger typography. Each Cursor limit, both 30-minute rows, and the Codex weekly row can be enabled separately; the layout expands automatically to use the 480×480 display.

In `USED` mode a normal bar fills from the left with the consumed percentage. In `REMAINING` mode the displayed value is `100 − used` and the bar fills from the right; for example, 10% remaining occupies the rightmost 10%. The white pace marker is read as the remaining time from the right in that mode. The preferred mode can be selected persistently in the web UI, so Touch is not required. A short tap is recognized directly from the GT911 press/release gesture and temporarily switches modes until restart, independent of LVGL object hit-testing. The activity mini-charts always show consumption and are never inverted. A long press on a limit opens Used, Remaining, Reset, elapsed period, and provider status; long-pressing a 30-minute row opens Cursor token/call details or Codex measurement buckets.

**Display Off Time** can switch the backlight off every day between a configured start and end time. The schedule uses Europe/Berlin local time including daylight-saving changes and can cross midnight. Touch remains active while the backlight is off: the first touch wakes the display without changing the Used/Remaining view, keeps it on for 60 seconds, and then returns to the off schedule. The settings page uses two equal columns on wider screens and automatically stacks them on phones.

## Security notes

- `.gitignore` excludes the local `platformio.ini`, common secret files, and build output. `platformio.ini.example` contains placeholders only. There are no credentials or tokens in this repository.
- The web portal is HTTP on the local LAN. Use a trusted home/office network; the setup AP is intended only for initial provisioning.
- Provider traffic requires an `https://` URL. The current small-device client encrypts transport but does not yet pin/validate a CA certificate (`setInsecure()`); this limitation is explicit in `HttpJson.cpp`. Do not expose the device to an untrusted network.
- Diagnostics expose booleans such as `codex_configured`, never credential values.
- Codex and Cursor access tokens grant account access. Never share them, commit them, or paste them into serial logs.
- For production hardening, enable ESP32 NVS encryption and Secure Boot/Flash Encryption during provisioning.

## Build

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e guition-4848s040
```

Tested with PlatformIO `espressif32@6.12.0`, Arduino-ESP32 2.0.17, Arduino_GFX 1.6.0, LVGL 8.4.0, and ArduinoJson 7.4.2. GT911 communication is implemented directly over Arduino `Wire` to match this board's pinless reset/interrupt configuration.

## HTTP endpoints

| Endpoint | Method | Purpose |
|---|---:|---|
| `/` | GET | Configuration and OTA UI |
| `/api/health` | GET | Minimal liveness response |
| `/api/status` | GET | Redacted runtime/debug status |
| `/api/usage` | GET | Current sanitized provider limits and 30-minute buckets; never credentials |
| `/api/touch` | GET | Live I²C, GT911, coordinate, and gesture diagnostics; never credentials |
| `/api/config` | POST | Save settings to NVS and restart |
| `/api/wifi/scan` | GET | Scan nearby Wi-Fi networks |
| `/api/wifi` | POST | Save selected Wi-Fi credentials and restart |
| `/api/wifi` | DELETE | Delete Wi-Fi credentials and restart in setup mode |
| `/api/ota` | POST | Upload an application `firmware.bin` |

## License

MIT. Hardware pin/timing attribution is retained above; no EspControl/ESPHome source code is included.

