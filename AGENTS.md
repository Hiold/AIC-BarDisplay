# AGENTS.md — AIC-BarDisplay 工程指南

> 本文档面向在 **AIC-BarDisplay** 仓库中工作的 AI 代理 / 协作者，由"系统硬件架构师"角度撰写。
> 目标：让代理在上手前就理解这是一套什么样的**嵌入式硬件固件工程**、哪里能动、哪里不能动、改一处会波及什么。

工程性质速览：**匠芯创（ArtInChip）D125/D133 长条屏（电摩仪表 / USB 副屏）固件**。RV32 内核、RT-Thread 实时操作系统 + baremetal 引导加载器两级固件，运行在 **8 MiB PSRAM + 32 MiB SPI NOR** 硬件上，由主机经 USB 送 JPEG 帧、芯片 VE 硬件解码、DE 图层合成输出到 MIPI DSI 长条面板。仓库自带完整交叉工具链与构建脚本，**无需 Luban-Lite SDK / SCons / Kconfig**，一条命令产出可烧录 IMG。

---

## 1. 系统架构总览（硬件视角）

### 1.1 芯片与内存

| 项 | 值 | 说明 |
| --- | --- | --- |
| 内核 | RV32 (`rv32imafdcpzpsfoperand_xtheade`, `ilp32d`) | XuanTie e9xx 系列，内嵌 DSP 扩展 |
| MCU 频率 | 480 MHz 核 / AXI0 200 MHz / AHB0 200 MHz / APB0 100 MHz | `AIC_CLK_*_FREQ` |
| PSRAM | 8 MiB（`AIC_PSRAM_SIZE 0x800000`） | 关键资源，几乎一切显示缓冲都在此 |
| SRAM | 512 KiB（`AIC_SRAM_TOTAL_SIZE 0x80000`） | 引导 / 堆栈 |
| SPI NOR | 32 MiB（Q256），四线 | 代码 + 只读 FAT 资源盘 + 可写设置 |
| 芯片代号 | `d12p` = D125，`d13x` = D133 | 两套 SoC，启动/HAL 各自独立目录 |

**内存铁律**：PSRAM 仅 8 MiB（CMA 可用约 7 MiB）。480×1920 全屏 RGB888 framebuffer ≈ 2.64 MiB，两份 NV12 解码表面 ≈ 2.64 MiB，双槽 JPEG RX ≈ 0.57 MiB，再加 1 MiB 软件保留区——**内存预算几乎封顶**。任何"再加一份全屏缓冲 / 放大 framebuffer"的事件几乎必然 OOM、导致屏不亮 + USB 不识别（见 `doc/横屏显示设计说明.md` 的失败教训）。**改内存布局必须先在 8 MiB 预算内复核**。

### 1.2 两级固件 Boot 流

```
SPI NOR ──> tinySPL bootloader (baremetal) ──> 应用固件 (RT-Thread)
```

- **bootloader**：`application/baremetal/bootloader/`，baremetal 运行，负责上电初始化（时钟/pinmux/内存/控制台/UART）、检测升级模式（AICUPG）、按分区启动应用。同芯片三款屏共用一套 bootloader（`build.py` 中每芯片只编一次）。
- **应用固件**：RT-Thread，含三种应用形态：
  - `usbdisp`：USB 副屏（主机 JPEG 流 + 空闲视频）。
  - `cdcdebug`：USB-CDC 控制台调屏固件（无 usbdisp）。
  - `dashdisp`：电摩仪表整机界面（LVGL 全屏绘制，320×1280）。

### 1.3 视频 / 显示数据通路

```
主机(Windows IDD) --JPEG/Bulk--> USB RX 双槽 --> VE JPEG 硬件解码 --> NV12 双表面
空闲视频(SPI NOR FAT) --LVGLMJPG--> 同一 VE 解码  --> NV12
LVGL ARGB8888 UI 窗口 ----像素 Alpha----> DE 视频+UI 图层合成 --> MIPI DSI 长条屏
```

关键设计：**传输与解码分离**（USB 回调不做内存分配/阻塞/解码）；**硬件解码**（VE）；**原生分辨率**声明；**逐像素合成**（UI 与 NV12 视频独立）；**受控内存生命周期**（空闲视频复用 RX/解码资源）。

### 1.4 存储分区（32 MiB NOR）

| 分区 | 大小 | 用途 |
| --- | --- | --- |
| `spl` | 512 KiB | 共享 bootloader |
| `env` / `env_r` | 各 128 KiB | 环境变量与冗余副本 |
| `userid` | 256 KiB | D125 保留分区 |
| `os` | 3 MiB | 应用固件 |
| `rodata` | 27 MiB | 只读 FAT 资源盘（Windows 驱动 + 空闲视频） |
| `data` | 1 MiB | 可写设置区 |

