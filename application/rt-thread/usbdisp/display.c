/* SPDX-License-Identifier: Apache-2.0 */
/* Minimal pattern viewer: decode a single full-screen JPEG and hold it on
 * the VIDEO layer so the panel orientation can be checked. No USB, no UI. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "usbdisp.h"
#include "packet_allocator.h"

struct aicfb_screeninfo udisp_screen;
struct mpp_fb *udisp_fb;
static struct mpp_decoder *decoder;
static const uint8_t *input;
static uint32_t input_size;

static int packet_alloc(struct packet_allocator *a, struct mpp_packet *p)
{
    (void)a;
    p->data = (void *)input;
    p->size = input_size;
    p->len = input_size;
    p->flag = PACKET_FLAG_EOS;
    return 0;
}

static int packet_free(struct packet_allocator *a, struct mpp_packet *p)
{
    (void)a; (void)p;
    return 0;
}

static void decoder_close(void)
{
    if (!decoder) return;
    mpp_decoder_destory(decoder);
    decoder = NULL;
}

static struct pkt_alloc_ops ops = {.alloc=packet_alloc,.free=packet_free};
static struct packet_allocator allocator = {.ops=&ops};

static void udisp_disable_ui(void)
{
    struct aicfb_layer_data layer = {0};
    layer.layer_id = AICFB_LAYER_TYPE_UI;
    layer.rect_id = 0;
    mpp_fb_ioctl(udisp_fb, AICFB_UPDATE_LAYER_CONFIG, &layer);
}

int udisp_fb_init(void)
{
    udisp_fb = mpp_fb_open();
    if (!udisp_fb)
        return -1;
    if (mpp_fb_ioctl(udisp_fb, AICFB_GET_SCREENINFO, &udisp_screen) < 0) {
        mpp_fb_close(udisp_fb);
        udisp_fb = NULL;
        return -1;
    }
    mpp_fb_ioctl(udisp_fb, AICFB_POWERON, NULL);
    /* Drop the aicfb boot checkerboard. The default pattern lives in the UI
     * layer's framebuffer and sits above VIDEO in z-order; without it the
     * decoded test frame is the only thing scanned out. */
    udisp_disable_ui();
    printf("usbdisp: panel %ux%u\n", udisp_screen.width, udisp_screen.height);
    return 0;
}

static int decoder_open(void)
{
    struct decode_config config = {0};
    if (decoder) return 0;
    decoder = mpp_decoder_create(MPP_CODEC_VIDEO_DECODER_MJPEG);
    if (!decoder) return -1;
    config.pix_fmt = MPP_FMT_NV12;
    config.bitstream_buffer_size = UDISP_FRAME_LIMIT;
    config.packet_count = 1;
    config.extra_frame_num = 1;
    mpp_decoder_control(decoder, MPP_DEC_INIT_CMD_SET_EXT_PACKET_ALLOCATOR, &allocator);
    if (mpp_decoder_init(decoder, &config)) {
        mpp_decoder_destory(decoder); decoder = NULL;
        return -1;
    }
    return 0;
}

int udisp_show_jpeg(const char *path)
{
    struct mpp_packet packet = {0};
    struct mpp_frame frame = {0};
    struct aicfb_layer_data layer = {0};
    uint8_t *data;
    unsigned width, height;
    uint32_t size;
    FILE *f;

    f = fopen(path, "rb");
    if (!f) { printf("usbdisp: cannot open %s\n", path); return -1; }
    fseek(f, 0, SEEK_END);
    size = (uint32_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    if (!size || size > UDISP_FRAME_LIMIT) {
        printf("usbdisp: bad size %u for %s\n", size, path);
        fclose(f);
        return -1;
    }
    data = aicos_malloc_align(MEM_CMA, size, CACHE_LINE_SIZE);
    if (!data) { fclose(f); return -1; }
    if (fread(data, 1, size, f) != size) {
        fclose(f); aicos_free_align(MEM_CMA, data); return -1;
    }
    fclose(f);

    input = data; input_size = size;
    aicos_dcache_clean_range(data, ALIGN_UP(size, CACHE_LINE_SIZE));
    if (decoder_open() ||
        mpp_decoder_get_packet(decoder, &packet, size) ||
        mpp_decoder_put_packet(decoder, &packet) ||
        mpp_decoder_decode(decoder) ||
        mpp_decoder_get_frame(decoder, &frame)) {
        printf("usbdisp: decode failed for %s\n", path);
        decoder_close();
        aicos_free_align(MEM_CMA, data);
        return -1;
    }
    aicos_free_align(MEM_CMA, data);

    layer.layer_id = AICFB_LAYER_TYPE_VIDEO;
    layer.enable = 1;
    layer.buf = frame.buf;
    width = frame.buf.crop_en ? frame.buf.crop.width : frame.buf.size.width;
    height = frame.buf.crop_en ? frame.buf.crop.height : frame.buf.size.height;
    /* Center and crop an over-sized source to fill the panel exactly. */
    if (width > udisp_screen.width || height > udisp_screen.height) {
        layer.buf.crop.x += width > udisp_screen.width ?
                            ((width - udisp_screen.width) / 2) & ~1U : 0;
        layer.buf.crop.y += height > udisp_screen.height ?
                            ((height - udisp_screen.height) / 2) & ~1U : 0;
        width = width > udisp_screen.width ? udisp_screen.width : width;
        height = height > udisp_screen.height ? udisp_screen.height : height;
        layer.buf.crop_en = 1;
        layer.buf.crop.width = width;
        layer.buf.crop.height = height;
    }
    layer.pos.x = (udisp_screen.width - width) / 2;
    layer.pos.y = (udisp_screen.height - height) / 2;
    layer.scale_size.width = width;
    layer.scale_size.height = height;
    if (mpp_fb_ioctl(udisp_fb, AICFB_UPDATE_LAYER_CONFIG, &layer) < 0) {
        printf("usbdisp: layer update failed\n");
        /* Drop the decoder and its NV12 surfaces: the frame was never
         * handed to scanout, so nothing must keep it alive. */
        decoder_close();
        return -1;
    }
    /* Keep the decoded frame owned by the scanout; a return would release it. */
    printf("usbdisp: showing %ux%u pattern from %s\n", width, height, path);
    return 0;
}