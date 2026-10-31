# AIC-BarDisplay USB 副屏固件：SPI NOR → SPI NAND 移植计划

> 目标：把 B 仓库 `AIC-BarDisplay`（当前仅 SPI NOR 存储、SFUD/spinor 驱动、NOR bootloader、32 MiB 分区布局）的 USB 副屏变体 `d133-mipipi-ek79030`，移植到板上的 **128 MiB = 1 Gbit SPI NAND**（Winbond W25N01GV 类，料号 `25N01GVZE1G`，挂在 SoC SPI0），做到 **从 NAND 启动 + 镜像存 NAND**。
> 参考实现：A 仓库 `E:\001\mipi-pi-d133-master`（Luban-Lite SDK）的 `target/d13x/mipiPI` 板，该板即 SPI NAND 128m boot+storage，且正好使用 `AIC_PANEL_DSI_EK79030_ZT0686` 屏（与目标变体同屏）。
> 本文仅作研究/计划，**不修改任何文件**。

---

## 0. 结论速览（TL;DR）

1. **工具链已基本就绪**：B 的三个打包脚本 `gen_partition_table.py`、`gen_partition_file_list.py`、`makefatfs.py`、`mk_image.py` **已经**原生支持 `spi-nand` 媒体类型（含 page/block/OOB、SPL page-table、NFTL 预留块），无需移植 A 的烧录工具。真正要改的是 **build.py 的硬编码路径/校验** + **bootloader 加 NAND 驱动** + **RT-Thread 应用存储栈换 NAND 驱动**。
2. **NAND 上的 `rodata` 分区（只读 FAT）不需要 NFTL/FTL**：A 的 mipiPI 将 `rodata` 作为原始 MTD 分区（`"attr": ["mtd"]`），ECC 由 spinand 驱动在硬件/驱动里做，FAT 直接读原始页。只有可写的 `data` 分区才需要 **NFTL**（A 用 ArtInChip 专有 NFTL 库 `libnftl_lib_d13x.a`）。
3. B 的应用同时用两条路读 rodata 资源：**FAL 直接读**（`usbdisp/transport.c` 的 MSC 磁盘）和 **FAT 挂载 `/rodata` 读**（`usbdisp/video.c` 的空闲视频）。移植后两条路都要保住。
4. 最大难点：B 的 RT-Thread 存储是 **FAL + SFUD(NOR)** 架构；A 用的是 **spinand → MTD NAND → `blk_*` 块设备**。两者不是同一套，需要把 A 的 spinand/NFTL 栈（或等价实现）搬进 B，并让 FAL 和 FAT 都指向它。
5. B 的 bootloader 主循环已经预写 `BD_SPINAND → "nand_boot"` 逻辑（`bootloader/main.c` L183-185），且 graph 已编译 `boot_res_nand.c`，基础设施已埋好，只缺 NAND flash 驱动和 `nand_boot` 命令、`nand_fwc` 升级后端。

---

## 1. 现状盘点（读到的具体文件与硬编码点）

### 1.1 B 仓库（源）关键事实

