# ESP8266 living room controller



C++/PlatformIO replacement for `esp8266.yaml`, targeting NodeMCU v2 with two daisy-chained 74HC595s, 16 active-low relays, four PWM outputs, an AHT20, and an SSD1306 128×32 display. The original YAML is preserved locally and ignored because it contains a Wi-Fi password.



## Configure and flash



1. Install PlatformIO Core or its VS Code extension.

2. Copy `include/secrets.example.h` to `include/secrets.h` and enter Wi-Fi, MQTT, OTA and web credentials. The build creates this copy if absent. OTA/web passwords require at least 8 characters; distinct passwords of 12+ characters are recommended. Invalid Wi-Fi configuration keeps relay outputs disabled. Missing MQTT, OTA or web configuration disables only that service; it does not prevent Wi-Fi, sensors or other configured services from running. A successful compile alone does not produce a configured device.

3. Check the pin table and especially OE wiring below. Configure channel timers/interlocks in `include/config.h` if needed.

4. Build: `python -m platformio run -e nodemcuv2`.

5. First flash over USB: `python -m platformio run -e nodemcuv2 -t upload --upload-port COM5` (replace COM5).

6. Enable Home Assistant's MQTT integration using the same broker. Discovery creates one device with **39 entities**. Its ID is `living_ctrl_<chip-id-in-hex>`, and its base topic is `home/living_ctrl_<chip-id-in-hex>`.

7. Find the device IP in your router's DHCP leases or Home Assistant's device configuration link. Open `http://DEVICE_IP/` and authenticate with your configured web credentials. Read recent device events through the dashboard's **Device logs** link or `/api/logs`. The normal build keeps USB serial logging disabled because GPIO1/GPIO3 are PWM outputs; use the diagnostic build below for COM-port troubleshooting.



No filesystem upload is needed. Configuration is compiled into the firmware; change it and rebuild to update it. No credentials are copied automatically from the YAML.



## OTA



After the initial USB flash, PowerShell:



```powershell

$env:ESP_OTA_HOST = '192.168.1.50'

$env:ESP_OTA_PASSWORD = 'your-configured-ota-password'

python -m platformio run -e ota -t upload

```



The password must match `OTA_PASSWORD`. Use the IP if mDNS is unavailable. Allow TCP/UDP 8266 and the uploader's incoming connection through the host firewall. On accepted OTA start all relays and PWM outputs switch off, OE is disabled, and the OLED turns off. A rejected password leaves the running controller unchanged. An interrupted accepted update leaves outputs locked off until restart; a successful update reboots with all outputs off. This is ArduinoOTA, so an ESPHome OTA uploader cannot perform the first conversion. The firmware reserves space for OTA in a 4 MB flash layout and does not implement automatic rollback or signed firmware verification.



## Serial diagnostics and live checks



For USB diagnostics on COM9:



```powershell

python -m platformio run -e diagnostic -t upload --upload-port COM9

python scripts/serial_logs.py --port COM9 --seconds 30 --reset

```



This build prints startup/configuration status, an I²C address scan, Wi-Fi connection/IP and disconnect reasons, MQTT progress, sensor health and OTA events at 115200 baud. ROM boot output uses 74880 baud, so the first bytes at 115200 may look garbled. The capture is saved under `.pio/`. Passwords are never logged.



**Diagnostic mode reserves GPIO1 and GPIO3, disabling PWM lights 2 and 4** to avoid USB/UART contention. To restore all four channels, flash the normal `nodemcuv2` build after diagnosis. The normal build still records recent events in the authenticated web log; it does not print application logs on COM9.



With the board on Wi-Fi, `python scripts/check_device.py --host DEVICE_IP --device living_ctrl_CHIPID` checks web authentication, validation, MQTT discovery/availability and telemetry using your private credentials. Optional `--test-led` toggles only the onboard LED and restores its state. `python scripts/check_ota.py --host DEVICE_IP` checks that an incorrect OTA password is rejected without locking outputs. Its optional `--upload` sends the built normal firmware, turns outputs OFF and reboots; build `nodemcuv2` first. These tools do not automatically energize relay or PWM loads.



`python scripts/check_weather.py --host DEVICE_IP --device living_ctrl_CHIPID` checks actual weather readings and retained MQTT publication. Its optional `--test-unavailable` verifies that an unavailable HA provider cannot erase a working direct fallback.



## Wiring



