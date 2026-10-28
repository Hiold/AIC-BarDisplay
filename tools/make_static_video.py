#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Build a single-frame (static image) LVGLMJPG video.bin from a source image.

The idle/default screen of the USB-display firmware previously played an
LVGLMJPG animation on the full-screen VIDEO layer.  The product requirement
changed the default interface to a single static image (bg.png); this script
packages that image as a one-frame LVGLMJPG clip so the existing, proven
video-layer pipeline (VE JPEG decode + hardware rotation to the portrait panel)
shows it full-screen with no changes to the display/video driver code.

The source is re-encoded as a baseline 4:2:0 JPEG at the given quality, so the
result is small and uniform with the animation frames the clip replaces.
"""
import argparse
import io
import pathlib
import struct

from PIL import Image

MAGIC = b"LVGLMJPG"


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("src", type=pathlib.Path, help="source image (PNG/JPEG/BMP...)")
    ap.add_argument("dst", type=pathlib.Path, help="output LVGLMJPG video.bin")
    ap.add_argument("--width", type=int, default=1920, help="frame width (landscape, pre-rotation)")
    ap.add_argument("--height", type=int, default=440, help="frame height (landscape, pre-rotation)")
    ap.add_argument("--quality", type=int, default=80,
                    help="JPEG encode quality (static frame can afford quality)")
    ap.add_argument("--fps-num", type=int, default=1)
    ap.add_argument("--fps-den", type=int, default=1)
    args = ap.parse_args()

    im = Image.open(args.src).convert("RGB")
    if (im.size[0] & 1) or (im.size[1] & 1) or (args.width & 1) or (args.height & 1):
        raise SystemExit("frame dims must be even")
    if im.size != (args.width, args.height):
        print("note: source %dx%d resized to target %dx%d"
              % (im.size[0], im.size[1], args.width, args.height))
        im = im.resize((args.width, args.height), Image.LANCZOS)

    buf = io.BytesIO()
    im.save(buf, "JPEG", quality=args.quality, subsampling=2,
            progressive=False, optimize=True)
    frame = buf.getvalue()

    n = 1
    header_size = 64
    index_offset = header_size
    frames_offset = index_offset + n * 8

    out = bytearray()
    out += MAGIC
    out += struct.pack("<HH", args.width, args.height)
    out += struct.pack("<HH", args.fps_num, args.fps_den)
    out += struct.pack("<IIIII", n, index_offset, frames_offset, len(frame), 0)
    out += bytes(28)
    assert len(out) == header_size
    out += struct.pack("<II", frames_offset, len(frame))
    out += frame

    args.dst.parent.mkdir(parents=True, exist_ok=True)
    args.dst.write_bytes(out)
    print("wrote %s: %dx%d, 1 frame, %d bytes (single-frame static idle clip)"
          % (args.dst, args.width, args.height, len(out)))


if __name__ == "__main__":
    main()