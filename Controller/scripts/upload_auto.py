"""Upload the correct PlatformIO firmware to connected ESP32-C3/S3 boards.

Run with PlatformIO's Python: python scripts/upload_auto.py
"""

from __future__ import annotations

import os
import re
import subprocess
import sys
import time
from pathlib import Path

from serial.tools import list_ports


PROJECT_DIR = Path(__file__).resolve().parents[1]
PLATFORMIO_DIR = Path(os.environ.get("PLATFORMIO_CORE_DIR", Path.home() / ".platformio"))
ESPTOOL = PLATFORMIO_DIR / "packages" / "tool-esptoolpy" / "esptool.py"
PIO = Path(sys.executable).with_name("platformio.exe")

# USB identifiers observed on these boards in normal and ROM download mode.
SUPPORTED_USB_IDS = {(0x303A, 0x1001), (0x2341, 0x0070)}
ENV_FOR_CHIP = {"ESP32-C3": "controller", "ESP32-S3": "receiver_s3_nano"}
CHIP_PATTERN = re.compile(r"Chip is (ESP32-[CS]3)\b")
MAC_PATTERN = re.compile(r"MAC:\s*([0-9a-f:]{17})", re.IGNORECASE)


def connected_ports():
    return {
        port.device: port
        for port in list_ports.comports()
        if (port.vid, port.pid) in SUPPORTED_USB_IDS
    }


def identify(port_name: str):
    result = subprocess.run(
        [sys.executable, str(ESPTOOL), "--chip", "auto", "--port", port_name,
         "--after", "no_reset", "read_mac"],
        cwd=PROJECT_DIR,
        capture_output=True,
        text=True,
        timeout=15,
        check=False,
    )
    output = result.stdout + result.stderr
    chip = CHIP_PATTERN.search(output)
    mac = MAC_PATTERN.search(output)
    if result.returncode != 0 or not chip or not mac:
        return None
    return chip.group(1), mac.group(1).lower()


def main() -> int:
    if not ESPTOOL.is_file() or not PIO.is_file():
        print("PlatformIO/esptool niet gevonden. Gebruik de PlatformIO Python-omgeving.", file=sys.stderr)
        return 1

    found = {}  # MAC -> (chip, bootloader port)
    failed_ports = set()
    for attempt in range(5):
        ports = connected_ports()
        to_probe = [name for name in ports if name not in {p for _, p in found.values()}]
        for port_name in to_probe:
            try:
                identity = identify(port_name)
            except subprocess.TimeoutExpired:
                identity = None
            if identity is None:
                failed_ports.add(port_name)
                continue
            chip, mac = identity
            if chip not in ENV_FOR_CHIP:
                print(f"Onbekende ESP32-chip {chip} op {port_name}; upload afgebroken.", file=sys.stderr)
                return 1
            found[mac] = (chip, port_name)
            failed_ports.discard(port_name)
            print(f"Gevonden: {chip} op {port_name} ({mac})", flush=True)

        # A running S3 can change from its application COM port to its ROM
        # COM port during the probe. Give Windows time to enumerate it.
        current_ports = connected_ports()
        recognized_ports = {port for _, port in found.values()}
        if (attempt >= 2 and found
                and not any(name in current_ports for name in failed_ports)
                and all(name in recognized_ports for name in current_ports)):
            break
        if attempt < 4:
            time.sleep(1)

    if not found:
        print("Geen ondersteunde ESP32-C3 of ESP32-S3 gevonden.", file=sys.stderr)
        return 1

    chips = [chip for chip, _ in found.values()]
    if len(chips) != len(set(chips)):
        print("Meerdere borden van hetzelfde type gevonden; sluit er één per type aan.", file=sys.stderr)
        return 1

    if any(name in connected_ports() for name in failed_ports):
        print("Een aangesloten ESP32 kon niet worden herkend; upload afgebroken.", file=sys.stderr)
        return 1

    for chip, port_name in found.values():
        env = ENV_FOR_CHIP[chip]
        print(f"Upload {env} naar {chip} op {port_name}...", flush=True)
        result = subprocess.run(
            [str(PIO), "run", "-e", env, "-t", "upload", "--upload-port", port_name],
            cwd=PROJECT_DIR,
            check=False,
        )
        if result.returncode != 0:
            print(f"Upload naar {chip} mislukt.", file=sys.stderr)
            return result.returncode

    print("Alle aangesloten borden hebben de juiste firmware.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
