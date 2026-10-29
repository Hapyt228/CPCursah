#ifndef USB_SCANNER_H
#define USB_SCANNER_H

#include <libudev.h>
#include "usb_device.h"

/* Класс UsbScanner: чтение сведений об USB-накопителе через libudev. */
typedef struct {
    struct udev *udev;
} UsbScanner;

int  usb_scanner_init(UsbScanner *self);
void usb_scanner_destroy(UsbScanner *self);
/* Является ли udev-устройство целым USB-диском (а не разделом). */
int  usb_scanner_is_usb_disk(const struct udev_device *dev);
/* Заполняет базовые поля из диска (без ФС). */
void usb_scanner_read_disk(UsbScanner *self, struct udev_device *disk, UsbDevice *out);
/* Дочитывает сведения о ФС и точке монтирования; ждёт появления раздела до wait_ms. */
void usb_scanner_read_fs(UsbScanner *self, struct udev_device *disk, UsbDevice *out, int wait_ms);

#endif
