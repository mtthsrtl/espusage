# ESP Usage for GUITION 4848S040

<img width="480" height="480" alt="espusage-live" src="https://github.com/user-attachments/assets/5dae4c96-af69-479b-8966-f2922e78a7d0" />

Native, standalone firmware for the 4-inch GUITION ESP32-S3 4848S040 (480×480), providing a touch-first dark usage dashboard, local configuration portal, NVS persistence, modular HTTPS providers, OTA updates, and diagnostics.

## What is included

- Modern 480×480 LVGL dashboard with Cursor/Codex provider icons, five selectable designs, individually selectable rows, horizontal or vertical progress bars, reset-period pace markers, and status colors
- Touch operation: short tap switches the whole dashboard between `USED` and `REMAINING`; long press opens details for the selected limit or 30-minute row
- Six 5-minute activity buckets for Cursor tokens/calls and Codex weekly-limit changes
- ST7701S RGB panel, GT911 touch, 150 Hz PWM backlight, octal PSRAM, and 16 MB flash configuration
- Wi-Fi station mode plus automatic setup/recovery AP (`ESPUsage-Setup`)
- Browser-based Wi-Fi scan, network selection, password entry, and NVS-backed reset/reconfiguration
- Local web configuration at `http://espusage.local/` or the IP shown on screen
- Runtime configuration stored in ESP32 NVS; provider secrets are not compiled into firmware or returned by diagnostics
- Browser-upload OTA page, dual OTA partitions, `/api/health`, redacted `/api/status`, sanitized live `/api/usage`, and live `/api/touch` diagnostics
- Separate transport, Codex adapter, and Cursor provider modules

The display uses the 4848S040 RGB pinout: GPIO 39/48/47 for panel control, GPIO 18/17/16/21 for timing, GPIO 19/45 for GT911 touch, and GPIO 38 for the backlight. Arduino_GFX is configured with 10/8/50 horizontal and 10/8/20 vertical porch/pulse values at a 10 MHz pixel clock.

GT911 is polled every 16 ms on SDA GPIO19/SCL GPIO45 at 50 kHz, with no reset or interrupt GPIO assigned. Register selection and data reads use separate I²C transactions, and the ready flag is acknowledged before the current point buffer is read. The firmware probes both supported addresses (`0x5D` and `0x14`), logs a full I²C scan when neither responds, and retries automatically after communication loss. GPIO41/42 are SD-card pins and are not used for touch on this board.

The normal LAN web UI contains a live **Touch diagnostics** panel. It refreshes every two seconds and shows the I²C scan, detected controller/address, LVGL callback and GT911 poll counters, state-read errors, raw/display coordinates, and the separate down/up/tap/toggle counters. This makes serial access optional when diagnosing Touch after an OTA update.

The 4848S040 can leave its RGB/touch peripherals in an unusable state after a software reset. ESP Usage detects OTA and settings restarts and immediately enters a 100 ms deep sleep to force a clean hardware reset. An OTA update therefore produces two short boot sequences before the web portal becomes available again.

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

The USB upload writes the bootloader, partition table, OTA metadata, and application at their correct offsets. Existing firmware configuration is erased or ignored; keep a backup if you may want to return to it.

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

The web UI accepts a short-lived Codex access token and an optional account identifier at runtime. These values are stored in NVS and sent to the configured usage service. The firmware does not store refresh tokens or perform an OAuth refresh. Treat every access token as an account credential and replace or revoke it if the device or network may have been compromised.

The direct route is **undocumented and unsupported** and may stop working. A custom HTTPS adapter can return:

```json
{
  "plan": "Plus",
  "credits": 42.5,
  "primary": { "used_percent": 63, "elapsed_percent": 48, "reset_text": "resets in 2h 38m" },
  "secondary": { "used_percent": 27, "elapsed_percent": 71, "reset_text": "resets Monday" }
}
```

Only use an adapter you control and trust. The current implementation may forward stored authorization information to the configured destination.

The Codex section supports separate **5-hour limit** and **weekly limit** rows. A rate-limit window of up to six hours is assigned to the 5-hour row; longer windows are assigned to the weekly row. If the usage response omits either window, its row remains available but displays `--%` until that data is supplied.

The firmware deliberately does not use OpenAI's [organization Usage API](https://developers.openai.com/api/reference/resources/admin/subresources/organization/subresources/usage). That API requires an organization admin key and measures OpenAI API organization usage; it does not represent the consumer Codex/ChatGPT subscription gauges shown here.

### Cursor

