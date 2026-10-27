/* SPDX-License-Identifier: Apache-2.0 */
#include <string.h>
#include "usbdisp.h"
#include "packet_allocator.h"

struct aicfb_screeninfo udisp_screen;
struct mpp_fb *udisp_fb;
aicos_mutex_t udisp_display_lock;
struct udisp_stats udisp_stats;
volatile bool udisp_stream_hint;
volatile bool udisp_stream_enabled;
volatile bool udisp_stream_requested;
volatile bool udisp_page_hidden;
volatile bool udisp_hid_enabled = true;
volatile bool udisp_idle_pause;
aicos_sem_t udisp_ready;

static struct mpp_decoder *decoder;
static const uint8_t *input;
static uint32_t input_size;
static struct mpp_frame visible;
static bool has_visible;

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

static struct pkt_alloc_ops ops = {.alloc=packet_alloc,.free=packet_free};
static struct packet_allocator allocator = {.ops=&ops};

int udisp_decoder_open(void)
{
    struct decode_config config = {0};
    int rotmir = 0;
    if (decoder) return 0;
    decoder = mpp_decoder_create(MPP_CODEC_VIDEO_DECODER_MJPEG);
    if (!decoder) return -1;
    /* Use the NV12 output path verified on D125. */
    config.pix_fmt = MPP_FMT_NV12;
    config.bitstream_buffer_size = UDISP_FRAME_LIMIT;
    config.packet_count = 1;
    /* Keep a second surface so the video layer never scans from a surface
     * that the decoder is writing. The usbdisp defconfigs reserve enough
     * PSRAM CMA for the framebuffer and both NV12 surfaces. */
    config.extra_frame_num = 1;
    mpp_decoder_control(decoder, MPP_DEC_INIT_CMD_SET_EXT_PACKET_ALLOCATOR, &allocator);
    /* Landscape (横屏) display: the panel scanout is physically portrait but
     * the content is rendered landscape (long axis horizontal) and rotated 90
     * degrees. The host / idle video supply landscape JPEG frames, so the VE
     * JPEG decoder performs the rotation to fill the portrait panel full
     * screen. MPP_DEC_INIT_CMD_SET_ROT_FLIP_FLAG mirrors into
     * decoder->rotmir_flag used by the MJPEG software/hardware decoder. */
#ifdef AIC_USB_DISP_ROTATION_FULLSCREEN
    rotmir = MPP_ROTATION_90;
    mpp_decoder_control(decoder, MPP_DEC_INIT_CMD_SET_ROT_FLIP_FLAG, &rotmir);
#endif
    if (mpp_decoder_init(decoder, &config)) {
        mpp_decoder_destory(decoder); decoder = NULL;
        return -1;
    }
    return 0;
}

void udisp_decoder_close(void)
{
    struct aicfb_layer_data layer = {0};
    if (!decoder) return;
    layer.layer_id = AICFB_LAYER_TYPE_VIDEO;
    aicos_mutex_take(udisp_display_lock, AICOS_WAIT_FOREVER);
    if (mpp_fb_ioctl(udisp_fb, AICFB_UPDATE_LAYER_CONFIG, &layer) < 0) {
        udisp_stats.display_errors++;
        printf("usbdisp: video disable update failed\n");
    }
    aicos_msleep(UDISP_PRESENT_PERIOD_MS);
    aicos_mutex_give(udisp_display_lock);
    if (has_visible) mpp_decoder_put_frame(decoder, &visible);
    has_visible = false;
    mpp_decoder_destory(decoder);
    decoder = NULL;
}

void udisp_idle_video_pause(bool pause)
{
    struct aicfb_layer_data layer = {0};
    udisp_idle_pause = pause;
    if (pause) {
        /* All-zero layer (enable=0) drops the VIDEO layer so it stops
         * covering the static full-buffer pattern. */
        layer.layer_id = AICFB_LAYER_TYPE_VIDEO;
        aicos_mutex_take(udisp_display_lock, AICOS_WAIT_FOREVER);
        mpp_fb_ioctl(udisp_fb, AICFB_UPDATE_LAYER_CONFIG, &layer);
        aicos_mutex_give(udisp_display_lock);
    }
}

