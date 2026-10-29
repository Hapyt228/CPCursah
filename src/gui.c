#include "gui.h"

#include <glib-unix.h>
#include <gtk/gtk.h>
#include <libudev.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "logger.h"
#include "registry.h"
#include "usb_scanner.h"

#define MAX_DEV 32
#define MAX_TRIES 8

enum { C_TIME, C_NODE, C_VENDOR, C_MODEL, C_SERIAL, C_VIDPID, C_SIZE, C_FS, C_LABEL, C_MOUNT, C_STATUS, C_SYS, C_N };

static const char *COL_TITLES[C_N - 1] = {
    "Время", "Узел", "Производитель", "Модель", "Серийный номер", "VID:PID",
    "Объём", "ФС", "Метка", "Монтирование", "Статус"};

typedef struct {
    UsbDevice dev;
    char status[96];
} Connected;

typedef struct {
    UsbScanner scanner;
    Registry registry;
    Logger logger;
    Connected connected[MAX_DEV];
    size_t connected_count;
    struct udev_monitor *mon;

    GtkWidget *window, *notebook, *dev_view, *status;
    GtkWidget *log_view;
    GtkListStore *dev_store, *detail_store, *reg_store;
    GtkTextBuffer *log_buf;
} Gui;

typedef struct {
    Gui *g;
    struct udev_device *disk;
    UsbDevice dev;
    int tries;
    int startup;
} Pending;

static void fmt_time(time_t t, char *out, size_t n) {
    struct tm tmv;
    localtime_r(&t, &tmv);
    strftime(out, n, "%Y-%m-%d %H:%M:%S", &tmv);
}

/* Записывает событие в log-файл и во вкладку «Журнал». */
static void gui_log(Gui *g, const char *fmt, ...) {
    char msg[1024], ts[32], line[1200];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    logger_write(&g->logger, "INFO", "%s", msg);
    fmt_time(time(NULL), ts, sizeof(ts));
    snprintf(line, sizeof(line), "%s [INFO] %s\n", ts, msg);

    GtkTextIter end;
    gtk_text_buffer_get_end_iter(g->log_buf, &end);
    gtk_text_buffer_insert(g->log_buf, &end, line, -1);
    GtkTextMark *mark = gtk_text_buffer_create_mark(g->log_buf, NULL, &end, FALSE);
    gtk_text_view_scroll_to_mark(GTK_TEXT_VIEW(g->log_view), mark, 0.0, FALSE, 0, 0);
    gtk_text_buffer_delete_mark(g->log_buf, mark);
}

static void set_status(Gui *g, const char *color, const char *text) {
    char *esc = g_markup_escape_text(text, -1);
    char *m = g_strdup_printf("<span foreground=\"%s\" weight=\"bold\" size=\"large\">%s</span>", color, esc);
    gtk_label_set_markup(GTK_LABEL(g->status), m);
    g_free(esc);
    g_free(m);
}

/* ---------- панель «Поле | Значение» ---------- */

static void detail_add(Gui *g, const char *name, const char *value) {
    GtkTreeIter it;
    gtk_list_store_append(g->detail_store, &it);
    gtk_list_store_set(g->detail_store, &it, 0, name, 1, (value && value[0]) ? value : "-", -1);
}

static void show_details(Gui *g, const Connected *c) {
    const UsbDevice *d = &c->dev;
    char tm[32], size[64], vp[24];
    gtk_list_store_clear(g->detail_store);
    fmt_time(d->connected_at, tm, sizeof(tm));
    usb_device_format_size(d, size, sizeof(size));
    snprintf(vp, sizeof(vp), "%s:%s", d->vid[0] ? d->vid : "?", d->pid[0] ? d->pid : "?");

    detail_add(g, "Время подключения", tm);
    detail_add(g, "Узел устройства", d->devnode);
    detail_add(g, "Производитель", d->vendor);
    detail_add(g, "Модель", d->model);
    detail_add(g, "Серийный номер", usb_device_has_serial(d) ? d->serial : "отсутствует");
    detail_add(g, "VID:PID", vp);
    detail_add(g, "Объём", size);
    detail_add(g, "Файловая система", d->fs_type);
    detail_add(g, "Метка тома", d->label);
    detail_add(g, "UUID тома", d->uuid);
    detail_add(g, "Точка монтирования", d->mountpoint);
    detail_add(g, "Статус", c->status);
}

static Connected *find_connected(Gui *g, const char *sys) {
    for (size_t i = 0; i < g->connected_count; ++i) {
        if (strcmp(g->connected[i].dev.syspath, sys) == 0) return &g->connected[i];
    }
    return NULL;
}

