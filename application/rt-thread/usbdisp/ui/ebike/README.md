# ebike 电摩仪表 UI 移植（usbdisp UI 叠加层）

把 `E:\001\lvgl-ui`（LVGL v9 `lv_demo_ebike` 电摩仪表 Home 页）移植到本 usbdisp
工程，作为 LCD **UI 图层**在 LCD **背景视频图层**之上渲染，保留本项目的背景视频
（空闲视频 / USB 副屏）功能。

## 作用对象

- 变体：`d133-mipipi-boe1126`（mipiPI，SPI-NAND + SD 卡，`AIC_USB_DISP_EBIKE_UI` 开）。
  其它 display-nor 变体不定义该宏，仍维持原全透明 overlay/背景视频行为。
- 屏幕：440×1920 竖长条。LVGL 显示按 320×480 原生创建，`lv_display_set_rotation(,
  LV_DISPLAY_ROTATION_90)`（LVGL 9.1 只交换**逻辑**分辨率）使 LVGL 按 **480×320 逻辑画布**
  排布——恰等于 Home 页设计尺寸，无拉伸、壁纸铺满。真正的像素旋转 + 等比例放大在 `ui.c`
  的 flush 回调里做：GE2D 单趟把横版画布旋转 90° 并缩放 440/320=×1.375，写入 440×660 竖版
  扫描缓冲；DE UI 层扫描该缓冲并居中于 (0, 630)，短边 440 全占满；视频在 VIDEO 图层于其下合成。

## 目录结构

```
application/rt-thread/usbdisp/
  ui.c                       UI 层入口；AIC_USB_DISP_EBIKE_UI 时调用 lv_demo_ebike_create(screen)，
                             设 90° 逻辑旋转并在 flush 中用 GE2D 把画布旋转+等比缩放进 440×660
                             竖版扫描缓冲（GE 不可用时软件融合缩放回退）；
                             屏幕背景透明透出视频
  ui/uires_res.c/.h          一次性把 uires .bin 读进 RAM（SRAM/PSRAM-SW 小图、CMA 大图）
  ui/ebike/                  （本移植）
    lv_demo_ebike.c/.h        demo 入口（lv_demo_ebike_create 组件化入口）
    lv_demo_ebike_home.c/.h   Home 页
    lv_demo_ebike_private.h   字体/颜色宏 + observer 兼容原型
    lvgl_compat.c             observer 桥接（lv_observer_get_user_data / lv_subject_deinit）
    ebike_assets.c/.h         用 uires_res_load 加载 ebike 图，缓存 lv_draw_buf_t*
    translations/lv_i18n.c/.h i18n（en/zh/ar；默认 zh）
    fonts/font_ebike_{130,inter_14,trump_24,trump_48}.c  字库（链接为 rodata）
```

`lv_demo_ebike.c / lv_demo_ebike_home.c` 里的图片（scale/arrow/lamp/clock）经
`ebike_asset("img_...")` 从 uires 资源加载，而非 C 数组符号，避免把数 MB 位图编进固件。
页面背景图 `img_ebike_bg` 与半透明白色磨砂玻璃面板均已移除：UI 不贴壁纸、不垫白色底板，
中间带背景完全透明，逐像素透出 VIDEO 图层视频；右侧卡片维持原设计亮色底
（`dist` 0x55FFEB、`battery` 亮绿指示条）与黑字黑圆点。透明能真正落到屏上的前提是
GE 透传画布 alpha（见下文 `ui_rotate_canvas`）。

## LVGL 配置

在 `packages/artinchip/lvgl-ui/lvgl_v9/lv_conf.h` 的 `APP_USB_DISP` 块之后新增
`AIC_USB_DISP_EBIKE_UI` 块，重新开启 ebike 所需特性：
`LV_USE_DEMO_EBIKE / LV_DEMO_EBIKE_PORTRAIT=0 / LV_STDINT_INCLUDE / LV_USE_THEME_SIMPLE /
LV_USE_FLEX / LV_USE_GRID / LV_USE_OBSERVER / LV_USE_IMAGE / LV_USE_BAR / LV_USE_ROLLER /
LV_USE_ARC / LV_USE_LABEL`。

补入 LVGL bundle 缺失的模块源并加入 graph（`d13x_display-mipipi-boe1126_rt-thread_usbdisp.json`）：
`layouts/flex/lv_flex.c`、`layouts/grid/lv_grid.c`、`widgets/bar/lv_bar.c`、
`widgets/roller/lv_roller.c`、`widgets/image/lv_image.c`、`themes/simple/lv_theme_simple.c`。

> 本项目 LVGL v9 bundle 缺 `lv_bidi.c`，故移除 `lv_demo_ebike.h` 里对 BIDI/Arabic 的
> `#error`（目标为简体中文，不启用阿拉伯文渲染）。

## 资源部署（mipiPI / SD 卡）

mipiPI 为 `AIC_USB_DISP_NO_RODATA`，无 on-flash rodata；ebike 图片走 SD 卡。
把已生成的 uires 资源复制到 SD 卡对应目录（与 `LVGL_STORAGE_PATH` 一致）：

```
/sdcard/lvgl_data/uires/ebike/
    img_ebike_scale.bin       # 180x320 刻度盘（RGB565A8）
    img_ebike_arrow_left.bin
    img_ebike_arrow_right.bin
    img_ebike_lamp.bin
    img_ebike_clock.bin
```

