# D133-BOE1126 USB-CDC 调试固件使用说明

> 配套文档：`doc/未知屏调试指南-d133-boe1126.md`（屏幕时序调试方法论与 dispdbg 命令详解）
> 本文只讲“没有 UART 引出时，如何用 USB 虚拟串口当控制台”。

## 一、背景与方案

开发板没有预留 UART 接口，而 RT-Thread 控制台默认绑定 `uart6`，无法交互。
方案：**新建一个独立调试固件变体 `d133-boe1126-cdc`**，把控制台重定向到 USB CDC-ACM 虚拟串口。

为什么用独立变体而不是改正式固件：
- 正式固件 `d133-boe1126` 的 USB 口已被 usbdisp 复合设备（vendor+HID+MSC）占用，那是给 PC 端投屏用的，不能同时当控制台。
- 独立变体把 USB 完全让给 CDC 控制台，Windows 免驱识别为标准 COM 口，直接跑 `dispdbg` 调屏。
- **不影响正式固件**：两者是不同的 graph/config/变体，`d133-boe1126` 照常构建。

关键前提（已核实）：屏幕显示是 `aicfb_probe` 通过 `INIT_DEVICE_EXPORT` **自动初始化**的，开机即画五色棋盘测试图并注册 `dispdbg` 命令，不依赖任何应用逻辑。所以调试固件砍掉 usbdisp/LVGL/mpp 后，屏幕依旧正常驱动，控制台照常可用。

## 二、已完成的改动

| 文件 | 改动 | 说明 |
|---|---|---|
| `packages/third-party/cherryusb/class/cdc/usb_cdc.h` | 新增（从参考工程移植） | CDC 类常量与描述符宏 |
| `packages/third-party/cherryusb/class/cdc/usbd_cdc_acm.h` | 新增（移植） | CDC ACM 类接口 |
| `packages/third-party/cherryusb/class/cdc/usbd_cdc_acm.c` | 新增（移植） | CDC ACM 类实现（line coding/DTR） |
| `application/rt-thread/cdcdebug/cdc_console.c` | 新增（自研桥接层） | 注册 `vcom` 字符设备 + 绑定 console + 收发环形缓冲 + 端点回调 |
| `application/rt-thread/cdcdebug/main.c` | 新增 | 精简应用入口，打印引导提示 |
| `firmware/graphs/d13x_display-boe1126-cdc_rt-thread_usbdisp.json` | 生成 | 裁剪后的源文件清单（186 个源文件，砍掉 136 个） |
| `firmware/configs/d13x_display-boe1126-cdc_rt-thread_usbdisp.h` | 生成 | `RT_CONSOLE_DEVICE_NAME` 改为 `vcom`，关闭 usbdisp/LVGL/littlefs/MSC/HID |
| `variants.json` | 追加条目 | 新增 `d133-boe1126-cdc` 变体 |
| `tools/gen_cdcdebug_variant.py` | 新增 | 从正式变体派生调试变体的生成脚本（可重复运行） |

桥接层工作原理：
- **输出**：`rt_kprintf` → console 设备 `vcom` 的 write 回调 → TX 环形缓冲 → CDC bulk IN 端点发往 PC。USB 未枚举前的输出先存缓冲，枚举完成（CONFIGURED 事件）后自动补发。
- **输入**：CDC bulk OUT 端点收到 PC 数据 → RX 环形缓冲 → 触发 `rx_indicate` 唤醒 finsh shell 线程 → `dispdbg` 等命令即可交互。
- 设备注册在 `INIT_DEVICE_EXPORT`（早于 finsh），USB 控制器启动在 `INIT_APP_EXPORT`，时序安全。

## 三、构建产物

```
E:\001\AIC-BarDisplay-main\images\d133-boe1126-cdc.img
```

- 体积 27.9 MB（与正式固件同格式的 NOR 镜像）
- 已通过 build.py 的镜像完整性校验
- text 段 356 KB（正式固件 635 KB，确认 LVGL/mpp 已裁掉）
- 已核验关键符号进入固件：`usbd_cdc_acm_init_intf`、`vcom_write`/`vcom_read`、`__rt_init_cdc_console_register`/`__rt_init_cdc_console_usb_init`、`cmd_dispdbg`、`aicfb_probe`