static void on_selection_changed(GtkTreeSelection *sel, gpointer data) {
    Gui *g = data;
    GtkTreeModel *model;
    GtkTreeIter it;
    if (!gtk_tree_selection_get_selected(sel, &model, &it)) return;
    char *sys = NULL;
    gtk_tree_model_get(model, &it, C_SYS, &sys, -1);
    Connected *c = sys ? find_connected(g, sys) : NULL;
    if (c) show_details(g, c);
    g_free(sys);
}

/* ---------- таблицы ---------- */

static void refresh_registry(Gui *g) {
    gtk_list_store_clear(g->reg_store);
    for (size_t i = 0; i < g->registry.size; ++i) {
        const RegistryEntry *e = &g->registry.items[i];
        char a[32], b[32], vp[24], cnt[16];
        fmt_time(e->first_seen, a, sizeof(a));
        fmt_time(e->last_seen, b, sizeof(b));
        snprintf(vp, sizeof(vp), "%s:%s", e->vid, e->pid);
        snprintf(cnt, sizeof(cnt), "%u", e->count);
        GtkTreeIter it;
        gtk_list_store_append(g->reg_store, &it);
        gtk_list_store_set(g->reg_store, &it, 0, e->key, 1, e->vendor, 2, e->model, 3, vp, 4, a, 5, b, 6, cnt, -1);
    }
}

static void add_device_row(Gui *g, const Connected *c, GtkTreeIter *out) {
    const UsbDevice *d = &c->dev;
    char tm[32], size[64], vp[24];
    fmt_time(d->connected_at, tm, sizeof(tm));
    usb_device_format_size(d, size, sizeof(size));
    snprintf(vp, sizeof(vp), "%s:%s", d->vid, d->pid);
    gtk_list_store_append(g->dev_store, out);
    gtk_list_store_set(g->dev_store, out,
                       C_TIME, tm, C_NODE, d->devnode, C_VENDOR, d->vendor, C_MODEL, d->model,
                       C_SERIAL, usb_device_has_serial(d) ? d->serial : "-", C_VIDPID, vp, C_SIZE, size,
                       C_FS, d->fs_type, C_LABEL, d->label, C_MOUNT, d->mountpoint,
                       C_STATUS, c->status, C_SYS, d->syspath, -1);
}

/* ---------- события подключения / отключения ---------- */

static void finalize_connect(Gui *g, Pending *p) {
    UsbDevice *d = &p->dev;
    unsigned prev = 0;
    int is_new = registry_touch(&g->registry, d, &prev);
    if (g->connected_count >= MAX_DEV) return;

    Connected *c = &g->connected[g->connected_count++];
    c->dev = *d;
    if (is_new) snprintf(c->status, sizeof(c->status), "НОВОЕ устройство");
    else snprintf(c->status, sizeof(c->status), "ИЗВЕСТНОЕ (ранее подключалось: %u)", prev);

    gui_log(g, "CONNECT%s dev=%s serial=%s vid:pid=%s:%s vendor=\"%s\" model=\"%s\" size=%llu fs=%s label=\"%s\" mount=%s status=%s",
            p->startup ? "(already)" : "", d->devnode, usb_device_has_serial(d) ? d->serial : "NONE",
            d->vid, d->pid, d->vendor, d->model, (unsigned long long)d->size_bytes,
            d->fs_type[0] ? d->fs_type : "-", d->label, d->mountpoint[0] ? d->mountpoint : "-",
            is_new ? "NEW" : "KNOWN");

    GtkTreeIter it;
    add_device_row(g, c, &it);
    refresh_registry(g);
    gtk_notebook_set_current_page(GTK_NOTEBOOK(g->notebook), 0);
    gtk_tree_selection_select_iter(gtk_tree_view_get_selection(GTK_TREE_VIEW(g->dev_view)), &it);
    show_details(g, c);

    char msg[300];
    snprintf(msg, sizeof(msg), "Подключено: %s %s (S/N: %s)", d->vendor, d->model,
             usb_device_has_serial(d) ? d->serial : "нет");
    set_status(g, "#1a7f37", msg);
    if (!p->startup) gtk_window_present(GTK_WINDOW(g->window));
}

static void pending_free(Pending *p) {
    udev_device_unref(p->disk);
    g_free(p);
}

