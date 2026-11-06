/*
 * uires_res.c - uires 分区大图的一次性 RAM 驻留加载（见 uires_res.h 注释）。
 *
 * 堆选择（关键）：rt_malloc 走系统堆 heap_sys，本变体只有 1 MiB，2.4 MiB 的
 * 全局背景永远放不下，而 LVGL lv_malloc 失败在开启 RT_USING_MEMTRACE 时还会
 * 把调用线程 mdelay(9999999) 冻死。因此像素数据不走 lv_draw_buf_create，而是
 * 直接向板级 memheap 申请：
 *   小图（<= UIRES_SRAM_MAX）优先 SRAM 堆 heap_sram0_sw（~500 KiB 近乎空闲，
 *   CPU 读取还比 PSRAM 快），其余走 heap_cma（PSRAM CMA 池）。
 * heap_cma 分配打上 LV_IMAGE_FLAGS_USER2，与 lv_draw_buf_destroy 在
 * LV_MEM_HEAP_ADAPTIVE_SUPPORT 下的 aicos_free(MEM_CMA) 分支对称；这些缓冲
 * 常驻到关机，正常路径不会被 LVGL 释放。
 */
#include <stdio.h>
#include <rtthread.h>

#include <aic_core.h>

#include "lvgl.h"
#include "uires_res.h"

/* 允许放进取 SRAM 堆的最大字节数（刻度盘 172,800B 走这里，为大图腾出 CMA）*/
#define UIRES_SRAM_MAX (256 * 1024)

/* 与 lv_draw_buf.c 的 _calculate_draw_buf_size 保持一致的布局计算 */
static size_t res_data_size(uint32_t stride, uint32_t h, lv_color_format_t cf)
{
    size_t sz = (size_t)stride * h;
    if(cf == LV_COLOR_FORMAT_RGB565A8) {
        sz += (stride / 2) * h; /* 尾部 A8 掩码区，与 gen_ebike_resbin.py 布局一致 */
    }
    return sz;
}

lv_draw_buf_t * uires_res_load(const char * lv_path)
{
    /* LVGL POSIX 盘 "L:" 的根是 DFS 根（LV_FS_POSIX_PATH ""），剥掉盘符即可
     * 直接用 stdio（DFS elm 文件系统）读取。 */
    const char * posix_path = (lv_path[0] != '\0' && lv_path[1] == ':')
                                  ? lv_path + 2 : lv_path;
    lv_image_header_t hdr;

    FILE * f = fopen(posix_path, "rb");
    if(!f) {
        printf("uires: fopen fail %s\n", posix_path);
        return NULL;
    }

    if(fread(&hdr, 1, sizeof(hdr), f) != sizeof(hdr) ||
       hdr.magic != LV_IMAGE_HEADER_MAGIC || hdr.w == 0 || hdr.h == 0) {
        printf("uires: bad header %s\n", posix_path);
        fclose(f);
        return NULL;
    }

    /* stride 用文件里的值（gen_ebike_resbin.py 写的 w*bpp，8 对齐） */
    lv_color_format_t cf = (lv_color_format_t)hdr.cf;
    size_t size = res_data_size(hdr.stride, hdr.h, cf);

    /* 关键：lv_draw_buf_init 会把 db->data 向上对齐到 LV_DRAW_BUF_ALIGN
     * （本板 = CACHE_LINE_SIZE 32），最多前移 31B。LVGL 自己的 buf_malloc
     * 也是这样多分配 ALIGN-1 的。若只分配 size 字节，fread 写到 db->data 会
     * 越过块尾，打烂 heap_cma/heap_sram0_sw 下一个 rt_memheap_item 的 next
     * 指针——下一次分配在 rt_memheap_alloc 链表写入时 Store-misaligned
     * 异常（实测崩在 memheap.c:264）。 */
    size_t alloc_size = size + (LV_DRAW_BUF_ALIGN - 1);

    /* 像素数据：小图先试 SRAM 堆，失败/大图走 CMA 堆 */
    void * data = NULL;
    int heap_type = -1;
#ifdef AIC_SRAM0_SW_EN
    if(size <= UIRES_SRAM_MAX) {
        data = aic_memheap_malloc(MEM_SRAM0_SW, alloc_size);
        if(data) heap_type = MEM_SRAM0_SW;
    }
#endif
    if(!data) {
        data = aic_memheap_malloc(MEM_CMA, alloc_size);
        if(data) heap_type = MEM_CMA;
    }
    if(!data) {
        printf("uires: no RAM for %s (%ux%u cf%u, %u B)\n", posix_path,
               (unsigned)hdr.w, (unsigned)hdr.h, (unsigned)hdr.cf,
               (unsigned)size);
        fclose(f);
        return NULL;
    }

    lv_draw_buf_t * db = rt_malloc(sizeof(*db));
    if(!db) {
        aic_memheap_free(heap_type, data);
        printf("uires: no RAM for %s (struct)\n", posix_path);
        fclose(f);
        return NULL;
    }

    if(lv_draw_buf_init(db, hdr.w, hdr.h, cf, hdr.stride, data,
                        (uint32_t)size) != LV_RESULT_OK) {
        aic_memheap_free(heap_type, data);
        rt_free(db);
        printf("uires: bad header %s (init)\n", posix_path);
        fclose(f);
        return NULL;
    }

    /* ALLOCATED：bin_decoder 对 VARIABLE 源直接把它当 lv_draw_buf_t 用，
     * 每帧零解码零 IO。USER2 标记 CMA 来源，destroy 时对称释放。 */
    db->header.flags = LV_IMAGE_FLAGS_MODIFIABLE | LV_IMAGE_FLAGS_ALLOCATED;
    if(heap_type == MEM_CMA) {
        db->header.flags |= LV_IMAGE_FLAGS_USER2;
    }

    size_t got = fread(db->data, 1, size, f);
    fclose(f);
    if(got != size) {
        printf("uires: short read %s (%u/%u)\n", posix_path,
               (unsigned)got, (unsigned)size);
        aic_memheap_free(heap_type, data);
        rt_free(db);
        return NULL;
    }

    printf("uires: cached %s %ux%u cf%u (%u B, %s)\n", posix_path,
           (unsigned)hdr.w, (unsigned)hdr.h, (unsigned)hdr.cf,
           (unsigned)size, heap_type == MEM_CMA ? "cma" : "sram");
    return db;
}
