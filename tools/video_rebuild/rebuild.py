#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Unpack a LVGLMJPG video.bin, crop every frame to a target resolution and
repack into a new LVGLMJPG video.bin.

The original 480x1920 clip is centre-cropped to 320x1280 (drop 80 px each
side, 320 px top, 320 px bottom) and re-encoded as baseline 4:2:0 JPEG at the
same quality, matching the panel's native resolution so the DE video layer
shows it full-screen without scaling.
"""
import argparse
import io
import pathlib
import struct
import tempfile

from PIL import Image

MAGIC = b"LVGLMJPG"


def read_header(data):
    (magic, width, height, fps_num, fps_den,
     frame_count, index_offset, frames_offset,
     max_frame_size, flags) = struct.unpack_from("<8sHHHHIIIII", data, 0)
    return dict(magic=magic, width=width, height=height,
                fps_num=fps_num, fps_den=fps_den, frame_count=frame_count,
                index_offset=index_offset, frames_offset=frames_offset,
                max_frame_size=max_frame_size, flags=flags)


def iter_frames(data, header):
    ix = header["index_offset"]
    for i in range(header["frame_count"]):
        off, size = struct.unpack_from("<II", data, ix)
        ix += 8
        if off < header["frames_offset"] or size < 2 or off + size > len(data):
            raise ValueError("bad index %d: off=%d size=%d" % (i, off, size))
        yield i, data[off:off + size]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("src", type=pathlib.Path)
    ap.add_argument("dst", type=pathlib.Path)
    ap.add_argument("--out", type=int, default=(320, 1280), nargs=2,
                    metavar=("W", "H"))
    ap.add_argument("--quality", type=int, default=33)
    ap.add_argument("--work", type=pathlib.Path,
                    help="unpacked/cropped JPEG work dir (kept on disk)")
    ap.add_argument("--frames", type=int, default=None,
                    help="limit to first N frames (debug)")
    args = ap.parse_args()

    src = args.src.read_bytes()
    h = read_header(src)
    if h["magic"] != MAGIC:
        raise SystemExit("bad magic")
    out_w, out_h = args.out

    work = args.work or pathlib.Path(tempfile.mkdtemp(prefix="vg-"))
    raw = work / "raw"
    crop = work / "crop"
    raw.mkdir(parents=True, exist_ok=True)
    crop.mkdir(parents=True, exist_ok=True)

    n = h["frame_count"] if args.frames is None else min(args.frames, h["frame_count"])
    print("source %dx%d / %d frames; target %dx%d, quality %d"
          % (h["width"], h["height"], n, out_w, out_h, args.quality))

    # Crop geometry: keep the centre out_w x out_h window of the source.
    x0 = (h["width"] - out_w) // 2
    y0 = (h["height"] - out_h) // 2
    x0 &= ~1  # even for 4:2:0 chroma alignment
    y0 &= ~1

    max_frame = 0
    frame_enc = []  # (offset, bytes) later resolved to actual offsets
    for i, frame in iter_frames(src, h):
        if i >= n:
            break
        im = Image.open(io.BytesIO(frame)).convert("RGB")
        if im.size != (h["width"], h["height"]):
            print("WARN frame %d size %s != header" % (i, im.size))
        cropped = im.crop((x0, y0, x0 + out_w, y0 + out_h))
        if (cropped.size[0] & 1) or (cropped.size[1] & 1):
            raise SystemExit("cropped dims not even")
        cpath = crop / ("frame_%04d.jpg" % i)
        cropped.save(cpath, "JPEG", quality=args.quality,
                     subsampling=2, progressive=False, optimize=True)
        frame_enc.append(cpath.read_bytes())
        max_frame = max(max_frame, len(frame_enc[-1]))

    if n != len(frame_enc):
        print("WARN unpacked %d, expected %d" % (len(frame_enc), n))

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
    header += struct.pack("<HH", out_w, out_h)
    header += struct.pack("<HH", h["fps_num"], h["fps_den"])
    header += struct.pack("<IIIII", n, index_offset, frames_offset,
                          max_frame, h["flags"])
    header += bytes(28)
    assert len(header) == header_size

    index = bytearray()
    for off, fb in zip(frame_offsets, frame_enc):
        index += struct.pack("<II", off, len(fb))

    out = bytearray(header)
    out += index
    out += body
    dst = pathlib.Path(args.dst)
    dst.parent.mkdir(parents=True, exist_ok=True)
    dst.write_bytes(out)

    print("wrote %s: %dx%d %d frames, max_frame=%d, %d bytes"
          % (dst, out_w, out_h, n, max_frame, len(out)))
    print("work dir kept at: %s" % work)


if __name__ == "__main__":
    main()