#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Re-encode a directory of JPEG frames into a LVGLMJPG video.bin.

Packs numbered JPEG frames (frame_00001.jpg, ...) into the LVGLMJPG container
used by the usbdisp idle-video layer (see video.c): 64-byte header, per-frame
offset/size index and concatenated JPEG frames.  Each frame is decoded and
re-encoded as baseline 4:2:0 JPEG at the given quality so the output quality /
size is uniform and matches the other idle-clip assets (quality like
make_video_from_mp4.py).  Use this instead of make_video_from_mp4.py when the
host ffmpeg cannot decode straight to PNG (e.g. ImageMagick's ffmpeg crashes
on the PNG encoder): extract JPEG frames with
    ffmpeg -i in.mp4 -c:v mjpeg -q:v 5 frame_%05d.jpg
and repack here.
"""
import argparse
import pathlib
import struct

from PIL import Image

MAGIC = b"LVGLMJPG"


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("src", type=pathlib.Path,
                    help="dir of numbered JPEG frames (frame_%05d.jpg ...)")
    ap.add_argument("dst", type=pathlib.Path, help="output LVGLMJPG video.bin")
    ap.add_argument("--width", type=int, required=True, help="frame width (even)")
    ap.add_argument("--height", type=int, required=True, help="frame height (even)")
    ap.add_argument("--quality", type=int, default=13, help="JPEG encode quality")
    ap.add_argument("--fps-num", type=int, default=15, help="header fps numerator")
    ap.add_argument("--fps-den", type=int, default=1, help="header fps denominator")
    ap.add_argument("--frames", type=int, default=None,
                    help="limit to first N frames (debug)")
    args = ap.parse_args()

    if (args.width & 1) or (args.height & 1):
        raise SystemExit("frame dims must be even (4:2:0 chroma alignment)")

    src = args.src
    paths = sorted(src.glob("frame_*.jpg"))
    if not paths:
        raise SystemExit("no frame_*.jpg found in %s" % src)
    if args.frames is not None:
        paths = paths[:args.frames]
    n = len(paths)
    print("packing %d frames -> %dx%d quality=%d fps=%d/%d"
          % (n, args.width, args.height, args.quality, args.fps_num, args.fps_den))

    max_frame = 0
    frame_enc = []
    for i, p in enumerate(paths):
        im = Image.open(p).convert("RGB")
        if im.size != (args.width, args.height):
            im = im.resize((args.width, args.height), Image.LANCZOS)
        import io
        buf = io.BytesIO()
        im.save(buf, "JPEG", quality=args.quality, subsampling=2,
                progressive=False, optimize=True)
        fb = buf.getvalue()
        frame_enc.append(fb)
        max_frame = max(max_frame, len(fb))
        if i < 3 or (i + 1) % 100 == 0:
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
    header += struct.pack("<IIIII", n, index_offset, frames_offset, max_frame, 0)
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


if __name__ == "__main__":
    main()