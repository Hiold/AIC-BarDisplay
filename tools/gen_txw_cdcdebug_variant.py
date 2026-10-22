#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Generate the d133-txw935003b-cdc debug variant (graph + config + variants entry).

Same idea as gen_cdcdebug_variant.py (BOE1126): derive a stripped firmware graph
from the TXW935003B usbdisp graph so the display stack auto-probes at boot and
registers `dispdbg`, with the RT-Thread console redirected to a USB CDC-ACM
virtual serial port. The TXW panel driver (ICNL9707, panel_dsi_icnl9707_
txw935003b.c with its BIST/async-init diagnostic switches) compiles unchanged;
only the console and heavies are swapped out, mirroring the BOE1126-CDC variant.

Dropped: usbdisp app, LVGL, mpp decoder/encoder stack, littlefs, and the
MSC/HID/vendor USB classes. Kept: fatfs + rodata assets packaging, because
build.py's pack()/verify_image() unconditionally repacks and byte-compares
application/rt-thread/usbdisp/assets into the rodata partition.
"""
import json
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
SRC_GRAPH = ROOT / "firmware/graphs/d13x_display-txw935003b_rt-thread_usbdisp.json"
SRC_CONFIG = ROOT / "firmware/configs/d13x_display-txw935003b_rt-thread_usbdisp.h"
VARIANTS = ROOT / "variants.json"

GRAPH_NAME = "d13x_display-txw935003b-cdc_rt-thread_usbdisp"
OUT_GRAPH = ROOT / ("firmware/graphs/" + GRAPH_NAME + ".json")
OUT_CONFIG = ROOT / ("firmware/configs/" + GRAPH_NAME + ".h")
VARIANT_ID = "d133-txw935003b-cdc"

# source-path fragments to remove from the base graph (identical to BOE-CDC)
REMOVE_SUBSTRINGS = [
    "application/rt-thread/usbdisp/",
    "packages/artinchip/lvgl-ui/",
    "packages/artinchip/mpp/",
    "packages/third-party/littlefs/",
    "packages/third-party/cherryusb/class/msc/",
    "packages/third-party/cherryusb/class/hid/",
    "packages/third-party/cherryusb/class/vendor/",
]

# sources to add for the CDC console (relative to ${ROOT})
ADD_SOURCES = [
    "packages/third-party/cherryusb/class/cdc/usbd_cdc_acm.c",
    "application/rt-thread/cdcdebug/main.c",
    "application/rt-thread/cdcdebug/cdc_console.c",
]

# include dirs to add
ADD_INCLUDES = [
    "${ROOT}/packages/third-party/cherryusb/class/cdc",
    "${ROOT}/application/rt-thread/cdcdebug",
]

# config.h edits: (old_line, new_line); new_line None => delete the line
CONFIG_EDITS = [
    ('#define PRJ_DEFCONFIG_FILENAME "d13x_display-txw935003b_rt-thread_usbdisp_defconfig"',
     '#define PRJ_DEFCONFIG_FILENAME "d13x_display-txw935003b-cdc_rt-thread_usbdisp_defconfig"'),
    ('#define AIC_IMAGE_PRODUCT "d13x-usbdisp-txw935003b"',
     '#define AIC_IMAGE_PRODUCT "d13x-cdcdebug-txw935003b"'),
    ('#define RT_CONSOLE_DEVICE_NAME "uart6"',
     '#define RT_CONSOLE_DEVICE_NAME "vcom"'),
    # drop the usbdisp application and its heavy deps (LVGL / littlefs)
    ('#define APP_USB_DISP', None),
    ('#define LPKG_USING_LVGL', None),
    ('#define LVGL_V_9', None),
    ('#define LPKG_LVGL_PATH "/packages/multimedia/LVGL/LVGL"', None),
    ('#define LPKG_LVGL_THREAD_PRIO 20', None),
    ('#define LPKG_LVGL_THREAD_STACK_SIZE 12288', None),
    ('#define LPKG_LVGL_DISP_REFR_PERIOD 5', None),
    ('#define LVGL_STORAGE_PATH "/rodata/lvgl_data"', None),
    ('#define LPKG_USING_LITTLEFS', None),
    ('#define LPKG_LITTLEFS_USING_MTD_NOR', None),
    ('#define LPKG_LITTLEFS_PATH "/packages/third-party/littlefs"', None),
    ('#define LPKG_USING_LITTLEFS_LATEST_VERSION', None),
    ('#define LFS_READ_SIZE 16', None),
    ('#define LFS_PROG_SIZE 256', None),
    ('#define LFS_BLOCK_SIZE 4096', None),
    ('#define LFS_CACHE_SIZE 4096', None),
    ('#define LFS_BLOCK_CYCLES -1', None),
    ('#define LFS_THREADSAFE', None),
    ('#define LFS_LOOKAHEAD_MAX 128', None),
    ('#define LPKG_LITTLEFS_VER "latest"', None),
    ('#define RT_DEF_LFS_DRIVERS 2', None),
    # USB device classes: keep core+port+osal only; drop MSC/HID/vendor
    ('#define LPKG_CHERRYUSB_DEVICE_MSC', None),
    ('#define USBDEV_MSC_THREAD', None),
    ('#define USBDEV_MSC_READ_ONLY', None),
    ('#define MSC_STORAGE_PATH "/sdcard"', None),
    ('#define CONFIG_USBDEV_MSC_MAX_BUFSIZE 4096', None),
    ('#define USBDEV_MSC_MEDIUM_EJECTED', None),
    ('#define CONFIG_USBDEV_MSC_MANUFACTURER_STRING "ESP32P4"', None),
    ('#define CONFIG_USBDEV_MSC_PRODUCT_STRING "Assets Disk"', None),
    ('#define CONFIG_USBDEV_MSC_VERSION_STRING "1.0"', None),
    ('#define LPKG_CHERRYUSB_DEVICE_HID', None),
    ('#define LPKG_CHERRYUSB_DEVICE_VENDER', None),
    ('#define LPKG_CHERRYUSB_DEVICE_VENDER_SINGLE', None),
    ('#define LPKG_CHERRYUSB_DEVICE_SINGLE', None),
    # mpp package is no longer compiled; drop its option macro
    ('#define LPKG_MPP', None),
]


def build_graph():
    graph = json.loads(SRC_GRAPH.read_text(encoding="utf-8"))

    kept = []
    removed = 0
    for entry in graph["sources"]:
        src = entry["source"]
        if any(sub in src for sub in REMOVE_SUBSTRINGS):
            removed += 1
            continue
        kept.append(entry)

    existing = {e["source"] for e in kept}
    added = 0
    for rel in ADD_SOURCES:
        path = "${ROOT}/" + rel
        if path not in existing:
            kept.append({"source": path, "defines": [], "cflags": "", "cpppath": []})
            added += 1

    # The TXW panel driver defaults TXW_PANEL_BIST=1, which forces the panel
    # into its own color-bar BIST and hides the framebuffer. For the CDC debug
    # image we need the DE to drive the actual framebuffer (dispdbg patterns),
    # so compile the panel source with BIST off. Scoped to this source only.
    for entry in kept:
        if entry["source"].endswith("panel_dsi_icnl9707_txw935003b.c"):
            if "defines" not in entry:
                entry["defines"] = []
            entry["defines"].append("TXW_PANEL_BIST=0")
            # Async init spawns a worker that returns before the DE/DSI are
            # ready and makes dispdbg reset a no-op (panel_enable() skips when
            # the worker already exists). Force synchronous init so panel
            # OFF/ON reloads in dispdbg_reload() genuinely re-init the panel.
            entry["defines"].append("TXW_ASYNC_INIT=0")
            break

    graph["sources"] = kept

    for inc in ADD_INCLUDES:
        if inc not in graph["includes"]:
            graph["includes"].append(inc)

    if "linkflags" in graph:
        graph["linkflags"] = graph["linkflags"].replace(
            "output/d13x_display-txw935003b_rt-thread_usbdisp/",
            "output/" + GRAPH_NAME + "/")

    OUT_GRAPH.write_text(json.dumps(graph, indent=2) + "\n", encoding="utf-8")
    print("graph: %s (kept %d sources, removed %d, added %d)" %
          (OUT_GRAPH.name, len(kept), removed, added))


def build_config():
    text = SRC_CONFIG.read_text(encoding="utf-8")
    lines = text.splitlines()
    edits = dict(CONFIG_EDITS)
    out = []
    applied = set()
    for line in lines:
        if line in edits:
            applied.add(line)
            new = edits[line]
            if new is not None:
                out.append(new)
        else:
            out.append(line)
    missing = [old for old, _ in CONFIG_EDITS if old not in applied]
    if missing:
        print("WARNING: %d config edits did not match:" % len(missing), file=sys.stderr)
        for m in missing:
            print("  " + m, file=sys.stderr)
    OUT_CONFIG.write_text("\n".join(out) + "\n", encoding="utf-8")
    print("config: %s (applied %d/%d edits)" %
          (OUT_CONFIG.name, len(applied), len(edits)))


def build_variants():
    variants = json.loads(VARIANTS.read_text(encoding="utf-8"))
    base = variants["d133-txw935003b"]
    entry = {
        "chip": base["chip"],
        "graph": GRAPH_NAME,
        "product": "d13x-cdcdebug-txw935003b",
        "version": base.get("version", "1.0.0"),
        "fps": base["fps"],
        "jpeg_quality": base["jpeg_quality"],
        "timing": dict(base["timing"]),
    }
    # hfp/hbp=20 stops the horizontal-line drift observed at the stock 40/40
    # under the reference timing; the smaller htotal needs a recomputed pixel
    # clock to hold ~60 Hz: htotal=480+20+20+4=524, vtotal=1920+32+12+4=1968,
    # so pclk = 524 * 1968 * 60 = 61.88 MHz.
    # TXW935003B glass is 440 px wide (panel driver header); driving it at 480
    # overflows 40 px/line and produces periodic artifacts, so use 440 wide.
    entry["timing"]["width"] = 440
    entry["timing"]["hfront_porch"] = 20
    entry["timing"]["hback_porch"] = 20
    htotal = (entry["timing"]["width"] + entry["timing"]["hfront_porch"]
              + entry["timing"]["hback_porch"] + entry["timing"]["hsync_len"])
    vtotal = (entry["timing"]["height"] + entry["timing"]["vfront_porch"]
              + entry["timing"]["vback_porch"] + entry["timing"]["vsync_len"])
    entry["timing"]["pixelclock"] = htotal * vtotal * entry["fps"]
    variants[VARIANT_ID] = entry
    VARIANTS.write_text(json.dumps(variants, indent=2) + "\n", encoding="utf-8")
    print("variants: added %s -> graph %s" % (VARIANT_ID, GRAPH_NAME))


if __name__ == "__main__":
    build_graph()
    build_config()
    build_variants()
    print("done. Build with: build.cmd %s" % VARIANT_ID)