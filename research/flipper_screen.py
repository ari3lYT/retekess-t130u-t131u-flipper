"""Capture Flipper's 128x64 display over the official USB protobuf RPC API.

The official flipperzero_protobuf_py repository is vendored under work/ for
this investigation. This script only reads the screen and closes the port.
"""

import argparse
import pathlib
import sys

from PIL import Image


sys.path.insert(0, str(pathlib.Path(__file__).parent / "flipperzero_protobuf_py"))
from flipperzero_protobuf.flipper_proto import FlipperProto  # noqa: E402


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("destination", type=pathlib.Path)
    parser.add_argument("--port", default="/dev/ttyACM0")
    parser.add_argument("--scale", type=int, default=4)
    args = parser.parse_args()

    device = FlipperProto(args.port)
    try:
        data = device.rpc_gui_snapshot_screen()
    finally:
        device._serial.close()
    if len(data) != 1024:
        raise RuntimeError(f"Unexpected frame length: {len(data)}")

    screen = Image.new("1", (128, 64), 1)
    for page in range(8):
        for x in range(128):
            column = data[page * 128 + x]
            for bit in range(8):
                screen.putpixel((x, page * 8 + bit), 0 if column & (1 << bit) else 1)
    if args.scale != 1:
        screen = screen.resize((128 * args.scale, 64 * args.scale), Image.Resampling.NEAREST)
    args.destination.parent.mkdir(parents=True, exist_ok=True)
    screen.save(args.destination)
    print(args.destination)


if __name__ == "__main__":
    main()
