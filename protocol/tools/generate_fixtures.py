#!/usr/bin/env python3
"""Writes protocol/fixtures from docs/PROTOCOL.md, independently of the C++ implementation.

Usage:
  generate_fixtures.py          write every fixture and the manifest
  generate_fixtures.py --check  exit 1 if the files on disk differ from what would be written
"""

from __future__ import annotations

import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
FIXTURES = ROOT / "fixtures"

MAGIC = b"OP"
VERSION = 1

# osplay_result codes (docs/PROTOCOL.md, "Error codes")
TRUNCATED = 3
BAD_MAGIC = 4
UNSUPPORTED_VERSION = 5
UNKNOWN_TYPE = 6
TRAILING_BYTES = 7
VALUE_OUT_OF_RANGE = 8
INVALID_STRING = 9
TOO_LARGE = 10

MAX_VIDEO_PAYLOAD = 1154
MAX_CURSOR_PAYLOAD = 1155


def u8(v: int) -> bytes:
    return struct.pack("<B", v)


def u16(v: int) -> bytes:
    return struct.pack("<H", v)


def u32(v: int) -> bytes:
    return struct.pack("<I", v)


def i32(v: int) -> bytes:
    return struct.pack("<i", v)


def u64(v: int) -> bytes:
    return struct.pack("<Q", v)


def string(text: str | bytes) -> bytes:
    raw = text.encode("utf-8") if isinstance(text, str) else text
    return u8(len(raw)) + raw


def control(message_type: int, body: bytes = b"", magic: bytes = MAGIC, version: int = VERSION) -> bytes:
    return magic + u8(version) + u8(message_type) + body


def media(packet_type: int, sequence: int, body: bytes, magic: bytes = MAGIC, version: int = VERSION) -> bytes:
    return magic + u8(version) + u8(packet_type) + u64(sequence) + body


# --- Control message bodies --------------------------------------------------------------------

def hello(role: int, platform: int, name: str | bytes, minor: int = 0, reserved: int = 0) -> bytes:
    return control(0x01, u8(role) + u8(platform) + u8(minor) + u8(reserved) + string(name))


def handshake(step: int, payload: bytes, declared_length: int | None = None) -> bytes:
    length = len(payload) if declared_length is None else declared_length
    return control(0x02, u8(step) + u8(0) + u16(length) + payload)


def capabilities(codecs: int, max_streams: int, flags: int, width: int, height: int, refresh: int) -> bytes:
    return control(0x04, u32(codecs) + u8(max_streams) + u8(flags) + u16(width) + u16(height) + u16(refresh))


def monitor(monitor_id: int, width: int, height: int, refresh_mhz: int, mm_w: int, mm_h: int, scale: int,
            name: str) -> bytes:
    return (u8(monitor_id) + u16(width) + u16(height) + u32(refresh_mhz) + u16(mm_w) + u16(mm_h) + u16(scale)
            + string(name))


def viewer_monitors(*monitors: bytes, count: int | None = None) -> bytes:
    return control(0x05, u8(len(monitors) if count is None else count) + b"".join(monitors))


def stream_config(stream_id: int = 0, monitor_id: int = 0, width: int = 2560, height: int = 1440,
                  refresh_mhz: int = 60_000, codec: int = 1, profile: int = 100, cursor_mode: int = 0,
                  bitrate_kbps: int = 30_000) -> bytes:
    return control(0x06, u8(stream_id) + u8(monitor_id) + u16(width) + u16(height) + u32(refresh_mhz) + u8(codec)
                   + u8(profile) + u8(cursor_mode) + u8(0) + u32(bitrate_kbps))


def nack(stream_id: int, frame_id: int, ranges: list[tuple[int, int]], count: int | None = None) -> bytes:
    body = u8(stream_id) + u32(frame_id) + u8(len(ranges) if count is None else count)
    body += b"".join(u16(first) + u16(length) for first, length in ranges)
    return control(0x0C, body)


def receiver_report(loss_permille: int = 12) -> bytes:
    return control(0x0D, u8(1) + u8(0) + u16(loss_permille) + u32(36_000) + u32(17) + u32(2_350) + u32(410))


# --- Media packet bodies -----------------------------------------------------------------------

TOKEN = bytes(range(0xA0, 0xB0))


