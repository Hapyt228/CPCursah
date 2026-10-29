#include "logger.h"

#include <stdarg.h>
#include <string.h>
#include <time.h>

int logger_open(Logger *self, const char *path) {
    snprintf(self->path, sizeof(self->path), "%s", path);
    self->fp = fopen(path, "a");
    return self->fp ? 0 : -1;
}

void logger_write(Logger *self, const char *level, const char *fmt, ...) {
    if (!self->fp) return;

    char ts[32];
    time_t now = time(NULL);
    struct tm tmv;
    localtime_r(&now, &tmv);
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tmv);

    fprintf(self->fp, "%s [%s] ", ts, level);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(self->fp, fmt, ap);
    va_end(ap);
    fputc('\n', self->fp);
    fflush(self->fp);
}

void logger_close(Logger *self) {
    if (self->fp) fclose(self->fp);
    self->fp = NULL;
}
