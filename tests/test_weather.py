"""Offline checks for the HA package's weather templates, using HA-like globals.

This validates rendered payloads, not a running Home Assistant installation.
"""
import datetime as dt
import json
from pathlib import Path
import unittest
from types import SimpleNamespace
import jinja2
import yaml

ROOT = Path(__file__).resolve().parents[1]
PACKAGE = yaml.safe_load((ROOT / "homeassistant/living_controller.yaml").read_text(encoding="utf-8"))

class WeatherTemplates(unittest.TestCase):
    def setUp(self):
        self.now = dt.datetime(2026, 10, 2, 12, tzinfo=dt.timezone.utc)
        self.states = {"weather.home": "unavailable", "sensor.living_controller_open_meteo": "2",
                       "sun.sun": "above_horizon", "input_text.living_controller_mqtt_base": "home/living_ctrl_abc",
                       "input_text.living_controller_weather_entity": "weather.home",
                       "input_text.living_controller_region": "Iran"}
        self.attributes = {
            "zone.home": {"latitude": 35.6892, "longitude": 51.3890, "friendly_name": "Tehran home"},
            "weather.home": {},
            "sensor.living_controller_open_meteo": {
                "time": "2026-10-02T11:59", "temperature_2m": 24.1,
                "relative_humidity_2m": 40, "cloud_cover": 35,
                "precipitation": 0.1, "is_day": 1}}
        self.env = jinja2.Environment(undefined=jinja2.StrictUndefined)
        def number(value):
            try:
                return value is not None and float("-inf") < float(value) < float("inf")
            except (ValueError, TypeError):
                return False
        def timestamp(value, default=0):
            if isinstance(value, dt.datetime):
                return value.timestamp()
            try:
                return dt.datetime.fromisoformat(value).timestamp()
            except (TypeError, ValueError):
                return default
        self.env.globals.update(states=lambda e: self.states.get(e, "unknown"),
                                state_attr=lambda e, a: self.attributes.get(e, {}).get(a),
                                is_state=lambda e, s: self.states.get(e) == s,
                                now=lambda: self.now, is_number=number, as_timestamp=timestamp)
    def payload(self):
        value = PACKAGE["automation"][0]["actions"][-1]["data"]["payload"]
        return json.loads(self.env.from_string(value).render())
    def test_rest_fallback(self):
        payload = self.payload()
        self.assertEqual(payload["condition"], "cloudy")
        self.assertEqual(payload["temperature"], 24.1)
        self.assertEqual(payload["humidity"], 40)
        self.assertEqual(payload["code"], 2)
        self.assertEqual(payload["precipitation"], 0.1)
        self.assertEqual(payload["source"], "open-meteo")
        self.assertLess(payload["sample_time"], self.now.timestamp())
    def test_ha_priority_fahrenheit_and_night(self):
        self.states["weather.home"] = "clear-night"
        self.attributes["weather.home"] = {"temperature": 77, "temperature_unit": "°F", "humidity": 55, "cloud_coverage": "12"}
        payload = self.payload()
        self.assertEqual(payload["temperature"], 25)
        self.assertEqual(payload["humidity"], 55)
        self.assertEqual(payload["cloud_cover"], 12)
        self.assertEqual(payload["is_day"], 0)
        self.assertEqual(payload["source"], "home-assistant")
    def test_stale_rest_unavailable(self):
        self.attributes["sensor.living_controller_open_meteo"]["time"] = "2026-10-02T10:00"
        self.assertEqual(self.payload(), {"available": False})
    def test_future_or_failed_rest_unavailable(self):
        self.attributes["sensor.living_controller_open_meteo"]["time"] = "2026-10-03T10:00"
        self.assertEqual(self.payload(), {"available": False})
        self.states["sensor.living_controller_open_meteo"] = "unavailable"
        self.assertEqual(self.payload(), {"available": False})
    def test_missing_optional_metrics_unknown(self):
        self.states["sensor.living_controller_open_meteo"] = "unavailable"
        self.states["weather.home"] = "sunny"
        self.attributes["weather.home"] = {"temperature": 20, "humidity": 40}
        payload = self.payload()
        self.assertIsNone(payload["cloud_cover"])
        self.assertIsNone(payload["precipitation"])
    def test_rest_coordinates(self):
        value = self.env.from_string(PACKAGE["rest"][0]["resource_template"]).render()
        self.assertIn("latitude=35.6892&longitude=51.389", value)
        self.assertIn("timezone=UTC", value)
    def test_location_independent_of_weather(self):
        self.states["sensor.living_controller_open_meteo"] = "unavailable"
        action = PACKAGE["automation"][0]["actions"][0]["data"]
        payload = json.loads(self.env.from_string(action["payload"]).render())
        self.assertEqual(payload["latitude"], 35.6892)
        self.assertEqual(payload["longitude"], 51.389)
        self.assertEqual(payload["region"], "Iran")
        self.assertEqual(payload["location"], "Tehran home")
        self.assertEqual(payload["utc_offset_minutes"], 0)
        self.assertFalse(action["retain"])
    def test_configurable_weather_entity(self):
        self.states["input_text.living_controller_weather_entity"] = "weather.forecast_home"
        self.states["weather.forecast_home"] = "rainy"
        self.attributes["weather.forecast_home"] = {"temperature": 18, "humidity": 70}
        self.assertEqual(self.payload()["source"], "home-assistant")
        self.assertEqual(self.payload()["temperature"], 18)
    def test_location_timezone_offsets(self):
        action = PACKAGE["automation"][0]["actions"][0]["data"]
        for minutes in (210, -300, 0):
            self.now = self.now.astimezone(dt.timezone(dt.timedelta(minutes=minutes)))
            payload = json.loads(self.env.from_string(action["payload"]).render())
            self.assertEqual(payload["utc_offset_minutes"], minutes)
    def test_reconnect_filter_and_nonretained_feed(self):
        automation = PACKAGE["automation"][0]
        template = self.env.from_string(automation["conditions"][0]["value_template"])
        self.assertEqual(template.render(trigger=SimpleNamespace(platform="mqtt", topic="home/living_ctrl_abc/availability")).strip(), "True")
        self.assertEqual(template.render(trigger=SimpleNamespace(platform="mqtt", topic="home/other/availability")).strip(), "False")
        self.assertEqual(template.render(trigger=SimpleNamespace(platform="mqtt", topic="home/living_ctrl_abc/weather/request")).strip(), "True")
        self.assertEqual(template.render(trigger=SimpleNamespace(platform="mqtt", topic="home/other/weather/request")).strip(), "False")
        self.assertIn({"trigger": "mqtt", "topic": "home/+/weather/request", "payload": "PRESS"}, automation["triggers"])
        self.assertEqual(template.render(trigger=SimpleNamespace(platform="homeassistant")).strip(), "True")
        self.assertFalse(automation["actions"][0]["data"]["retain"])

if __name__ == "__main__":
    unittest.main()
