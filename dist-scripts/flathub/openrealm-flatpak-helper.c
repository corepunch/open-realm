#include <gio/gio.h>
#include <SDL2/SDL.h>

#include <stdio.h>
#include <string.h>

struct portal_response {
    GMainLoop *loop;
    guint response;
    char *request_path;
    char *path;
};

static char *portal_sender_path(GDBusConnection *bus) {
    const char *unique_name = g_dbus_connection_get_unique_name(bus);
    char *sender;

    if (!unique_name || unique_name[0] != ':') {
        return NULL;
    }
    sender = g_strdup(unique_name + 1);
    for (char *p = sender; *p; p++) {
        if (*p == '.') {
            *p = '_';
        }
    }
    return sender;
}

static char *portal_handle_token(void) {
    char *uuid = g_uuid_string_random();
    char *token;

    for (char *p = uuid; *p; p++) {
        if (*p == '-') {
            *p = '_';
        }
    }
    token = g_strdup_printf("openrealm_%s", uuid);
    g_free(uuid);
    return token;
}

static void portal_response_cb(GDBusConnection *connection,
                               const gchar *sender_name,
                               const gchar *object_path,
                               const gchar *interface_name,
                               const gchar *signal_name,
                               GVariant *parameters,
                               gpointer user_data) {
    struct portal_response *response = user_data;
    GVariant *results = NULL;
    gchar **uris = NULL;

    (void)connection;
    (void)sender_name;
    (void)interface_name;
    (void)signal_name;

    if (!response->request_path || strcmp(object_path, response->request_path)) {
        return;
    }

    g_variant_get(parameters, "(u@a{sv})", &response->response, &results);
    if (response->response == 0 &&
        g_variant_lookup(results, "uris", "^as", &uris) &&
        uris && uris[0]) {
        GError *error = NULL;
        response->path = g_filename_from_uri(uris[0], NULL, &error);
        if (!response->path && error) {
            fprintf(stderr, "Could not convert portal URI: %s\n", error->message);
            g_error_free(error);
        }
    }

    g_strfreev(uris);
    g_variant_unref(results);
    g_main_loop_quit(response->loop);
}


