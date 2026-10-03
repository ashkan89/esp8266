"""Exercise the real one-minute OLED timeout; restore display settings afterward."""
import argparse
import time
import requests
from requests.auth import HTTPDigestAuth
from check_device import credentials, MqttProbe

parser = argparse.ArgumentParser()
parser.add_argument("--host", required=True)
parser.add_argument("--device", required=True)
args = parser.parse_args()
config = credentials()
session = requests.Session()
session.auth = HTTPDigestAuth(config["WEB_USER"], config["WEB_PASSWORD"])
session.headers["X-Controller-Request"] = "1"
url = "http://" + args.host

def state():
    r = session.get(url + "/api/state", timeout=5)
    r.raise_for_status()
    return r.json()

def command(topic, value, expected=200):
    r = session.post(url + "/api/command", json={"topic": topic, "payload": value}, timeout=5)
    assert r.status_code == expected, (topic, value, r.status_code)

initial = state()
assert initial["oled_present"]
try:
    command("oled/set", "ON")
    command("oled_timeout/set", "1")
    assert 59 <= state()["oled_remaining_s"] <= 60
    time.sleep(2.2)
    assert 56 <= state()["oled_remaining_s"] <= 58
    command("oled/set", "ON")
    assert 59 <= state()["oled_remaining_s"] <= 60
    time.sleep(2.2)
    command("oled_page/set", "clock")
    assert 59 <= state()["oled_remaining_s"] <= 60
    for value in ("0", "-1", "1.5", "1441", "garbage"):
        command("oled_timeout/set", value, 400)
        assert state()["oled_timeout_minutes"] == 1
    command("oled_timeout/set", "7")
    assert state()["oled_timeout_minutes"] == 7
    command("oled_timeout/set", "OFF")
    assert state()["oled_timeout_minutes"] == 0 and state()["oled_remaining_s"] == 0
    command("oled/set", "OFF")
    command("oled_timeout/set", "2")
    assert state()["oled"] == "OFF", "Changing timeout must not wake an OFF screen"
    command("oled/set", "ON")
    broker = MqttProbe(config)
    try:
        broker.publish("home/" + args.device + "/oled_timeout/set", "1")
        deadline = time.monotonic() + 5
        while state()["oled_timeout_minutes"] != 1:
            assert time.monotonic() < deadline, "MQTT timeout command did not apply"
            time.sleep(.2)
    finally:
        broker.close()
    print("PASS: custom minutes, OFF, invalid input rejection, countdown and wake/page reset, MQTT configuration", flush=True)
    started = time.monotonic()
    deadline = time.monotonic() + 65
    while True:
        current = state()
        assert (current["relay_mask"], current["pwm"]) == (initial["relay_mask"], initial["pwm"])
        if current["oled"] == "OFF": break
        assert time.monotonic() < deadline, "OLED did not turn off at its deadline"
        time.sleep(1)
    assert current["oled_remaining_s"] == 0 and current["oled_timeout_minutes"] == 1
    assert time.monotonic() - started >= 58, "OLED switched off before the one-minute deadline"
    print("PASS: actual one-minute auto-off; relay/PWM outputs unchanged", flush=True)
finally:
    value = str(initial["oled_timeout_minutes"]) if initial["oled_timeout_minutes"] else "OFF"
    command("oled_timeout/set", value)
    command("oled_page/set", initial["oled_mode"])
    command("oled/set", initial["oled"])
