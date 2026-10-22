#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Generate a test variant that drives the unknown AV096XHM panel with the
known-good AV092K5U FL7707N init sequence + timing.

The unknown bay has the same panel-control pins as the BOE11.26 reference board
(PD.15 PEN / PD.16 RST / PD.10 BL), and both are FL7707N series panels, but the
BOE init sequence leaves the unknown panel with a fixed horizontal/vertical
partial-scan corruption (top black, bottom color band, fixed boundary across
every timing/lane/pixel-format experiment). The AV092K5U-N10 sequence lives in
the same FL7707N family and is already proven to light its 462x1920 panel, so
drive the unknown panel with that sequence + timing as a cross-check.

Derives from the existing d133-boe1126-cdc debug graph (stripped kernel +
display drivers + CDC console), swapping:
  - panel source: panel_dsi_fl7707_boe1126_b.c -> panel_dsi_fl7707_av092k5u_b.c
  - panel config macro + GPIO names: BOE1126 -> AV092K5U
  - timing: 440x1920 (BOE) -> 462x1920 (AV092K5U, pixelclock 84.672MHz)
"""
import json
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
SRC_GRAPH = ROOT / "firmware/graphs/d13x_display-boe1126-cdc_rt-thread_usbdisp.json"
SRC_CONFIG = ROOT / "firmware/configs/d13x_display-boe1126-cdc_rt-thread_usbdisp.h"
VARIANTS = ROOT / "variants.json"

GRAPH_NAME = "d13x_display-boe1126-av092k5u-test_rt-thread_usbdisp"
OUT_GRAPH = ROOT / ("firmware/graphs/" + GRAPH_NAME + ".json")
OUT_CONFIG = ROOT / ("firmware/configs/" + GRAPH_NAME + ".h")
VARIANT_ID = "d133-boe1126-av092k5u-test"


def build_graph():
    graph = json.loads(SRC_GRAPH.read_text(encoding="utf-8"))

    swapped = 0
    for entry in graph["sources"]:
        src = entry["source"]
        if src.endswith("panel_dsi_fl7707_boe1126_b.c"):
            entry["source"] = src.replace("panel_dsi_fl7707_boe1126_b.c",
                                          "panel_dsi_fl7707_av092k5u_b.c")
            swapped += 1

    # fix the map/link output path so it doesn't collide with the CDC variant
    if "linkflags" in graph:
        graph["linkflags"] = graph["linkflags"].replace(
            "output/d13x_display-boe1126-cdc_rt-thread_usbdisp/",
            "output/" + GRAPH_NAME + "/")

    if swapped != 1:
        print("ERROR: expected exactly 1 boe1126 panel source swap, got %d"
              % swapped, file=sys.stderr)
        sys.exit(1)

    OUT_GRAPH.write_text(json.dumps(graph, indent=2) + "\n", encoding="utf-8")
    print("graph: %s (swapped %d panel source)" % (OUT_GRAPH.name, swapped))


def build_config():
    text = SRC_CONFIG.read_text(encoding="utf-8")
    lines = text.splitlines()
    edits = {
        ('#define PRJ_DEFCONFIG_FILENAME '
         '"d13x_display-boe1126-cdc_rt-thread_usbdisp_defconfig"'):
        '#define PRJ_DEFCONFIG_FILENAME "%s_defconfig"' % GRAPH_NAME,
        '#define AIC_IMAGE_PRODUCT "d13x-cdcdebug-boe1126"':
        '#define AIC_IMAGE_PRODUCT "d13x-av092test-boe1126"',
        '#define AIC_PANEL_DSI_FL7707_BOE1126_B':
        '#define AIC_PANEL_DSI_FL7707_AV092K5U_B',
        '#define AIC_PANEL_FL7707_BOE1126_B_PEN_GPIO "PD.15"':
        '#define AIC_PANEL_FL7707_AV092K5U_B_PEN_GPIO "PD.15"',
        '#define AIC_PANEL_FL7707_BOE1126_B_RESET_GPIO "PD.16"':
        '#define AIC_PANEL_FL7707_AV092K5U_B_RESET_GPIO "PD.16"',
    }
    out = []
    applied = 0
    for line in lines:
        if line in edits:
            applied += 1
            out.append(edits[line])
        else:
            out.append(line)
    missing = [old for old in edits if old not in set(lines)]
    if missing:
        print("WARNING: %d config edits did not match:" % len(missing),
              file=sys.stderr)
        for m in missing:
            print("  " + m, file=sys.stderr)
    OUT_CONFIG.write_text("\n".join(out) + "\n", encoding="utf-8")
    print("config: %s (applied %d/%d edits)" % (OUT_CONFIG.name, applied, len(edits)))


def build_variants():
    variants = json.loads(VARIANTS.read_text(encoding="utf-8"))
    entry = {
        "chip": "d13x",
        "graph": GRAPH_NAME,
        "product": "d13x-av092test-boe1126",
        "version": "1.0.0",
        "fps": 60,
        "jpeg_quality": 6,
        "timing": {
            "width": 462,
            "height": 1920,
            # htotal = 462+118+118+22 = 720 ; vtotal = 1920+16+20+4 = 1960
            # pixelclock = 720 * 1960 * 60 = 84.672MHz
            "pixelclock": 84672000,
            "hfront_porch": 118,
            "hback_porch": 118,
            "hsync_len": 22,
            "vfront_porch": 16,
            "vback_porch": 20,
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