| Function | GPIO | NodeMCU pin |

|---|---:|---|

| I²C SDA | 4 | D2 |

| I²C SCL | 5 | D1 |

| 74HC595 clock / SH_CP | 14 | D5 |

| 74HC595 data / DS, nearest IC | 13 | D7 |

| 74HC595 latch / ST_CP | 0 | D3 |

| Both 74HC595 OE, active-low | 2 | D4 |

| Onboard LED, active-low | 16 | D0 |

| PWM lights 1–4 | 12, 1, 15, 3 | D6, TX, D8, RX |



**Connect both OE pins to GPIO2 with a 10 kΩ pull-up to 3.3 V.** The original YAML's comments suggested grounding OE, but that bypasses startup protection. GPIO2 must be high during boot; firmware loads `0xFFFF` (all active-low outputs off) before pulling OE low. GPIO0/latch needs a 10 kΩ pull-up; GPIO15 must stay low at boot. Connect MR/SRCLR high, Q7S of the first register to DS of the second, and place a 100 nF decoupling capacitor at each register. Clock/data/latch should be short, with the original clock pull-down. Register VCC is 3.3 V; check your relay input stage accepts its output levels.



Relay 1 is Q0 of the nearest register, relay 8 is Q7 of that register, relay 9 is Q0 of the far register. High byte transfers first, then low byte, MSB first, at 400 kHz.



Use relay drivers/module input stages, a separate regulated coil supply and common ground. When OE is high, shift register outputs float: **each active-low relay input needs a suitable pull-up or a driver stage whose disabled state is OFF**. Firmware cannot guarantee power-on safety without this circuitry. Measure startup, reset and brownout behavior with loads disconnected. GPIO1 emits ROM boot messages before the application runs, so PWM light 2 can pulse at boot; use external output gating if its load requires a guaranteed inactive startup. Avoid attaching USB/UART transmitters to GPIO3 while it drives a load. GPIO12 hardware SPI MISO is released back to GPIO/PWM after SPI initialization.



I²C addresses are AHT20 `0x38` and SSD1306 `0x3C` (also probes `0x3D`). Missing OLED hardware does not stop relay control. Missing/invalid AHT20 readings clear the readings and mark its sensors unavailable, with retries every ten seconds. The OLED displays "AHT20 unavailable" while the sensor is disconnected and continues cycling through the other pages. The custom AHT20 driver performs conversions without blocking for the measurement delay and checks the seven-byte response CRC.



## Behavior and improvements



- Relay, PWM and LED outputs start OFF after power-up, reset or OTA. No energized state is restored and relay switching makes no flash writes.

- MQTT connection and reconnects are asynchronous. Discovery/state publications use QoS 1 and one tracked packet at a time. Retained state, broker Last Will and Home Assistant birth handling restore accurate entities after reconnects. Commands with the retained flag are rejected to prevent replay on boot; publish control messages with `retain: false`.

- Strict ON/OFF, channel, PWM and weather validation; bounded MQTT payloads and inbox. Outputs are changed only by the main loop, after complete MQTT messages are assembled. Invalid commands are counted in telemetry.

- Authenticated local dashboard, recent-event log and POST control API. POST requires a custom header to prevent cross-origin browser form submissions. Local network services start after Wi-Fi obtains an address, and configuration checks no longer make unrelated services prevent startup.

- PWM uses 1 kHz/8-bit brightness. The YAML's 400 kHz software PWM is replaced with a supported frequency suitable for four channels.

- OLED targets the connected 128×32 SSD1306: seven pages for the clock (HH:MM:SS, updated once per second), indoor climate, outdoor weather, clouds/precipitation, numbered relay map, four dimmer levels and network diagnostics. FreeSans Bold makes primary values readable; small labels stay pixel crisp. Weather icons distinguish sun, moon, partial cloud, cloud, rain, snow/hail, fog, thunderstorms, wind and unknown conditions. Indoor sensor errors and expired weather never display as current readings. Header steps mark the current page, W/M indicate Wi-Fi/MQTT, and bars show signal strength. Relays show commanded state (filled = ON), not measured contact feedback.