| 位置 | 内容 | 行 |
|---|---|---|
| `application/baremetal/bootloader/main.c` | `bd==BD_SPINAND` 时 `console_set_bootcmd("nand_boot")`，不依赖任何宏 | L183-185 |
| `application/baremetal/bootloader/cmd/nor_boot.c` | `do_nor_boot()`：`mtd_probe()`→`mtd_get_device("os")`→`info.dev_type=DEVICE_SPINOR`→`spl_load_simple_fit`→`boot_app` | L34-84 |
| `application/baremetal/bootloader/cmd/` | 有 `aicupg.*`、`nor_boot`、`part`、`ram_boot` 等，**无 `nand_boot.c`** | glob |
| `application/baremetal/bootloader/lib/aicupg/` | `nor_fwc.c`、`upg_fat_direct_nor.c` 等，**无 `nand_fwc.c`** | glob |
| `firmware/configs/d13x_display-nor_baremetal_bootloader.h` | `AIC_QSPI0_DEVICE_SPINOR`、`AIC_BOOTLOADER_SPINOR_SUPPORT`、`AIC_BOOTLOADER_CMD_NOR_BOOT`、`AIC_BOOTLOADER_CMD_SPI_NOR`、`AICUPG_NOR_ARTINCHIP`，无任何 NAND 宏 `CONFIG_SPI_NAND_*` / `AIC_NFTL_*` | L146-148, 295, 310, 317-318 |
| `firmware/graphs/d13x_display-nor_baremetal_bootloader.json` | sources 含 `cmd/nor_boot.c`、`lib/aicupg/nor_fwc.c`、`drv_bare/spinor/*`、`peripheral/spinor_sfud/*`；**不含** NAND 相关源。已含 `bsp/artinchip/sys/d13x/boot_res_nand.c` | L562, 628, L850 |
| `firmware/configs/d13x_display-mipipi-ek79030_rt-thread_usbdisp.h` | `AIC_QSPI0_DEVICE_SPINOR`、`AIC_SPINOR_DRV`、`FAL_USING_SFUD_PORT`、`FAL_USING_NOR_FLASH_DEV_NAME "norflash0"`、`RT_USING_MTD_NOR`；无 MTD NAND / NFTL / SPI_NAND | L172, 428, 693 |
| `firmware/graphs/d13x_display-mipipi-ek79030_rt-thread_usbdisp.json` | sources 含 `drv/spinor/spinor_fal.c`、`spinor_sfud.c`、`peripheral/spinor_sfud/*`；无 spinand 源；includes 指向 `bsp/peripheral/spinor_sfud/inc` | L522-540, L49 |
| `application/rt-thread/usbdisp/transport.c` | `fal_partition_find("rodata")` + `fal_partition_read` 直接把 rodata 分区当 U 盘/MSC 磁盘读 | L32, 359, 373 |
| `application/rt-thread/usbdisp/video.c` | 空闲视频走 FAT：`fopen("/rodata/video/video.bin")` | L33-38 |
| `application/rt-thread/usbdisp/main.c` | 等 `/rodata` 挂载（`dfs_filesystem_lookup("/rodata")`） | L71-76 |
| `application/rt-thread/usbdisp/settings.c` | 写 `/data/udisp.cfg`（FAT 写，要求 `data` 可写） | L17, 35-40 |
| `target/d13x/display-nor/board.c` | `mount_table[]`：`"blk_rodata"→/rodata→elm(RO)`、`"data"→/data→lfs`（后者受 `LPKG_USING_LITTLEFS` 保护，本变体未开） | L239-250 |
| `target/d13x/display-nor/pack/image_cfg.json` | 媒体 `"spi-nor"` + `"size":"32m"`；分区 spl 512k / env 128k / env_r 128k / os 3m / rodata 27m / data 1m；`image.target.data.file="data.lfs"` | 全文件 |
| `build.py` | `build_elf()` 写死 `target/<chip>/display-nor/pack/image_cfg.json`；`build.py` 每 chip 只编一个 bootloader graph `chip + "_display-nor_baremetal_bootloader"`；`pack()` 写死 `target/<chip>/display-nor/pack`；`boot_variant` 选 `'txw935003b' in graph`；`verify_image()` 写死 `cfg['spi-nor']` 且 `cfg['size']!='32m'` 即报错、总计 ≤32 MiB | L88, 159, 167, 218-222, 285-286 |
| `tools/scripts/mk_image.py` | 已支持 `spi-nand`：`img_gen_page_table`、`_calc_block_info`(`spi-nand` 用 page/block)、`is_spl_nand` 预留 page-table、burner 分支只在 `-b` 时生成 | L2010-2131, 2327-2501 |
| `tools/scripts/gen_partition_table.py` | 已按 `spi-nand` 生成 `IMAGE_CFG_JSON_PARTS_MTD` 和 `IMAGE_CFG_JSON_PARTS_NFTL`；**但 `FAL_PART_TABLE` 只在 `spi-nor` 时生成**（NAND 不生成 FAL 表，因为 NAND 不走 FAL 分区表宏） | L590-641, 326 |
| `tools/scripts/gen_partition_file_list.py` | 已有 `aic_create_nand_part_file_string`（写 page/block/oob + 分区文件清单） | L226-277 |
| `tools/scripts/makefatfs.py` | `check_is_nftl_part()` + `NFTL_RESERVED = 51*64*2048`，`--fullpart` 对 nftl 分区自动减预留；`--reserved` 参数 | L79-95, 284-316 |

### 1.2 A 仓库（参考实现）关键事实

