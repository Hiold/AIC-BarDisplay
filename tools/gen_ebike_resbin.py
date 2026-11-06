#!/usr/bin/env python3
# -*- coding:utf-8 -*-
# SPDX-License-Identifier: Apache-2.0
#
# Convert the LVGL "exported as C array" images of the ebike UI into LVGL
# binary image files (`.bin`) so they can live on the read-only "uires"
# partition instead of being linked into the firmware as rodata.
#
# Source of truth stays the generated C descriptors under
#   application/rt-thread/usbdisp/ui/ebike/generated/img_*.c
# (LVGL Image Converter output).  Each `uint8_t <name>_map[]` array already
# holds the payload in exact LVGL bin order:
#   RGB565/RGB888/ARGB8888/...      : stride * h
#   RGB565A8                        : stride * h  +  (stride / 2) * h  (A8 mask)
# so the .bin is just a 12-byte `lv_image_header_t` (little-endian bitfields,
# see lvgl/src/draw/lv_image_dsc.h) prepended to those bytes.
#
# A few big *opaque* backgrounds skip the lossy RGB565 C array and are
# re-encoded from their original PNGs (kept under tools/uires_src/) straight
# into full-color LV_COLOR_FORMAT_RGB888 (byte order per pixel: B, G, R --
# see rgb888_image_blend() in lv_draw_sw_blend_to_argb8888.c).  This needs
# Pillow (+numpy) on the host.
#
# Output: application/rt-thread/usbdisp/uires/ebike/<name>.bin, packed into
# uires.fatfs by build.py and mounted read-only at /uires at runtime
# (LVGL POSIX drive "L:" -> "L:/uires/ebike/<name>.bin").
#
# Usage: python tools/gen_ebike_resbin.py [name ...]   (default: runtime set)

import pathlib
import re
import struct
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
SRC_DIR = ROOT / 'application/rt-thread/usbdisp/ui/ebike/generated'
PNG_DIR = ROOT / 'tools/uires_src'
OUT_DIR = ROOT / 'application/rt-thread/usbdisp/uires/ebike'

# Images referenced by the running UI (lv_demo_ebike*.c / display.c).
RUNTIME_IMAGES = [
    'img_global_bg',
    'img_ebike_bg',
    'img_ebike_scale',
    'img_ebike_arrow_left',
    'img_ebike_arrow_right',
    'img_ebike_lamp',
    'img_ebike_clock',
]

# name -> source PNG: re-encoded RGB888 instead of the RGB565 C array.
# band_lit.png is the original of img_global_bg (ui/ebike/assets has no PNG
# for it); band_lit_left.png is a near-identical mirror-variant.
PNG888_IMAGES = {
    'img_global_bg': 'band_lit.png',       # 1920x440, full-screen canvas bg
    'img_ebike_bg': 'img_ebike_bg.png',    # 480x320, landscape page bg
}

LV_IMAGE_HEADER_MAGIC = 0x19
CF = {
    'L8': 0x06,
    'I4': 0x09,
    'I8': 0x0A,
    'A8': 0x0E,
    'RGB888': 0x0F,
    'ARGB8888': 0x10,
    'XRGB8888': 0x11,
    'RGB565': 0x12,
    'ARGB8565': 0x13,
    'RGB565A8': 0x14,
}


def parse_image(cfile):
    text = cfile.read_text(encoding='utf-8')
    name = re.search(r'const\s+lv_image_dsc_t\s+(\w+)\s*=', text)
    cf = re.search(r'\.header\.cf\s*=\s*LV_COLOR_FORMAT_(\w+)', text)
    flags = re.search(r'\.header\.flags\s*=\s*(\d+)', text)
    fields = dict(re.findall(r'\.header\.(w|h|stride)\s*=\s*(\d+)', text))
    arr = re.search(r'\.data\s*=\s*(\w+)', text)
    if not (name and cf and flags and len(fields) == 3 and arr):
        raise ValueError('unexpected descriptor layout: ' + cfile.name)
    dsc = dict(name=name.group(1), cf=cf.group(1), flags=int(flags.group(1)),
               w=int(fields['w']), h=int(fields['h']),
               stride=int(fields['stride']), array=arr.group(1))
    body = re.search(r'uint8_t\s+' + re.escape(dsc['array']) +
                     r'\s*\[\s*\]\s*=\s*\{(.*?)\};', text, re.S)
    if not body:
        raise ValueError('map array not found: ' + cfile.name)
    dsc['data'] = bytes(int(x, 16) for x in re.findall(r'0x([0-9a-fA-F]{2})', body.group(1)))
    return dsc


def validate(dsc):
    expect = dsc['stride'] * dsc['h']
    if dsc['cf'] == 'RGB565A8':
        expect += (dsc['stride'] // 2) * dsc['h']  # trailing A8 mask
    if len(dsc['data']) != expect:
        raise ValueError('{}: map is {} bytes, expected {}'.format(
            dsc['name'], len(dsc['data']), expect))


def emit(dsc, outdir):
    header = struct.pack('<BBHHHHH', LV_IMAGE_HEADER_MAGIC, CF[dsc['cf']],
                         dsc['flags'], dsc['w'], dsc['h'], dsc['stride'], 0)
    out = outdir / (dsc['name'] + '.bin')
    out.write_bytes(header + dsc['data'])
    return out, len(header) + len(dsc['data'])


def png_rgb888(name, png):
    """Encode an opaque PNG straight into an RGB888 payload (B, G, R order)."""
    try:
        import numpy as np
        from PIL import Image
    except ImportError as exc:
        raise SystemExit('RGB888 re-encoding needs Pillow + numpy '
                         '(python -m pip install pillow numpy)') from exc
    if not png.exists():
        raise SystemExit('missing source PNG: ' + str(png))
    im = Image.open(png).convert('RGB')
    w, h = im.size
    data = np.asarray(im)[:, :, ::-1].tobytes()  # PIL RGB -> LVGL BGR
    return dict(name=name, cf='RGB888', flags=0, w=w, h=h, stride=w * 3,
                data=data)


def main(argv):
    names = argv[1:] or RUNTIME_IMAGES
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    total = 0
    for nm in names:
        if nm in PNG888_IMAGES:
            dsc = png_rgb888(nm, PNG_DIR / PNG888_IMAGES[nm])
        else:
            dsc = parse_image(SRC_DIR / (nm + '.c'))
        validate(dsc)
        out, size = emit(dsc, OUT_DIR)
        total += size
        print('{} -> {} ({} B, {}, {}x{}, stride {})'.format(
            nm, out.relative_to(ROOT), size, dsc['cf'], dsc['w'], dsc['h'], dsc['stride']))
    print('total {} bytes in {}'.format(total, OUT_DIR.relative_to(ROOT)))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
