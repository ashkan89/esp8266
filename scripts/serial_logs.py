"""Capture bounded diagnostic UART output without leaving COM ports open."""
import argparse
from pathlib import Path
import sys
import time
import serial

parser = argparse.ArgumentParser()
parser.add_argument("--port", default="COM9")
parser.add_argument("--baud", type=int, default=115200)
parser.add_argument("--seconds", type=float, default=30)
parser.add_argument("--reset", action="store_true")
args = parser.parse_args()
sys.stdout.reconfigure(errors="backslashreplace")
output = Path(__file__).resolve().parents[1] / ".pio" / "serial-diagnostic.log"
output.parent.mkdir(exist_ok=True)
port = serial.Serial()
port.port, port.baudrate, port.timeout = args.port, args.baud, 0.2
port.dtr = port.rts = False
port.open()
try:
    if args.reset:
        port.rts = True
        time.sleep(0.1)
        port.rts = False
    until = time.monotonic() + args.seconds
    with output.open("wb") as capture:
        while time.monotonic() < until:
            data = port.read(512)
            if data:
                capture.write(data)
                print(data.decode("ascii", errors="backslashreplace"), end="", flush=True)
finally:
    port.close()
print("\nCapture saved in .pio/serial-diagnostic.log")