- The OLED starts ON, rotates on a 42-second cycle and uses lower contrast from 22:00 to 07:00 in the device's configured local timezone. Choose a page or **Auto rotate** on the dashboard; page selection wakes the screen. Settings are session-only and reset to automatic at boot. The dashboard includes a pixel-accurate live framebuffer preview and display detection/status. MQTT page selection uses `BASE/oled_page/set` with `AUTO`, `clock`, `room`, `weather`, `sky`, `relays`, `lights` or `network`. OFF/ON preserves a pinned page; waking automatic mode starts with the clock. Set `oledTimeout` to a nonzero duration to enable auto-off. The device uses the accepted HA timezone/location, or configured Tehran defaults. Non-ASCII location names are replaced with `?`, and long labels end with `~`.



- Optional per-relay auto-off and interlock masks in `config.h`. Interlocks prevent simultaneous ON states but offer no certified deadtime; they are for ordinary light/load exclusion, not reversing motors. Timers use wrap-safe millisecond arithmetic. Uptime accumulates across `millis()` rollover.

- Optional network-loss timeout (`offlineOffMs`); default zero keeps the commanded outputs during an outage. AHT20 and local web control continue when MQTT is unavailable. There is no open fallback access point and no automatic reboot on network loss.

- MQTT and web JSON serialization avoid stacking large buffers/documents on the ESP8266's small continuation stack.

- Weather accepts MQTT updates in the background. Direct Open-Meteo HTTPS fetching runs only after an explicit refresh, so startup and feed expiry never initiate TLS. Certificate/hostname verification uses `include/weather_ca.h` and valid NTP time.



## MQTT interface



Replace `BASE` with the device's base topic. Commands are not retained. States below are retained.



| Topic | Payload / meaning |

|---|---|

| `BASE/relay/01/set` … `/16/set` | `ON` or `OFF` |

| `BASE/pwm/1/set` … `/4/set` | `{"state":"ON","brightness":128}`; brightness 0–255 |

| `BASE/oled/set`, `BASE/led/set` | `ON` or `OFF` |

| `BASE/oled_page/set` | `AUTO`, `clock`, `room`, `weather`, `sky`, `relays`, `lights`, `network` |

| `BASE/all_off/set` | `PRESS` turns all relays and PWM outputs off |

| `BASE/restart/set` | `PRESS` turns all outputs off and restarts |

| `BASE/weather/set` | JSON feed below, non-retained |

| `BASE/weather_refresh/set` | `PRESS` queues a weather refresh, non-retained |

| `BASE/weather/request` | Device sends non-retained `PRESS` to request the HA feed |

| `BASE/location/set` | HA latitude/longitude, region/location, IANA timezone name and UTC offset in minutes; non-retained |

| `BASE/state` | JSON containing `r01` … `r16`, relay mask, LED/OLED, environment/weather, uptime, RSSI, heap, reset reason, failures and IP |

| `BASE/pwm/1/state` … `/4/state` | MQTT JSON light state/brightness |

| `BASE/availability` | `online` / `offline`; Last Will is `offline` |

| `BASE/sensor_availability` | AHT20 health availability |

| `BASE/weather_availability` | Weather feed availability |



State telemetry refreshes every ten seconds and following commands/sensor changes. Weather telemetry also exposes `weather_source`, `weather_error`, `weather_age_s` and `time_valid` for troubleshooting. Relay feedback reports the software's commanded state; there is no physical contact feedback sensor. Physical switching, delivery acknowledgements and hazardous-load controls require appropriate hardware.



Weather feed example:



```json

{"condition":"partlycloudy","temperature":24.1,"humidity":40,"cloud_cover":35,"precipitation":0,"code":2,"is_day":1}

```



Temperature is Celsius; humidity/cloud cover are percentages, precipitation is mm, and `code` is WMO 0–99. Cloud cover and precipitation may be `null` when the source lacks them. Other fields are required. `{"available":false}` marks the feed unavailable immediately; no valid update for 30 minutes also expires it.



## Weather fallback and optional Home Assistant feed



Weather never delays boot waiting for MQTT, HA or NTP. Click **Refresh weather** on the dashboard, or publish non-retained `PRESS` to `BASE/weather_refresh/set`. The command queues work and returns immediately. When MQTT is connected, the device publishes `PRESS` to `BASE/weather/request` and gives HA up to five seconds to reply, while the main loop continues running. Copy the updated HA package or standalone automation to enable this request trigger. Valid background HA updates also satisfy pending requests.



