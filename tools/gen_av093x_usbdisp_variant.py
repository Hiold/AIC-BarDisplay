#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Generate the full usbdisp variant for the BOE AV093XHQ-N10 (320x1280) panel.

Derived from the verified d133-boe1126 usbdisp variant (same FL7707N wiring and
control pins), swapping only:
  - panel driver:  boe1126 -> av093x
  - panel config macros
  - timing: 320x1280 with corrected DSI lane rate (see variants.json)
  - idle-video assets: dedicated assets_av093x dir (320x1280 LVGLMJPG clip)

Run this once; commit the generated graph/config and variants.json.
"""
import json
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
SRC_GRAPH = ROOT / "firmware/graphs/d13x_display-boe1126_rt-thread_usbdisp.json"
SRC_CONFIG = ROOT / "firmware/configs/d13x_display-boe1126_rt-thread_usbdisp.h"
VARIANTS = ROOT / "variants.json"

GRAPH_NAME = "d13x_display-boe-av093x_rt-thread_usbdisp"
OUT_GRAPH = ROOT / ("firmware/graphs/" + GRAPH_NAME + ".json")
OUT_CONFIG = ROOT / ("firmware/configs/" + GRAPH_NAME + ".h")
VARIANT_ID = "d133-boe-av093x"


def build_graph():
    graph = json.loads(SRC_GRAPH.read_text(encoding="utf-8"))
    swapped = 0
    for entry in graph["sources"]:
        src = entry["source"]
        if src.endswith("panel_dsi_fl7707_boe1126_b.c"):
            entry["source"] = src.replace("panel_dsi_fl7707_boe1126_b.c",
                                          "panel_dsi_fl7707_av093x_b.c")
            swapped += 1
    if "linkflags" in graph:
        graph["linkflags"] = graph["linkflags"].replace(
            "output/d13x_display-boe1126_rt-thread_usbdisp/",
            "output/" + GRAPH_NAME + "/")
    if swapped != 1:
        print("ERROR: expected exactly 1 boe1126 panel swap, got %d" % swapped,
              file=sys.stderr)
        sys.exit(1)
    OUT_GRAPH.write_text(json.dumps(graph, indent=2) + "\n", encoding="utf-8")
    print("graph: %s (swapped %d panel source)" % (OUT_GRAPH.name, swapped))


def build_config():
    text = SRC_CONFIG.read_text(encoding="utf-8")
    edits = {
        '#define PRJ_DEFCONFIG_FILENAME '
        '"d13x_display-boe1126_rt-thread_usbdisp_defconfig"':
        '#define PRJ_DEFCONFIG_FILENAME "%s_defconfig"' % GRAPH_NAME,
        '#define AIC_IMAGE_PRODUCT "d13x-usbdisp-boe1126"':
        '#define AIC_IMAGE_PRODUCT "d13x-usbdisp-boe-av093x"',
        '#define AIC_PANEL_DSI_FL7707_BOE1126_B':
        '#define AIC_PANEL_DSI_FL7707_AV093X_B',
        '#define AIC_PANEL_FL7707_BOE1126_B_PEN_GPIO "PD.15"':
        '#define AIC_PANEL_FL7707_AV093X_B_PEN_GPIO "PD.15"',
        '#define AIC_PANEL_FL7707_BOE1126_B_RESET_GPIO "PD.16"':
        '#define AIC_PANEL_FL7707_AV093X_B_RESET_GPIO "PD.16"',
    }
    out, applied = [], 0
    for line in text.splitlines():
        if line in edits:
            applied += 1
            out.append(edits[line])
        else:
            out.append(line)
    missing = [k for k in edits if k not in text]
    if missing:
        print("WARNING: %d config edits did not match:" % len(missing),
              file=sys.stderr)
        for m in missing:
            print("  " + m, file=sys.stderr)
    OUT_CONFIG.write_text("\n".join(out) + "\n", encoding="utf-8")
    print("config: %s (applied %d/%d edits)"
          % (OUT_CONFIG.name, applied, len(edits)))


def build_variants():
    variants = json.loads(VARIANTS.read_text(encoding="utf-8"))
    entry = {
        "chip": "d13x",
        "graph": GRAPH_NAME,
        "product": "d13x-usbdisp-boe-av093x",
        "version": "1.0.0",
        "fps": 60,
        "jpeg_quality": 6,
        "assets_dir": "application/rt-thread/usbdisp/assets_av093x",
        "timing": {
            "width": 320,
            "height": 1280,
            "pixelclock": 65198400,
            "hfront_porch": 150,
            "hback_porch": 150,
            "hsync_len": 60,
            "vfront_porch": 150,
            "vback_porch": 164,
            "vsync_len": 4,
            "lane_num": 4,
            "ln_assign": 291,
            "ln_polrs": 0,
        },
    }
    variants[VARIANT_ID] = entry
    VARIANTS.write_text(json.dumps(variants, indent=2) + "\n", encoding="utf-8")
    print("variants: added %s -> graph %s" % (VARIANT_ID, GRAPH_NAME))


if __name__ == "__main__":
    build_graph()
    build_config()
    build_variants()
    print("done. Build with: build.cmd %s" % VARIANT_ID)