| 位置 | 内容 | 行 |
|---|---|---|
| `target/configs/d13x_mipiPI_baremetal_bootloader_defconfig` | `AIC_QSPI0_DEVICE_SPINAND`、`AICUPG_NAND_ARTINCHIP`、`AIC_BOOTLOADER_CMD_NAND_BOOT`、`AIC_BOOTLOADER_CMD_SPI_NAND`、`CONFIG_SPI_NAND_DOSILICON/MICRON/.../ZETTA`、`AIC_NFTL_SUPPORT`、`AIC_NFLT_USED_LIB`；`AIC_BOOTLOADER_SPINOR_SUPPORT` **not set**（NAND 板不打 NOR 驱动） | 全文件 |
| `application/baremetal/bootloader/cmd/nand_boot.c` | `do_nand_boot()`：与 nor_boot 同构，但 `info.bl_len=mtd->writesize`、`info.dev_type=DEVICE_SPINAND` | L32-81 |
| `application/baremetal/bootloader/lib/aicupg/nand_fwc.c` | NAND 升级后端：`spinand_probe`、bad-block 跳过/标记、`mtd_write_oob`、`AIC_NFTL_SUPPORT` 下 `nftl_api_write` 写 NFTL | 全文件（709 行） |
| `lib/aicupg/nand_fwc_spl.c`, `nand_fwc_spl_v20.c`, `upg_fat_direct_nand.c` | SPL 与 FAT 直写 NAND 的补充后端 | glob |
| `bsp/artinchip/drv_bare/spinand/spinand_mtd.c` + `spinand_port.c` | **bootloader 用** bare spinand → MTD 驱动；`qspi_controller[]` 在 `AIC_QSPI0_DEVICE_SPINAND` 下挂 SPI0 | glob / L35 |
| `bsp/peripheral/spinand/` | `spinand.c`、`winbond.c`、`spinand_bbt.c`、`spinand_ooblayout.c`，以及 dosilicon/etron/micron/macronix/gigadevice/toshiba/foresee/xtx/zetta 等厂商文件 | glob |
| `bsp/artinchip/drv/spinand/` | **RT-Thread 用** `spinand_port.c`(注册 `spinand0` MTD)、`spinand_block.c`(在 MTD 上建 `blk_%s` 块设备，`PART_ATTR_NFTL` 走 NFTL，否则 raw MTD 只读)、`spinand_parts.c`(解析 mtdparts/nftlvols)、`spinand_block.h` | glob |
| `bsp/peripheral/nftl/` | `libnftl_lib_d13x.a`(预编译库)、`nftl_api/*/nftl_api.h`、`nftl_port/boot|rtos/src/nftl_port_hw.c` | glob |
| `kernel/rt-thread/components/drivers/mtd/mtd_nand.c` | RT-Thread MTD NAND 设备实现（B 缺此文件，只有 `mtd_nor.c`） | glob(B 只有 mtd_nor) |
| `target/configs/d13x_mipiPI_rt-thread_devkit_defconfig` | `AIC_QSPI0_DEVICE_SPINAND`、`CONFIG_SPI_NAND_*`、`AIC_NFTL_SUPPORT`、`AIC_NFLT_USED_LIB`、`RT_DFS_ELM_MAX_SECTOR_SIZE=4096` | 全文件 |
| `target/d13x/mipiPI/pack/image_cfg.json` | 媒体 `"spi-nand"` + `"size":"128m"`，`array_organization:[{page:2k,block:128k,oob:64}]`；分区 spl 1m / env 256k / env_r 256k / os 4m / os_r 4m / rodata 14m / rodata_r 14m / data 40m(nftl.data) / data_r 40m；`image.target.rodata.attr=["mtd"]`，`image.target.data.attr=["block"]`(NFTL) | 全文件 |
| `bsp/artinchip/drv/spinand/spinand_port.c` | `rt_hw_mtd_spinand_register("spinand0", ...)`；分区来自 `private_get_partition_string` / `mtd_parts_parse`，`nftlvols` 里命中的分区 `PART_ATTR_NFTL`，其余 `PART_ATTR_MTD`；每个分区 `rt_mtd_nand_register_device` + `rt_blk_nand_register_device` | L440-578 |
| `bsp/artinchip/drv/spinand/spinand_block.c` | `rt_blk_nand_register_device` 生成 `blk_%s` 块设备；`PART_ATTR_NFTL` 时 `rt_spinand_init_nftl` 建 NFTL、读写走 `nftl_api_read/write`；非 NFTL 只读(`rt_spinand_write_nonftl` 直接报错)，`blk_rodata` 靠 ECC 读原始页 | L25-102, 399-405, 553-617 |

---

## 2. 目标分区设计（对照 A mipiPI，适配 B）

> 与 A 的 mipiPI 保持一致最稳妥（同屏、同 SoC），rodata/data 大小可微调以装下变体资源，但必须保持 **128 MiB 总量** 与 **2k 页 / 128k 块 / 64 字节 OOB** 的器件几何。

建议新建 `target/d13x/display-nand/pack/image_cfg.json`（媒体名 `spi-nand`，128m）：