def video(sequence: int, flags: int, frame_id: int, index: int, count: int, payload: bytes,
          stream_id: int = 0) -> bytes:
    body = u8(stream_id) + u8(flags) + u32(frame_id) + u16(index) + u16(count) + u64(5_000_123_456) + payload
    return media(0x02, sequence, body)


def cursor_position(visible: int = 1, stream_id: int = 0) -> bytes:
    body = u8(stream_id) + u8(visible) + u32(42) + i32(-5) + i32(1200) + u64(5_000_200_000)
    return media(0x03, 9, body)


def cursor_shape(width: int, height: int, index: int, count: int | None = None, payload: bytes | None = None,
                 hotspot: tuple[int, int] = (1, 2), stream_id: int = 0) -> bytes:
    total = width * height * 4
    expected_count = -(-total // MAX_CURSOR_PAYLOAD)
    if payload is None:
        start = index * MAX_CURSOR_PAYLOAD
        payload = bytes((start + i) % 256 for i in range(min(MAX_CURSOR_PAYLOAD, total - start)))
    body = (u8(stream_id) + u32(7) + u16(width) + u16(height) + u16(hotspot[0]) + u16(hotspot[1]) + u16(index)
            + u16(expected_count if count is None else count) + payload)
    return media(0x04, 11, body)


def annex_b_keyframe() -> bytes:
    sps = bytes.fromhex("000000016764001FACD940501EC8")
    pps = bytes.fromhex("0000000168EBE3CB22C0")
    idr = bytes.fromhex("0000000165888400") + bytes(range(32))
    return sps + pps + idr


# --- Fixture list ------------------------------------------------------------------------------

def fixtures() -> list[tuple[str, str, str, int, str, bytes]]:
    """(file, kind, outcome, type-or-error, description, bytes)"""
    v, e = "valid", "error"
    full = bytes(i % 251 for i in range(MAX_VIDEO_PAYLOAD))
    return [
        # Control: one or more valid examples of every message type.
        ("control/hello_viewer.bin", "control", v, 0x01, "Hello from a Windows viewer",
         hello(2, 2, "Akinalp's PC")),
        ("control/hello_host_utf8.bin", "control", v, 0x01, "Hello from a macOS host with a UTF-8 name",
         hello(1, 1, "MacBook Air — Çalışma")),
        ("control/handshake_step1.bin", "control", v, 0x02, "Noise XX message 1",
         handshake(1, bytes(range(32)))),
        ("control/pairing_accept.bin", "control", v, 0x03, "PairingConfirm accepted", control(0x03, u8(1))),
        ("control/capabilities.bin", "control", v, 0x04, "Capabilities, H.264, two streams, cursor both ways",
         capabilities(1, 2, 0b11, 7680, 4320, 240)),
        ("control/viewer_monitors_two.bin", "control", v, 0x05, "Two lent monitors",
         viewer_monitors(monitor(0, 2560, 1440, 60_000, 597, 336, 100, "DELL U2723QE"),
                         monitor(1, 1920, 1080, 144_000, 527, 296, 100, ""))),
        ("control/stream_config.bin", "control", v, 0x06, "StreamConfig 2560x1440 60 Hz High profile",
         stream_config()),
        ("control/stream_start.bin", "control", v, 0x07, "StreamStart stream 1", control(0x07, u8(1))),
        ("control/stream_pause.bin", "control", v, 0x08, "StreamPause stream 0", control(0x08, u8(0))),
        ("control/stream_resume.bin", "control", v, 0x09, "StreamResume stream 0", control(0x09, u8(0))),
        ("control/stream_stop.bin", "control", v, 0x0A, "StreamStop stream 3", control(0x0A, u8(3))),
        ("control/keyframe_request.bin", "control", v, 0x0B, "KeyframeRequest after frame loss",
         control(0x0B, u8(0) + u8(2))),
        ("control/nack.bin", "control", v, 0x0C, "Nack with two ranges", nack(0, 123_456, [(3, 2), (10, 1)])),
        ("control/receiver_report.bin", "control", v, 0x0D, "ReceiverReport", receiver_report()),
        ("control/ping.bin", "control", v, 0x0E, "Ping", control(0x0E, u32(77) + u64(1_000_000))),
        ("control/pong.bin", "control", v, 0x0F, "Pong",
         control(0x0F, u32(77) + u64(1_000_000) + u64(9_000_500) + u64(9_000_620))),
        ("control/heartbeat.bin", "control", v, 0x10, "Heartbeat", control(0x10)),
        ("control/error_incompatible.bin", "control", v, 0x11, "Error incompatible version",
         control(0x11, u16(1))),
        ("control/error_unknown_code.bin", "control", v, 0x11, "Error with a code from a newer version",
         control(0x11, u16(999))),
        ("control/bye.bin", "control", v, 0x12, "Bye going to sleep", control(0x12, u8(3))),
        ("control/media_setup.bin", "control", v, 0x13, "MediaSetup", control(0x13, u16(50_123) + TOKEN)),

        # Control: malformed.
        ("malformed/control_empty.bin", "control", e, TRUNCATED, "empty input", b""),
        ("malformed/control_prefix_only.bin", "control", e, TRUNCATED, "prefix without type", MAGIC + u8(VERSION)),
        ("malformed/control_bad_magic.bin", "control", e, BAD_MAGIC, "magic is not OP",
         control(0x10, magic=b"XP")),
        ("malformed/control_version_2.bin", "control", e, UNSUPPORTED_VERSION, "version 2",
         control(0x10, version=2)),
        ("malformed/control_unknown_type.bin", "control", e, UNKNOWN_TYPE, "type 0x7F", control(0x7F)),
        ("malformed/control_too_large.bin", "control", e, TOO_LARGE, "4097-byte message",
         control(0x10) + bytes(4093)),
        ("malformed/hello_trailing_byte.bin", "control", e, TRAILING_BYTES, "Hello with one extra byte",
         hello(2, 2, "PC") + b"\x00"),
        ("malformed/hello_bad_role.bin", "control", e, VALUE_OUT_OF_RANGE, "role 3", hello(3, 2, "PC")),
        ("malformed/hello_reserved_set.bin", "control", e, VALUE_OUT_OF_RANGE, "reserved byte is 1",
         hello(2, 2, "PC", reserved=1)),
        ("malformed/hello_empty_name.bin", "control", e, INVALID_STRING, "device name is empty", hello(2, 2, "")),
        ("malformed/hello_invalid_utf8.bin", "control", e, INVALID_STRING, "name is not UTF-8",
         hello(2, 2, b"PC\xff")),
        ("malformed/hello_control_char.bin", "control", e, INVALID_STRING, "name contains a newline",
         hello(2, 2, "PC\nname")),
        ("malformed/hello_overlong_utf8.bin", "control", e, INVALID_STRING, "overlong UTF-8 encoding",
         hello(2, 2, b"\xc0\xaf")),
        ("malformed/hello_name_65_bytes.bin", "control", e, INVALID_STRING, "65-byte name",
         hello(2, 2, "a" * 65)),
        ("malformed/hello_name_past_end.bin", "control", e, TRUNCATED, "name length points past the end",
         hello(2, 2, "PC")[:-1]),
        ("malformed/handshake_step_4.bin", "control", e, VALUE_OUT_OF_RANGE, "step 4", handshake(4, b"\x01")),
        ("malformed/handshake_empty_payload.bin", "control", e, VALUE_OUT_OF_RANGE, "payload length 0",
         handshake(1, b"")),
        ("malformed/handshake_short_payload.bin", "control", e, TRUNCATED, "payload shorter than declared",
         handshake(1, bytes(10), declared_length=32)),
        ("malformed/pairing_decision_0.bin", "control", e, VALUE_OUT_OF_RANGE, "decision 0", control(0x03, u8(0))),
        ("malformed/capabilities_reserved_codec.bin", "control", e, VALUE_OUT_OF_RANGE, "reserved codec bit",
         capabilities(0b11, 2, 0, 7680, 4320, 240)),
        ("malformed/capabilities_no_codec.bin", "control", e, VALUE_OUT_OF_RANGE, "no codec",
         capabilities(0, 2, 0, 7680, 4320, 240)),
        ("malformed/viewer_monitors_none.bin", "control", e, VALUE_OUT_OF_RANGE, "count 0", viewer_monitors()),
        ("malformed/viewer_monitors_duplicate_id.bin", "control", e, VALUE_OUT_OF_RANGE, "same monitor ID twice",
         viewer_monitors(monitor(1, 1920, 1080, 60_000, 0, 0, 100, "A"),
                         monitor(1, 1920, 1080, 60_000, 0, 0, 100, "B"))),
        ("malformed/viewer_monitors_count_mismatch.bin", "control", e, TRUNCATED, "count 2, one monitor",
         viewer_monitors(monitor(0, 1920, 1080, 60_000, 0, 0, 100, "A"), count=2)),
        ("malformed/stream_config_odd_width.bin", "control", e, VALUE_OUT_OF_RANGE, "width 1921",
         stream_config(width=1921)),
        ("malformed/stream_config_bad_profile.bin", "control", e, VALUE_OUT_OF_RANGE, "profile 88",
         stream_config(profile=88)),
        ("malformed/stream_start_stream_4.bin", "control", e, VALUE_OUT_OF_RANGE, "stream 4",
         control(0x07, u8(4))),
        ("malformed/keyframe_reason_5.bin", "control", e, VALUE_OUT_OF_RANGE, "reason 5",
         control(0x0B, u8(0) + u8(5))),
        ("malformed/nack_no_ranges.bin", "control", e, VALUE_OUT_OF_RANGE, "range_count 0", nack(0, 1, [])),
        ("malformed/nack_range_overflow.bin", "control", e, VALUE_OUT_OF_RANGE, "range ends past 4096",
         nack(0, 1, [(4090, 10)])),
        ("malformed/receiver_report_loss.bin", "control", e, VALUE_OUT_OF_RANGE, "loss 1001 permille",
         receiver_report(1001)),
        ("malformed/heartbeat_with_body.bin", "control", e, TRAILING_BYTES, "Heartbeat with a body",
         control(0x10, b"\x00")),
        ("malformed/bye_reason_0.bin", "control", e, VALUE_OUT_OF_RANGE, "reason 0", control(0x12, u8(0))),
        ("malformed/media_setup_port_0.bin", "control", e, VALUE_OUT_OF_RANGE, "UDP port 0",
         control(0x13, u16(0) + TOKEN)),

        # Media: valid examples of every packet type.
        ("media/udp_hello.bin", "media", v, 0x01, "UdpHello", media(0x01, 0, TOKEN)),
        ("media/video_keyframe_single.bin", "media", v, 0x02, "keyframe with SPS/PPS in one fragment",
         video(7, 0b11, 1, 0, 1, annex_b_keyframe())),
        ("media/video_middle_fragment.bin", "media", v, 0x02, "full middle fragment of three",
         video(8, 0, 2, 1, 3, full)),
        ("media/video_last_fragment.bin", "media", v, 0x02, "short last fragment of three",
         video(9, 0, 2, 2, 3, full[:100])),
        ("media/cursor_position.bin", "media", v, 0x03, "CursorPosition with negative x", cursor_position()),
        ("media/cursor_shape_single.bin", "media", v, 0x04, "16x16 shape in one fragment", cursor_shape(16, 16, 0)),
        ("media/cursor_shape_first.bin", "media", v, 0x04, "first of four fragments of a 32x32 shape",
         cursor_shape(32, 32, 0)),
        ("media/cursor_shape_last.bin", "media", v, 0x04, "last of four fragments of a 32x32 shape",
         cursor_shape(32, 32, 3)),

        # Media: malformed.
        ("malformed/media_truncated_header.bin", "media", e, TRUNCATED, "header cut at 8 bytes",
         media(0x01, 0, TOKEN)[:8]),
        ("malformed/media_bad_magic.bin", "media", e, BAD_MAGIC, "magic is not OP",
         media(0x01, 0, TOKEN, magic=b"OQ")),
        ("malformed/media_version_0.bin", "media", e, UNSUPPORTED_VERSION, "version 0",
         media(0x01, 0, TOKEN, version=0)),
        ("malformed/media_unknown_type.bin", "media", e, UNKNOWN_TYPE, "type 0x09", media(0x09, 0, TOKEN)),
        ("malformed/media_too_large.bin", "media", e, TOO_LARGE, "1185-byte plaintext datagram",
         video(1, 0, 1, 0, 1, bytes(MAX_VIDEO_PAYLOAD + 1))),
        ("malformed/udp_hello_short_token.bin", "media", e, TRUNCATED, "15-byte token", media(0x01, 0, TOKEN[:15])),
        ("malformed/udp_hello_trailing.bin", "media", e, TRAILING_BYTES, "17 bytes after the header",
         media(0x01, 0, TOKEN + b"\x00")),
        ("malformed/video_index_past_count.bin", "media", e, VALUE_OUT_OF_RANGE, "index 3 of 3",
         video(1, 0, 1, 3, 3, bytes(10))),
        ("malformed/video_count_0.bin", "media", e, VALUE_OUT_OF_RANGE, "fragment_count 0",
         video(1, 0, 1, 0, 0, bytes(10))),
        ("malformed/video_config_without_keyframe.bin", "media", e, VALUE_OUT_OF_RANGE,
         "codec config flag without keyframe", video(1, 0b10, 1, 0, 1, bytes(10))),
        ("malformed/video_reserved_flag.bin", "media", e, VALUE_OUT_OF_RANGE, "reserved flag bit",
         video(1, 0b100, 1, 0, 1, bytes(10))),
        ("malformed/video_short_middle_fragment.bin", "media", e, VALUE_OUT_OF_RANGE,
         "non-last fragment shorter than 1154 bytes", video(1, 0, 1, 0, 2, bytes(100))),
        ("malformed/video_empty_payload.bin", "media", e, VALUE_OUT_OF_RANGE, "no payload",
         video(1, 0, 1, 0, 1, b"")),
        ("malformed/video_stream_4.bin", "media", e, VALUE_OUT_OF_RANGE, "stream 4",
         video(1, 0, 1, 0, 1, bytes(10), stream_id=4)),
        ("malformed/cursor_visible_2.bin", "media", e, VALUE_OUT_OF_RANGE, "visible 2", cursor_position(visible=2)),
        ("malformed/cursor_shape_zero_width.bin", "media", e, VALUE_OUT_OF_RANGE, "width 0",
         cursor_shape(0, 16, 0, count=1, payload=b"\x00")),
        ("malformed/cursor_shape_hotspot_outside.bin", "media", e, VALUE_OUT_OF_RANGE, "hotspot x = width",
         cursor_shape(16, 16, 0, hotspot=(16, 0))),
        ("malformed/cursor_shape_wrong_count.bin", "media", e, VALUE_OUT_OF_RANGE, "fragment_count 5 for 32x32",
         cursor_shape(32, 32, 0, count=5)),
        ("malformed/cursor_shape_wrong_length.bin", "media", e, VALUE_OUT_OF_RANGE,
         "last fragment one byte short", cursor_shape(32, 32, 3, payload=bytes(630))),
    ]


def manifest(entries: list[tuple[str, str, str, int, str, bytes]]) -> str:
    lines = [
        "# Generated by protocol/tools/generate_fixtures.py from docs/PROTOCOL.md. Do not edit.",
        "# file\tkind\toutcome\ttype-or-error\tdescription",
    ]
    lines += [f"{name}\t{kind}\t{outcome}\t{detail}\t{description}"
              for name, kind, outcome, detail, description, _ in entries]
    return "\n".join(lines) + "\n"


def main() -> int:
    check = sys.argv[1:] == ["--check"]
    entries = fixtures()
    expected = {FIXTURES / name: data for name, *_, data in entries}
    expected[FIXTURES / "manifest.tsv"] = manifest(entries).encode()

    if check:
        stale = [str(path.relative_to(ROOT.parent)) for path, data in expected.items()
                 if not path.exists() or path.read_bytes() != data]
        on_disk = {p for p in FIXTURES.rglob("*") if p.is_file()}
        extra = [str(p.relative_to(ROOT.parent)) for p in sorted(on_disk - set(expected))]
        for path in stale:
            print(f"out of date: {path}", file=sys.stderr)
        for path in extra:
            print(f"not generated: {path}", file=sys.stderr)
        if stale or extra:
            print("Run protocol/tools/generate_fixtures.py", file=sys.stderr)
            return 1
        print(f"Fixtures up to date ({len(entries)} files).")
        return 0

    for path, data in expected.items():
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
    print(f"Wrote {len(entries)} fixtures and the manifest.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