## 四、烧录与连接

### 1. 烧录
烧录方式**与正式固件完全相同**（同格式 `.img`，写入 NOR）。用你现有的 aicupg / AIC 烧录工具把 `d133-boe1126-cdc.img` 刷进去即可。
> 注意：这会替换板上当前的正式固件。调完屏、拿到正确时序参数后，把参数回填 `variants.json` 的 `d133-boe1126` 段，重新构建正式固件刷回。

### 2. USB 连接
- 用 USB 线连接开发板的 **USB Device 口**（即原 usbdisp 用的那个口）到 PC。
- 设备标识：**VID `0x303A` / PID `0x2988`**，产品名 “D133 CDC Console”。
- Windows 10/11 免驱，自动识别为一个 **COM 口**（设备管理器 → 端口）。

### 3. 打开终端
用任意串口终端（PuTTY / MobaXterm / SecureCRT / minicom 等）连接该 COM 口：
- 波特率：**任意值均可**（CDC 虚拟串口不关心波特率，习惯填 115200）
- 数据位 8、停止位 1、无校验（8N1）
- 连上后按回车，应出现 RT-Thread shell 提示符 `aic>`

## 五、连上之后：调屏流程

进入 `aic>` 提示符后，按配套指南《未知屏调试指南》的决策树操作。速查：

```
dispdbg info          # 查看当前时序/lane/fb 参数
dispdbg pattern 1     # 全白测试图
dispdbg pattern 2     # 全黑测试图
dispdbg read 0x04     # DCS 读屏 ID，识别未知屏型号
dispdbg set assign 0x321   # 在线改 lane 顺序（自动重启面板）
dispdbg set fps 50         # 在线改帧率试探
dispdbg reset         # 用当前参数重新初始化面板
dispdbg               # 不带参数打印完整帮助
```

`dispdbg set` 改的参数在内存里生效、免重烧；调通后把最终值写回 `variants.json` 的 `d133-boe1126` 段，重新构建正式固件。

## 六、重新构建

改动 `cdc_console.c` / `main.c` 或时序参数后，重新构建调试固件：

```
cd E:\001\AIC-BarDisplay-main
.\build.cmd d133-boe1126-cdc
```

若改动了 `variants.json` 里调试变体的 timing，或想从最新正式变体重新派生（比如正式固件的源文件清单有更新），先跑生成脚本再构建：

```
python tools\gen_cdcdebug_variant.py
.\build.cmd d133-boe1126-cdc
```

## 七、验证状态与注意事项

**已验证**（本机可确认的部分）：
- [x] 编译通过，无警告无错误
- [x] 镜像完整性校验通过（build.py 内置 verify_image）
- [x] 关键符号链接进 ELF，未被 `--gc-sections` 裁掉
- [x] 自动初始化表项 `__rt_init_cdc_console_*` 就位

**待上板实测**（需硬件，本机无法验证）：
- [ ] PC 端能否枚举出 COM 口（取决于 USB 硬件连接与 D+/D- 走线）
- [ ] 终端连上后能否出 `aic>` 提示符并交互
- [ ] `dispdbg` 调屏是否按预期改变屏幕显示

**注意事项**：
1. **枚举前的日志会缓冲**：USB 连上 PC 完成枚举前，`rt_kprintf` 输出暂存 TX 环形缓冲（4 KB），枚举后补发。若启动日志很长可能溢出丢弃早期部分，但不影响枚举后的 `dispdbg` 交互。
2. **VID/PID 可改**：如与 PC 上其他设备冲突，改 `cdc_console.c` 里的 `USBD_VID`/`USBD_PID` 重新构建。
3. **一次只刷一个固件**：调试固件和正式固件都独占 USB 口，不能共存。调屏用 CDC 固件，出货/投屏用正式固件。
4. **CDC 类来源**：移植自同芯片 D13x 参考工程 `mipi-pi-d133-master` 的 CherryUSB v1.5，与当前工程 core 同代（busid API），已确认兼容。
