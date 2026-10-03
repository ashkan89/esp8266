# Hardware acceptance checks

Perform initial checks with mains and controlled loads disconnected. Record board revision, driver polarity, power-supply details, firmware version and test results for each installed unit.

1. Confirm 4 MB flash and the configured GPIO/address mapping. Measure OE high before boot and all driver inputs OFF while shift registers are disabled. Verify GPIO0/GPIO2 pull-ups and GPIO15 boot pull-down.
2. With a logic analyzer or low-voltage test indicators, capture cold power-up, reset, USB programming, brownout and restoration. Check that no relay energizes before the all-off pattern is latched. GPIO1 ROM output and floating OE-disabled inputs need hardware mitigation where required.
3. Issue ON/OFF to each of the 16 relay entities one at a time. Confirm numbering, active-low polarity, driver voltage levels and no cross-channel glitches. Load test the supply and check switching noise does not reset the ESP or corrupt I²C/SPI.
4. Sweep all four PWM outputs from 0 to 255. Confirm frequency, physical pin mapping, full OFF and full ON. Check UART/USB does not contend with GPIO3.
5. Unplug AHT20: its entities should become unavailable, other functions continue, and reconnect should recover within approximately ten seconds. Verify plausible calibrated readings and display size/address. Missing OLED must not prevent control.
6. Turn OLED on and verify pages/time/weather. It should turn off after 60 seconds. Check the LED, all-off button and authenticated dashboard. Invalid/unauthenticated control requests must fail.
7. Restart broker, HA and Wi-Fi separately. Verify discovery/state recovery, offline availability and no replay of retained commands. Publish malformed/oversized/retained messages and check no outputs change. Confirm broker ACLs with a different unprivileged client.
8. Confirm optional per-relay timers/interlocks and chosen outage policy. Cut power with relays ON: all must remain OFF after reboot. Do not use the interlocks for motor reversal.
9. Perform an OTA update and verify outputs go OFF, device reboots and entities recover. Repeat with a wrong password; the upload must be refused. On a deliberately interrupted accepted upload, outputs must stay OFF/locked, and a restart must recover normal operation. Keep USB recovery access.
10. Stop the MQTT weather publisher and verify direct Open-Meteo fallback after the feed expires (or an unavailable marker is sent). Disconnect internet and stop all weather publishers: readings must expire after 30 minutes. Check HA priority, direct/REST fallback, coordinates, timezone, units and HTTPS/NTP errors in `/api/logs`. Check the direct fallback again after a power cycle without a PC publisher running.
11. Run an extended soak test under realistic Wi-Fi/broker outages, all expected loads and temperature range. Track heap/free-heap trends, sensor failures, reset reasons and actual hardware faults. Check watchdog recovery with outputs defaulting OFF.

The project has no contact feedback, certified functional safety, secure boot, encrypted local transports or automatic rollback. Match deployment requirements to those limits and obtain appropriate electrical validation for mains hardware.