```
"spi-nand": {
  "size": "128m",
  "partitions": {
    "spl":    { "size": "1m" },
    "env":    { "size": "256k" },
    "env_r":  { "size": "256k" },
    "os":     { "size": "4m" },
    "rodata": { "size": "14m" },
    "data":   { "size": "40m", "nftl": { "data": { "size": "-" } } }
  }
}
"image": { "info": { "media": {
  "type": "spi-nand", "device_id": 0, "planes": 1,
  "array_organization": [ { "page": "2k", "block": "128k", "oob": "64" } ]
} } }
"image": { "target": {
  "data": { "file": "data.fatfs", "attr": ["block","optional"], "part": ["data"] }
} }
```

说明：
- **rodata 保持原始 MTD 分区**（`attr:mtd`），A 已证明 raw-NAND + 驱动内 ECC 可被只读 FAT 挂载，**不需要 NFTL/FTL**。
- **data 必须走 NFTL**（`attr:block` + 分区内 `nftl.data` volume），因为 settings 要写 `/data`。
- A 有 os_r / rodata_r / data_r 冗余分区；B 无 A/B 系统接口（`AIC_AB_SYSTEM_INTERFACE` 未开），**可不加冗余**，把省下的空间给 rodata/data。若想保留 env 双副本，env/env_r 照搬即可。

---

## 3. Bootloader NAND 支持（第 1 大块）

### 3.1 需要移植/新增的源码

从 A 拷入（B 目前缺失）：
- `cmd/nand_boot.c`（A `application/baremetal/bootloader/cmd/nand_boot.c`）——与 `nor_boot.c` 同构，仅 `info.bl_len = mtd->writesize`、`info.dev_type = DEVICE_SPINAND`。
- `lib/aicupg/nand_fwc.c`、`nand_fwc_spl.c`（如用 `nand_fwc_spl_v20.c` 的路径则对应 v20）——NAND 升级后端（有 bad-block / ECC / OOB / NFTL 分支）。
- `lib/aicupg/upg_fat_direct_nand.c`（如需 U 盘 FAT 直写 NAND）。
- **bare** NAND 驱动：`bsp/artinchip/drv_bare/spinand/spinand_mtd.c` + `spinand_port.c`（来自 A）。
- **厂商库**：`bsp/peripheral/spinand/`（spinand.c、winbond.c、spinand_bbt.c、spinand_ooblayout.c + 需要的厂商文件）+ 头目录 `bsp/peripheral/spinand/inc/`。
- MTD bare 层的 NAND 相关 ops（`mtd_write_oob`、`mtd_block_isbad/markbad`、`mtd_erase` 按块）——A 的 `drv_bare/mtd/*` 已支持，对照 B 现有 `drv_bare/mtd/mtdcore.c` 补齐。

### 3.2 bootloader config 头改动
在 `firmware/configs/d13x_display-nor_baremetal_bootloader.h`（或为其复制一份 `d13x_display-nand_baremetal_bootloader.h`——**因为 build.py 每 chip 只编一次 bootloader，且同一份 bootloader 要在 NAND 板上用**）：

- `AIC_QSPI0_DEVICE_SPINOR` → `AIC_QSPI0_DEVICE_SPINAND`（并补 `AIC_QSPI0_DEVICE_SPINAND_FREQ`）。
- `AIC_BOOTLOADER_SPINOR_SUPPORT` → 增加 `AIC_BOOTLOADER_SPINAND_SUPPORT`（若有此宏；A defconfig 是去掉 SPINOR 且用 `AIC_BOOTLOADER_CMD_SPI_NAND` + `AIC_BOOTLOADER_CMD_NAND_BOOT`）。
- 命令开关：`AIC_BOOTLOADER_CMD_NOR_BOOT` → `AIC_BOOTLOADER_CMD_NAND_BOOT`；`AIC_BOOTLOADER_CMD_SPI_NOR` → `AIC_BOOTLOADER_CMD_SPI_NAND`；保留 `AIC_BOOTLOADER_CMD_MTD`/`PART`。
- 升级后端：`AICUPG_NOR_ARTINCHIP` → `AICUPG_NAND_ARTINCHIP`。
- 新增厂商宏：`CONFIG_SPI_NAND_WINBOND`（或 A 那套 `CONFIG_SPI_NAND_*`；至少含目标器件 `W25N01GV` 的 Winbond 支持），以及 `CONFIG_SPI_NAND_*` 其余按需。
- 如有 NFTL 需要（bootloader 里写 data 卷），补 `AIC_NFTL_SUPPORT` + `CONFIG_AIC_NFLT_USED_LIB` + `AIC_NFTL_MINI_RESERVED_BLOCK`，并链接 `libnftl_lib_d13x.a`。
- bootloader heap 需足够容纳 spinand 的页/坏块表缓冲（A mipiPI 用了 `AIC_BOOTLOADER_HEAP_SIZE=0x380000`，远大于 B 的 `0xC0000`，需评估）。

