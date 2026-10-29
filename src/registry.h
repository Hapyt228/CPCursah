#ifndef REGISTRY_H
#define REGISTRY_H

#include <stddef.h>
#include <time.h>
#include "usb_device.h"

/* Запись о ранее виденном устройстве. */
typedef struct {
    char key[UD_STR];      /* серийный номер или NOSERIAL-VID:PID */
    char vid[8];
    char pid[8];
    char vendor[UD_STR];
    char model[UD_STR];
    time_t first_seen;
    time_t last_seen;
    unsigned count;        /* сколько раз подключалось */
} RegistryEntry;

/* Класс Registry: база известных устройств, хранится в файле. */
typedef struct {
    char path[256];
    RegistryEntry *items;
    size_t size;
    size_t cap;
} Registry;

int  registry_open(Registry *self, const char *path);
/* Фиксирует подключение. Возвращает 1, если устройство новое; prev_count - число прошлых подключений. */
int  registry_touch(Registry *self, const UsbDevice *dev, unsigned *prev_count);
int  registry_save(const Registry *self);
void registry_close(Registry *self);

#endif
