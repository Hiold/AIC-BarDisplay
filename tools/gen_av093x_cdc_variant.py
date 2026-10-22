#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Generate the BOE AV093XHQ-N10 (320x1280) CDC debug variant.

Derives from the existing d133-boe1126-cdc debug graph (CDC console + display
drivers), swapping:
  - panel source: panel_dsi_fl7707_boe1126_b.c -> panel_dsi_fl7707_av093x_b.c
  - panel config macro + GPIO names: BOE1126 -> AV093X
  - timing: 440x1920 (BOE) -> 320x1280

NOTE on timing / DSI lane rate (fix for "one frame then slow blackout"):

The original 320x1280 timing kept a minimal frame: htotal = 430, vtotal = 1311,
pixelclock = 430*1311*60 = 33.8238 MHz.  That yields a DSI HS lane rate of
    sclk_rate = pixelclock * 24 / lane_num = 33.82M * 24 / 4 = ~203 Mbps/lane
(see aic_dsi_attach_panel() in drv_dsi.c).  FL7707N's TCON PLL/CDR cannot stay
locked at such a low continuous-HS lane rate, so the panel shows one frame at
power-on (init handshake in command mode succeeds) and then slowly goes black
once sustained video-burst HS starts.

Fix: enlarge blanking to raise pixelclock / lane rate toward the BOE 11.26
(65.856 MHz / ~395 Mbps/lane) reference that is verified stable:
    htotal = 320+150+150+60   = 680
    vtotal = 1280+150+164+4   = 1598
    pixelclock = 680 * 1598 * 60 = 65.1984 MHz
    lane rate  = 65.1984M * 24 / 4 = ~391 Mbps/lane
Dot-clock stays /6 below sclk (integer divisor), so CLK_PIX and the 60 Hz
refresh are exact; only the blanking duty grows, which TCON tolerates.

Init sequence from AV093XHQ-N10_IPS_Code_正掃_2 Power_V4.3_20221026(2).txt
"""
import json
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
SRC_GRAPH = ROOT / "firmware/graphs/d13x_display-boe1126-cdc_rt-thread_usbdisp.json"
SRC_CONFIG = ROOT / "firmware/configs/d13x_display-boe1126-cdc_rt-thread_usbdisp.h"
VARIANTS = ROOT / "variants.json"

GRAPH_NAME = "d13x_display-boe-av093x-cdc_rt-thread_usbdisp"
OUT_GRAPH = ROOT / ("firmware/graphs/" + GRAPH_NAME + ".json")
OUT_CONFIG = ROOT / ("firmware/configs/" + GRAPH_NAME + ".h")
VARIANT_ID = "d133-boe-av093x-cdc"


def build_graph():
    graph = json.loads(SRC_GRAPH.read_text(encoding="utf-8"))

    swapped = 0
    for entry in graph["sources"]:
        src = entry["source"]
        if src.endswith("panel_dsi_fl7707_boe1126_b.c"):
            entry["source"] = src.replace("panel_dsi_fl7707_boe1126_b.c",
                                          "panel_dsi_fl7707_av093x_b.c")
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
        '#define AIC_IMAGE_PRODUCT "d13x-cdcdebug-boe-av093x"',
        '#define AIC_PANEL_DSI_FL7707_BOE1126_B':
        '#define AIC_PANEL_DSI_FL7707_AV093X_B',
        '#define AIC_PANEL_FL7707_BOE1126_B_PEN_GPIO "PD.15"':
        '#define AIC_PANEL_FL7707_AV093X_B_PEN_GPIO "PD.15"',
        '#define AIC_PANEL_FL7707_BOE1126_B_RESET_GPIO "PD.16"':
        '#define AIC_PANEL_FL7707_AV093X_B_RESET_GPIO "PD.16"',
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
        "product": "d13x-cdcdebug-boe-av093x",
        "version": "1.0.0",
        "fps": 60,
        "jpeg_quality": 6,
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