### 3.3 bootloader graph 改动
在 `firmware/graphs/d13x_display-nor_baremetal_bootloader.json`，把:
- `cmd/nor_boot.c` → `cmd/nand_boot.c`
- `lib/aicupg/nor_fwc.c` → `lib/aicupg/nand_fwc.c`(+可选 `nand_fwc_spl.c`、`upg_fat_direct_nand.c`)
- `drv_bare/spinor/{partition.c,sfud_port.c}` → `drv_bare/spinand/{spinand_mtd.c,spinand_port.c}`
- add `bsp/peripheral/spinand/*` (spinand.c, winbond.c, spinand_bbt.c, spinand_ooblayout.c, …)
- includes 增加 `bsp/artinchip/drv_bare/spinand/inc`(如存在)、`bsp/peripheral/spinand/inc`；去掉 `bsp/peripheral/spinor_sfud/inc`。
- 若启用 NFTL：libpaths 加 nftl 库目录，libs 加 `nftl_lib_d13x`。

> 注意：`boot_res_nand.c` 已在 graph 中（`d13x_display-nor_baremetal_bootloader.json` L850）。这是 NAND 的 SPL 页表/首块资源加载函数，只有配 NAND 时才被 BROM/SPL 真正引用，说明基础设施已埋好。配 NAND 后启动的 SPL 会用它读首块 AIC 头与私密资源。

### 3.4 main.c 已就绪，无需改
`bootloader/main.c` L183-185 在 `BD_SPINAND` 时已置 `nand_boot` bootcmd；只要编译进 `nand_boot` 命令 + NAND 驱动即可；`of_fdt_dt_init_bare_nornand()`（`nand_boot.c` L75）在两个 boot 里都调用。

### 3.5 分区源
`gen_partition_table.py` 已能按 NAND 布局产出 `IMAGE_CFG_JSON_PARTS_MTD` / `IMAGE_CFG_JSON_PARTS_NFTL`。bootloader 的 `drv_bare/mtd/mtdparts` 与 `fwc_cmd.c`（L977/981）读 `IMAGE_CFG_JSON_PARTS_MTD`，与 NOR 路径同机制，**无需改动**。

---

## 4. RT-Thread 应用存储（第 2 大块）

### 4.1 B 现架构（只读事实）
- `drv/spinor/spinor_fal.c` `fal_with_sfud_flash_init()`（INIT_DEVICE_EXPORT）：SFUD → `norflash0` FAL flash → 分区 → 每个分区 `fal_mtd_nor_device_create` + `fal_blk_device_create`。- `mount_table`（`target/d13x/display-nor/board.c` L239-250）：`blk_rodata→/rodata→elm(RO)`。
- 应用：`transport.c` 用 `fal_partition_read`（MSC 磁盘），`video.c`/`settings.c` 用 FAT `/rodata`、`/data`。

### 4.2 目标 NAND 架构（照搬 A）
A 的 RT-Thread NAND 栈**不经过 FAL**，而是：
1. `drv/spinand/spinand_port.c`（`AIC_QSPI0_DEVICE_SPINAND`）：spinand probe → 建 MTD NAND 分区 → `rt_mtd_nand_register_device(name, &mtd[i])` + `rt_blk_nand_register_device(name, &mtd[i])`（生成 `blk_<part>`）。
2. `drv/spinand/spinand_block.c`：`PART_ATTR_NFTL` 分区初始化 NFTL 并可读可写；`PART_ATTR_MTD`（rodata）只读、按页 ECC 读。
3. `mtd_nand.c`（RT-Thread MTD）承载上层的读页坏块接口。

要在 B 复刻，需要把 A 的这些东西搬进 B 并接到 B 的启动流程：
- `bsp/artinchip/drv/spinand/*`（spinand_port.c, spinand_block.c, spinand_parts.c, spinand_block.h）
- `bsp/artinchip/drv_bare/spinand/spinand_mtd.c`（RT-Thread 版也需对应页 ops）
- `kernel/rt-thread/components/drivers/mtd/mtd_nand.c`（B 目前只有 `mtd_nor.c`）
- `bsp/peripheral/spinand/*`（厂商/chip/bbt/ooblayout）
- `bsp/peripheral/nftl/*`（仅 `data` 需要；`libnftl_lib_d13x.a` + `nftl_api/*` + `nftl_port/rtos/*`）