static int prompt_data_directory(void) {
    const SDL_MessageBoxButtonData buttons[] = {
        { SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "Cancel" },
        { SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Choose Warcraft III folder" },
    };
    const SDL_MessageBoxData message = {
        SDL_MESSAGEBOX_INFORMATION,
        NULL,
        "OpenRealm - Warcraft III data",
        "OpenRealm requires the original Warcraft III game data, which is not "
        "included with OpenRealm.\n\n"
        "Next, choose the Warcraft III installation/data folder containing "
        "War3.mpq. If War3x.mpq is also present, The Frozen Throne will be "
        "enabled automatically.\n\n"
        "OpenRealm will remember the selected portal folder for future launches.",
        (int)(sizeof(buttons) / sizeof(buttons[0])),
        buttons,
        NULL,
    };
    int button_id = 0;

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init failed for data prompt: %s\n", SDL_GetError());
        return 2;
    }
    if (SDL_ShowMessageBox(&message, &button_id) != 0) {
        fprintf(stderr, "SDL_ShowMessageBox failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 2;
    }
    SDL_Quit();
    return button_id == 1 ? 0 : 1;
}

static int pick_data_directory(void) {
    GError *error = NULL;
    GDBusConnection *bus = NULL;
    GVariantBuilder options;
    GVariant *reply = NULL;
    const gchar *returned_request_path = NULL;
    char *sender = NULL;
    char *token = NULL;
    char *expected_request_path = NULL;
    guint subscription = 0;
    struct portal_response response = { 0 };
    int rc = 1;

    bus = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &error);
    if (!bus) {
        fprintf(stderr, "Could not connect to session bus: %s\n",
                error ? error->message : "unknown error");
        g_clear_error(&error);
        return 2;
    }

    sender = portal_sender_path(bus);
    token = portal_handle_token();
    if (!sender || !token) {
        fprintf(stderr, "Could not prepare XDG portal request handle.\n");
        rc = 2;
        goto cleanup;
    }
    expected_request_path = g_strdup_printf(
        "/org/freedesktop/portal/desktop/request/%s/%s", sender, token);
    response.request_path = g_strdup(expected_request_path);
    response.loop = g_main_loop_new(NULL, FALSE);

    /*
     * Supplying handle_token makes the request object path predictable on
     * modern xdg-desktop-portal. Subscribe before OpenFile so an immediate
     * Response cannot race the subscription. If an older portal returns a
     * different path, switch the subscription to that returned handle.
     */
    subscription = g_dbus_connection_signal_subscribe(
        bus,
        "org.freedesktop.portal.Desktop",
        "org.freedesktop.portal.Request",
        "Response",
        expected_request_path,
        NULL,
        G_DBUS_SIGNAL_FLAGS_NONE,
        portal_response_cb,
        &response,
        NULL);

    g_variant_builder_init(&options, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(&options, "{sv}", "handle_token", g_variant_new_string(token));
    g_variant_builder_add(&options, "{sv}", "multiple", g_variant_new_boolean(FALSE));
    g_variant_builder_add(&options, "{sv}", "directory", g_variant_new_boolean(TRUE));
    g_variant_builder_add(&options, "{sv}", "modal", g_variant_new_boolean(TRUE));
    g_variant_builder_add(&options, "{sv}", "accept_label",
                          g_variant_new_string("Use this folder"));

    reply = g_dbus_connection_call_sync(
        bus,
        "org.freedesktop.portal.Desktop",
        "/org/freedesktop/portal/desktop",
        "org.freedesktop.portal.FileChooser",
        "OpenFile",
        g_variant_new("(ssa{sv})",
                      "",
                      "Select Warcraft III game data folder",
                      &options),
        G_VARIANT_TYPE("(o)"),
        G_DBUS_CALL_FLAGS_NONE,
        -1,
        NULL,
        &error);
    if (!reply) {
        fprintf(stderr, "File chooser portal failed: %s\n",
                error ? error->message : "unknown error");
        g_clear_error(&error);
        rc = 2;
        goto cleanup;
    }

    g_variant_get(reply, "(&o)", &returned_request_path);
    if (strcmp(returned_request_path, expected_request_path)) {
        g_dbus_connection_signal_unsubscribe(bus, subscription);
        subscription = g_dbus_connection_signal_subscribe(
            bus,
            "org.freedesktop.portal.Desktop",
            "org.freedesktop.portal.Request",
            "Response",
            returned_request_path,
            NULL,
            G_DBUS_SIGNAL_FLAGS_NONE,
            portal_response_cb,
            &response,
            NULL);
        g_free(response.request_path);
        response.request_path = g_strdup(returned_request_path);
    }

    g_main_loop_run(response.loop);
    if (response.response == 0 && response.path) {
        puts(response.path);
        rc = 0;
    }

cleanup:
    if (subscription) {
        g_dbus_connection_signal_unsubscribe(bus, subscription);
    }
    if (response.loop) {
        g_main_loop_unref(response.loop);
    }
    g_free(response.request_path);
    g_free(response.path);
    g_free(expected_request_path);
    g_free(token);
    g_free(sender);
    if (reply) {
        g_variant_unref(reply);
    }
    if (bus) {
        g_object_unref(bus);
    }
    return rc;
}

static int prompt_steam_shortcut(void) {
    const SDL_MessageBoxButtonData buttons[] = {
        { SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "Not now" },
        { SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Install Steam shortcut" },
    };
    const SDL_MessageBoxData message = {
        SDL_MESSAGEBOX_INFORMATION,
        NULL,
        "OpenRealm",
        "Would you like to install an OpenRealm shortcut in Steam?\n\n"
        "This updates Steam's non-Steam shortcuts, installs the packaged "
        "OpenRealm artwork, and assigns Valve's Steam Deck gamepad + mouse "
        "template.\n\n"
        "Nothing is changed if you choose Not now. Restart Steam after "
        "installation to see the shortcut.",
        (int)(sizeof(buttons) / sizeof(buttons[0])),
        buttons,
        NULL,
    };
    int button_id = 0;

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init failed for Steam prompt: %s\n", SDL_GetError());
        return 2;
    }
    if (SDL_ShowMessageBox(&message, &button_id) != 0) {
        fprintf(stderr, "SDL_ShowMessageBox failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 2;
    }
    SDL_Quit();
    return button_id == 1 ? 0 : 1;
}


static int show_data_error(void) {
    int rc;

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init failed for data error dialog: %s\n", SDL_GetError());
        return 2;
    }
    rc = SDL_ShowSimpleMessageBox(
        SDL_MESSAGEBOX_ERROR,
        "OpenRealm",
        "That folder does not contain War3.mpq.\n\n"
        "Select the Warcraft III installation/data folder itself, not a parent folder.",
        NULL);
    if (rc != 0) {
        fprintf(stderr, "SDL_ShowSimpleMessageBox failed: %s\n", SDL_GetError());
    }
    SDL_Quit();
    return rc == 0 ? 0 : 2;
}

static void usage(const char *argv0) {
    fprintf(stderr,
            "usage: %s prompt-data | pick-data | prompt-steam | data-error\n",
            argv0 ? argv0 : "openrealm-flatpak-helper");
}

int main(int argc, char **argv) {
    if (argc != 2) {
        usage(argv[0]);
        return 2;
    }
    if (!strcmp(argv[1], "prompt-data")) {
        return prompt_data_directory();
    }
    if (!strcmp(argv[1], "pick-data")) {
        return pick_data_directory();
    }
    if (!strcmp(argv[1], "prompt-steam")) {
        return prompt_steam_shortcut();
    }
    if (!strcmp(argv[1], "data-error")) {
        return show_data_error();
    }
    usage(argv[0]);
    return 2;
}
