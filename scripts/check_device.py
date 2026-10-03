"""Check a live device using private secrets without printing credentials.

Optional --test-led toggles only the onboard LED and restores its initial state.
Relay and PWM loads are never energized by this script.
"""
import argparse
import json
from pathlib import Path
import re
import socket
import struct
import time
import uuid
import requests
from requests.auth import HTTPDigestAuth

ROOT = Path(__file__).resolve().parents[1]

def expected_version():
    text = (ROOT / "include/config.h").read_text(encoding="utf-8")
    match = re.search(r'version\[\]\s*=\s*"([^"]+)"', text)
    if not match: raise ValueError("Firmware version missing from config.h")
    return match.group(1)

def credentials():
    text = (ROOT / "include/secrets.h").read_text(encoding="utf-8")
    result = {}
    for name, literal in re.findall(r'^#define\s+(\w+)\s+("(?:\\.|[^"\\])*")', text, re.M):
        result[name] = json.loads(literal)
    port = re.search(r'^#define\s+MQTT_PORT\s+(\d+)', text, re.M)
    result["MQTT_PORT"] = int(port.group(1)) if port else 1883
    return result

def field(text):
    data = text.encode(); return struct.pack("!H", len(data)) + data

class MqttProbe:
    def __init__(self, config):
        self.socket = socket.create_connection((config["MQTT_HOST"], config["MQTT_PORT"]), timeout=5)
        auth = bool(config["MQTT_USER"])
        body = field("MQTT") + bytes([4, 0xc2 if auth else 2]) + struct.pack("!H", 30)
        body += field("diagnostic_" + uuid.uuid4().hex[:12])
        if auth: body += field(config["MQTT_USER"]) + field(config["MQTT_PASSWORD"])
        self.send(0x10, body)
        packet_type, response = self.read()
        assert packet_type == 0x20 and response == b"\0\0", "Broker rejected connection"
    def send(self, header, body):
        size = len(body); remaining = bytearray()
        while True:
            value = size % 128; size //= 128
            remaining.append(value | (128 if size else 0))
            if not size: break
        self.socket.sendall(bytes([header]) + remaining + body)
    def exact(self, size):
        output = bytearray()
        while len(output) < size:
            chunk = self.socket.recv(size - len(output))
            if not chunk: raise ConnectionError("Broker closed connection")
            output.extend(chunk)
        return bytes(output)
    def read(self):
        header = self.exact(1)[0]; size = 0; multiplier = 1
        for _ in range(4):
            value = self.exact(1)[0]; size += (value & 127) * multiplier
            if not value & 128: break
            multiplier *= 128
        else: raise ValueError("Invalid MQTT remaining length")
        return header, self.exact(size)
    def subscribe(self, filters):
        self.send(0x82, b"\0\1" + b"".join(field(item) + b"\0" for item in filters))
    def publish(self, topic, payload):
        self.send(0x30, field(topic) + payload.encode())
    def close(self):
        try: self.send(0xe0, b"")
        finally: self.socket.close()

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", required=True)
    parser.add_argument("--device", required=True)
    parser.add_argument("--test-led", action="store_true")
    args = parser.parse_args()
    config = credentials(); url = "http://" + args.host
    session = requests.Session()
    session.auth = HTTPDigestAuth(config["WEB_USER"], config["WEB_PASSWORD"])
    def state():
        response = session.get(url + "/api/state", timeout=5); response.raise_for_status(); return response.json()
    assert requests.get(url + "/api/state", timeout=5).status_code == 401
    current = state()
    assert current["firmware"] == expected_version() and not current["locked"]
    assert session.get(url + "/", timeout=5).status_code == 200
    assert session.get(url + "/api/logs", timeout=5).status_code == 200
    command_url = url + "/api/command"
    bad = {"topic": "relay/17/set", "payload": "ON"}
    assert session.post(command_url, json=bad, timeout=5).status_code == 403
    header = {"X-Controller-Request": "1"}
    for invalid in (bad, {"topic": "relay/01/set", "payload": "on"},
                    {"topic": "pwm/1/set", "payload": '{"brightness":256}'}):
        assert session.post(command_url, json=invalid, headers=header, timeout=5).status_code == 400
    assert state()["relay_mask"] == current["relay_mask"]
    print("PASS: authenticated dashboard/state/logs, rejected unsafe or malformed requests, outputs unlocked")
    broker = MqttProbe(config)
    base = "home/" + args.device
    broker.subscribe([f"homeassistant/+/{args.device}/+/config", base + "/state", base + "/availability"])
    configs = set(); online = False; observed_state = None
    deadline = time.monotonic() + 15
    try:
        while time.monotonic() < deadline:
            broker.socket.settimeout(max(.1, deadline - time.monotonic()))
            try: kind, body = broker.read()
            except socket.timeout: break
            if kind >> 4 != 3: continue
            length = struct.unpack("!H", body[:2])[0]
            target = body[2:2+length].decode(); payload = body[2+length:].decode()
            if target.endswith("/config"):
                doc = json.loads(payload)
                assert doc["unique_id"].startswith(args.device + "_")
                configs.add(target)
            elif target == base + "/availability": online = payload == "online"
            elif target == base + "/state": observed_state = json.loads(payload)
            if len(configs) == 39 and online and observed_state is not None: break
        assert len(configs) == 39, f"Expected 39 retained discovery records, got {len(configs)} (check read ACL)"
        assert online and observed_state is not None
        print("PASS: MQTT credentials, 39 retained discovery records, online availability and valid telemetry")
        if args.test_led:
            original = current["led"]
            try:
                for value in ("ON", "OFF"):
                    broker.publish(base + "/led/set", value)
                    deadline = time.monotonic() + 5
                    while time.monotonic() < deadline:
                        if state()["led"] == value: break
                        time.sleep(.2)
                    else: raise AssertionError("MQTT LED command did not apply")
            finally:
                broker.publish(base + "/led/set", original)
            print("PASS: live MQTT ON/OFF commands; onboard LED restored")
    finally: broker.close()
    current = state()
    print(json.dumps({key: current.get(key) for key in ("ip", "firmware", "mqtt_connected", "mqtt_ready", "relay_mask", "sensor_healthy", "serial_diagnostics", "heap", "uptime")}))

if __name__ == "__main__":
    main()
