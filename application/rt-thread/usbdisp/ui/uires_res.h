/*
 * uires_res.h - uires 分区大图的一次性 RAM 驻留加载。
 *
 * LV_BIN_DECODER_RAM_LOAD=0 是逐行流式解码：动画帧的脏区每帧都要把其下的
 * 背景图从 SPI NAND 重读一遍（img_global_bg 每行 5760B，一帧可到 1.8MB），
 * 帧率跌到个位数，且单缓冲下 DE 边扫边写产生数字残影。大图必须在开机时
 * 整体读进 RAM，之后每帧只是普通的 RAM 位块混合。
 */
#ifndef UIRES_RES_H
#define UIRES_RES_H

#include "lvgl.h"

/*
 * 把 uires 的 LVGL .bin（12B lv_image_header_t + 裸像素，RGB888/RGB565A8 等）
 * 一次性读入内存，返回常驻的 lv_draw_buf_t*（ALLOCATED 标志），可直接作为
 * lv_image_set_src() 的源——走解码器 VARIABLE 直通路径，每帧零解码零文件 IO。
 * 文件缺失 / 头非法 / 内存不足时返回 NULL，调用方应回退为流式文件加载。
 * lv_path 使用 LVGL 路径串（形如 "L:/uires/ebike/xxx.bin"，"L:" 盘符自动剥离
 * 成 DFS POSIX 路径）。返回值生命周期与 UI 相同，不做释放。
 */
lv_draw_buf_t * uires_res_load(const char * lv_path);

#endif /* UIRES_RES_H */