### 4.3 字节/配置改动
在 `firmware/configs/d13x_display-mipipi-ek79030_rt-thread_usbdisp.h`：
- 存储设备：`AIC_QSPI0_DEVICE_SPINOR` → `AIC_QSPI0_DEVICE_SPINAND`（+FREQ）。
- 驱动：`AIC_SPINOR_DRV` → 补 `AIC_SPINAND_DRV`；`FAL_USING_SFUD_PORT` / `FAL_USING_NOR_FLASH_DEV_NAME` 按 NAND 停用或改指向 NAND 设备。
- MTD：保留 `RT_USING_MTD_NOR`（若走 `mtd_nor`），增加 `RT_USING_MTD_NAND`。
- 厂商：`CONFIG_SPI_NAND_WINBOND`（及 A 那套 `CONFIG_SPI_NAND_*`）。
- NFTL：`AIC_NFTL_SUPPORT`、`CONFIG_AIC_NFLT_USED_LIB`、`AIC_NFTL_MINI_RESERVED_BLOCK`（bootloader 或 app 任一侧需在首烧时建卷）。
- FAT：`RT_DFS_ELM_MAX_SECTOR_SIZE 4096`（NAND 页 2k/4k；A devkit 已设 4096）。现有 rodata.fatfs 用 512 扇区/8 簇生成（makefatfs.py `--sector 512 --cluster 8`），**可直接被 NAND `blk_rodata` 的 512B 逻辑扇区读取**（`spinand_block.c` geometry.bytes_per_sector=512），无需改 FAT 生成。

在 graph `d13x_display-mipipi-ek79030_rt-thread_usbdisp.json`：
- sources/includes 换入 `drv/spinand/*`、`peripheral/spinand/*`（含 inc 目录）、`drv_bare/spinand/*`（如 RT 版复用）、`mtd/mtd_nand.c`；移除或停用 `drv/spinor/*`、`peripheral/spinor_sfud/*`。
- 增加 NFTL 库路径 + `-lnftl_lib_d13x`。

### 4.4 FAL / 挂载 / 应用的衔接（最容易翻车处）
指标：B 的 `transport.c` 用 `fal_partition_find("rodata")` + `fal_partition_read`；`board.c` 挂 `blk_rodata→/rodata`；`video.c` 走 `/rodata/...`。
- 方案（推荐，最小改法，对齐 A）：让 spinand 栈直接建 `blk_rodata` / `blk_data` 块设备，`mount_table` 沿用 `blk_rodata→/rodata→elm(RO)`；**`data` 由 FAL 的 blk 设备改为 spinand 的 `blk_data`（NFTL 可写）挂 `/data`→elm**。
- 对 `transport.c` 的 FAL 读：需要**同时**提供一个能覆盖 `rodata` 分区的 FAL flash/partition 设备，支持 `fal_partition_read`。两条实现路线：
  1. 移植/新增 `fal_nand_device_create`（在 MTD NAND 上建只读 FAL flash 设备），把 `rodata` 注册成 FAL 分区；MSC 读就继续走 FAL。
  2. 或改 `transport.c`，把 `fal_partition_read` 换成直接调用 NAND blob 设备的 `rt_device_read`（读 `blk_rodata`）。A 的 devkit 没有此接口（devkit 不提供 MSC 磁盘），所以这条要 B 自研。
- `rodata.fatfs` 的字节仍作为目标组件写进 rodata 分区（见第 5 章），FAL/MSC 读到的就是 FAT 镜像的原始字节——与 NOR 语义一致。

---

## 5. Pack / Build 流水线（第 3 大块）

### 5.1 已就绪的部分（无需大改）
- **`mk_image.py` 完全支持 NAND**：`build_firmware_image`→`_calc_block_info`（`spi-nand` 分支，L2343-2349）、SPL page-table（`img_gen_page_table` L2010）、`is_spl_nand` 预留 page-table（L2448-2470）、`img_write_fw_header` 写 `array_organization`（L1883-1892）。同一工具能打 NAND 镜像。
- **`gen_partition_table.py` / `gen_partition_file_list.py` / `makefatfs.py`** 均已有 NAND 分支（见 §1.1 表）。
- 目标变体名为 `d13x-usbdisp-mipipi-ek79030`（config L14），`AIC_IMAGE_PRODUCT` 会写进镜像，无需动。

