#include "table.h"

#include <stdio.h>
#include <string.h>

/* Длина строки в символах (а не в байтах) - нужна для русских букв. */
static size_t u8len(const char *s) {
    size_t n = 0;
    for (; *s; ++s) {
        if (((unsigned char)*s & 0xC0) != 0x80) n++;
    }
    return n;
}

static void repeat(const char *s, size_t n) {
    for (size_t i = 0; i < n; ++i) fputs(s, stdout);
}

static void hline(size_t w1, size_t w2) {
    fputs("+", stdout);
    repeat("-", w1 + 2);
    fputs("+", stdout);
    repeat("-", w2 + 2);
    fputs("+\n", stdout);
}

static void cell(const char *s, size_t w) {
    fputs("| ", stdout);
    fputs(s, stdout);
    repeat(" ", w - u8len(s));
    fputs(" ", stdout);
}

void table_init(Table *self, const char *title) {
    memset(self, 0, sizeof(*self));
    self->title = title;
}

void table_add(Table *self, const char *name, const char *value) {
    if (self->rows >= TABLE_MAX_ROWS) return;
    self->names[self->rows] = name;
    snprintf(self->values[self->rows], sizeof(self->values[0]), "%s",
             (value && value[0]) ? value : "-");
    self->rows++;
}

void table_print(const Table *self) {
    size_t w1 = u8len("Поле"), w2 = u8len("Значение");
    for (size_t i = 0; i < self->rows; ++i) {
        size_t a = u8len(self->names[i]), b = u8len(self->values[i]);
        if (a > w1) w1 = a;
        if (b > w2) w2 = b;
    }
    size_t total = w1 + w2 + 7;
    size_t tl = u8len(self->title);
    if (tl + 4 > total) w2 += tl + 4 - total, total = tl + 4;

    putchar('\n');
    fputs("+", stdout); repeat("-", total - 2); fputs("+\n", stdout);
    fputs("| ", stdout); fputs(self->title, stdout);
    repeat(" ", total - 4 - tl); fputs(" |\n", stdout);
    hline(w1, w2);
    cell("Поле", w1); cell("Значение", w2); fputs("|\n", stdout);
    hline(w1, w2);
    for (size_t i = 0; i < self->rows; ++i) {
        cell(self->names[i], w1);
        cell(self->values[i], w2);
        fputs("|\n", stdout);
    }
    hline(w1, w2);
    fflush(stdout);
}
