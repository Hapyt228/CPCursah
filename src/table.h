#ifndef TABLE_H
#define TABLE_H

#include <stddef.h>

#define TABLE_MAX_ROWS 24

/* Класс Table: двухколоночная таблица "Поле | Значение" с поддержкой UTF-8. */
typedef struct {
    const char *title;
    const char *names[TABLE_MAX_ROWS];
    char values[TABLE_MAX_ROWS][320];
    size_t rows;
} Table;

void table_init(Table *self, const char *title);
void table_add(Table *self, const char *name, const char *value);
void table_print(const Table *self);

#endif
