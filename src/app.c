#include "app.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <sys/stat.h>

#include "table.h"

static volatile sig_atomic_t g_stop = 0;

static void on_signal(int sig) {
    (void)sig;
    g_stop = 1;
}

static void fmt_time(time_t t, char *out, size_t n) {
    struct tm tmv;
    localtime_r(&t, &tmv);
    strftime(out, n, "%Y-%m-%d %H:%M:%S", &tmv);
}

int app_init(App *self, const char *data_dir) {
    memset(self, 0, sizeof(*self));
    mkdir(data_dir, 0755);

    char path[300];
    snprintf(path, sizeof(path), "%s/usb_audit.log", data_dir);
    if (logger_open(&self->logger, path) != 0) {
        fprintf(stderr, "Не удалось открыть лог-файл %s: %s\n", path, strerror(errno));
        return -1;
    }
    snprintf(path, sizeof(path), "%s/usb_registry.db", data_dir);
    if (registry_open(&self->registry, path) != 0) return -1;
    if (usb_scanner_init(&self->scanner) != 0) {
        fprintf(stderr, "Не удалось инициализировать libudev\n");
        return -1;
    }
    logger_write(&self->logger, "INFO", "START программа запущена, известных устройств: %zu",
                 self->registry.size);
    return 0;
}

void app_destroy(App *self) {
    logger_write(&self->logger, "INFO", "STOP программа завершена");
    usb_scanner_destroy(&self->scanner);
    registry_close(&self->registry);
    logger_close(&self->logger);
}

/* Выводит таблицу со всеми полями устройства. */
static void print_device_table(const UsbDevice *d, int is_new, unsigned prev, const char *title) {
    Table t;
    char buf[64], vidpid[24], tm[32], status[96];

    table_init(&t, title);
    fmt_time(d->connected_at, tm, sizeof(tm));
    snprintf(vidpid, sizeof(vidpid), "%s:%s", d->vid[0] ? d->vid : "?", d->pid[0] ? d->pid : "?");
    usb_device_format_size(d, buf, sizeof(buf));
    if (is_new) {
        snprintf(status, sizeof(status), "НОВОЕ устройство (раньше не подключалось)");
    } else {
        snprintf(status, sizeof(status), "ИЗВЕСТНОЕ устройство (подключений ранее: %u)", prev);
    }

    table_add(&t, "Время подключения", tm);
    table_add(&t, "Узел устройства", d->devnode);
    table_add(&t, "Производитель", d->vendor);
    table_add(&t, "Модель", d->model);
    table_add(&t, "Серийный номер", usb_device_has_serial(d) ? d->serial : "отсутствует");
    table_add(&t, "VID:PID", vidpid);
    table_add(&t, "Объём", buf);
    table_add(&t, "Файловая система", d->fs_type);
    table_add(&t, "Метка тома", d->label);
    table_add(&t, "UUID тома", d->uuid);
    table_add(&t, "Точка монтирования", d->mountpoint);
    table_add(&t, "Статус", status);
    table_print(&t);
}

static void handle_connect(App *self, struct udev_device *disk, int at_startup) {
    UsbDevice dev;
    usb_scanner_read_disk(&self->scanner, disk, &dev);
    usb_scanner_read_fs(&self->scanner, disk, &dev, at_startup ? 0 : 4000);

    unsigned prev = 0;
    int is_new = registry_touch(&self->registry, &dev, &prev);

    if (self->connected_count < APP_MAX_DEVICES) {
        self->connected[self->connected_count++] = dev;
    }

    logger_write(&self->logger, "INFO",
                 "CONNECT%s dev=%s serial=%s vid:pid=%s:%s vendor=\"%s\" model=\"%s\" size=%llu fs=%s label=\"%s\" mount=%s status=%s",
                 at_startup ? "(already)" : "", dev.devnode,
                 usb_device_has_serial(&dev) ? dev.serial : "NONE", dev.vid, dev.pid,
                 dev.vendor, dev.model, (unsigned long long)dev.size_bytes,
                 dev.fs_type[0] ? dev.fs_type : "-", dev.label, dev.mountpoint[0] ? dev.mountpoint : "-",
                 is_new ? "NEW" : "KNOWN");

    print_device_table(&dev, is_new, prev,
                       at_startup ? "USB-накопитель уже подключён" : "USB-накопитель ПОДКЛЮЧЁН");
}