static gboolean pending_tick(gpointer data) {
    Pending *p = data;
    Gui *g = p->g;
    if (access(p->dev.syspath, F_OK) != 0) { /* устройство уже извлечено */
        pending_free(p);
        return G_SOURCE_REMOVE;
    }
    usb_scanner_read_fs(&g->scanner, p->disk, &p->dev, 0);
    p->tries++;
    if ((p->dev.fs_type[0] && p->dev.mountpoint[0]) || p->tries >= MAX_TRIES) {
        finalize_connect(g, p);
        pending_free(p);
        return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}

static void begin_connect(Gui *g, struct udev_device *disk, int startup) {
    Pending *p = g_new0(Pending, 1);
    p->g = g;
    p->disk = udev_device_ref(disk);
    p->startup = startup;
    usb_scanner_read_disk(&g->scanner, disk, &p->dev);
    g_timeout_add(500, pending_tick, p);
}

static void handle_disconnect(Gui *g, struct udev_device *disk) {
    const char *sys = udev_device_get_syspath(disk);
    if (!sys) return;
    Connected *c = find_connected(g, sys);
    if (!c) return;

    char msg[300];
    snprintf(msg, sizeof(msg), "Отключено: %s %s (S/N: %s)", c->dev.vendor, c->dev.model,
             usb_device_has_serial(&c->dev) ? c->dev.serial : "нет");
    gui_log(g, "DISCONNECT dev=%s serial=%s vendor=\"%s\" model=\"%s\"", c->dev.devnode,
            usb_device_has_serial(&c->dev) ? c->dev.serial : "NONE", c->dev.vendor, c->dev.model);
    set_status(g, "#b42318", msg);

    GtkTreeModel *m = GTK_TREE_MODEL(g->dev_store);
    GtkTreeIter it;
    gboolean ok = gtk_tree_model_get_iter_first(m, &it);
    while (ok) {
        char *s = NULL;
        gtk_tree_model_get(m, &it, C_SYS, &s, -1);
        int match = s && strcmp(s, sys) == 0;
        g_free(s);
        if (match) {
            gtk_list_store_remove(g->dev_store, &it);
            break;
        }
        ok = gtk_tree_model_iter_next(m, &it);
    }
    *c = g->connected[--g->connected_count];
    gtk_list_store_clear(g->detail_store);
}

static gboolean on_udev_event(gint fd, GIOCondition cond, gpointer data) {
    (void)fd;
    (void)cond;
    Gui *g = data;
    struct udev_device *d = udev_monitor_receive_device(g->mon);
    if (!d) return G_SOURCE_CONTINUE;
    const char *action = udev_device_get_action(d);
    if (action && usb_scanner_is_usb_disk(d)) {
        if (strcmp(action, "add") == 0) begin_connect(g, d, 0);
        else if (strcmp(action, "remove") == 0) handle_disconnect(g, d);
    }
    udev_device_unref(d);
    return G_SOURCE_CONTINUE;
}

static void scan_existing(Gui *g) {
    struct udev_enumerate *en = udev_enumerate_new(g->scanner.udev);
    udev_enumerate_add_match_subsystem(en, "block");
    udev_enumerate_add_match_property(en, "ID_BUS", "usb");
    udev_enumerate_scan_devices(en);
    struct udev_list_entry *e;
    udev_list_entry_foreach(e, udev_enumerate_get_list_entry(en)) {
        struct udev_device *d = udev_device_new_from_syspath(g->scanner.udev, udev_list_entry_get_name(e));
        if (!d) continue;
        if (usb_scanner_is_usb_disk(d)) begin_connect(g, d, 1);
        udev_device_unref(d);
    }
    udev_enumerate_unref(en);
}

/* ---------- построение окна ---------- */

static GtkWidget *make_view(GtkListStore *store, const char **titles, int n, GtkWidget **scroll_out) {
    GtkWidget *view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
    gtk_tree_view_set_grid_lines(GTK_TREE_VIEW(view), GTK_TREE_VIEW_GRID_LINES_BOTH);
    for (int i = 0; i < n; ++i) {
        GtkCellRenderer *r = gtk_cell_renderer_text_new();
        GtkTreeViewColumn *col = gtk_tree_view_column_new_with_attributes(titles[i], r, "text", i, NULL);
        gtk_tree_view_column_set_resizable(col, TRUE);
        gtk_tree_view_append_column(GTK_TREE_VIEW(view), col);
    }
    GtkWidget *sw = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(sw), view);
    *scroll_out = sw;
    return view;
}

static void load_log_file(Gui *g) {
    FILE *f = fopen(g->logger.path, "r");
    if (!f) return;
    char line[1200];
    while (fgets(line, sizeof(line), f)) {
        GtkTextIter end;
        gtk_text_buffer_get_end_iter(g->log_buf, &end);
        gtk_text_buffer_insert(g->log_buf, &end, line, -1);
    }
    fclose(f);
}

