"""Wait for the RSSI probe to finish and copy its CSV from Flipper CLI."""

import hashlib
import pathlib
import re
import sys
import time

import serial


def main() -> None:
    destination = pathlib.Path(sys.argv[1])
    remote_file = sys.argv[2] if len(sys.argv) > 2 else "last.csv"
    with serial.Serial("/dev/ttyACM0", 230400, timeout=0.2) as port:
        port.read(8192)
        for _ in range(80):
            port.reset_input_buffer()
            port.write(b"loader info\r")
            time.sleep(0.4)
            if b"No application is running" in port.read(2048):
                break
            time.sleep(0.6)
        else:
            raise RuntimeError("RSSI probe did not finish")

        port.reset_input_buffer()
        port.write(f"storage read /ext/retekess_probe/{remote_file}\r".encode())
        response = bytearray()
        deadline = time.monotonic() + 120
        while time.monotonic() < deadline:
            chunk = port.read(4096)
            if chunk:
                response.extend(chunk)
                if response.endswith(b">: "):
                    break
        size_match = re.search(rb"Size: (\d+)\r\n", response)
        if size_match is None:
            raise RuntimeError(f"Missing file size: {response[:200]!r}")
        size = int(size_match.group(1))
        data = bytes(response[size_match.end() : size_match.end() + size])
        if len(data) != size or b"," not in data.split(b"\n", 1)[0] or not data.endswith(b"\n"):
            raise RuntimeError("Incomplete CSV transfer")
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(data)
        print(
            f"{destination}: {data.count(bytes([10])) - 1} rows, "
            f"{size} bytes, sha256={hashlib.sha256(data).hexdigest()}"
        )


if __name__ == "__main__":
    main()
