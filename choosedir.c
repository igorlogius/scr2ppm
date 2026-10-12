/**
 * description: asks for a folder through the desktop portal's file chooser and prints it
 *              (works inside Flatpak, where the chosen folder is made accessible to the app)
 * usage: scr2ppm-choosedir [title]
 * exit codes: 0 = folder printed on stdout, 1 = cancelled, 2 = portal not available / error
 * repo: https://github.com/igorlogius/scr2ppm
 **/

#include<stdio.h>
#include<gio/gio.h>

static GMainLoop *loop;
static char *handle;      /* object path of our request, used to ignore other apps' responses */
static char *chosen;
static int result = 2;

static void
onResponse(
    GDBusConnection *conn, const gchar *sender, const gchar *path,
    const gchar *iface, const gchar *signal, GVariant *params, gpointer data
){
    guint32 code = 2;
    GVariant *results = NULL;
    GVariantIter *it = NULL;
    const gchar *uri;

    (void)conn; (void)sender; (void)iface; (void)signal; (void)data;
    if (handle && g_strcmp0(path, handle) != 0) { return; }

    g_variant_get(params, "(u@a{sv})", &code, &results);
    if (code == 0) {
        if (g_variant_lookup(results, "uris", "as", &it)) {
            if (g_variant_iter_next(it, "&s", &uri)) {
                chosen = g_filename_from_uri(uri, NULL, NULL);
                if (chosen) { result = 0; }
            }
            g_variant_iter_free(it);
        }
    } else {
        result = 1;   /* cancelled by the user (1) or ended otherwise (2) */
    }
    g_variant_unref(results);
    g_main_loop_quit(loop);
}

int
main(int argc, char *argv[]){
    const char *title = (argc > 1) ? argv[1] : "Choose a folder";
    GError *err = NULL;
    GVariantBuilder opts;
    GVariant *reply;

    GDBusConnection *conn = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &err);
    if (conn == NULL) {
        fprintf(stderr, "scr2ppm-choosedir: %s\n", err->message);
        return 2;
    }

    loop = g_main_loop_new(NULL, FALSE);
    g_dbus_connection_signal_subscribe(conn, "org.freedesktop.portal.Desktop",
        "org.freedesktop.portal.Request", "Response", NULL, NULL,
        G_DBUS_SIGNAL_FLAGS_NONE, onResponse, NULL, NULL);

    g_variant_builder_init(&opts, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&opts, "{sv}", "directory", g_variant_new_boolean(TRUE));

    reply = g_dbus_connection_call_sync(conn,
        "org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop",
        "org.freedesktop.portal.FileChooser", "OpenFile",
        g_variant_new("(ssa{sv})", "", title, &opts),
        G_VARIANT_TYPE("(o)"), G_DBUS_CALL_FLAGS_NONE, -1, NULL, &err);
    if (reply == NULL) {
        fprintf(stderr, "scr2ppm-choosedir: %s\n", err->message);
        return 2;
    }
    g_variant_get(reply, "(o)", &handle);

    g_main_loop_run(loop);

    if (result == 0) { printf("%s\n", chosen); }
    return result;
}
