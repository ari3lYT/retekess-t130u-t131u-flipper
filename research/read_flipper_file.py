"""Read one SD file through Flipper CLI `storage read`, preserving binary bytes."""

import hashlib
from pathlib import Path
import re
import sys
import time

import serial


remote = sys.argv[1]
local = Path(sys.argv[2])
with serial.Serial("/dev/ttyACM0", 230400, timeout=0.2) as port:
    port.read(8192)
    for _ in range(30):
        port.reset_input_buffer()
        port.write(b"loader info\r")
        time.sleep(.25)
        info = port.read(2048)
        if b"No application is running" in info:
            break
        time.sleep(.25)
    else:
        raise RuntimeError("app still running")
    port.reset_input_buffer()
    port.write(f"storage read {remote}\r".encode())
    response = bytearray()
    size = None
    start = None
    deadline = time.monotonic() + 25
    while time.monotonic() < deadline:
        response.extend(port.read(8192))
        if size is None:
            match = re.search(rb"Size: (\d+)\r\n", response)
            if match:
                size = int(match.group(1))
                start = match.end()
        if size is not None and len(response) - start >= size:
            data = bytes(response[start:start + size])
            local.parent.mkdir(parents=True, exist_ok=True)
            local.write_bytes(data)
            print(local, size, hashlib.sha256(data).hexdigest())
            break
    else:
        raise RuntimeError(f"incomplete transfer: {remote}, {len(response)} bytes read")
