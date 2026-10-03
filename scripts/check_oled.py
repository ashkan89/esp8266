"""Validate live OLED navigation and capture its actual framebuffer.

Only display settings change; initial mode and ON/OFF are restored. Optional
Pillow produces a contact sheet; PBM snapshots need no imaging dependency.
"""
import argparse
import io
import time
from pathlib import Path
import requests
from requests.auth import HTTPDigestAuth
from check_device import credentials, MqttProbe

parser = argparse.ArgumentParser()
parser.add_argument("--host", required=True)
parser.add_argument("--device", required=True)
args = parser.parse_args()
config = credentials()
url = "http://" + args.host
session = requests.Session()
session.auth = HTTPDigestAuth(config["WEB_USER"], config["WEB_PASSWORD"])
session.headers["X-Controller-Request"] = "1"
output = Path(__file__).resolve().parents[1] / ".pio" / "oled-captures"
output.mkdir(parents=True, exist_ok=True)

def state():
    r = session.get(url + "/api/state", timeout=5)
    r.raise_for_status()
    return r.json()

def command(topic, payload, expected=200):
    r = session.post(url + "/api/command", json={"topic": topic, "payload": payload}, timeout=5)
    assert r.status_code == expected, (topic, payload, r.status_code)

initial = state()
assert initial["oled_present"], "OLED not detected"
assert requests.get(url + "/api/oled", timeout=5).status_code == 401
captures = []
try:
    for page in ("clock", "room", "weather", "sky", "relays", "lights", "network"):
        command("oled_page/set", page)
        time.sleep(1.1)
        current = state()
        assert current["oled_mode"] == page and current["oled_page"] == page and current["oled"] == "ON"
        assert (current["relay_mask"], current["pwm"]) == (initial["relay_mask"], initial["pwm"])
        r = session.get(url + "/api/oled", timeout=5)
        r.raise_for_status()
        assert r.content.startswith(b"P4\n128 32\n") and len(r.content) == 522
        assert r.content[10:] != b"\xff" * 512, "Blank framebuffer"
        (output / (page + ".pbm")).write_bytes(r.content)
        captures.append((page, r.content))
    command("oled_page/set", "invalid", 400)
    assert state()["oled_mode"] == "network"
    broker = MqttProbe(config)
    try:
        broker.publish("home/" + args.device + "/oled_page/set", "weather")
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            if state()["oled_mode"] == "weather": break
            time.sleep(.2)
        else: raise AssertionError("MQTT page selection did not apply")
    finally:
        broker.close()
    command("oled_page/set", "AUTO")
    assert state()["oled_mode"] == "AUTO"
    print("PASS: authenticated framebuffer, seven pinned pages, invalid-page rejection, MQTT selection, AUTO mode; load outputs unchanged")
finally:
    command("oled_page/set", initial["oled_mode"])
    command("oled/set", initial["oled"])

try:
    from PIL import Image, ImageDraw
    sheet = Image.new("RGB", (552, len(captures) * 158 + 20), "#101820")
    draw = ImageDraw.Draw(sheet)
    for i, (page, data) in enumerate(captures):
        draw.text((20, 20 + i * 158), page.upper(), fill="white")
        image = Image.open(io.BytesIO(data)).convert("RGB").resize((512, 128), Image.Resampling.NEAREST)
        sheet.paste(image, (20, 38 + i * 158))
    sheet.save(output / "preview.png")
except ImportError:
    pass
print("OLED captures:", output)