精确布局以 `target/<chip>/display-nor/pack/image_cfg.json` 为准。**rodata 分区上限 27 MiB + os 等，构建时会校验总和不超 32 MiB**。

---

## 2. 仓库目录结构（职责与可动性）

```text
build.py / build.cmd        构建唯一入口（自研，非 SDK）
variants.json               所有固件变体的唯一事实来源
cconfig.h                   编译器配置（自动生成，勿手改注释）
firmware/
  configs/                  rtconfig.h 式配置头（每个变体一份，编译期宏）
  graphs/                   变体的源码清单 / includes / flags / libs
application/
  baremetal/bootloader/     tinySPL 引导加载器（多命令、AICUPG 升级）
  rt-thread/usbdisp/        USB 副屏应用：main/transport/video/display/ui/settings/simhei 字库
  rt-thread/cdcdebug/       调屏 CDC 控制台：cdc_console + main
  rt-thread/dashdisp/       电摩仪表：main/dash_hmi/dash_data/dash_widgets
bsp/
  artinchip/                芯片启动、HAL、drv、drv_bare、sys/d12p|d13x
  artinchip/drv/display/    drv_fb.c（dispdbg 调屏命令）、panel/（面板驱动）
  common/  peripheral/      CRC/image/partition/private_param、SFUD
kernel/
  rt-thread/                裁剪版 RT-Thread 内核 + 组件(DFS/FAL/finsh/驱动/IPC/libc)
  common/                   OSAL(aic_osal)、命令
packages/
  artinchip/                env、lvgl-ui(lvgl_v9+ge2d+mpp_dec+port)、mpp(VE/GE/fb)、of 等
  third-party/              cherryusb、fdtlib、littlefs、cpu_usage
target/
  <chip>/display-nor/       board.c / pinmux.c / sys_clk.c、pack/image_cfg.json、pbp_cfg.json
tools/
  scripts/                  gen_partition_*/makefatfs/mk_image/mk_prebootprog/mk_private_resource
  gen_*_variant.py          派生变体生成脚本、make_static_video.py
toolchain/                  riscv64-unknown-elf GCC 10.2.0 交叉工具链（已有 .ready 标记）
doc/                        调屏 / CDC / 横屏 / TXW935003B 调试文档
images/                     (gitignore / 构建输出) 最终 .img 与 manifest.json
build/                      (构建中间产物，默认构建后清理)
```

> **注意**：`kernel/`、`packages/`、`bsp/`、`target/` 虽是源码树，但多为**从 Luban-Lite 裁剪移植的第三方/厂商代码**。代理通常不应大改它们；改动前先确认没有更上层/更合适的注入点（变体、config 头、应用层）。

---

## 3. 构建系统（最重要的工作流）

**入口**：`build.cmd <变体id>...` 或直接 `python build.py <变体id>...`。构建在 `build.py` 中完成，核心要点：

### 3.1 变体驱动模型
- 事实来源 = `variants.json`：每个条目含 `chip`、`graph`（源码模板图）、`product`、`version`、`fps`、`jpeg_quality`、`timing{width,height,pixelclock,porches,hsync,vsync,lane_num,ln_assign,ln_polrs}`，可选项 `assets_dir`。
- **graph**（`firmware/graphs/*.json`）是"这一份固件究竟编哪些 .c、"的唯一清单：`sources`（每个有独立 cflags/cpppath/defines）、`includes`、`defines`、`libpaths`、`libs`、`cflags/cxxflags/asflags`、`linkflags`。
- **config 头**（`firmware/configs/*.h`）被复制为构建目录的 `rtconfig.h`，叠加 `cconfig.h` 与生成的 `variant.h`（`FIRMWARE_*` 时序/JPEG/fps 宏）。

### 3.2 构建/打包流水线
```
对所有选中变体：
  每芯片先 build bootloader（chip + "_display-nor_baremetal_bootloader" graph）一次
  每变体:
    configure() → 生成 rtconfig.h/cconfig.h/variant.h + 分区表头
    build_elf() → 并行编译(TD  线程池) + 链接 + objcopy .bin
    pack()      → 打包 rodata FAT、pbp、bootloader + os.its → mpk_image → 唯一 .img
    verify_image() → 校验组件匹配/资源SHA256/NOR≤32MiB
 写 images/manifest.json（变体、路径、字节数、SHA256）
```
- **并发安全**：变体间按 `chip` 共享 bootloader 构建缓存；用 `build/.lock` 防止并发。
- **缓存**：每个变体独立输出目录；对象按构建命令 SHA 固化签名，config 头/依赖时间戳过期则重编。
- **校验**：IMG 内 `image.target.rodata`/`spl` 必须与生成文件逐字节一致；视频 SHA256 必须匹配源资产；分区总和必须 ≤32 MiB。