### 5.2 `build.py` 必改点（阻断项）
1. **pack 目录不再写死 `display-nor`**（L159 `pack_dir`）：要支持 NAND，需按媒体类型/目标板选择 `target/<chip>/display-nand/pack`（或新建一个 `display-nand` 板目录 + `image_cfg.json`）。`build_elf` 的 L88「image_cfg 路径」同理从 `display-nor` 改到 `display-nand`。
2. **`verify_image()` 的 NOR 硬检查**（L218-222）：
   ```python
   cfg = json.loads(...)['spi-nor']
   if cfg['size'] != '32m' or total > 32*1048576: raise ...
   ```
   要改成按 `cfg['image']['info']['media']['type']` 取媒体段，NAND 时校验 `size=='128m'`、各分区大小符合 NAND 几何（页/块对齐）且总和 ≤128 MiB。
3. **`pack()` 需要生成 `data.fatfs`**（当前只生成 `rodata.fatfs`，L179-181）。NAND 的 `data` 是 NFTL 卷、`image_cfg.json` 里 `image.target.data.file="data.fatfs"`。用 `makefatfs.py --fullpart ... --reserved <n>`（`check_is_nftl_part` 会自动减 51 块预留）生成。
4. **bootloader graph 选择**（L285-286）：`boot_variant` 目前按 `'txw935003b' in graph` 找 bootloader，且 bootloader graph 名是 `chip + "_display-nor_baremetal_bootloader"`。NAND 板要用 NAND bootloader graph，调用处 L286 需按目标板/媒体选择（例如新建 `d13x_display-nand_baremetal_bootloader` graph，L286 变量化）。
5. **`pack()` 镜像产物**：B 调用 `mk_image.py` **不带 `-b`（burner）**，故只产 `.img`（组件封包，含 update 所需 page-table 用的 SPL 首块信息）。烧录 NAND 时：
   - 通过 AICUPG USB 升级：bootloader 的 `nand_fwc` 会把目标组件按 MTD/NFTL 写进 NAND，**不需要** burner `.bin` 的整盘 `spi-nand` 几何镜像；但 SPL 必须落在候选块并生成 **page-table**（`img_gen_page_table`）。建议在 `pack()` 里为 NAND 额外产出一份 burner `.bin`（`mk_image.py -b`），作为**烧录器/量产**用整盘镜像，与升级用的 `.img` 并存。
6. `bootloader.bin` 拷贝、`elf2its`、`mk_private_resource`（pbp 私密资源里含分区串）路径都与 pack 目录相关，随 5.2.1 一起改到 display-nand。

### 5.3 分区几何语义（NAND vs NOR）
- NOR 按"字节线性"排布；NAND 按 **page(2k) / block(128k) / oob(64)**。`gen_partition_table.py` / `mk_image.py` 已按 `array_organization` 换算 page/block，`gen_partition_file_list.py` 也写 `nands=2048,131072,64;` 片段。
- `verify_image` 和打包层要对分区大小做 **128k 块对齐**检查（分区起止需落在块边界，避免跨坏块/烧录歧义）。
- SPL 分区（1m）里首个 2k 块放 AIC 页表 + 镜像首块，候选块表见 `boot_res_nand.c` `spl_candidate_block_table`（L60-63）。

---

## 6. 冲突 / 风险（第 4 大块）

### 6.1 不能干净移植的点
1. **B 的 FAL-NOR 存储架构 ≠ A 的 MTD-NAND/`blk_*` 架构**：B 的 `spinor_fal.c` 用 SFUD+FAL 建 `norflash0`/`blk_rodata`；A 的 NAND 用 spinand 驱动直接建 `blk_rodata`/`blk_data`，**不经过 FAL**。`transport.c` 的 `fal_partition_read` 是唯一向 FAL 的要-停-点（§4.4）；要么在 NAND 上补 FAL 只读设备，要么改 `transport.c`。这是全工程最硬的一块。
2. **`data` 分区必须 NFTL/FTL**：`settings.c` 写 `/data/udisp.cfg` 需随机写+擦除+坏块管理，raw MTD 不可行；必须用 A 的 `libnftl_lib_d13x.a`（闭源预编译库）+ `nftl_*` 驱动。B 目前完全没这套。**rodata 只读则不需要 FTL**（ECC 由驱动搞定）。
3. **bootloader 每 chip 只编一份**：build.py 把 bootloader 按 chip 缓存（`bootloaders[chip]`）。若 NAND 与 NOR 板混产，需两套 bootloader graph/目录并存，build.py 的 L285-286 选择逻辑要按媒体/板分支；否则全 chip 会编同一份（要么只 NAND、要么只 NOR）。
4. **makerfas 的扇区/簇**：rodata.fatfs 用它生成时以 512 扇区/8 簇，`spinand_block` 的逻辑扇区固定 512B（L583），可直读；**但 `RT_DFS_ELM_MAX_SECTOR_SIZE` 需 ≥ 页对齐并设 4096**（A devkit 已如此），否则 elm 层只按 512 访问会在 NAND 上层行为不一致。
5. **厂商/闭源库**：NFTL 为预编译 `.a`，需 `libnftl_lib_d13x.a`（A 的 `bsp/peripheral/nftl/`），B 需从 A 拷贝并在 linkflags/libs 引入；架构/工具链需与 D13x 匹配（`riscv64-unknown-elf`）。

