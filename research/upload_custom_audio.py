from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "work/flipperzero_protobuf_py"))
from flipperzero_protobuf.flipper_proto import FlipperProto

device = FlipperProto("/dev/ttyACM0")
try:
    for source, target in (
        (ROOT / "work/tx_custom_audio/dist/retekess_tx_custom_audio.fap",
         "/ext/apps/Sub-GHz/retekess_tx_custom_audio.fap"),
        (ROOT / "outputs/captures/custom_alaw_radio.bin",
         "/ext/retekess_probe/custom_alaw_radio.bin"),
    ):
        data = source.read_bytes()
        device.rpc_write(target, data)
        print(target, len(data), "bytes", "verified", device.rpc_read(target) == data)
finally:
    device._serial.close()
