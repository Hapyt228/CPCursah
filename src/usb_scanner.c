#include "usb_scanner.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void copy_prop(struct udev_device *d, const char *name, char *dst, size_t n) {
    const char *v = udev_device_get_property_value(d, name);
    if (v) snprintf(dst, n, "%s", v);
}

static void underscores_to_spaces(char *s) {
    for (; *s; ++s) {
        if (*s == '_') *s = ' ';
    }
}

static void sleep_ms(int ms) {
    struct timespec ts = {ms / 1000, (long)(ms % 1000) * 1000000L};
    nanosleep(&ts, NULL);
}

/* Ищет в /proc/mounts точку монтирования для узла устройства. */
static int find_mountpoint(const char *devnode, char *out, size_t n) {
    FILE *f = fopen("/proc/mounts", "r");
    if (!f) return 0;
    char line[1024];
    int found = 0;
    while (fgets(line, sizeof(line), f)) {
        char dev[256], mnt[512];
        if (sscanf(line, "%255s %511s", dev, mnt) != 2) continue;
        if (strcmp(dev, devnode) != 0) continue;
        /* в /proc/mounts пробел записан как \040 */
        char *w = out;
        for (const char *r = mnt; *r && (size_t)(w - out) < n - 1; ++r) {
            if (r[0] == '\\' && r[1] == '0' && r[2] == '4' && r[3] == '0') {
                *w++ = ' ';
                r += 3;
            } else {
                *w++ = *r;
            }
        }
        *w = '\0';
        found = 1;
        break;
    }
    fclose(f);
    return found;
}

int usb_scanner_init(UsbScanner *self) {
    self->udev = udev_new();
    return self->udev ? 0 : -1;
}

void usb_scanner_destroy(UsbScanner *self) {
    if (self->udev) udev_unref(self->udev);
    self->udev = NULL;
}

int usb_scanner_is_usb_disk(const struct udev_device *dev) {
    struct udev_device *d = (struct udev_device *)dev;
    const char *type = udev_device_get_devtype(d);
    const char *bus = udev_device_get_property_value(d, "ID_BUS");
    return type && strcmp(type, "disk") == 0 && bus && strcmp(bus, "usb") == 0;
}

void usb_scanner_read_disk(UsbScanner *self, struct udev_device *disk, UsbDevice *out) {
    (void)self;
    usb_device_init(out);
    const char *node = udev_device_get_devnode(disk);
    const char *sys = udev_device_get_syspath(disk);
    if (node) snprintf(out->devnode, sizeof(out->devnode), "%s", node);
    if (sys) snprintf(out->syspath, sizeof(out->syspath), "%s", sys);

    copy_prop(disk, "ID_VENDOR", out->vendor, sizeof(out->vendor));
    if (!out->vendor[0]) copy_prop(disk, "ID_VENDOR_FROM_DATABASE", out->vendor, sizeof(out->vendor));
    copy_prop(disk, "ID_MODEL", out->model, sizeof(out->model));
    copy_prop(disk, "ID_SERIAL_SHORT", out->serial, sizeof(out->serial));
    copy_prop(disk, "ID_VENDOR_ID", out->vid, sizeof(out->vid));
    copy_prop(disk, "ID_MODEL_ID", out->pid, sizeof(out->pid));
    underscores_to_spaces(out->vendor);
    underscores_to_spaces(out->model);

    const char *sectors = udev_device_get_sysattr_value(disk, "size");
    if (sectors) out->size_bytes = strtoull(sectors, NULL, 10) * 512ULL;
    out->connected_at = time(NULL);
}

/* Первый раздел диска или сам диск, если у него есть ФС. */
static struct udev_device *find_fs_device(UsbScanner *self, struct udev_device *disk) {
    if (udev_device_get_property_value(disk, "ID_FS_TYPE")) {
        return udev_device_ref(disk);
    }
    struct udev_enumerate *en = udev_enumerate_new(self->udev);
    udev_enumerate_add_match_parent(en, disk);
    udev_enumerate_add_match_subsystem(en, "block");
    udev_enumerate_scan_devices(en);

    struct udev_device *res = NULL;
    struct udev_list_entry *e;
    udev_list_entry_foreach(e, udev_enumerate_get_list_entry(en)) {
        struct udev_device *c = udev_device_new_from_syspath(self->udev, udev_list_entry_get_name(e));
        if (!c) continue;
        const char *t = udev_device_get_devtype(c);
        if (t && strcmp(t, "partition") == 0 && udev_device_get_property_value(c, "ID_FS_TYPE")) {
            res = c;
            break;
        }
        udev_device_unref(c);
    }
    udev_enumerate_unref(en);
    return res;
}

void usb_scanner_read_fs(UsbScanner *self, struct udev_device *disk, UsbDevice *out, int wait_ms) {
    struct udev_device *fsdev = NULL;
    for (int waited = 0;; waited += 200) {
        fsdev = find_fs_device(self, disk);
        if (fsdev || waited >= wait_ms) break;
        sleep_ms(200);
    }
    if (!fsdev) return;

    copy_prop(fsdev, "ID_FS_TYPE", out->fs_type, sizeof(out->fs_type));
    copy_prop(fsdev, "ID_FS_LABEL", out->label, sizeof(out->label));
    copy_prop(fsdev, "ID_FS_UUID", out->uuid, sizeof(out->uuid));

    const char *node = udev_device_get_devnode(fsdev);
    if (node) {
        /* автомонтирование происходит с задержкой - ждём немного */
        for (int waited = 0; waited <= wait_ms; waited += 200) {
            if (find_mountpoint(node, out->mountpoint, sizeof(out->mountpoint))) break;
            sleep_ms(200);
        }
    }
    udev_device_unref(fsdev);
}
