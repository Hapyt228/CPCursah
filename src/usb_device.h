#ifndef USB_DEVICE_H
#define USB_DEVICE_H

#include <stddef.h>
#include <stdint.h>
#include <time.h>

#define UD_STR 128
#define UD_PATH 256

/* Класс UsbDevice: данные об одном USB-накопителе. */
typedef struct {
    char devnode[UD_STR];       /* /dev/sdb */
    char syspath[UD_PATH];      /* путь в sysfs, уникален для подключения */
    char vendor[UD_STR];
    char model[UD_STR];
    char serial[UD_STR];        /* серийный номер (ключ идентификации) */
    char vid[8];                /* идентификатор производителя, hex */
    char pid[8];                /* идентификатор продукта, hex */
    char fs_type[32];
    char label[UD_STR];
    char uuid[64];
    char mountpoint[UD_PATH];
    uint64_t size_bytes;
    time_t connected_at;
} UsbDevice;

void usb_device_init(UsbDevice *self);
int  usb_device_has_serial(const UsbDevice *self);
/* Ключ идентификации: серийный номер, а если его нет - VID:PID. */
void usb_device_key(const UsbDevice *self, char *out, size_t n);
void usb_device_format_size(const UsbDevice *self, char *out, size_t n);

#endif
