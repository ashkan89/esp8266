"""Validate OTA rejection, then optionally upload a configured production image.

An accepted --upload turns all outputs OFF and restarts the device. Secrets are
read privately and redacted from uploader output before it reaches the console.
"""
import argparse
from pathlib import Path
import subprocess
import sys
import time
import requests
from requests.auth import HTTPDigestAuth
from check_device import credentials, expected_version

ROOT = Path(__file__).resolve().parents[1]

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", required=True)
    parser.add_argument("--upload", action="store_true")
    args = parser.parse_args()
    config = credentials()
    framework = Path.home() / ".platformio/packages/framework-arduinoespressif8266"
    uploader = framework / "tools/espota.py"
    image = ROOT / ".pio/build/nodemcuv2/firmware.bin"
    if not uploader.exists() or not image.exists():
        raise SystemExit("Build nodemcuv2 first; OTA uploader and production image are required.")
    session = requests.Session()
    session.auth = HTTPDigestAuth(config["WEB_USER"], config["WEB_PASSWORD"])
    url = "http://" + args.host
    def state():
        response = session.get(url + "/api/state", timeout=4)
        response.raise_for_status(); return response.json()
    def upload(password):
        try:
            result = subprocess.run([sys.executable, str(uploader), "-i", args.host,
                                     "-p", "8266", "-a", password, "-f", str(image)],
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                    text=True, errors="replace", timeout=60)
        except subprocess.TimeoutExpired:
            # Timeout exceptions include argv, which contains the private password.
            print("OTA uploader timed out; inspect device logs and firewall settings.")
            return 1
        output = result.stdout
        for key, value in config.items():
            if "PASSWORD" in key and value: output = output.replace(value, "[REDACTED]")
        print(output[-1500:])
        return result.returncode
    before = state()
    assert not before["locked"]
    assert upload("deliberately-invalid-password-for-test") != 0, "Wrong OTA password unexpectedly accepted"
    time.sleep(1)
    after = state()
    assert not after["locked"] and after["relay_mask"] == before["relay_mask"]
    assert after["pwm"] == before["pwm"]
    print("PASS: incorrect OTA password rejected without locking or changing outputs")
    if args.upload:
        assert upload(config["OTA_PASSWORD"]) == 0, "Authenticated OTA upload failed"
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            try:
                current = state()
                if current["firmware"] == expected_version() and not current["serial_diagnostics"] and current["mqtt_ready"]:
                    assert not current["locked"] and current["relay_mask"] == 0 and current["pwm"] == [0,0,0,0]
                    print("PASS: authenticated OTA production image booted, MQTT recovered, all outputs OFF")
                    break
            except requests.RequestException: pass
            time.sleep(.5)
        else: raise SystemExit("Upload finished; device recovery check failed.")

if __name__ == "__main__":
    main()
