#include "usb_device.h"

#include <stdio.h>
#include <string.h>

void usb_device_init(UsbDevice *self) {
    memset(self, 0, sizeof(*self));
}

int usb_device_has_serial(const UsbDevice *self) {
    return self->serial[0] != '\0';
}

void usb_device_key(const UsbDevice *self, char *out, size_t n) {
    if (usb_device_has_serial(self)) {
        snprintf(out, n, "%s", self->serial);
    } else {
        snprintf(out, n, "NOSERIAL-%s:%s", self->vid, self->pid);
    }
}

void usb_device_format_size(const UsbDevice *self, char *out, size_t n) {
    static const char *units[] = {"Б", "КБ", "МБ", "ГБ", "ТБ"};
    double v = (double)self->size_bytes;
    int i = 0;
    while (v >= 1024.0 && i < 4) {
        v /= 1024.0;
        i++;
    }
    if (i == 0) {
        snprintf(out, n, "%.0f %s", v, units[i]);
    } else {
        snprintf(out, n, "%.2f %s", v, units[i]);
    }
}