Cursor officially documents an [Admin API](https://docs.cursor.com/en/account/teams/admin-api) for Team and Enterprise administration. Personal usage support in this firmware relies on an undocumented session-based endpoint that may change without notice. The required runtime token is stored in ESP32 NVS; treat it as an account credential and revoke it if the device or network may have been compromised.

For **Last 30 min**, the firmware also reads the personal dashboard's undocumented `POST /api/dashboard/get-filtered-usage-events` route. It processes timestamp, model, call kind/Max Mode, input/output/cache-read/cache-write tokens, and cost when present. Results include total tokens, calls, token split, cost, top model, and six 5-minute buckets. Pages are read in groups of 50 and capped at 500 events; reaching the cap or losing a later page marks the result as partial. If only some events expose tokens, a `+` is shown after the token total; if none do, the six bars fall back to call counts. The web UI states the token coverage explicitly.

The display maps the currently observed response fields as follows:

- Cursor Models: `individualUsage.plan.autoPercentUsed`
- Other Models: `individualUsage.plan.apiPercentUsed`
- On Demand: percentage calculated from `individualUsage.onDemand.used` and `.limit` (with `teamUsage.onDemand` as a fallback)

All three Cursor limits use `billingCycleStart` and `billingCycleEnd` for the remaining reset time and white elapsed-period marker. Codex uses the rate-limit windows' duration and reset time for the same marker; adapters can supply `elapsed_percent`. Under **Display and status**, choose **Panels** for framed provider sections, **Flat** for open sections, **Telemetry** for layered signal cards, **Matrix** for independent Cursor and Codex columns, or **Vertical** for top-to-bottom limit columns on subtly tinted provider halves. Each Cursor limit, both 30-minute rows, and both Codex limit rows can be enabled separately; every design expands automatically to use the 480×480 display.

In `USED` mode a normal bar fills from the left with the consumed percentage. In `REMAINING` mode the displayed value is `100 − used` and the bar fills from the right; for example, 10% remaining occupies the rightmost 10%. The white pace marker is read as the remaining time from the right in that mode. The preferred mode can be selected persistently in the web UI, so Touch is not required. A short tap is recognized directly from the GT911 press/release gesture and temporarily switches modes until restart, independent of LVGL object hit-testing. The activity mini-charts always show consumption and are never inverted. A long press on a limit opens Used, Remaining, Reset, elapsed period, and provider status; long-pressing a 30-minute row opens Cursor token/call details or Codex measurement buckets.

Limit status always uses the consumed percentage in both display modes. Its priority is **Critical → Warning → Overpace → OK**. Critical and Warning use the configured consumption thresholds. Below those thresholds, Overpace appears when the consumed percentage is greater than the elapsed reset-period percentage shown by the white marker; Warning or Critical replaces Overpace as soon as its threshold is reached.

**Display Off Time** can switch the backlight off every day between a configured start and end time. The schedule uses Europe/Berlin local time including daylight-saving changes and can cross midnight. Touch remains active while the backlight is off: the first touch wakes the display without changing the Used/Remaining view, keeps it on for 60 seconds, and then returns to the off schedule. The settings page uses two equal columns on wider screens and automatically stacks them on phones.

## Security notes

- The currently tracked tree contains no known provider token. `.gitignore` excludes local configuration, common secret files, and build output. Git history must be reviewed separately before publishing a previously private repository.
- The HTTP web portal has no authentication. Every device on the same network can access diagnostics, change settings, replace provider URLs, reconfigure Wi-Fi, and submit OTA firmware.
- The setup/recovery access point currently has no WPA password. Use it only in a controlled location and disable or secure it before deploying the device elsewhere.
- Provider traffic requires `https://`, but the client currently calls `setInsecure()` and does not validate the server certificate. Encryption without certificate verification does not prevent an active network attacker from impersonating the destination.
- Custom provider URLs are security-sensitive: stored authorization or session information may be sent to the configured host. Use only fixed, trusted destinations.
- Wi-Fi passwords, provider tokens, and account identifiers are stored in NVS without application-level encryption. Physical flash access can expose them unless platform security features are enabled.
- OTA images are not authenticated by this application. Because the OTA endpoint is also unauthenticated, a network participant can replace the firmware with another valid ESP32 image.
- There is no CSRF or Origin protection. Do not browse untrusted sites while the device portal is reachable from the same browser/network.
- Diagnostics do not intentionally return raw credential values, but they expose operational and usage information. `/api/screenshot` exposes the complete current display.
- For production deployment, add portal authentication, signed OTA verification, certificate validation, a protected setup AP, NVS encryption, Secure Boot, and Flash Encryption.

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
| `/api/usage` | GET | Current sanitized provider limits and 30-minute buckets; does not intentionally include stored secrets |
| `/api/usage/refresh` | GET/POST | Queue an immediate refresh of Codex and Cursor limits |
| `/api/touch` | GET | Live I²C, GT911, coordinate, and gesture diagnostics; does not intentionally include stored secrets |
| `/api/display?mode=toggle\|used\|remaining` | GET | Change the displayed usage mode |
| `/api/display/toggle` | GET/POST | Toggle the physical display backlight and return its new state |
| `/api/config` | POST | Save settings to NVS and restart |
| `/api/wifi/scan` | GET | Scan nearby Wi-Fi networks |
| `/api/wifi` | POST | Save selected Wi-Fi credentials and restart |
| `/api/wifi` | DELETE | Delete Wi-Fi credentials and restart in setup mode |
| `/api/ota` | POST | Upload an application `firmware.bin` |

## License

MIT.