这些 .bin 由 `tools/gen_ebike_resbin.py` 生成（源 PNG/C 数组在 tools/uires_src 与
lvgl-ui 的 ebike generated）。固件端 `uires_res_load` 会把 `L:/uires/ebike/x.bin`
映射到 `/sdcard/lvgl_data/uires/ebike/x.bin`（no_rodata 分支）。

## 构建

```
build.cmd d133-mipipi-boe1126 --keep-build
```

## 说明 / 已知点

- `ebike_warning.c`（左右警示弧形叠加）依赖 LVGL `canvas` 全屏像素绘制（内存/CPU 开销大），
  本移植暂未纳入，避免挤占 8 MiB PSRAM 预算；如需可后续打开（需注册 `widgets/canvas`）。
- LVGL 9.1 的 `lv_display_set_rotation()` **不旋转像素**（9.2+ 才由驱动/GPU 负责），把横版
  画布直送竖版 DE 窗口会因 stride 不匹配整带错乱。修复：`ui_flush()` 经 GE2D
  （`mpp_ge_bitblt`，映射与厂商 `lv_draw_ge2d_rotate()` 一致：LVGL 90° = `MPP_ROTATION_270`）
  把横版画布旋进第 3 块 fb 尾部缓冲（竖版扫描缓冲）。运行时 GE 走 cmdq ops
  （`mpp_ge_open` 查 `IOC_GE_MODE` 得 `GE_MODE_CMDQ` → `ge_cmdq_ops`），其 `ge_bitblt` 在
  src/dst 尺寸不同时**自动启用硬件缩放器**（16.16 相位按尺寸比推导，90°/270° rot0 会交换
  dst 宽高），故旋转+缩放是**单趟 GE2D**；GE 打开/位图失败回退 `ui_scale_rotate()`
  （自写“最近邻缩放+旋转”融合软件路径，16.16 定点步长取样，输出与 GE 同一映射）。
- **GE alpha 透传（透明背景的关键）**：`ge_bitblt` 在 `ctrl.alpha_en=0` 时会把 ARGB8888
   目的缓冲按 XRGB8888 写出——所有扫描像素 alpha 被强制成不透明，DE 用整条不透明 UI
   盖住视频，实测中间带渲染为纯白不透明条。故按厂商 ARGB8888 配方（`lv_draw_ge2d_img.c`）
   置 `alpha_en=1`、`src_alpha_mode=2`（像素 alpha × 全局）、`src_global_alpha=255`，
   像素 alpha 原样透传；PD 规则取 `GE_PD_SRC`（系数 fs=1.0、fd=0.0）：结果即源像素本身、
   不读 dst，因此扫描缓冲**无需每帧清零**，也不会引入 dst 残影。软件回退是纯像素拷贝，
   alpha 天然保留。
- 横版 Home 页的原生设计分辨率是 **480×320**（刻度盘 `img_ebike_scale` 高 320，
  `lv_demo_ebike.h` 注明横版最大设计尺寸 480×320）。早期画布取 480×440 导致 demo 根容器
  `lv_pct(100)` 铺满时
  沿短边拉伸 1.375 倍、壁纸盖不满露出白磨砂玻璃“白边”；曾用钳尺寸居中缓解。现方案：
  `lv_display_create(320, 480)` + rot90 使逻辑画布 = **精确 480×320 设计盒**，页面按原始
  比例渲染（无拉伸、无白边，背景完全透明），放大交给 flush：等比缩放 440/320=×1.375 →
  扫描窗口 **440×660**（480×440/320=660 整除），DE 居中于 `pos=(0, (1920-660)/2=630)`，
  短边 440 全占满、长边窗口自适应。缩放比在 `ui_pick_canvas()` 中算好
  （`ui_scale_num/den`，若长边超屏则改为按长边取比），并同步用于 GE dst 尺寸与软件回退。
- 缓冲布局：横版双画布 480×320×4×2 = 1,228,800 + 竖版扫描 440×660×4 = 1,161,600，
  合计 **2,390,400 ≤ 全屏 fb 440×1920×3 = 2,534,400**（余量 144,000），全部复用
  `udisp_screen.framebuffer`，零新增分配。`udisp_ui_init()` 有兜底：若面板 fb 容不下缩放
  窗口，自动降级为 1:1 旋转窗口（scan == canvas 尺寸，几何仍正确、只是不放大）；再不容纳
  才放弃 UI。非 ebike 变体 scan == canvas，行为与旧版逐字节一致。
- 实机若内容方向不对（上下颠倒），把 `ui.c` 的 `LV_DISPLAY_ROTATION_90` 改为
  `LV_DISPLAY_ROTATION_270`（GE 旋转标志与软件回退映射都已支持 270）。
- 扫描缓冲单份（fb 放不下第 4 块），GE 旋转+缩放约数 ms < 17 ms 帧周期，UI 刷新瞬间可能有
  轻微撕裂，属接受的折中。正常路径扫描缓冲仅由 GE 写入；软件回退路径 CPU 写完会
  `aicos_dcache_clean_range` 主动清脏行，不存在脏行回写覆盖 GE 结果的问题。
- 移除 `UDISP_FRAME_COUNT`/USB JPEG 接收以进一步省 CMA 属可选优化；当前中部窗口
  （取 `udisp_screen.framebuffer` 复用）已置于 UI 图层内存预算内。