static void build_window(Gui *g) {
    g->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(g->window), "USB Audit — аудит USB-накопителей");
    gtk_window_set_default_size(GTK_WINDOW(g->window), 1150, 680);
    g_signal_connect(g->window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 8);
    gtk_container_add(GTK_CONTAINER(g->window), vbox);

    g->status = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(g->status), 0.0f);
    set_status(g, "#555555", "Ожидание подключения USB-накопителя… Вставьте флешку.");
    gtk_box_pack_start(GTK_BOX(vbox), g->status, FALSE, FALSE, 4);

    g->notebook = gtk_notebook_new();
    gtk_box_pack_start(GTK_BOX(vbox), g->notebook, TRUE, TRUE, 0);

    /* Вкладка 1: подключённые устройства + подробности */
    GType types[C_N];
    for (int i = 0; i < C_N; ++i) types[i] = G_TYPE_STRING;
    g->dev_store = gtk_list_store_newv(C_N, types);
    GtkWidget *sw1;
    g->dev_view = make_view(g->dev_store, COL_TITLES, C_N - 1, &sw1);
    g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(g->dev_view)), "changed",
                     G_CALLBACK(on_selection_changed), g);

    static const char *dt[] = {"Поле", "Значение"};
    GType t2[2] = {G_TYPE_STRING, G_TYPE_STRING};
    g->detail_store = gtk_list_store_newv(2, t2);
    GtkWidget *sw2;
    make_view(g->detail_store, dt, 2, &sw2);
    GtkWidget *frame = gtk_frame_new("Сведения об устройстве");
    gtk_container_add(GTK_CONTAINER(frame), sw2);

    GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_VERTICAL);
    gtk_paned_pack1(GTK_PANED(paned), sw1, TRUE, FALSE);
    gtk_paned_pack2(GTK_PANED(paned), frame, TRUE, FALSE);
    gtk_paned_set_position(GTK_PANED(paned), 200);
    gtk_notebook_append_page(GTK_NOTEBOOK(g->notebook), paned, gtk_label_new("Подключённые устройства"));

    /* Вкладка 2: реестр */
    static const char *rt[] = {"Серийный номер", "Производитель", "Модель", "VID:PID",
                               "Первое подключение", "Последнее подключение", "Всего подключений"};
    GType t3[7];
    for (int i = 0; i < 7; ++i) t3[i] = G_TYPE_STRING;
    g->reg_store = gtk_list_store_newv(7, t3);
    GtkWidget *sw3;
    make_view(g->reg_store, rt, 7, &sw3);
    gtk_notebook_append_page(GTK_NOTEBOOK(g->notebook), sw3, gtk_label_new("Реестр устройств"));

    /* Вкладка 3: журнал */
    g->log_view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(g->log_view), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(g->log_view), TRUE);
    g->log_buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(g->log_view));
    GtkWidget *sw4 = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(sw4), g->log_view);
    gtk_notebook_append_page(GTK_NOTEBOOK(g->notebook), sw4, gtk_label_new("Журнал"));
}

int gui_run(int argc, char **argv, const char *data_dir) {
    if (!gtk_init_check(&argc, &argv)) {
        fprintf(stderr, "Нет графической среды. Запустите с параметром --cli\n");
        return 1;
    }
    Gui *g = g_new0(Gui, 1);
    mkdir(data_dir, 0755);

    char path[300];
    snprintf(path, sizeof(path), "%s/usb_audit.log", data_dir);
    if (logger_open(&g->logger, path) != 0) {
        fprintf(stderr, "Не удалось открыть %s\n", path);
        return 1;
    }
    snprintf(path, sizeof(path), "%s/usb_registry.db", data_dir);
    registry_open(&g->registry, path);
    if (usb_scanner_init(&g->scanner) != 0) {
        fprintf(stderr, "Не удалось инициализировать libudev\n");
        return 1;
    }

    build_window(g);
    load_log_file(g);
    refresh_registry(g);
    gui_log(g, "START программа запущена, известных устройств: %zu", g->registry.size);

    g->mon = udev_monitor_new_from_netlink(g->scanner.udev, "udev");
    udev_monitor_filter_add_match_subsystem_devtype(g->mon, "block", "disk");
    udev_monitor_enable_receiving(g->mon);
    g_unix_fd_add(udev_monitor_get_fd(g->mon), G_IO_IN, on_udev_event, g);

    scan_existing(g);
    gtk_widget_show_all(g->window);
    gtk_main();

    gui_log(g, "STOP программа завершена");
    udev_monitor_unref(g->mon);
    usb_scanner_destroy(&g->scanner);
    registry_close(&g->registry);
    logger_close(&g->logger);
    g_free(g);
    return 0;
}
