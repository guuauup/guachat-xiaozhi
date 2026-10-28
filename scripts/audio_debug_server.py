#!/usr/bin/env python3
"""Receive the firmware's UDP audio-debug stream and store it as WAV files.

Enable the sender on the device:

    CONFIG_USE_AUDIO_DEBUGGER=y
    CONFIG_AUDIO_DEBUG_UDP_SERVER="192.168.1.50:8000"

`AudioDebugger::Feed()` (main/audio/audio_debugger.cc) sends one datagram per
captured microphone frame. The payload is raw little-endian int16 PCM with no
header, channel tag, or sample rate, so the rate and channel count below have to
match the board's audio path (16000 Hz mono on the GUACHAT 2G4).

Each moment of silence longer than `--gap`, or each `--segment-seconds` of
audio, closes the current WAV and starts a new one, so a session produces one
file per utterance instead of a single hour-long recording.

Usage:

    python scripts/audio_debug_server.py --port 8000
    python scripts/audio_debug_server.py --output-dir captures --segment-seconds 120
"""

import argparse
import socket
import sys
import time
import wave
from pathlib import Path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--host", default="0.0.0.0", help="Address to bind (default: every interface)")
    parser.add_argument("--port", type=int, default=8000, help="UDP port matching CONFIG_AUDIO_DEBUG_UDP_SERVER")
    parser.add_argument("--rate", type=int, default=16000, help="Sample rate of the captured stream in Hz")
    parser.add_argument("--channels", type=int, default=1, help="Channel count of the captured stream")
    parser.add_argument("--output-dir", default="audio_debug", help="Directory that receives the WAV files")
    parser.add_argument("--gap", type=float, default=1.0, help="Silence in seconds that starts a new file")
    parser.add_argument("--segment-seconds", type=float, default=60.0, help="Maximum length of one file in seconds")
    parser.add_argument("--count", type=int, default=0, help="Stop after this many packets (0 = run until Ctrl+C)")
    return parser.parse_args()


class SegmentWriter:
    """Append PCM frames to numbered WAV files, rotating on silence or length."""

    def __init__(self, output_dir: Path, rate: int, channels: int, gap: float, segment_seconds: float) -> None:
        self._output_dir = output_dir
        self._rate = rate
        self._channels = channels
        self._gap = gap
        self._max_frames = max(1, int(segment_seconds * rate))
        self._index = 0
        self._file: wave.Wave_write | None = None
        self._frames = 0
        self._path: Path | None = None
        self._last_packet = 0.0
        self._total_frames = 0

    def write(self, pcm: bytes, now: float) -> None:
        if self._file is not None and (
            now - self._last_packet > self._gap or self._frames >= self._max_frames
        ):
            self._close()
        if self._file is None:
            self._output_dir.mkdir(parents=True, exist_ok=True)
            self._path = self._output_dir / f"audio_debug_{self._index:03d}.wav"
            self._file = wave.open(str(self._path), "wb")
            self._file.setnchannels(self._channels)
            self._file.setsampwidth(2)
            self._file.setframerate(self._rate)
            self._frames = 0
            self._index += 1
        self._file.writeframes(pcm)
        frame_bytes = 2 * self._channels
        self._frames += len(pcm) // frame_bytes
        self._total_frames += len(pcm) // frame_bytes
        self._last_packet = now

    def _close(self) -> None:
        if self._file is None:
            return
        self._file.close()
        seconds = self._frames / self._rate
        print(f"wrote {self._path} ({seconds:.1f}s)", flush=True)
        self._file = None

    def close(self) -> None:
        self._close()
        print(f"total {self._total_frames / self._rate:.1f}s in {self._index} file(s)", flush=True)


def main() -> int:
    args = parse_args()
    if args.rate <= 0 or args.channels <= 0:
        print("--rate and --channels must be positive", file=sys.stderr)
        return 2

    writer = SegmentWriter(Path(args.output_dir), args.rate, args.channels, args.gap, args.segment_seconds)
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind((args.host, args.port))
    print(f"listening on udp://{args.host}:{args.port}, writing to {Path(args.output_dir)}", flush=True)

    packets = 0
    try:
        while True:
            data, peer = sock.recvfrom(65535)
            if len(data) % 2:
                data = data[:-1]
            if not data:
                continue
            writer.write(data, time.monotonic())
            packets += 1
            if args.count and packets >= args.count:
                print(f"stopping after {packets} packet(s) from {peer[0]}:{peer[1]}", flush=True)
                break
    except KeyboardInterrupt:
        print("interrupted", flush=True)
    finally:
        sock.close()
        writer.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
