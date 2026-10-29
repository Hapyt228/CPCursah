#ifndef APP_H
#define APP_H

#include <libudev.h>
#include "logger.h"
#include "registry.h"
#include "usb_scanner.h"

#define APP_MAX_DEVICES 32

/* Класс App: связывает сканер, реестр, журнал и вывод таблицы. */
typedef struct {
    UsbScanner scanner;
    Registry registry;
    Logger logger;
    UsbDevice connected[APP_MAX_DEVICES];
    size_t connected_count;
} App;

int  app_init(App *self, const char *data_dir);
void app_destroy(App *self);
void app_scan_existing(App *self);
int  app_run(App *self);                 /* главный цикл мониторинга */
void app_print_history(const App *self); /* таблица всех известных устройств */

#endif
