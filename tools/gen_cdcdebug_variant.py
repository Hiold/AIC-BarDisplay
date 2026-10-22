#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Generate the d133-boe1126-cdc debug variant (graph + config + variants entry).

Derives a stripped firmware graph from the existing d133-boe1126 usbdisp graph:
the display stack (bsp drv/hal, fb, panel, DE/DSI) auto-probes at boot and draws
the color block + registers `dispdbg`, independent of any application. So the
debug image only needs the kernel, bsp display drivers, the CherryUSB device
core/port/osal, a CDC-ACM class and a small console bridge.

Dropped: usbdisp app, LVGL, mpp decoder/encoder stack, littlefs, and the
MSC/HID/vendor USB classes. Kept: fatfs + rodata assets packaging, because
build.py's pack()/verify_image() unconditionally repacks and byte-compares
application/rt-thread/usbdisp/assets into the rodata partition.
"""
import json
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
SRC_GRAPH = ROOT / "firmware/graphs/d13x_display-boe1126_rt-thread_usbdisp.json"
SRC_CONFIG = ROOT / "firmware/configs/d13x_display-boe1126_rt-thread_usbdisp.h"
VARIANTS = ROOT / "variants.json"

GRAPH_NAME = "d13x_display-boe1126-cdc_rt-thread_usbdisp"
OUT_GRAPH = ROOT / ("firmware/graphs/" + GRAPH_NAME + ".json")
OUT_CONFIG = ROOT / ("firmware/configs/" + GRAPH_NAME + ".h")
VARIANT_ID = "d133-boe1126-cdc"

# source-path fragments to remove from the base graph
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
    ('#define PRJ_DEFCONFIG_FILENAME "d13x_display-boe1126_rt-thread_usbdisp_defconfig"',
     '#define PRJ_DEFCONFIG_FILENAME "d13x_display-boe1126-cdc_rt-thread_usbdisp_defconfig"'),
    ('#define AIC_IMAGE_PRODUCT "d13x-usbdisp-boe1126"',
     '#define AIC_IMAGE_PRODUCT "d13x-cdcdebug-boe1126"'),
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

    graph["sources"] = kept

    for inc in ADD_INCLUDES:
        if inc not in graph["includes"]:
            graph["includes"].append(inc)

    # fix the map output path so it doesn't collide with the base variant
    if "linkflags" in graph:
        graph["linkflags"] = graph["linkflags"].replace(
            "output/d13x_display-boe1126_rt-thread_usbdisp/",
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
            # else: delete the line
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
    base = variants["d133-boe1126"]
    entry = {
        "chip": base["chip"],
        "graph": GRAPH_NAME,
        "product": "d13x-cdcdebug-boe1126",
        "version": base.get("version", "1.0.0"),
        "fps": base["fps"],
        "jpeg_quality": base["jpeg_quality"],
        "timing": dict(base["timing"]),
    }
    variants[VARIANT_ID] = entry
    VARIANTS.write_text(json.dumps(variants, indent=2) + "\n", encoding="utf-8")
    print("variants: added %s -> graph %s" % (VARIANT_ID, GRAPH_NAME))


if __name__ == "__main__":
    build_graph()
    build_config()
    build_variants()
    print("done.")
