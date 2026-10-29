#include "registry.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void sanitize(char *s) {
    for (; *s; ++s) {
        if (*s == '\t' || *s == '\n' || *s == '\r') *s = ' ';
    }
}

static int push(Registry *self, const RegistryEntry *e) {
    if (self->size == self->cap) {
        size_t ncap = self->cap ? self->cap * 2 : 16;
        RegistryEntry *p = realloc(self->items, ncap * sizeof(*p));
        if (!p) return -1;
        self->items = p;
        self->cap = ncap;
    }
    self->items[self->size++] = *e;
    return 0;
}

int registry_open(Registry *self, const char *path) {
    memset(self, 0, sizeof(*self));
    snprintf(self->path, sizeof(self->path), "%s", path);

    FILE *f = fopen(path, "r");
    if (!f) return 0; /* файла ещё нет - это нормально */

    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (line[0] == '#' || line[0] == '\0') continue;

        RegistryEntry e;
        memset(&e, 0, sizeof(e));
        long long first = 0, last = 0;
        unsigned cnt = 0;
        char *save = NULL;
        char *tok[8];
        int n = 0;
        for (char *t = strtok_r(line, "\t", &save); t && n < 8; t = strtok_r(NULL, "\t", &save)) {
            tok[n++] = t;
        }
        if (n < 8) continue;
        snprintf(e.key, sizeof(e.key), "%s", tok[0]);
        snprintf(e.vid, sizeof(e.vid), "%s", tok[1]);
        snprintf(e.pid, sizeof(e.pid), "%s", tok[2]);
        snprintf(e.vendor, sizeof(e.vendor), "%s", tok[3]);
        snprintf(e.model, sizeof(e.model), "%s", tok[4]);
        first = atoll(tok[5]);
        last = atoll(tok[6]);
        cnt = (unsigned)atoi(tok[7]);
        e.first_seen = (time_t)first;
        e.last_seen = (time_t)last;
        e.count = cnt;
        if (push(self, &e) != 0) break;
    }
    fclose(f);
    return 0;
}

int registry_save(const Registry *self) {
    char tmp[300];
    snprintf(tmp, sizeof(tmp), "%s.tmp", self->path);
    FILE *f = fopen(tmp, "w");
    if (!f) return -1;

    fprintf(f, "# key\tvid\tpid\tvendor\tmodel\tfirst_seen\tlast_seen\tcount\n");
    for (size_t i = 0; i < self->size; ++i) {
        const RegistryEntry *e = &self->items[i];
        fprintf(f, "%s\t%s\t%s\t%s\t%s\t%lld\t%lld\t%u\n", e->key, e->vid, e->pid,
                e->vendor, e->model, (long long)e->first_seen, (long long)e->last_seen, e->count);
    }
    if (fclose(f) != 0) return -1;
    return rename(tmp, self->path);
}

int registry_touch(Registry *self, const UsbDevice *dev, unsigned *prev_count) {
    char key[UD_STR];
    usb_device_key(dev, key, sizeof(key));
    sanitize(key);
    time_t now = time(NULL);

    for (size_t i = 0; i < self->size; ++i) {
        RegistryEntry *e = &self->items[i];
        if (strcmp(e->key, key) == 0) {
            if (prev_count) *prev_count = e->count;
            e->count++;
            e->last_seen = now;
            registry_save(self);
            return 0;
        }
    }

    RegistryEntry e;
    memset(&e, 0, sizeof(e));
    snprintf(e.key, sizeof(e.key), "%s", key);
    snprintf(e.vid, sizeof(e.vid), "%s", dev->vid);
    snprintf(e.pid, sizeof(e.pid), "%s", dev->pid);
    snprintf(e.vendor, sizeof(e.vendor), "%s", dev->vendor);
    snprintf(e.model, sizeof(e.model), "%s", dev->model);
    sanitize(e.vendor);
    sanitize(e.model);
    e.first_seen = e.last_seen = now;
    e.count = 1;
    if (prev_count) *prev_count = 0;
    if (push(self, &e) == 0) registry_save(self);
    return 1;
}

void registry_close(Registry *self) {
    free(self->items);
    self->items = NULL;
    self->size = self->cap = 0;
}
