# Validation record

## Clock/weather bitmap icons, 2026-10-03

Firmware **1.1.3** uploaded on COM9 with verified flash hash. Clock: independent 12x12 light-phase and weather bitmap icons beside HH:MM:SS. Outdoor: 22x22 weather bitmaps. Both use the checked-in Lucide SVG assets, pinned upstream revision and full Lucide/Feather license notices; icon pixels are generated once and stored in flash. Procedural weather circles/lines were removed.

Solar phase uses the NOAA UTC solar-position approximation with accepted HA/configured coordinates. Sunrise/sunset indicate a twilight band (-6 to +6 degrees solar elevation), not exact event times. Day/night is independent of stale/missing weather; weather icons remain subject to the existing freshness check. Computation is cached per minute and invalidated on coordinate changes.

Host tests passed for day/night, sunrise/sunset, dateline longitude changes, northern/southern polar seasons and invalid time/coordinates, alongside existing control/timeout checks. All ten weather-template tests passed. Live seven-page navigation, authenticated framebuffer capture and MQTT selection passed. The actual clock sun/cloud icons and outdoor cloud icon were compared pixel-for-pixel against the committed bitmap arrays and matched exactly. The full capture sheet was visually inspected for clipping/overlap. Device reported solar_phase=day, firmware 1.1.3, MQTT ready, automatic rotation ON, timeout disabled and relay_mask=0. Captures: `.pio/oled-captures/preview.png`; complete icon atlas: `.pio/icon-tools/icon-sheet.png`. Night and twilight symbols were checked offline; current daytime hardware observation does not claim a live sunrise/sunset test.

## Selectable OLED timeout, 2026-10-03

Firmware **1.1.2** adds dashboard presets/custom whole minutes (1-1440) and OFF to disable auto-off, plus MQTT `oled_timeout/set`. The countdown restarts on changing the duration, ON commands and page selection; changing the duration leaves an OFF panel asleep. It affects only the OLED and resets to the configured default after reboot. Release build: 50,232 bytes static RAM, 431,080 bytes application flash.

Host tests cover duration limits, strict parsing, rejected commands preserving the previous deadline, OFF, expiration boundaries, wake and timer rollover. Dashboard JavaScript syntax checks pass. An initial live test exposed an old loop timestamp immediately expiring a newly started countdown; display updates now sample fresh time after commands. Final COM9 upload passed flash-hash verification. Live checks passed for custom 7-minute configuration, OFF, invalid input rejection, ON/page countdown resets, leaving an OFF screen asleep when its duration changes, MQTT timer selection and the actual one-minute automatic shutoff. Relay/PWM outputs remained unchanged; original display mode, power and timeout were restored.

## Clock seconds, 2026-10-03

Firmware **1.1.1** adds HH:MM:SS to the OLED clock, with the date and timezone below it. The time buffer includes room for all eight characters and a terminator; the existing once-per-second display refresh updates seconds. Release build: 50,128 bytes static RAM, 429,088 bytes application flash. COM9 upload succeeded with verified flash hash; two live clock snapshots taken 1.2 seconds apart differed, confirming seconds update. The framebuffer was visually inspected and the initial page mode restored. Capture: `.pio/oled-captures/clock-seconds.png`.

## OLED redesign, 2026-10-03

Firmware **1.1.0** was uploaded to the ESP8266EX on **COM9**. The first high-speed upload failed with an invalid serial packet; a complete 115200-baud retry succeeded and verified the flash hash. `upload_speed` is now 115200 for subsequent serial uploads. The final normal build disables application UART diagnostics and makes all four PWM channels available.

- Release build passed without application warnings: 50,144 bytes static RAM (61.2%), 429,120 bytes application flash (41.1% of application capacity).
- Host C++ checks passed, including new carousel boundaries, pinned-page behavior, invalid command preservation, wake behavior, `millis()` rollover and weather-icon classification. All ten offline weather-template tests passed.
- Live checks passed for all seven pinned OLED pages, authenticated 522-byte PBM framebuffers, invalid-page rejection, MQTT page selection and return to automatic mode. Actual device framebuffers were captured and visually inspected for text overlap and clipping; captures are in `.pio/oled-captures/preview.png`. Initial display mode/power were restored. Framebuffer inspection verifies rendered pixels, not the physical panel's optical quality.
- Authenticated dashboard/state/logs, unauthenticated endpoint rejection, malformed command rejection, all 39 MQTT discovery records, online availability and current weather telemetry passed. Accepted HA weather was displayed as 29.2 C, 22% RH, 98% cloud, 0.0 mm precipitation, cloudy; this is a live sample, not embedded demo data.
- Device reported the OLED present, automatic rotation ON, Wi-Fi/MQTT ready, outputs unlocked and normal serial mode. Relays 6, 8 and 12 were ON before flashing; the reboot reset every relay/PWM output to OFF. Verification did not energize load outputs.
- AHT20 remains unavailable on the actual board. The indoor page correctly shows a wiring/power fault; no physical sensor repair or contact-feedback validation is claimed. Board networking recovered and continued running without a new reset during the observation period.

Page selection is session-only; pinning, display power and location overrides are not restored from flash at boot. Non-ASCII labels use `?`, long labels are bounded with `~`, and the clock uses the device timezone rather than a fixed displayed label. Weather beyond the existing 30-minute freshness limit is hidden and marked expired. There is no forecast feed; icons represent supplied current conditions.

