# Home Assistant weather and location setup

The active firmware project is C++/PlatformIO. The ignored `esp8266.yaml` is the original ESPHome configuration; editing it does not change this firmware.

1. In HA, open **Settings → System → General** and confirm the home location and timezone. The firmware defaults match the original configuration: Tehran, Iran, latitude `35.6892`, longitude `51.3890`, timezone `Asia/Tehran` (UTC+03:30). These are city defaults, not a verified home address.
2. Enable the MQTT integration with the same broker configured privately in `include/secrets.h`.
3. Copy [living_controller.yaml](../homeassistant/living_controller.yaml) to `/config/packages/living_controller.yaml`. Add the following under the existing `homeassistant:` section in `/config/configuration.yaml` (do not create a duplicate section):

   ```yaml
   homeassistant:
     packages: !include_dir_named packages
   ```

4. In **Developer Tools → States**, find your actual `weather.*` entity. Set `living_controller_weather_entity.initial` in the package to that ID. `weather.home` is a default that still needs verification on your HA installation. The MQTT base is already set to the previously tested board, `home/living_ctrl_8b3f40`. Confirm that ID against the MQTT device or its discovery topic. Set `living_controller_region.initial` to your region; it currently says `Iran`.
5. Check the HA configuration and restart HA. You can subsequently edit the weather entity and region helpers in HA. The feed checks the selected weather entity every 15 minutes; configured weather entity changes, HA startup and device reconnects also trigger it.
6. Build and flash firmware 1.0.4 using the README's USB or authenticated OTA instructions. Installing the HA package alone does not update the running firmware.
7. Open the controller dashboard. **Home / AHT20** shows the physical indoor sensor. **Outdoor** shows Home Assistant, Open-Meteo via HA, or direct Open-Meteo. The dashboard also shows region, home-zone name, coordinates, local time and the location source.

HA sends two non-retained QoS1 MQTT messages every 15 minutes, in order:

| Topic | Contents |
|---|---|
| `home/living_ctrl_8b3f40/location/set` | `zone.home` coordinates and name, region helper, HA timezone and current UTC offset |
| `home/living_ctrl_8b3f40/weather/set` | HA weather if usable; otherwise fresh Open-Meteo fetched by HA; otherwise `{"available":false}` |

The ESP boots without waiting for weather and accepts the HA feed in the background. **Refresh weather** on the dashboard, or non-retained `PRESS` to `BASE/weather_refresh/set`, queues a request. The device sends non-retained `PRESS` on `BASE/weather/request`; the updated package responds with location and weather. It gives HA five seconds to reply without blocking the main loop, then permits one direct HTTPS attempt if necessary. There is no automatic direct polling/retry at startup or expiry. Direct TLS setup can briefly pause commands during an explicit refresh; set `weatherFallback=false` for MQTT-only operation. Direct requests use the latest accepted HA coordinates. Fresh HA weather takes priority when it returns; updates expire after 30 minutes. The HA Open-Meteo sample timestamp is preserved rather than replaced by the MQTT send time.

Change defaults in [config.h](../include/config.h): `region`, `location`, `timezoneName`, POSIX `timezone`, `weatherLatitude` and `weatherLongitude`. Set `haLocationOverride=false` to ignore HA location/time overrides. Accepted overrides stay in RAM during HA outages and revert to compiled defaults after reboot until HA publishes again. HA sends its current UTC offset with each feed update, keeping the device clock aligned with daylight saving changes while HA is available. During an HA outage the ESP keeps the last offset; it does not contain an IANA timezone database.

To check the feed, listen to `home/living_ctrl_8b3f40/+/set` in the HA MQTT integration. To test fallback, temporarily disable the feed automation and press **Refresh weather**. Selecting an unavailable weather entity normally chooses HA's Open-Meteo REST sensor first. Direct fetching requires an explicit refresh. Check `/api/state` for `weather_source`, `weather_error`, `location_source`, `local_time` and `time_valid`. Restore the automation/helper afterward.

`include/secrets.h` already has all nine configuration fields filled in. `CHANGE_ME` in `include/secrets.example.h` is intentional for new installations; placeholders in tests verify that unconfigured services stay disabled. No credentials are printed or moved into the public example file. Your HA weather entity ID and access URL cannot be inferred from these credentials.

References: [HA packages](https://www.home-assistant.io/docs/configuration/packages/), [MQTT publish](https://www.home-assistant.io/actions/mqtt.publish/), [home information](https://www.home-assistant.io/docs/configuration/basic), [HA date/time templates](https://www.home-assistant.io/docs/templating/dates-and-times/).