### 6.2 顶层不确定 / 高风险项
1. **FAL 与 NAND 的分区表来源**：B 的 `fal_aic_partition.c` 从 flash 私有资源读分区串或回退 `IMAGE_CFG_JSON_PARTS_MTD`（L88-90）。NAND 的私有资源/镜像头格式（AIC 头 + private_data，读法见 `boot_res_nand.c`）与 NOR 不同（页表/首块），需要确认 `fal_aic_partition` 能在 NAND 上正确取到分区串，否则 `rodata` FAL 分区缺失 → MSC 副盘失效。
2. **烧录路径**：`.img` 走 AICUPG 由 bootloader 烧；整盘量产需 burner `.bin`（含 page-table/候选块），B 现有流程不产 burner；产不产出、以及首块 SPL 页表落地，决定能否量产。这是硬件/产线侧必须验证的死角。
3. **坏块与冗余**：NAND 有坏块，首块(SOP loader)失败会跳过；`boot_res_nand.c` 已有候选块表与备份逻辑。**必须**保留 A mipiPI 的 SPL 候选块机制，B 当前无生产 NAND 验证记录，属新增风险。
4. **heap/内存**：spinor→spinand 需要 `pagebuf`（`spinand_block.c` malloc `page_size` + `block_num_cache`）与 NFTL 运行时缓冲。B 的 PSRAM 8 MiB 预算紧绷（AGENTS.md 铁律），NFTL 预留块与运行时 RAM 需在 8 MiB 内核算。
5. **镜像大小与几何**：NAND 分区必须按 128k 块对齐；误把 32 MiB NOR 布局直接当 NAND 布局会导致覆盖/越界。`verify_image` 的 32 MiB 硬检查是显式陷阱（§5.2.2）。
6. **`AIC_QSPI0_DELAY_MODE / QSPI0_BUS_WIDTH`**：NAND 的数据线/时序参数（dummy cycles、模式切换）与 NOR 不同，需按 A mipiPI 的 spinor/QSPI 配置核对 pinmux 与 `spinand_port.c` 的 `AIC_QSPI0_DEVICE_SPINAND_FREQ`，否则上不了 4 线/高频。

---

## 7. 分阶段执行清单

| 阶段 | 动作 | 关键文件 |
|---|---|---|
| P1 打包 | 新建 `target/d13x/display-nand/pack/image_cfg.json`（spi-nand 128m，§2 布局）；改 `build.py`：pack 目录/媒体选择、`verify_image` 128m NAND 校验、bootloader graph 选择、`data.fatfs` 生成 | `build.py`、新 image_cfg.json |
| P2 bootloader | 拷入 A 的 `cmd/nand_boot.c`、`lib/aicupg/nand_fwc*.c`、`drv_bare/spinand/*`、`bsp/peripheral/spinand/*`；改 bootloader config 宏（§3.2）+ graph（§3.3） | `firmware/configs/*bootloader.h`、`firmware/graphs/*bootloader.json` |
| P3 RT-Thread 驱动 | 拷入 A 的 `drv/spinand/*`、`peripheral/spinand/*`、`mtd/mtd_nand.c`、`peripheral/nftl/*`；改 variant config 宏（§4.3）+ graph（§4.4）；核对 `RT_DFS_ELM_MAX_SECTOR_SIZE=4096` | `firmware/configs/d13x_display-mipipi-ek79030*.h`、对应 graph |
| P4 应用衔接 | 处理 `transport.c` 的 `fal_partition_read("rodata")`；`board.c` `mount_table` 指向 NAND `blk_rodata`/`blk_data` | `usbdisp/transport.c`、`target/*/board.c` |
| P5 产线/验证 | 生成 burner `.bin`（`-b`）；量产烧录验证 SPL 页表/候选块；`dispdbg` 上机确认屏/资源 | `pack()` 的 mk_image 调用、量产工具 |

> 验证闭环：`build.cmd d133-mipipi-ek79030` → 产物放进 `images/`；上机后串口看 boot log（应走 `nand_boot`），再 `dispdbg` 校屏、`ls /rodata` 看资源。