static void handle_disconnect(App *self, struct udev_device *disk) {
    const char *sys = udev_device_get_syspath(disk);
    for (size_t i = 0; i < self->connected_count; ++i) {
        UsbDevice *d = &self->connected[i];
        if (sys && strcmp(d->syspath, sys) == 0) {
            printf("\n[%s] USB-накопитель ОТКЛЮЧЁН: %s %s (серийный номер: %s)\n",
                   "!", d->vendor, d->model, usb_device_has_serial(d) ? d->serial : "отсутствует");
            logger_write(&self->logger, "INFO", "DISCONNECT dev=%s serial=%s vendor=\"%s\" model=\"%s\"",
                         d->devnode, usb_device_has_serial(d) ? d->serial : "NONE", d->vendor, d->model);
            self->connected[i] = self->connected[--self->connected_count];
            return;
        }
    }
}

void app_scan_existing(App *self) {
    struct udev_enumerate *en = udev_enumerate_new(self->scanner.udev);
    udev_enumerate_add_match_subsystem(en, "block");
    udev_enumerate_add_match_property(en, "ID_BUS", "usb");
    udev_enumerate_scan_devices(en);

    struct udev_list_entry *e;
    udev_list_entry_foreach(e, udev_enumerate_get_list_entry(en)) {
        struct udev_device *d = udev_device_new_from_syspath(self->scanner.udev, udev_list_entry_get_name(e));
        if (!d) continue;
        if (usb_scanner_is_usb_disk(d)) handle_connect(self, d, 1);
        udev_device_unref(d);
    }
    udev_enumerate_unref(en);
}

int app_run(App *self) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_signal;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    struct udev_monitor *mon = udev_monitor_new_from_netlink(self->scanner.udev, "udev");
    if (!mon) return -1;
    udev_monitor_filter_add_match_subsystem_devtype(mon, "block", "disk");
    udev_monitor_enable_receiving(mon);
    int fd = udev_monitor_get_fd(mon);

    printf("Мониторинг USB-накопителей запущен. Вставьте флешку (Ctrl+C - выход).\n");
    fflush(stdout);

    while (!g_stop) {
        fd_set set;
        FD_ZERO(&set);
        FD_SET(fd, &set);
        struct timeval tv = {1, 0};
        int r = select(fd + 1, &set, NULL, NULL, &tv);
        if (r < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (r == 0 || !FD_ISSET(fd, &set)) continue;

        struct udev_device *d = udev_monitor_receive_device(mon);
        if (!d) continue;
        const char *action = udev_device_get_action(d);
        if (action && usb_scanner_is_usb_disk(d)) {
            if (strcmp(action, "add") == 0) handle_connect(self, d, 0);
            else if (strcmp(action, "remove") == 0) handle_disconnect(self, d);
        }
        udev_device_unref(d);
    }
    udev_monitor_unref(mon);
    printf("\nЗавершение работы.\n");
    return 0;
}

void app_print_history(const App *self) {
    if (self->registry.size == 0) {
        printf("Реестр пуст: устройства ещё не подключались.\n");
        return;
    }
    printf("Известные устройства (%zu):\n", self->registry.size);
    for (size_t i = 0; i < self->registry.size; ++i) {
        const RegistryEntry *e = &self->registry.items[i];
        char a[32], b[32];
        fmt_time(e->first_seen, a, sizeof(a));
        fmt_time(e->last_seen, b, sizeof(b));
        Table t;
        char title[200], vp[24], cnt[16];
        snprintf(title, sizeof(title), "Устройство %zu: %s", i + 1, e->key);
        snprintf(vp, sizeof(vp), "%s:%s", e->vid, e->pid);
        snprintf(cnt, sizeof(cnt), "%u", e->count);
        table_init(&t, title);
        table_add(&t, "Производитель", e->vendor);
        table_add(&t, "Модель", e->model);
        table_add(&t, "VID:PID", vp);
        table_add(&t, "Первое подключение", a);
        table_add(&t, "Последнее подключение", b);
        table_add(&t, "Всего подключений", cnt);
        table_print(&t);
    }
}