If HA reports unavailable or does not respond, one direct Open-Meteo HTTPS fetch is permitted for that request. Coordinates default to Tehran (`35.6892`, `51.3890`); change `weatherLatitude`/`weatherLongitude` in `include/config.h` for another location. NTP synchronization is required for HTTPS. There is no automatic direct polling or retry after completion/failure; press refresh again to retry. Direct attempts are limited to one per 60 seconds, with queued requests waiting for this interval. A valid feed expires after 30 minutes without triggering a fetch. `weatherFallback=false` uses MQTT only and avoids direct TLS connection pauses even during refresh. Requests wait for Wi-Fi/NTP and safe relay/heap conditions without blocking the loop.



HTTP body processing is incremental, with a 2 KB limit, six-second response deadline, chunked-response support and rejection of stale/malformed samples. TLS connection setup is synchronous and can briefly delay local commands. It is deferred while a relay with an auto-off timer is active. The tested board completed its first verified HTTPS fetch in approximately four seconds. The TLS receive buffer is capped at 8 KB; a future server certificate/record change exceeding that limit fails closed and requires a firmware adjustment. No insecure certificate bypass is used. Trust-anchor changes also require a firmware update.



Home Assistant weather remains optional and takes priority while fresh. To feed it to the controller:



Copy `homeassistant/living_controller.yaml` into your Home Assistant packages directory and enable packages, for example:



```yaml

homeassistant:

  packages: !include_dir_named packages

```



The package is configured for the previously tested board, `home/living_ctrl_8b3f40`; confirm this topic for your board and set the weather-entity helper to your actual `weather.*` ID. The package prefers valid HA weather and falls back to Open-Meteo using `zone.home` coordinates. Open-Meteo is fetched every 15 minutes over HTTPS by Home Assistant, and the non-retained feed refreshes every 15 minutes and when the controller reconnects. Stale Open-Meteo data is rejected. HA cloud coverage and precipitation absent from both providers remain unknown; HA condition codes are mapped to WMO approximations. Adjust the REST entity ID if HA assigns a different name. See [the step-by-step HA setup guide](docs/HOME_ASSISTANT_SETUP.md).



The package also sends `BASE/location/set` before weather, including HA home coordinates/name, the region helper, timezone name and current UTC offset. These override `config.h` defaults when `haLocationOverride=true`, even if HA weather is unavailable. Overrides last until reboot; HA republishes every minute. The dashboard displays home AHT20 and outdoor readings separately, the selected weather provider, optional cloud/precipitation values, region/location, coordinates and device local time. Telemetry distinguishes `home-assistant`, `open-meteo-ha` and `open-meteo` providers. The ESP clock keeps the last HA UTC offset during an outage; without HA it uses the compiled POSIX timezone.



No Home Assistant package or PC background process is required for direct weather. The ESP no longer calls the ESPHome native API or downloads fonts. If an HA publisher reports `{"available":false}`, the controller preserves a working direct fallback; if the active source was HA, it schedules a direct refresh. A fresh valid HA/MQTT feed replaces the fallback and suspends direct polling until it expires.



## Security and deployment limits



MQTT on port 1883 and the local HTTP dashboard are **unencrypted**, and ArduinoOTA password authentication does not provide transport encryption or signed firmware. Deploy this firmware on a trusted, isolated IoT LAN with broker credentials/ACLs; keep it off the public internet. The sample Mosquitto ACL scopes the controller to its device topics. Home Assistant's sample ACL may need additional permissions for your other integrations. For untrusted networks or stronger firmware authenticity requirements, an ESP32 migration with verified TLS and signed OTA is the better hardware target.



Secrets are embedded in the firmware binary: do not publish configured binaries or commit `secrets.h`. The CI build uses placeholders and produces an unconfigured artifact. Preserve the private YAML outside source control if you initialize Git.



## Validation



Run `python scripts/test.py` with a host C++ compiler installed (or `CXX` pointing to one), then build the firmware. Tests cover every 16-bit polarity pattern, interlocks, malformed channel/ON/OFF input, timer rollover, AHT20 conversion/CRC/error recovery through a simulated I²C bus, MQTT message fragmentation/retained-message rejection/queue capacity, and fixed/chunked/close-delimited weather HTTP parsing with malformed/oversized/truncated response rejection. CI performs these checks and builds the ESP8266 image.



Before deployment, use `docs/COMMISSIONING.md` to test the real board, relay polarities, power behavior, MQTT/HA reconnection, and OTA success/failure. Compilation and simulated tests do not certify this wiring or firmware for unattended mains operation.



