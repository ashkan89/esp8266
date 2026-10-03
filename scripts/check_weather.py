"""Confirm live direct weather, MQTT publication and HA-unavailable behavior.

With --test-unavailable, send only an unavailable-provider marker: this does not
fabricate a weather reading or change relay/PWM states.
"""
import argparse
import json
import socket
import struct
import time
import requests
from requests.auth import HTTPDigestAuth
from check_device import credentials, expected_version, MqttProbe

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", required=True)
    parser.add_argument("--device", required=True)
    parser.add_argument("--test-unavailable", action="store_true")
    args = parser.parse_args()
    config = credentials()
    session = requests.Session()
    session.auth = HTTPDigestAuth(config["WEB_USER"], config["WEB_PASSWORD"])
    url = "http://" + args.host
    def state():
        response = session.get(url + "/api/state", timeout=20)
        response.raise_for_status(); return response.json()
    deadline = time.monotonic() + 60
    while time.monotonic() < deadline:
        current = state()
        if current["firmware"] == expected_version() and current["weather_available"] == "ON": break
        time.sleep(.5)
    else: raise SystemExit("Weather not ready: " + current.get("weather_error", "unknown error"))
    assert current["weather_source"] in ("open-meteo", "home-assistant", "open-meteo-ha")
    assert -90 <= current["weather_temperature"] <= 70
    assert 0 <= current["weather_humidity"] <= 100
    assert current["time_valid"] and not current["weather_error"]
    broker = MqttProbe(config); base = "home/" + args.device
    broker.subscribe([base + "/weather_availability", base + "/state"])
    online = False; snapshot = None; deadline = time.monotonic() + 10
    try:
        while time.monotonic() < deadline:
            broker.socket.settimeout(max(.1, deadline - time.monotonic()))
            try: kind, body = broker.read()
            except socket.timeout: break
            if kind >> 4 != 3: continue
            size = struct.unpack("!H", body[:2])[0]
            topic, payload = body[2:2+size].decode(), body[2+size:].decode()
            if topic == base + "/weather_availability": online = payload == "online"
            elif topic == base + "/state": snapshot = json.loads(payload)
            if online and snapshot and snapshot.get("weather_available") == "ON": break
        assert online and snapshot and snapshot["weather_available"] == "ON"
        assert snapshot["weather_source"] == current["weather_source"]
        assert abs(snapshot["weather_temperature"] - current["weather_temperature"]) < .01
        print("PASS: live weather, verified clock, retained MQTT weather availability and readings")
        if args.test_unavailable:
            assert current["weather_source"] == "open-meteo", "Run this case when direct fallback is active"
            broker.publish(base + "/weather/set", '{"available":false}')
            time.sleep(.5)
            after = state()
            assert after["weather_available"] == "ON" and after["weather_source"] == "open-meteo"
            assert after["relay_mask"] == current["relay_mask"] and after["pwm"] == current["pwm"]
            assert after["rejected_commands"] == current["rejected_commands"]
            print("PASS: unavailable HA feed does not erase working direct fallback or alter outputs")
    finally: broker.close()
    print(json.dumps({key: current.get(key) for key in ("firmware", "weather_source", "weather_temperature", "weather_humidity", "weather_cloud_cover", "weather_precipitation", "weather_condition", "weather_age_s")}))

if __name__ == "__main__":
    main()
