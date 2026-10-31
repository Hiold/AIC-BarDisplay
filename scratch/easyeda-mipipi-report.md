# EasyEDA Project Hardware Summary — "mipiPI-40pin MIPI屏幕开发板"

**Investigated via:** `easyeda-agent` CLI (v1.15.0) → daemon `127.0.0.1:60832` → EasyEDA Pro 3.2.175
connector (capabilities `schematic.v1`/`pcb.v1`). Read-only; nothing was modified in EasyEDA.

**Date:** 2026-10-30 (Asia/Shanghai) · **Project UUID:** `3de943d300544f9eb6fdb9239b3b6c65`

Schematic doc **`d133cbs`** — 3 pages: `SOC`, `DISPLAY`, `NAND CARD WIFI PA`. PCB `PCB1` linked to it.
**Core SoC: ArtInChip D133CBS (QFN-88).**

---

## 1. Display device / panel

### Panel connector (MIPI DSI FPC)
| Ref | Part | Footprint | LCSC | Notes |
|---|---|---|---|---|
| `fpc1` | XUNPU **FPC-05FB-40PH20** | `FPC-SMD_40P-P0.50_XUNPU_FPC-05FB-40PH20` | C2856837 | 40-pin, 0.5 mm-pitch MIPI-DSI FPC to an **external** panel. Footprint has 42 pads (2 spare). |

**The panel itself is NOT on this PCB** — it is an external module attached through `fpc1`. There is **no on-board LCD panel / LCD controller / driver IC** and **no model/resolution text** in the schematic (the DISPLAY page only carries two annotation strings: `LCD背光横流` and the FB-resistor formula `FB电阻计算公式R=200/背光电流（mA）`). The MIPI-DSI transmitter is the D133 SoC itself (built-in DSI). Panel resolution is **not stated anywhere in the project** — must come from the panel datasheet.

### Signal net map to `fpc1` (DISPLAY page)
| fpc1 pin | Net | Function |
|---|---|---|
| 2, 3, 22, 23 | `VCC_3V3` | Panel logic supply (3.3 V) |
| 4, 8, 9, 11, 14, 24, 25, 28, 31, 34, 37, 40 | `GND` | Ground |
| 5, 6 | `LEDA` | Backlight LED **anode** string |
| 12, 13 | `LEDK` | Backlight LED **cathode** (sensing) |
| 16 | `DSI_EN` | Panel/DSI enable (from U1 PD14 — U1.23) |
| 17 | `DSI_RST` | Panel reset (from U1 PD15 — U1.22) |
| 26 / 27 | `DSI_D3N` / `DSI_D3P` | MIPI DSI **data lane 3** |
| 29 / 30 | `DSI_D2N` / `DSI_D2P` | MIPI DSI **data lane 2** |
| 32 / 33 | `DSI_CKN` / `DSI_CKP` | MIPI DSI **clock lane** |
| 35 / 36 | `DSI_D1N` / `DSI_D1P` | MIPI DSI **data lane 1** |
| 38 / 39 | `DSI_D0N` / `DSI_D0P` | MIPI DSI **data lane 0** |

→ MIPI-DSI is **4 data lanes + 1 clock lane**, sourced directly from SoC U1 pins
`PD27/26=DSI_D0P/D0N, PD25/24=DSI_D1P/D1N, PD23/22=DSI_CKP/CKN, PD21/20=DSI_D2P/D2N, PD19/18=DSI_D3P/D3N`.

### Touch
| Ref | Part | Footprint | LCSC | Nets |
|---|---|---|---|---|
| `U2` | kinghelm **KH-FPC0.5-H1.0SMT-6P-QCHF** | `FPC-SMD_KH-FPC0.5-H1.0SMT-6P-QCHF` | C3250112 | 6-pin capacitive-touch FPC |

Touch nets: `CTP_SCL` (pin1), `CTP_SDA` (pin2), `CTP_RST` (pin4, active-low → R27 pulldown+N), `CTP_INT` (pin5), `VCC_3V3`, `GND`. All four signal lines pulled **up** to VCC_3V3 via 4.7kΩ (R25–R28). SoC side: U1.40=CTP_SCL, U1.39=CTP_SDA, U1.37=CTP_RST, U1.38=CTP_INT.

