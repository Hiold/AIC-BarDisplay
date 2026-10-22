/*
 * Copyright (c) 2026, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * USB-CDC ACM virtual COM port console bridge.
 *
 * The board has no UART pins broken out, so the RT-Thread console (and the
 * finsh shell that drives `dispdbg`) is redirected onto a USB CDC-ACM virtual
 * serial port. CherryUSB's CDC class only handles the control requests
 * (line coding / DTR); it carries no console plumbing, so this file:
 *
 *   - registers a character rt_device named "vcom"
 *   - binds it as the system console via rt_console_set_device()
 *   - implements lock-protected rx/tx ring buffers
 *   - wires the CDC bulk endpoints to those rings
 *
 * Timing: the "vcom" device is registered at INIT_DEVICE_EXPORT (earlier than
 * finsh's INIT_APP_EXPORT), so the shell thread binds to it automatically.
 * The USB controller itself is brought up at INIT_APP_EXPORT; console output
 * produced before enumeration is buffered in the tx ring and flushed once the
 * host finishes configuring the device.
 */

#include <rtthread.h>
#include <rthw.h>
#include <string.h>
#include "usbd_core.h"
#include "usbd_cdc_acm.h"
#include "aic_core.h"

#define VCOM_DEV_NAME "vcom"

/* CDC endpoint addresses (interrupt IN, bulk OUT = rx, bulk IN = tx). */
#define CDC_INT_EP 0x81
#define CDC_OUT_EP 0x02
#define CDC_IN_EP  0x82

#define USBD_VID 0x303A
#define USBD_PID 0x2988

#ifdef CONFIG_USB_HS
#define CDC_MAX_MPS 512
#else
#define CDC_MAX_MPS 64
#endif

#define USB_CONFIG_SIZE (9 + CDC_ACM_DESCRIPTOR_LEN)

#define RX_RING_SIZE 1024
#define TX_RING_SIZE 4096
#define EP_BUF_SIZE  CDC_MAX_MPS

/* ------------------------------------------------------------------ */
/* Descriptors                                                          */
/* ------------------------------------------------------------------ */
static const uint8_t cdc_descriptor[] = {
    USB_DEVICE_DESCRIPTOR_INIT(USB_2_0, 0xEF, 0x02, 0x01,
                               USBD_VID, USBD_PID, 0x0100, 0x01),
    USB_CONFIG_DESCRIPTOR_INIT(USB_CONFIG_SIZE, 0x02, 0x01,
                               USB_CONFIG_BUS_POWERED, 100),
    CDC_ACM_DESCRIPTOR_INIT(0x00, CDC_INT_EP, CDC_OUT_EP, CDC_IN_EP,
                            CDC_MAX_MPS, 0x02),
    /* string0: LANGID */
    USB_LANGID_INIT(1033),
    /* string1: manufacturer "ArtInChip" */
    0x14, USB_DESCRIPTOR_TYPE_STRING,
    'A', 0, 'r', 0, 't', 0, 'I', 0, 'n', 0,
    'C', 0, 'h', 0, 'i', 0, 'p', 0,
    /* string2: product "D133 CDC Console" */
    0x22, USB_DESCRIPTOR_TYPE_STRING,
    'D', 0, '1', 0, '3', 0, '3', 0, ' ', 0, 'C', 0, 'D', 0, 'C', 0,
    ' ', 0, 'C', 0, 'o', 0, 'n', 0, 's', 0, 'o', 0, 'l', 0, 'e', 0,
    /* string3: serial "0001" */
    0x0A, USB_DESCRIPTOR_TYPE_STRING,
    '0', 0, '0', 0, '0', 0, '1', 0,
#ifdef CONFIG_USB_HS
    /* device qualifier */
    0x0A, USB_DESCRIPTOR_TYPE_DEVICE_QUALIFIER,
    0x00, 0x02, 0x02, 0x02, 0x01, 0x40, 0x01, 0x00,
#endif
    0x00
};

/* ------------------------------------------------------------------ */
/* State                                                                */
/* ------------------------------------------------------------------ */
static USB_MEM_ALIGNX uint8_t rx_dma[EP_BUF_SIZE];
static USB_MEM_ALIGNX uint8_t tx_dma[EP_BUF_SIZE];

static uint8_t rx_ring[RX_RING_SIZE];
static volatile uint32_t rx_head, rx_tail;

static uint8_t tx_ring[TX_RING_SIZE];
static volatile uint32_t tx_head, tx_tail;

static volatile bool tx_busy;
static volatile bool usb_configured;

static struct rt_device vcom_dev;
static struct usbd_interface intf0;
static struct usbd_interface intf1;

/* ------------------------------------------------------------------ */
/* TX path: write() -> tx ring -> endpoint chain                       */
/* ------------------------------------------------------------------ */
static void vcom_tx_start(void)
{
    rt_base_t level;
    uint32_t n = 0;

    level = rt_hw_interrupt_disable();
    if (!tx_busy && usb_configured && tx_head != tx_tail) {
        while (n < EP_BUF_SIZE && tx_tail != tx_head) {
            tx_dma[n++] = tx_ring[tx_tail];
            tx_tail = (tx_tail + 1) % TX_RING_SIZE;
        }
        tx_busy = true;
        rt_hw_interrupt_enable(level);
        usbd_ep_start_write(0, CDC_IN_EP, tx_dma, n);
        return;
    }
    rt_hw_interrupt_enable(level);
}

static void cdc_in_cb(uint8_t busid, uint8_t ep, uint32_t nbytes)
{
    (void)busid; (void)ep; (void)nbytes;
    tx_busy = false;
    /* Continue draining the tx ring (also handles the case where write()
     * filled the ring while the previous transfer was in flight). */
    vcom_tx_start();
}