### 3.3 常用命令
```bat
build.cmd                              :: 编全部变体
build.cmd d125-txw935003b               :: 单变体
build.cmd --list                        :: 列变体
build.cmd --jobs 8 --keep-build         :: 保留 build/（ELF/map/日志/缓存）
build.cmd --clean                       :: 清 build/ 与 images/
```
只改 `timing`/`assets_dir` 等参数 → 只改 `variants.json`；换源码清单 → 改 `firmware/configs` + `graphs`；换面板初始化 → 改 `bsp/.../display/panel/*.c` 并同步 config/graph。**仅改 JSON 无法自动适配未知屏。**

### 3.4 平台约束
- **工程路径不能含空格**（FAT 打包工具要求）。当前工作区 `E:\001\AIC-BarDisplay` 满足。
- 变体 ID 限小写字母/数字/连字符；`product` 限 `[a-z0-9_-]+`；宽高必须偶数（NV12）；宽高 ≤4096。
- ARM/`x86` 无关——这是 RISC-V 交叉编译，`toolchain/` 内置 `riscv64-unknown-elf-gcc`，**不要试图用宿主编译器编译固件**。

---

## 4. 变体矩阵（现状）

| 变体 | 芯片 | 屏幕 | 分辨率 | 用途 |
| --- | --- | --- | --- | --- |
| `d125-txw935003b` | d12p | TXW935003B | 480×1920 | 正式副屏 |
| `d125-boe1126` | d12p | BOE1126 | 440×1920 | 正式副屏 |
| `d125-av092k5u` | d12p | AV092K5U | 462×1920 | 正式副屏 |
| `d133-txw935003b` | d13x | TXW935003B | 480×1920 | 正式副屏 |
| `d133-boe1126` | d13x | BOE1126 | 440×1920 | 正式副屏 |
| `d133-av092k5u` | d13x | AV092K5U | 462×1920 | 正式副屏 |
| `d133-cwd686` | d13x | CWD686 | 480×1280 | 正式副屏 |
| `d133-boe-av093x` | d13x | AV093X 320×1280 | 320×1280 | 正式副屏（自有 assets_av093x） |
| `d133-boe-av093x-dash` | d13x | AV093X | 320×1280 | **电摩仪表**（dashdisp） |
| `d133-boe1126-cdc` | d13x | BOE1126 | 440×1920 | USB-CDC 调屏 |
| `d133-boe1126-av092k5u-test` | d13x | AV092K5U | 462×1920 | AV092K5U 测试 |
| `d133-txw935003b-cdc` | d13x | TXW935003B | 480×1920 | USB-CDC 调屏 |
| `d133-boe-av093x-cdc` | d13x | AV093X | 320×1280 | AV093X 调屏 |

派生变体（CDC/AV093X/测试）由 `tools/gen_*_variant.py` 脚本从正式变体生成，可重复运行，**增量修改时优先沿用脚本而不是手抄**。

---

## 5. 关键改动规范（What to touch where）

### 5.1 新增屏幕 / 时序变体
1. `variants.json` 复制条目，改唯一 `id`、`product`、`timing.*`。
2. `ln_assign:291` = `0x0123`；时序 `pixelclock = htotal × vtotal × fps`；HTOTAL/VTOTAL 含 porch+hsync/vsync。
3. 若用独立空闲视频 → `assets_dir` 指向新目录。
4. 面板初始化表只在控制器/寄存器序列变时才动 `bsp/artinchip/drv/display/panel/`，并同步 `firmware/graphs`（面板源已列入该屏所用 graph）+ `firmware/configs`（面板宏/GPIO）。
5. CDC 调屏变体 → 用 `tools/gen_*_cdc_variant.py` 派生。
6. `./build.cmd <id>` 验证产出。

### 5.2 调屏（bring-up）工作流
- 控制台 `aic>` + `dispdbg` 在线改时序/DSI/DCS/图案，**免刷机、内存级、掉电失效**。
- 控制台通道：有 UART → D125=UART0 / D133=UART6（115200 8N1）；无 UART → 用 CDC 调试变体，Windows 免驱 COM 口。
- 调通后必须**回填** `variants.json` 的 `timing`，并重算 pixellock 后重建正式固件。`width/height`、`fps`、`jpeg_quality`、`ln_assign/polrs` 是编译期/固件宏，`dispdbg` 改不胜任部分（`res` 除外，但受已分配 fb 限制）。
- 实现：`bsp/artinchip/drv/display/drv_fb.c`。热重载需先停 DE 并切换时钟、重跑 `attach_panel()`、处理 DE 硬复位清寄存器、图层与缓冲重新关联。

