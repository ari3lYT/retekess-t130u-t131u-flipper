"""Bundle only the current reproducible source and bit templates."""

from pathlib import Path
import zipfile

ROOT = Path(__file__).resolve().parents[1]
items = (
    ("outputs/retekess_audio_encoder.py", "retekess_audio_encoder.py"),
    ("outputs/captures/voice_frames_fullspeed.bin", "captures/voice_frames_fullspeed.bin"),
    ("outputs/CUSTOM_AUDIO_README.md", "CUSTOM_AUDIO_README.md"),
    ("outputs/CONTROL_README.md", "CONTROL_README.md"),
    ("work/rx_control/application.fam", "flipper_rx_control/application.fam"),
    ("work/rx_control/retekess_rx_monitor.c", "flipper_rx_control/retekess_rx_monitor.c"),
    ("work/rx_control/frame_generated.h", "flipper_rx_control/frame_generated.h"),
    ("work/rx_control/retekess_rx_monitor.png", "flipper_rx_control/retekess_rx_monitor.png"),
    ("work/tx_custom_audio/application.fam", "flipper_custom_audio/application.fam"),
    ("work/tx_custom_audio/retekess_tx_recording_test.c", "flipper_custom_audio/retekess_tx_recording_test.c"),
    ("work/make_chs_clean.py", "analysis/make_chs_clean.py"),
    ("work/make_chs19_synth.py", "analysis/make_chs19_synth.py"),
    ("work/make_chs_target.py", "analysis/make_chs_target.py"),
    ("work/analyze_chs_payload.py", "analysis/analyze_chs_payload.py"),
    ("work/analyze_ch17_capture.py", "analysis/analyze_channel_capture.py"),
    ("work/packet_capture_ch1/retekess_packet_capture.c", "flipper_packet_capture_ch1/retekess_packet_capture.c"),
    ("work/packet_capture_ch1/application.fam", "flipper_packet_capture_ch1/application.fam"),
    ("work/packet_capture_ch17/retekess_packet_capture.c", "flipper_packet_capture_ch17/retekess_packet_capture.c"),
    ("work/packet_capture_ch17/application.fam", "flipper_packet_capture_ch17/application.fam"),
    ("work/make_control_header.py", "analysis/make_control_header.py"),
    ("work/analyze_radio_tail.py", "analysis/analyze_radio_tail.py"),
)
dest = ROOT / "outputs/retekess_protocol_tools_source.zip"
with zipfile.ZipFile(dest, "w", compression=zipfile.ZIP_DEFLATED) as archive:
    for source, name in items:
        archive.write(ROOT / source, arcname=name)
print(dest, dest.stat().st_size, "bytes")
