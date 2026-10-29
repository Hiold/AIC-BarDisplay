#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Build an LVGLMJPG video.bin from a source MP4.

Decodes the MP4 to individual frames, re-encodes each as baseline 4:2:0 JPEG
at the given quality and packs them into the LVGLMJPG container used by the
usbdisp idle-video layer (see video.c): 64-byte header, per-frame offset/size
index and concatenated JPEG frames.

Frames are resized to the target WxH (even dims required for 4:2:0) before
encoding.  Source fps is used to fill the header fps field.
"""
import argparse
import io
import pathlib
import subprocess
import struct

from PIL import Image

MAGIC = b"LVGLMJPG"


def main():
    ap = argparse.ArgumentParser(description="Build LVGLMJPG video.bin from an MP4.")
    ap.add_argument("src", type=pathlib.Path, help="source MP4")
    ap.add_argument("dst", type=pathlib.Path, help="output LVGLMJPG video.bin")
    ap.add_argument("--ffmpeg", default="ffmpeg",
                    help="path to ffmpeg executable (default: ffmpeg on PATH)")
    ap.add_argument("--width", type=int, default=1920, help="frame width (even)")
    ap.add_argument("--height", type=int, default=440, help="frame height (even)")
    ap.add_argument("--quality", type=int, default=13,
                    help="JPEG encode quality (baseline 4:2:0)")
    ap.add_argument("--fps-num", type=int, default=30,
                    help="header fps numerator")
    ap.add_argument("--fps-den", type=int, default=1,
                    help="header fps denominator")
    ap.add_argument("--work", type=pathlib.Path,
                    help="workdir for raw PNG frames (kept on disk)")
    ap.add_argument("--frames", type=int, default=None,
                    help="limit to first N frames (debug)")
    args = ap.parse_args()

    if (args.width & 1) or (args.height & 1):
        raise SystemExit("frame dims must be even (4:2:0 chroma alignment)")

    work = args.work or pathlib.Path("build/_mp4frames")
    raw = work / "raw"
    raw.mkdir(parents=True, exist_ok=True)

    # Decode the MP4 to numbered PNGs. -vsync 0 keeps every source frame.
    print("extracting frames with %s ..." % args.ffmpeg)
    frame_pattern = str(raw / "frame_%05d.png")
    subprocess.run(
        [args.ffmpeg, "-hide_banner", "-loglevel", "error", "-i", str(args.src),
         "-vsync", "0", frame_pattern],
        check=True)
    frames = sorted(raw.glob("frame_*.png"))
    if not frames:
        raise SystemExit("no frames extracted (bad ffmpeg path or source?)")
    if args.frames is not None:
        frames = frames[:args.frames]
    n = len(frames)
    print("decoded %d source frames" % n)

    # Re-encode each frame as baseline 4:2:0 JPEG at the target size.
    frame_enc = []
    max_frame = 0
    for i, p in enumerate(frames):
        im = Image.open(p).convert("RGB")
        if im.size != (args.width, args.height):
            im = im.resize((args.width, args.height), Image.LANCZOS)
        buf = io.BytesIO()
        im.save(buf, "JPEG", quality=args.quality, subsampling=2,
                progressive=False, optimize=True)
        fb = buf.getvalue()
        frame_enc.append(fb)
        max_frame = max(max_frame, len(fb))
        if i < 5 or (i + 1) % 100 == 0:
            print("  frame %d -> %d bytes" % (i, len(fb)))

    # Build the LVGLMJPG container.
    header_size = 64
    index_offset = header_size
    frames_offset = index_offset + n * 8
    frame_offsets = []
    body = bytearray()
    cursor = frames_offset
    for fb in frame_enc:
        frame_offsets.append(cursor)
        body.extend(fb)
        cursor += len(fb)
    total = cursor

    header = bytearray()
    header += MAGIC
    header += struct.pack("<HH", args.width, args.height)
    header += struct.pack("<HH", args.fps_num, args.fps_den)
    header += struct.pack("<IIIII", n, index_offset, frames_offset,
                          max_frame, 0)
    header += bytes(28)
    assert len(header) == header_size

    index = bytearray()
    for off, fb in zip(frame_offsets, frame_enc):
        index += struct.pack("<II", off, len(fb))

    out = bytearray(header)
    out += index
    out += body
    args.dst.parent.mkdir(parents=True, exist_ok=True)
    args.dst.write_bytes(out)

    print("wrote %s: %dx%d %d frames, fps=%d/%d, max_frame=%d, %d bytes"
          % (args.dst, args.width, args.height, n,
             args.fps_num, args.fps_den, max_frame, total))
    print("work dir kept at: %s" % work)


if __name__ == "__main__":
    main()