Validated with PlatformIO Core 6.2.0, espressif8266 4.2.1, ESP8266 Arduino core 3.1.2 and the pinned libraries in `platformio.ini`.

## Initial offline validation, 2026-10-02

- `python -m platformio run -e nodemcuv2`: successful firmware build for the 4 MB NodeMCU v2 layout; initial build used approximately 39,140 bytes static RAM and 408,364 bytes application flash.
- `python scripts/test.py`: passed host C++ checks for all 65,536 relay polarity masks, relay interlocks and channel boundaries, strict ON/OFF commands, timer rollover, AHT20 CRC/conversion/error recovery, and MQTT fragmentation, retained-message policy, embedded NUL rejection, fragment consistency and queue capacity.
- `python tests/test_weather.py`: seven passing offline YAML/Jinja template tests covering HA weather preference, Fahrenheit conversion, REST fallback, source age, unknown optional metrics, coordinate rendering and MQTT reconnect filtering.

The initial firmware was built with placeholder configuration. Those initial tests used I²C stubs and HA-like template globals, without live hardware.

## Live COM9 diagnosis and repair, 2026-10-03

Device: `living_ctrl_8b3f40`, connected on COM9, DHCP address `192.168.68.61` during validation. Firmware installed: **1.0.1, normal build**, with all four PWM outputs available and USB application logging disabled. Recent events remain available through authenticated `/api/logs`. The local binary now contains private configured credentials and must not be shared.

Observed root causes:

- Firmware 1.0.0 required 12-character OTA/web passwords and silently stopped the entire device when the configured nine-character passwords failed that test. Firmware 1.0.1 uses separate Wi-Fi/MQTT/OTA/web configuration checks, accepts configured OTA/web passwords of at least eight characters, and reports startup/service status. Longer passwords remain recommended.
- The configured Wi-Fi password differed from the original YAML. Restoring only that password fixed repeated Wi-Fi connection failures. Other user-supplied credentials were preserved.
- I²C scan found zero devices on GPIO4/GPIO5. The user confirmed the OLED/AHT20 were disconnected or unpowered. Missing hardware is reported unavailable; measurement recovery remains enabled.

Additional fixes: network listeners start after obtaining an IP, JSON construction/serialization no longer stacks large buffers on the small ESP8266 continuation stack, and pre-update OTA authentication errors no longer lock the running controller.

Live results:

- Normal ROM boot on COM9; diagnostic UART logs captured at 115200 baud, Wi-Fi connected and MQTT reached ready without resets during observation.
- Broker credentials accepted; all **39 retained MQTT discovery records**, online availability and valid telemetry received from the actual broker.
- Authenticated dashboard, state and recent-log endpoints succeeded. Unauthenticated state access returned 401; POST without the required header returned 403; malformed relay/PWM commands returned 400 without changing the relay mask.
- MQTT onboard LED ON/OFF commands applied and the initial LED state was restored. Relay and PWM loads were not energized by validation.
- Incorrect OTA password rejected; controller stayed unlocked and outputs unchanged.
- Correct OTA password accepted, normal production image uploaded, device rebooted, MQTT recovered, relay mask remained zero and all four PWM duties were zero. Diagnostic UART reservations were removed.
- C++ regression checks, including service-configuration independence and nine-character passwords, and all seven offline HA weather-template tests passed. Normal firmware build succeeded without application warnings: 44,672 bytes static RAM, 410,336 bytes application flash.

This validates the actual board's networking and OTA, not physical relay contact feedback, real PWM waveforms, disconnected sensors, live HA weather rendering or the Home Assistant UI. Physical load/startup/brownout tests still need the commissioning procedure.

Use `COMMISSIONING.md` for hardware acceptance and deployment validation. Repeat the build after entering credentials or changing hardware/timer settings.

## Live online-weather repair, 2026-10-03

Firmware **1.0.2** restores an on-device, certificate-verified Open-Meteo HTTPS fallback. The previous build depended on an external MQTT weather feed; the board showed weather unavailable with no rejected commands, and the local HA package still contained a placeholder topic. The package now targets `home/living_ctrl_8b3f40`, but direct weather no longer requires its installation.

The actual board fetched Open-Meteo using the original Tehran coordinates, validated HTTPS with ISRG Root X1 and NTP time, accepted a fresh Unix-timestamped sample, and reported `weather_available=ON`, `weather_source=open-meteo`, with no weather error. Observed sample: 27.6°C, 25% humidity, 65% cloud cover, 0 mm precipitation, WMO code 3/cloudy. These are a validation snapshot, not fixed/example firmware values.

Broker subscriptions confirmed retained `weather_availability=online` and actual matching weather fields in `BASE/state`. Sending a non-retained HA-unavailable marker preserved the working direct fallback and did not alter relay/PWM state. The final normal firmware was uploaded through authenticated OTA; incorrect OTA authentication remained rejected without locking outputs. All outputs remained OFF.

New host tests pass for fixed-length, chunked and close-delimited HTTP bodies and rejection of oversized, truncated, ambiguous and invalid-status responses. The existing C++ and seven HA template checks also pass. The final build succeeds without application warnings, using 48,652 bytes static RAM and 528,992 bytes application flash. The authenticated dashboard now displays online weather, and diagnostics expose source, sample age, errors and clock validity.

Direct HTTPS connection establishment can briefly block local command processing; the measured first fetch took approximately four seconds. Fetches are deferred while timed relays are active. A server trust-chain/TLS-record change can require a firmware update. This repair does not validate a running Home Assistant UI, disconnected physical sensors or hazardous-load timing.