bool udisp_decode(const uint8_t *data, uint32_t size, bool native_screen)
{
    struct mpp_packet packet = {0};
    struct mpp_frame frame = {0};
    struct mpp_frame old_visible = {0};
    bool had_visible;
    struct aicfb_layer_data layer = {0};
    if (!size || size > UDISP_FRAME_LIMIT || udisp_decoder_open()) return false;
    input = data; input_size = size;
    aicos_dcache_clean_range((void *)data, ALIGN_UP(size, CACHE_LINE_SIZE));
    uint32_t decode_start = (uint32_t)aic_get_time_ms();
    if (mpp_decoder_get_packet(decoder, &packet, size) ||
        mpp_decoder_put_packet(decoder, &packet) ||
        mpp_decoder_decode(decoder) ||
        mpp_decoder_get_frame(decoder, &frame)) {
        udisp_stats.decode_errors++;
        /* Recreate after malformed JPEG; do not retain a dangling packet. */
        udisp_decoder_close();
        return false;
    }
    uint32_t decode_done = (uint32_t)aic_get_time_ms();
    layer.layer_id = AICFB_LAYER_TYPE_VIDEO;
    layer.enable = 1;
    layer.buf = frame.buf;
    unsigned width = frame.buf.crop_en ? frame.buf.crop.width : frame.buf.size.width;
    unsigned height = frame.buf.crop_en ? frame.buf.crop.height : frame.buf.size.height;
    /* The physical panel scanout is portrait (FIRMWARE_WIDTH x HEIGHT).  In
     * landscape (横屏) mode the VE decoder rotates the landscape JPEG frames
     * to fill it, so the decoded NV12 surface has the portrait dimensions of
     * the panel - not the landscape dimensions reported to the USB host via
     * udisp_screen (which is swapped by the fb rotation). */
    unsigned scanout_w = FIRMWARE_WIDTH;
    unsigned scanout_h = FIRMWARE_HEIGHT;
    if (native_screen && (width != scanout_w || height != scanout_h)) {
        udisp_stats.invalid++;
        mpp_decoder_put_frame(decoder, &frame);
        return false;
    }
    /* Preserve native pixels. The original idle clip is centered, with a
     * centered crop on panels narrower than the clip. */
    if (width > scanout_w || height > scanout_h) {
        layer.buf.crop.x += width > scanout_w ?
                            ((width - scanout_w) / 2) & ~1U : 0;
        layer.buf.crop.y += height > scanout_h ?
                            ((height - scanout_h) / 2) & ~1U : 0;
        width = width > scanout_w ? scanout_w : width;
        height = height > scanout_h ? scanout_h : height;
        layer.buf.crop_en = 1;
        layer.buf.crop.width = width;
        layer.buf.crop.height = height;
    }
    layer.pos.x = (scanout_w - width) / 2;
    layer.pos.y = (scanout_h - height) / 2;
    layer.scale_size.width = width;
    layer.scale_size.height = height;
    aicos_mutex_take(udisp_display_lock, AICOS_WAIT_FOREVER);
    if (mpp_fb_ioctl(udisp_fb, AICFB_UPDATE_LAYER_CONFIG, &layer) < 0) {
        udisp_stats.display_errors++;
        printf("usbdisp: video frame update failed id=%d addr=0x%08x\n",
               frame.id, frame.buf.phy_addr[0]);
        aicos_mutex_give(udisp_display_lock);
        mpp_decoder_put_frame(decoder, &frame);
        return false;
    }
    /* The DE update is latched by the running timing engine. Waiting on the
     * optional IRQ queue here can time out during asynchronous panel enable
     * and can release the previous surface before scanout has latched it. */
    aicos_msleep(UDISP_PRESENT_PERIOD_MS);
    aicos_mutex_give(udisp_display_lock);
    uint32_t present_done = (uint32_t)aic_get_time_ms();
    had_visible = has_visible;
    if (had_visible)
        old_visible = visible;
    visible = frame; has_visible = true;
    udisp_stats.last_frame_id = frame.id;
    udisp_stats.last_frame_addr = frame.buf.phy_addr[0];
    if (had_visible)
        mpp_decoder_put_frame(decoder, &old_visible);
    udisp_stats.shown++;
    udisp_stats.perf_decode_frames++;
    udisp_stats.perf_decode_ms += decode_done - decode_start;
    udisp_stats.perf_present_ms += present_done - decode_done;
    return true;
}