---

## 2. Backlight
| Ref | Part | Footprint | LCSC | Role |
|---|---|---|---|---|
| `U6` | **AP3019AUKTR-G1** (DIODES Inc) | `SOT-23-6` | C2679068 | Boost LED driver (backlight) |
| `L1` | SLO252012F4R7MTT (Sunltech) **4.7µH** | L2520 | C275675 | Boost inductor |
| `R9` | **5.1Ω** | R0603 | C25197 | FB sense resistor (sets LED current) |
| `R10` | **4.7kΩ** | R0603 | – | `LCD_PWM` pulldown to GND |
| `C26~C28` | 10µF / 1µF / 1µF | C0603 | – | Bulk/decap |

- **Pins:** VIN = `VCC_5V` (pin 6) → inductor L1 → SW; VOUT = `LEDA` (panel LED anodes); FB = `LEDK` (cathode sense); CTRL = **`LCD_PWM`**.
- **PWM input / EN:** the single `CTRL` pin (pin 4) carries **`LCD_PWM`** — this is both dimming and the ON/OFF control (AP3019 has no separate EN; low = off).
- **PWM source (SoC):** `LCD_PWM` net is driven from **U1.33** (D133 GPIO, PD bank).
- **Current:** on-schematic note `R=200 / I_BL(mA)` → with R9 = 5.1Ω, set LED current ≈ **39 mA**. (Test points TP5=LEDA, TP6=LEDK, TP7=LCD_PWM.)

---

## 3. MCU / SoC and USB (firmware-relevant)

| Ref | Part | Package | LCSC | Notes |
|---|---|---|---|---|
| `U1` | **ArtInChip D133 “D133CBS”** | QFN-88, 10×10 mm, 0.4 mm pitch | C19268973 | RV32 (XuanTie e9xx) SoC; schematic doc is named `d133cbs` |
| `USB1` | SHOU HAN **TYPE-C16PIN** USB Type-C connector | `USB-C-SMD_TYPE-C16PIN` | C393939 | 16-pin (8+8) Type-C receptacle |

### USB Type-C wiring (USB secondary-screen path)
| Signal | USB1 pin | SoC pin | Notes |
|---|---|---|---|
| `USB0_P` (DP) | A6, B6 (DP1/DP2) | U1.88 | D13x USB0 differential + |
| `USB0_N` (DM) | A7, B7 (DN1/DN2) | U1.87 | D13x USB0 differential − |
| CC1 | A5 | → `R7` 5.1kΩ → GND | USB **device** (peripheral) configuration |
| CC2 | B5 | → `R8` 5.1kΩ → GND | (board acts as a USB device / DUT) |
| VBUS | A4B9, B4A9 | → `VCC_5V` | Input power rail |
| SBU1/SBU2 | A8/B8 | floating | Unused |
| SHELL | 1–4 | GND | Shield |

→ The board enumerates as a USB slave supplying **VBUS 5 V** to the on-board rails; USB0 is the D13x **device** controller — correct path for the USB secondary-screen / CDC debug firmware (AGENTS.md `usbdisp` / `cdcdebug`). DP/DM go straight to U1; no USB PHY/mux on-board.

---

## 4. Power architecture
**Input rail: `VCC_5V`** (from USB-C VBUS, also brought out to U4 pin2, CN1 pin5, CN4 pin9 JST headers).

| Rail | Voltage | Source | Feeds |
|---|---|---|---|
| `VCC_5V` | 5 V | USB-C VBUS | PMIC U5 VIN; backlight U6; UART/IO headers |
| `VCC_3V3` | 3.3 V | **U5 buck1** (LX1 via **l1** 1.5µH; FB1 68k/15k ≈3.3V) | SoC VCC33_IO, panel logic, WiFi, SD, audio, flash, touch |
| `VCC_1V1` | 1.1 V | **U5 buck2** (LX2 via **l2** 1.5µH; FB2 47k/56k ≈1.1V) | SoC **VDD11_SYS** (core, U1 pins 8/30/68) |
| `VCC_1V8` | 1.8 V | SoC **internal LDO18** (U1.31) | 1.8 V IO / RTC domain (C14 10µF, C15 100n) |
| `LDO25` | 2.5 V | SoC **internal LDO25** (U1.70) | 2.5 V analog (C12 1µF, C13 100n) |

