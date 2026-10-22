#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Unpack a LVGLMJPG video.bin into its individual JPEG frames."""
import argparse
import io
import pathlib
import struct

MAGIC = b"LVGLMJPG"


def unpack(src, outdir, limit=None):
    data = src.read_bytes()
    if not data.startswith(MAGIC):
        raise SystemExit("not an LVGLMJPG file (bad magic)")

    (magic, width, height, fps_num, fps_den,
     frame_count, index_offset, frames_offset,
     max_frame_size, flags) = struct.unpack_from("<8sHHHHIIIIII", data, 0)
    print("header: %dx%d fps=%d/%d frames=%d idx_off=%d frames_off=%d "
          "max_frame=%d flags=%d size=%d"
          % (width, height, fps_num, fps_den, frame_count,
             index_offset, frames_offset, max_frame_size, flags, len(data)))

    outdir.mkdir(parents=True, exist_ok=True)
    n = frame_count if limit is None else min(limit, frame_count)
    ix = index_offset
    for i in range(n):
        off, size = struct.unpack_from("<II", data, ix)
        ix += 8
        if off < frames_offset or size < 2 or off + size > len(data):
            raise SystemExit("bad index entry %d: off=%d size=%d" % (i, off, size))
        frame = data[off:off + size]
        path = outdir / ("frame_%04d.jpg" % i)
        path.write_bytes(frame)
        if i < 3:
            # sanity-decode a couple of frames
            try:
                from PIL import Image
                im = Image.open(io.BytesIO(frame))
                print("  frame %d: %s %s size=%d" % (i, im.format, im.size, len(frame)))
            except Exception as e:
                print("  frame %d: decode FAILED %r size=%d" % (i, e, len(frame)))
    print("unpacked %d frames to %s" % (n, outdir))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("src", type=pathlib.Path)
    ap.add_argument("outdir", type=pathlib.Path)
    ap.add_argument("--limit", type=int, default=None)
    args = ap.parse_args()
    unpack(args.src, args.outdir, args.limit)


if __name__ == "__main__":
    main()