#include <stdio.h>
#include <string.h>

#include "app.h"

static void usage(const char *prog) {
    printf("Использование: %s [--history] [--data-dir <каталог>]\n"
           "  (без параметров)   мониторинг подключений USB-накопителей\n"
           "  --history          показать реестр известных устройств и выйти\n"
           "  --data-dir <dir>   каталог для usb_audit.log и usb_registry.db (по умолчанию ./data)\n",
           prog);
}

int main(int argc, char **argv) {
    const char *data_dir = "data";
    int history = 0;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--history") == 0) {
            history = 1;
        } else if (strcmp(argv[i], "--data-dir") == 0 && i + 1 < argc) {
            data_dir = argv[++i];
        } else {
            usage(argv[0]);
            return 1;
        }
    }

    App app;
    if (app_init(&app, data_dir) != 0) return 1;

    if (history) {
        app_print_history(&app);
    } else {
        app_scan_existing(&app);
        app_run(&app);
    }

    app_destroy(&app);
    return 0;
}