static rt_size_t vcom_write(rt_device_t dev, rt_off_t pos,
                            const void *buffer, rt_size_t size)
{
    const uint8_t *p = (const uint8_t *)buffer;
    rt_base_t level;
    rt_size_t i;

    (void)dev; (void)pos;
    level = rt_hw_interrupt_disable();
    for (i = 0; i < size; i++) {
        uint32_t next = (tx_head + 1) % TX_RING_SIZE;
        if (next == tx_tail)
            break;              /* ring full: drop the rest */
        tx_ring[tx_head] = p[i];
        tx_head = next;
    }
    rt_hw_interrupt_enable(level);

    vcom_tx_start();
    /* Report the full request so callers (rt_kprintf) never block/retry. */
    return size;
}

/* ------------------------------------------------------------------ */
/* RX path: endpoint -> rx ring -> rx_indicate wakes finsh            */
/* ------------------------------------------------------------------ */
static void cdc_out_cb(uint8_t busid, uint8_t ep, uint32_t nbytes)
{
    rt_base_t level;
    uint32_t i;

    (void)busid; (void)ep;
    if (nbytes) {
        level = rt_hw_interrupt_disable();
        for (i = 0; i < nbytes; i++) {
            uint32_t next = (rx_head + 1) % RX_RING_SIZE;
            if (next == rx_tail)
                break;          /* overflow: drop newest */
            rx_ring[rx_head] = rx_dma[i];
            rx_head = next;
        }
        rt_hw_interrupt_enable(level);

        if (vcom_dev.rx_indicate)
            vcom_dev.rx_indicate(&vcom_dev, nbytes);
    }
    usbd_ep_start_read(0, CDC_OUT_EP, rx_dma, sizeof(rx_dma));
}

static rt_size_t vcom_read(rt_device_t dev, rt_off_t pos,
                           void *buffer, rt_size_t size)
{
    uint8_t *p = (uint8_t *)buffer;
    rt_base_t level;
    rt_size_t n = 0;

    (void)dev; (void)pos;
    level = rt_hw_interrupt_disable();
    while (n < size && rx_tail != rx_head) {
        p[n++] = rx_ring[rx_tail];
        rx_tail = (rx_tail + 1) % RX_RING_SIZE;
    }
    rt_hw_interrupt_enable(level);
    return n;
}

/* ------------------------------------------------------------------ */
/* rt_device boilerplate                                               */
/* ------------------------------------------------------------------ */
static rt_err_t vcom_init(rt_device_t dev)   { (void)dev; return RT_EOK; }
static rt_err_t vcom_open(rt_device_t dev, rt_uint16_t oflag)
{
    dev->open_flag = oflag;
    return RT_EOK;
}
static rt_err_t vcom_close(rt_device_t dev)  { dev->open_flag = 0; return RT_EOK; }
static rt_err_t vcom_control(rt_device_t dev, int cmd, void *args)
{
    (void)dev; (void)cmd; (void)args;
    return RT_EOK;
}

/* USB event handler */
static void usbd_event_handler(uint8_t busid, uint8_t event)
{
    (void)busid;
    switch (event) {
    case USBD_EVENT_CONFIGURED:
        usb_configured = true;
        usbd_ep_start_read(0, CDC_OUT_EP, rx_dma, sizeof(rx_dma));
        /* Flush anything printed before enumeration completed. */
        vcom_tx_start();
        break;
    case USBD_EVENT_RESET:
    case USBD_EVENT_DISCONNECTED:
        usb_configured = false;
        tx_busy = false;
        break;
    default:
        break;
    }
}

/* Register the vcom device and bind it as console. Runs before finsh. */
static int cdc_console_register(void)
{
    rt_memset(&vcom_dev, 0, sizeof(vcom_dev));
    vcom_dev.type    = RT_Device_Class_Char;
    vcom_dev.flag    = RT_DEVICE_FLAG_STREAM | RT_DEVICE_FLAG_RDWR |
                       RT_DEVICE_FLAG_INT_RX;
    vcom_dev.init    = vcom_init;
    vcom_dev.open    = vcom_open;
    vcom_dev.close   = vcom_close;
    vcom_dev.read    = vcom_read;
    vcom_dev.write   = vcom_write;
    vcom_dev.control = vcom_control;

    rt_device_register(&vcom_dev, VCOM_DEV_NAME,
                       RT_DEVICE_FLAG_STREAM | RT_DEVICE_FLAG_RDWR |
                       RT_DEVICE_FLAG_INT_RX);

    rt_console_set_device(VCOM_DEV_NAME);
    return 0;
}
INIT_DEVICE_EXPORT(cdc_console_register);

/* Bring up the USB controller + CDC interfaces. Runs at app-init level. */
static int cdc_console_usb_init(void)
{
    usbd_desc_register(0, cdc_descriptor);
    usbd_add_interface(0, usbd_cdc_acm_init_intf(0, &intf0));
    usbd_add_interface(0, usbd_cdc_acm_init_intf(0, &intf1));

    static struct usbd_endpoint out_ep = { .ep_addr = CDC_OUT_EP, .ep_cb = cdc_out_cb };
    static struct usbd_endpoint in_ep  = { .ep_addr = CDC_IN_EP,  .ep_cb = cdc_in_cb };
    usbd_add_endpoint(0, &out_ep);
    usbd_add_endpoint(0, &in_ep);

    usbd_initialize(0, USB_DEV_BASE, usbd_event_handler);
    return 0;
}
INIT_APP_EXPORT(cdc_console_usb_init);
