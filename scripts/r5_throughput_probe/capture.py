"""Trigger the one-second device-only MIDI throughput probe over USB CDC."""

import argparse
import time

import serial


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("port", help="Pico USB CDC port, for example /dev/cu.usbmodem101")
    args = parser.parse_args()

    with serial.Serial(args.port, 115200, timeout=0.25) as device:
        time.sleep(0.5)
        device.reset_input_buffer()
        device.write(b"g")
        deadline = time.monotonic() + 15
        saw_result = False
        while time.monotonic() < deadline:
            raw = device.readline()
            if not raw:
                continue
            line = raw.decode("utf-8", errors="replace").strip()
            print(line, flush=True)
            if line.startswith("R5_SPIKE,accepted="):
                saw_result = True
                break
    return 0 if saw_result else 1


if __name__ == "__main__":
    raise SystemExit(main())
