#ifndef LOGGER_H
#define LOGGER_H

#include <stdio.h>

/* Класс Logger: журнал событий в текстовом log-файле. */
typedef struct {
    FILE *fp;
    char path[256];
} Logger;

int  logger_open(Logger *self, const char *path);
void logger_write(Logger *self, const char *level, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));
void logger_close(Logger *self);

#endif