- **PMIC:** `U5` = everanalog **EA3036CQBR**, WQFN-20 3×3 mm, triple DC-DC buck (LCSC C570857).
  Inputs `VIN1/2/3` = `VCC_5V`; outputs: buck1 = 3V3, buck2 = 1V1, **buck3 (LX3) not connected**.
- **Enable:** `PMIC_EN` = `pwr1` button + R6 pull-up to 5 V → U5 EN1/EN2 (U5.8, U5.9).
- **Reset/boot:** `RESET` (U1.67) via `RST1` button; `BOOT1` button tied to `UART0_TX_DEBUG`/boot-select; main clock X1 = **24 MHz**; RTC X2 = **32.768 kHz**.

---

## 5. Flash / storage

| Ref | Part | Package | LCSC | Nets |
|---|---|---|---|---|
| `U7` | **ZB25VQ128ASIG** (Zbit / 恒烁) SPI NOR | SOP-8 | C609616 | `SPI0_CS, SPI0_MISO, SPI0_MOSI, SPI0_CLK, SPI0_WP, SPI0_HOLD`; VCC=`VCC_3V3` |

- Boot flash is the **SPI NOR U7**, on **SoC SPI0** (U1 pins 57/56/60/59/58/55).
- **ZB25VQ128 = 128 Mbit = 16 MiB.** ⚠️ **Firmware-relevant discrepancy:** the AIC-BarDisplay firmware partitions assume a **32 MiB NOR**, but this board’s boot NOR is a **16 MiB** device (or the part is mis-populated vs. the firmware target). Check the actual fitted device and the partition table before flashing.
- **No separate SPI NAND.** Other storage:
  - `CARD1` = microSD/TF socket (Yuandi TF-01A, LCSC C2889258) on **SDC1** (U1 PC0–PC6).
  - WiFi `U9` = RTL8189FTV module (M8189FS6) on **SDC0** (SDIO), `CHIP_EN = WIFI_ON` (U1.32).

---

## 6. Full BOM (key entries; all from `bom export` of schematic `d133cbs`)

| Ref | Part / Value | Footprint | Manufacturer | LCSC |
|---|---|---|---|---|
| U1 | **D133CBS** | QFN-88 10×10 | Artinchip | C19268973 |
| U5 | **EA3036CQBR** | WQFN-20 3×3 | everanalog | C570857 |
| U6 | **AP3019AUKTR-G1** | SOT-23-6 | DIODES | C2679068 |
| U7 | **ZB25VQ128ASIG** (16MiB NOR) | SOP-8 | Zbit | C609616 |
| U9 | **RTL8189FTV** (WiFi) | M8189FS6 | Realtek | – |
| U10 | **NS4168** (Class-D amp) | ESOP-8 | Nsiway | C910588 |
| U11 | **ICS-43434** (I2S MEMS mic) | MIC-SMD 6P | TDK InvenSense | C5656610 |
| USB1 | TYPE-C16PIN (Type-C) | USB-C-SMD | SHOU HAN | C393939 |
| fpc1 | FPC-05FB-40PH20 (40P MIPI FPC) | FPC 40P-0.5 | XUNPU | C2856837 |
| U2 | KH-FPC0.5-H1.0SMT-6P-QCHF (touch FPC) | FPC 6P | kinghelm | C3250112 |
| U3 | WAFER-GH1.25-4PWB (UART0 debug) | 4P | XUNPU | C3029379 |
| U4 | SM08B-GHS-TB (8P UART2/I2C/5V) | 8P | JST | C265111 |
| CN1 | HC-ZH-6PWT | 6P-1.5 | HCTL | C2845428 |
| CN2 | HC-1.25-2PWT (RTC battery) | 2P | HCTL | C2845379 |
| CN3 | AFC12-S02CCC-00 (speaker) | 2P | JS | C395343 |
| CN4 | 1.25-10P WTDK (JTAG/UART6/7/CAN/PA) | 10P | SHOU HAN | C5340802 |
| CARD1 | TF-01A (microSD) | TF-SMD | Yuandi | C2889258 |
| RST1/BOOT1/pwr1 | PTS810SJM250SMTRLFS (tact) | KEY 4P | C&K | C116501 |
| R9 | 5.1Ω (backlight FB) | R0603 | UNI-ROYAL | C25197 |
| R10,R19–R28 | 4.7kΩ (pullups/pulldowns) | R0603 | UNI-ROYAL | – |
| R7, R8 | 5.1kΩ (USB CC → GND) | R0603 | – | – |
| R14–R18,R2,R3,R5,R11–R13 | 4.7kΩ (SD/SDIO/SPI pullups) | R0603 | UNI-ROYAL | C23162 |
| X1 | 24 MHz | CRYSTAL 4P | YXC | C70590 |
| X2 | 32.768 kHz (DST310S) | OSC 2P | KDS | C93232 |
| L1 | 4.7µH (SLO252012F4R7MTT) | L2520 | Sunltech | C275675 |
| l1, l2 | 1.5µH (MLP2520V1R5ST0S1) | L1008 | TDK | C2835404 |

