#!/usr/bin/env python3
"""Host side of the NUCLEO-H533RE hardware test image.

Reads the report of the board on the ST-LINK virtual COM port (VCP), checks
the VCP echo and the USB CDC-ACM echo of the board, and prints a summary.
The exit code is 0 only if all checks pass.

Needs pyserial: pip install pyserial
"""

import argparse
import os
import re
import sys
import threading
import time

import serial
from serial.tools import list_ports

STLINK_VID = 0x0483
TEST_VID = 0x1209
TEST_PID = 0x0001
CHECK_LINE = re.compile(r"^(PASS|FAIL) (\S+)(.*)$")
ECHO_SIZES = (1, 2, 63, 64, 65, 127, 128, 129, 256, 1000, 4096)
CHUNK = 1024


class BoardLog(threading.Thread):
    """Reads the lines of the board from the VCP in the background."""

    def __init__(self, port):
        super().__init__(daemon=True)
        self._port = port
        self._lines = []
        self._condition = threading.Condition()

    def run(self):
        pending = b""
        while True:
            data = self._port.read(256)
            if not data:
                continue
            pending += data
            while b"\n" in pending:
                raw, pending = pending.split(b"\n", 1)
                line = raw.decode(errors="replace").rstrip("\r")
                print(f"board| {line}", flush=True)
                with self._condition:
                    self._lines.append(line)
                    self._condition.notify_all()

    def count(self):
        with self._condition:
            return len(self._lines)

    def lines(self, start, end):
        with self._condition:
            return list(self._lines[start:end])

    def wait_for(self, pattern, start, timeout):
        """Returns the index of the first line at or after `start` that
        matches `pattern`, or None after `timeout` seconds."""
        regex = re.compile(pattern)
        deadline = time.monotonic() + timeout
        index = start
        with self._condition:
            while True:
                while index < len(self._lines):
                    if regex.search(self._lines[index]):
                        return index
                    index += 1
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    return None
                self._condition.wait(remaining)


class Results:
    def __init__(self):
        self.items = []

    def add(self, source, name, ok, hint=""):
        """source: "board" (the board printed the line already) or "host"."""
        self.items.append((source, name, ok))
        if source == "host":
            detail = f" ({hint})" if hint and not ok else ""
            print(f"host| {'PASS' if ok else 'FAIL'} {name}{detail}", flush=True)
        return ok

    def failed(self):
        return [f"{source}: {name}" for source, name, ok in self.items if not ok]


def find_stlink_vcp():
    ports = [p.device for p in list_ports.comports() if p.vid == STLINK_VID]
    if len(ports) != 1:
        sys.exit(f"found {len(ports)} ST-LINK ports {ports}: give the VCP with --vcp")
    return ports[0]


def find_cdc_port(serial_number, timeout):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        for port in list_ports.comports():
            if port.vid == TEST_VID and port.pid == TEST_PID:
                if serial_number is None or port.serial_number == serial_number:
                    return port.device
        time.sleep(0.2)
    return None


def read_exact(port, size, timeout):
    data = b""
    deadline = time.monotonic() + timeout
    while len(data) < size and time.monotonic() < deadline:
        data += port.read(size - len(data))
    return data


def echo(port, data):
    """Sends `data` in chunks and compares each echoed chunk."""
    for offset in range(0, len(data), CHUNK):
        chunk = data[offset:offset + CHUNK]
        port.write(chunk)
        if read_exact(port, len(chunk), 2.0) != chunk:
            return False
    return True


def board_report(log, results, timeout):
    """Waits for the SUMMARY line and adds the checks after the last BANNER
    line. Returns (index of SUMMARY, UID), or (None, None)."""
    print("Push the reset button B2 of the board.", flush=True)
    summary = log.wait_for(r"^SUMMARY ", 0, timeout)
    if summary is None:
        results.add("host", "board-report", False, f"no SUMMARY line in {timeout} s")
        return None, None
    lines = log.lines(0, summary)
    banners = [i for i, line in enumerate(lines) if line.startswith("BANNER")]
    first = banners[-1] if banners else 0
    uid = None
    for line in lines[first:]:
        match = CHECK_LINE.match(line)
        if match:
            results.add("board", match.group(2), match.group(1) == "PASS", match.group(3).strip(" ()"))
        elif line.startswith("UID "):
            uid = line[4:].strip()
    return summary, uid


def test_vcp_echo(log, vcp, results):
    token = f"hwtest-echo-{os.getpid()}-{int(time.time())}"
    start = log.count()
    vcp.write((token + "\r\n").encode())
    results.add("host", "vcp-echo", log.wait_for(re.escape(token), start, 2.0) is not None)


def test_usb(log, results, summary, uid):
    configured = log.wait_for(r"^USB configured", summary, 10.0)
    results.add("host", "usb-configured", configured is not None, "connect CN3 to this host")
    device = find_cdc_port(uid, 10.0)
    if not results.add("host", "usb-enumeration", device is not None,
                       f"no port {TEST_VID:04x}:{TEST_PID:04x} with serial number {uid}"):
        return
    start = log.count()
    with serial.Serial(device, timeout=0.5) as port:
        port.dtr = True
        results.add("host", "usb-dtr-open", log.wait_for(r"^USB terminal open", start, 3.0) is not None)
        time.sleep(0.1)
        port.reset_input_buffer()
        for size in ECHO_SIZES:
            results.add("host", f"usb-echo-{size}", echo(port, os.urandom(size)))
        data = os.urandom(64 * 1024)
        begin = time.monotonic()
        ok = echo(port, data)
        elapsed = time.monotonic() - begin
        if ok:
            print(f"host| 64 KiB echo in {elapsed:.2f} s: {len(data) / elapsed / 1024:.0f} KiB/s each way", flush=True)
        results.add("host", "usb-echo-64k", ok)
        start = log.count()
        port.dtr = False
    results.add("host", "usb-dtr-close", log.wait_for(r"^USB terminal closed", start, 3.0) is not None)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--vcp", help="ST-LINK VCP port, e.g. /dev/ttyACM0 or COM5 (default: the only ST-LINK port)")
    parser.add_argument("--timeout", type=float, default=120.0,
                        help="seconds to wait for the board report (default: 120)")
    args = parser.parse_args()

    results = Results()
    with serial.Serial(args.vcp or find_stlink_vcp(), 115200, timeout=0.1) as vcp:
        log = BoardLog(vcp)
        log.start()
        summary, uid = board_report(log, results, args.timeout)
        if summary is not None:
            test_vcp_echo(log, vcp, results)
            test_usb(log, results, summary, uid)
            # The board prints this check after the USB configuration.
            crs = log.wait_for(r"^(PASS|FAIL) crs-sync-usb-sof", summary, 5.0)
            if crs is None:
                results.add("host", "crs-sync-usb-sof", False, "no line from the board")
            else:
                results.add("board", "crs-sync-usb-sof", log.lines(crs, crs + 1)[0].startswith("PASS"))

    failed = results.failed()
    print(f"\nRESULT {len(results.items) - len(failed)} passed, {len(failed)} failed")
    for name in failed:
        print(f"  FAIL {name}")
    return 1 if failed or not results.items else 0


if __name__ == "__main__":
    sys.exit(main())