For offline HA weather template checks, install `requirements-dev.txt` and run `python tests/test_weather.py`. These cover HA priority, Fahrenheit conversion, missing metrics, stale/future REST data and the reconnect filter; a real HA integration/OTA session still requires hardware and your broker.



References: [PlatformIO NodeMCU v2](https://docs.platformio.org/en/latest/boards/espressif8266/nodemcuv2.html), [ESP8266 PWM](https://arduino-esp8266.readthedocs.io/en/latest/reference.html#analog-output), [ArduinoOTA](https://arduino-esp8266.readthedocs.io/en/stable/ota_updates/readme.html), [Home Assistant MQTT](https://www.home-assistant.io/integrations/mqtt/), [REST integration](https://www.home-assistant.io/integrations/rest/), [Open-Meteo API](https://open-meteo.com/en/docs), [BearSSL client](https://arduino-esp8266.readthedocs.io/en/stable/esp8266wifi/bearssl-client-secure-class.html).



### OLED verification



After flashing, run `python scripts/check_oled.py --host DEVICE_IP --device living_ctrl_CHIPID`. It checks authenticated snapshots, all seven pages, rejected page commands and MQTT selection without changing relay/PWM outputs, then restores the original display mode and power. Snapshots are written to `.pio/oled-captures/`; optional Pillow creates `preview.png`. `/api/oled` returns a 128×32 binary PBM of the last framebuffer sent to the panel (the physical panel is dark when commanded OFF). State telemetry includes `oled_present`, `oled_mode` and `oled_page`.


The font layout uses the baseline and bounding-box conventions in [Adafruit GFX font documentation](https://learn.adafruit.com/adafruit-gfx-graphics-library/using-fonts).

### OLED auto-off timer

The dashboard's **OLED auto-off** selector offers **OFF - stay on** or a duration in minutes. Presets cover common durations; **Custom minutes** and **Set minutes** accept any whole number from 1 to 1440. OFF disables automatic shutoff; the separate OLED screen button still switches the panel on/off immediately. The dashboard shows the remaining countdown.

Selecting a duration restarts the countdown if the panel is ON and leaves an OFF panel asleep. Turning the OLED ON again or selecting a page restarts the countdown. Automatic page rotation, clock updates and weather updates do not extend it. At expiry only the OLED switches OFF; relay/PWM outputs continue unchanged. Settings apply until reboot, which restores `cfg::oledTimeout` (default 0/disabled, in milliseconds).

MQTT/API command: `BASE/oled_timeout/set` with `OFF` or a decimal minute count (`1` through `1440`). Telemetry exposes `oled_timeout_minutes` (0 means disabled) and `oled_remaining_s` (0 when disabled or the panel is OFF). Run `python scripts/check_oled_timeout.py --host DEVICE_IP --device living_ctrl_CHIPID` for a real one-minute auto-off check; it restores the previous display settings.

### Weather and light-phase bitmap icons

The clock shows two 12x12 icons next to HH:MM:SS: the current light phase (sun, moon, sunrise or sunset), then current weather. The outdoor page uses the same weather icon set at 22x22. All symbols are pre-rasterized [Lucide icons](https://lucide.dev/icons/) rendered with `drawBitmap`; weather symbols no longer use procedural circles/lines. Cloudy, rain, snow/hail, fog, thunderstorm, wind and separate daytime/nighttime partial-cloud icons are supported. Missing or expired weather shows a question-mark icon rather than an invented condition.

Light phase is estimated from NTP UTC time and the accepted HA/configured latitude and longitude using [NOAA solar-position equations](https://gml.noaa.gov/grad/solcalc/solareqns.PDF). Sunrise/sunset symbols indicate the rising/setting twilight band, from -6 to +6 degrees solar elevation; they do not report exact sunrise/sunset times. The result is refreshed once per minute or when coordinates change and works without a weather feed. Invalid clock/coordinates yield an unknown light phase. Telemetry includes `solar_phase`.

SVG sources, pinned upstream revision and Lucide/Feather licenses are in `assets/oled/lucide/`. Bitmap assets are committed in `include/oled_icons.h`, so firmware builds need no downloader or graphics dependency. To regenerate assets, install Pillow and run `npm install --prefix .pio/icon-tools --no-audit --no-fund @resvg/resvg-js@2.6.2`, then `python scripts/build_oled_icons.py`. Asset preview: `.pio/icon-tools/icon-sheet.png`.