*Full BOM (repeated values collapsed by EasyEDA) captured from `bom export`; 46 unique rows.*

---

## Firmware-relevant take-aways
- SoC **D133CBS (QFN-88)**, schematic named `d133cbs` — matches AGENTS.md `d13x`.
- MIPI-DSI **4-lane**, panel **off-board** on 40-pin FPC; **resolution not defined in the project** (get from panel datasheet).
- Backlight control: **`LCD_PWM` (U1.33) → AP3019 `CTRL`**, LED current ≈39 mA (R9=5.1Ω formula `R=200/I`).
- USB: Type-C connector on **USB0 device** (CC1/CC2 5.1kΩ pull-down ⇒ peripheral). DP=`USB0_P`, DM=`USB0_N`, VBUS=`VCC_5V`.
- Rails: 5V (USB VBUS) → EA3036 buck1 **3.3V** + buck2 **1.1V**; SoC internal LDO18→**1.8V**, LDO25→2.5V.
- Boot flash: **SPI NOR ZB25VQ128 = 16 MiB** (⚠️ vs. firmware’s 32 MiB assumption).

---

## Command evidence log
All via `& "C:\Users\hiold\.local\bin\easyeda.exe" … --project "mipiPI-40pin MIPI屏幕开发板"` (PowerShell; host shows expected EasyEDA-3.2.175 `block` — ignored).

1. `health` → daemon v1.15.0 on 60832, 1 window, capabilities `schematic.v1`/`pcb.v1`; hostCompatibility `block` (expected). 
2. `doc ls` → PCB1 (★ active) + schematic pages DISPLAY / NAND CARD WIFI PA / SOC (doc `d133cbs`).
3. `doc open DISPLAY` + `sch read --no-check` → 23 components; fpc1 (40P MIPI FPC) with full pin→net map; U6 AP3019 backlight; U2 touch FPC; nets LEDA/LEDK/LCD_PWM/DSI_*/CTP_*; annotation text (R=200/I).
4. `doc open SOC` + `sch read --no-check` → 80 components; U1=QFN-88 D133 with all pin nets; U5 EA3036 rails; USB1 Type-C; SPI0/SDC0/SDC1/UART/JTAG nets; X1/X2 clocks; power nets VCC_5V/VCC_3V3/VCC_1V1/VCC_1V8/LDO25.
5. `doc open "NAND CARD WIFI PA"` + `sch read --no-check` → U7 SPI NOR, U9 RTL8189FTV (SDC0), CARD1 microSD (SDC1), U10 NS4168, U11 ICS-43434, U8 BT antenna.
6. `bom export --type csv` → full BOM CSV (46 rows incl. exact MPNs/LCSC): D133CBS, EA3036CQBR, AP3019AUKTR-G1, ZB25VQ128ASIG, RTL8189FTV, NS4168, ICS-43434, FPC-05FB-40PH20, KH-FPC0.5-6P, TYPE-C16PIN, etc.
7. `doc open PCB1` + `pcb board-info` → PCB1 linked to schematic `d133cbs`; `pcb list` confirms placement; `pcb outline-get` → ~3375×2135 mil board.