### 5.3 电摩仪表（dashdisp）开发
`application/rt-thread/dashdisp/`：
- `dash_data.c/.h`：**数据模型 + 外部 MCU/传感器注入接口**（`dash_set_*`）；三轴滑动平均在 app 内做。
- `dash_widgets.c/.h`：四个自绘 LVGL 控件（罗盘/姿态/倾角/加速度）。
- `dash_hmi.c/.h`：组装 320×1280 主界面与演示数据源。
- `main.c`：起 16 KiB `dashdisp` 线程，`lv_port_disp_init` 绑定 AIC DE 图层。
- 接线需求（head/磁力计/IMU）真实接入时应只调 `dash_set_*`，不动模型；渲染由 LVGL 定时器驱动。
- 注意 screen = 320×1280（AV093X），与 usbdisp 的通用路径无关。

### 5.4 遍历注意
- 布尔/位域约定：`ln_assign`/`ln_polrs` 在 JSON 用**十进制**；`dispdbg` 支持十进制或 `0x` 前缀。
- 产品能力串 `R<WxH>_Ejpg<q>_Fps<f>_Bl<budget>` 由 transport 层用宏拼接，改动分辨率/FPS/JPEG 后注意一致性。
- 空转视频与 USB 副屏共享 RX/解码资源；进入副屏要清理 LVGL 页面对象及动画，保留可复用核心与 display 实例。
- 任何内存分配改动都要复核 8 MiB 预算与 IPC/mutex 生命周期。

---

## 6. 验证与常见陷阱

### 6.1 构建失败排查
- 依赖 `.d` 文件/签名/时间戳导致的重编：先删对应 `build/<variant>`，重跑。
- 报错 "Only D125/d12p and D133/d13x" → 芯片代号仅两种。
- 报错 partitions exceed 32 MiB → rodata 内容过大。
- 编译通过但屏不亮：先怀疑内存 OOM / fb 分配失败（看串口 `dispdbg info` 或 boot log）。

### 6.2 视为厂商/SDK 代码、非必要不动
- `kernel/rt-thread/`、`kernel/common/`、`packages/third-party/`、`bsp/artinchip/hal/`、`bsp/artinchip/sys/`。
- 改动集中在：`variants.json`、`firmware/{configs,graphs}`、`application/`、`bsp/artinchip/drv/display/{drv_fb.c,panel}`、`tools/gen_*`、`target/<chip>`（board/pinmux/sys_clk）。
- 裸机 bootloader (`application/baremetal/bootloader`) 独立于应用固件，改它需重编对应芯片 boot 并重新打包（build.py 已自动处理）。

### 6.3 文档底稿（保持同步）
- `README.md`：架构/内存/变体/构建/dispdbg 主文档。
- `doc/未知屏调试指南-d133-boe1126.md`：决策树与历史调试记录。
- `doc/USB-CDC调试固件使用说明.md`：CDC 连接与使用。
- `doc/TXW935003B调试记录.md`：TXW935003B 屏调试记录。
- `doc/横屏显示设计说明.md`：横屏 + 内存安全改造说明（含一次失败教训）。

---

## 7. 代理工作守则（总结）

1. **先定位再改动**：用 `variants.json` / `firmware/graphs` 确认源码清单，别改没被 graph 引用的文件（它们不参与编译）。
2. **改动最小化**：能在 `variants.json` + config 头解决的，不要动 C 源码；能派生脚本的，不要手写变体。
3. **参数 vs 代码**：屏参/时序/分辨率 → 配置文件；面板初始化/HAL/驱动逻辑 → C 源码 `bsp`；应用业务 → `application`。
4. **内存敏感**：改动涉及新增缓冲/放大多份全屏表面时，先在 8 MiB 预算内论证，参考 `doc/横屏显示设计说明.md` 的内存安全方案。
5. **验证闭环**：构建 → `verify_image`（自动）→ 若涉及屏，需上机 `dispdbg` 验证并回填参数。
6. **层级敬畏**：厂商/HAL/内核目录视为只读居多；优先在上层注入。
7. **交叉目标**：这是 RISC-V 裸机/RTOS 固件，不用宿主编译器、不联网安装、不假设 SDK 环境。

> 遇到不确定的硬件/驱动行为，优先阅读 `doc/` 下的调试记录与本文件第 6.3 节列出的底稿；它们是这个仓库最重要的"历史经验"载体。