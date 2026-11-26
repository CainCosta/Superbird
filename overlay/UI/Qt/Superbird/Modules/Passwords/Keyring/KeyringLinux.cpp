/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <UI/Qt/Superbird/Modules/Passwords/Keyring/Keyring.h>

#include <QCoreApplication>
#include <QMetaObject>

#include <gio/gio.h>

#include <memory>
#include <thread>

namespace Superbird::Passwords {

namespace {

constexpr char const* SECRET_BUS_NAME = "org.freedesktop.secrets";
constexpr char const* SECRET_SERVICE_PATH = "/org/freedesktop/secrets";
constexpr char const* SECRET_SERVICE_IFACE = "org.freedesktop.Secret.Service";
constexpr char const* SECRET_COLLECTION_IFACE = "org.freedesktop.Secret.Collection";
constexpr char const* SECRET_ITEM_IFACE = "org.freedesktop.Secret.Item";
constexpr char const* SECRET_SESSION_IFACE = "org.freedesktop.Secret.Session";
constexpr char const* DEFAULT_COLLECTION_PATH = "/org/freedesktop/secrets/aliases/default";

void post_to_gui(std::function<void()> fn)
{
    auto* app = QCoreApplication::instance();
    if (!app) {
        fn();
        return;
    }
    QMetaObject::invokeMethod(app, [fn = std::move(fn)]() mutable { fn(); }, Qt::QueuedConnection);
}

std::string open_plain_session(GDBusConnection* bus)
{
    GError* err = nullptr;
    GVariant* input = g_variant_new_string("");
    GVariant* res = g_dbus_connection_call_sync(
        bus,
        SECRET_BUS_NAME,
        SECRET_SERVICE_PATH,
        SECRET_SERVICE_IFACE,
        "OpenSession",
        g_variant_new("(sv)", "plain", input),
        G_VARIANT_TYPE("(vo)"),
        G_DBUS_CALL_FLAGS_NONE,
        5000,
        nullptr,
        &err);
    if (err) {
        g_error_free(err);
        return {};
    }
    if (!res)
        return {};

    GVariant* out_val = nullptr;
    char const* session_path = nullptr;
    g_variant_get(res, "(v&o)", &out_val, &session_path);
    std::string path = session_path ? session_path : "";
    if (out_val)
        g_variant_unref(out_val);
    g_variant_unref(res);
    return path;
}

void close_session(GDBusConnection* bus, std::string const& session_path)
{
    if (session_path.empty())
        return;
    GError* err = nullptr;
    GVariant* res = g_dbus_connection_call_sync(
        bus,
        SECRET_BUS_NAME,
        session_path.c_str(),
        SECRET_SESSION_IFACE,
        "Close",
        nullptr,
        nullptr,
        G_DBUS_CALL_FLAGS_NONE,
        2000,
        nullptr,
        &err);
    if (err)
        g_error_free(err);
    if (res)
        g_variant_unref(res);
}

std::vector<std::string> search_item_paths(GDBusConnection* bus, std::string_view service, std::string_view account)
{
    std::vector<std::string> paths;
    GVariantBuilder attrs;
    g_variant_builder_init(&attrs, G_VARIANT_TYPE("a{ss}"));
    g_variant_builder_add(&attrs, "{ss}", "application", "superbird");
    g_variant_builder_add(&attrs, "{ss}", "service", std::string(service).c_str());
    g_variant_builder_add(&attrs, "{ss}", "account", std::string(account).c_str());

    GError* err = nullptr;
    GVariant* res = g_dbus_connection_call_sync(
        bus,
        SECRET_BUS_NAME,
        SECRET_SERVICE_PATH,
        SECRET_SERVICE_IFACE,
        "SearchItems",
        g_variant_new("(a{ss})", &attrs),
        G_VARIANT_TYPE("(aoao)"),
        G_DBUS_CALL_FLAGS_NONE,
        5000,
        nullptr,
        &err);
    if (err) {
        g_error_free(err);
        return paths;
    }
    if (!res)
        return paths;

    GVariantIter* unlocked = nullptr;
    GVariantIter* locked = nullptr;
    g_variant_get(res, "(aoao)", &unlocked, &locked);
    char const* p = nullptr;
    while (unlocked && g_variant_iter_loop(unlocked, "&o", &p))
        paths.emplace_back(p);
    while (locked && g_variant_iter_loop(locked, "&o", &p))
        paths.emplace_back(p);
    if (unlocked)
        g_variant_iter_free(unlocked);
    if (locked)
        g_variant_iter_free(locked);
    g_variant_unref(res);
    return paths;
}

void unlock_object(GDBusConnection* bus, std::string const& obj_path)
{
    GVariantBuilder arr;
    g_variant_builder_init(&arr, G_VARIANT_TYPE("ao"));
    g_variant_builder_add(&arr, "o", obj_path.c_str());
    GError* err = nullptr;
    GVariant* res = g_dbus_connection_call_sync(
        bus,
        SECRET_BUS_NAME,
        SECRET_SERVICE_PATH,
        SECRET_SERVICE_IFACE,
        "Unlock",
        g_variant_new("(ao)", &arr),
        G_VARIANT_TYPE("(aoo)"),
        G_DBUS_CALL_FLAGS_NONE,
        15000,
        nullptr,
        &err);
    if (err)
        g_error_free(err);
    if (res)
        g_variant_unref(res);
}

} // namespace

bool Keyring::is_available()
{
    GError* err = nullptr;
    GDBusConnection* bus = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &err);
    if (err) {
        g_error_free(err);
        return false;
    }
    if (!bus)
        return false;
    auto session = open_plain_session(bus);
    if (!session.empty())
        close_session(bus, session);
    g_object_unref(bus);
    return !session.empty();
}

bool Keyring::supports_biometrics()
{
    return is_available();
}

bool Keyring::store_secret(
    std::string_view service,
    std::string_view account,
    std::span<uint8_t const> secret,
    bool)
{
    if (service.empty() || account.empty() || secret.empty())
        return false;

    GError* err = nullptr;
    GDBusConnection* bus = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &err);
    if (err || !bus) {
        if (err)
            g_error_free(err);
        return false;
    }

    auto session = open_plain_session(bus);
    if (session.empty()) {
        g_object_unref(bus);
        return false;
    }

    unlock_object(bus, DEFAULT_COLLECTION_PATH);

    GVariantBuilder attrs;
    g_variant_builder_init(&attrs, G_VARIANT_TYPE("a{ss}"));
    g_variant_builder_add(&attrs, "{ss}", "application", "superbird");
    g_variant_builder_add(&attrs, "{ss}", "service", std::string(service).c_str());
    g_variant_builder_add(&attrs, "{ss}", "account", std::string(account).c_str());

    std::string label = "Superbird Passwords (" + std::string(account) + ")";
    GVariantBuilder props;
    g_variant_builder_init(&props, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&props, "{sv}", "org.freedesktop.Secret.Item.Label", g_variant_new_string(label.c_str()));
    g_variant_builder_add(&props, "{sv}", "org.freedesktop.Secret.Item.Attributes", g_variant_builder_end(&attrs));

    GVariant* params = g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE, nullptr, 0, 1);
    GVariant* value = g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE, secret.data(), secret.size(), 1);
    GVariant* secret_struct = g_variant_new("(o@ay@ays)", session.c_str(), params, value, "application/octet-stream");

    GVariant* res = g_dbus_connection_call_sync(
        bus,
        SECRET_BUS_NAME,
        DEFAULT_COLLECTION_PATH,
        SECRET_COLLECTION_IFACE,
        "CreateItem",
        g_variant_new("(@a{sv}@(oayays)b)", g_variant_builder_end(&props), secret_struct, TRUE),
        G_VARIANT_TYPE("(oo)"),
        G_DBUS_CALL_FLAGS_NONE,
        5000,
        nullptr,
        &err);

    bool ok = (res != nullptr && err == nullptr);
    if (err)
        g_error_free(err);
    if (res)
        g_variant_unref(res);
    close_session(bus, session);
    g_object_unref(bus);
    return ok;
}

void Keyring::load_secret_async(
    std::string_view service,
    std::string_view account,
    std::string_view,
    LoadCallback callback)
{
    std::string svc(service);
    std::string acct(account);
    auto cb = std::make_shared<LoadCallback>(std::move(callback));

    std::thread([svc = std::move(svc), acct = std::move(acct), cb = std::move(cb)] {
        GError* err = nullptr;
        GDBusConnection* bus = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &err);
        if (err || !bus) {
            std::string msg = err ? err->message : "Unable to connect to session D-Bus";
            if (err)
                g_error_free(err);
            post_to_gui([cb, msg = std::move(msg)] {
                if (*cb)
                    (*cb)(std::nullopt, msg);
            });
            return;
        }

        auto session = open_plain_session(bus);
        if (session.empty()) {
            g_object_unref(bus);
            post_to_gui([cb] {
                if (*cb)
                    (*cb)(std::nullopt, "Secret Service unavailable");
            });
            return;
        }

        auto paths = search_item_paths(bus, svc, acct);
        if (paths.empty()) {
            close_session(bus, session);
            g_object_unref(bus);
            post_to_gui([cb] {
                if (*cb)
                    (*cb)(std::nullopt, "No saved unlock key in Secret Service");
            });
            return;
        }

        unlock_object(bus, paths[0]);

        GVariant* res = g_dbus_connection_call_sync(
            bus,
            SECRET_BUS_NAME,
            paths[0].c_str(),
            SECRET_ITEM_IFACE,
            "GetSecret",
            g_variant_new("(o)", session.c_str()),
            G_VARIANT_TYPE("((oayays))"),
            G_DBUS_CALL_FLAGS_NONE,
            5000,
            nullptr,
            &err);

        if (err || !res) {
            std::string msg = err ? err->message : "Failed to read secret from Secret Service";
            if (err)
                g_error_free(err);
            close_session(bus, session);
            g_object_unref(bus);
            post_to_gui([cb, msg = std::move(msg)] {
                if (*cb)
                    (*cb)(std::nullopt, msg);
            });
            return;
        }

        GVariant* tuple = g_variant_get_child_value(res, 0);
        GVariant* val_var = g_variant_get_child_value(tuple, 2);
        gsize n_elements = 0;
        auto const* raw = static_cast<uint8_t const*>(g_variant_get_fixed_array(val_var, &n_elements, 1));
        SecureBytes secret({ raw, static_cast<size_t>(n_elements) });

        g_variant_unref(val_var);
        g_variant_unref(tuple);
        g_variant_unref(res);
        close_session(bus, session);
        g_object_unref(bus);

        post_to_gui([cb, secret = std::move(secret)]() mutable {
            if (*cb)
                (*cb)(std::move(secret), {});
        });
    }).detach();
}

bool Keyring::has_secret(std::string_view service, std::string_view account)
{
    if (service.empty() || account.empty())
        return false;
    GError* err = nullptr;
    GDBusConnection* bus = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &err);
    if (err || !bus) {
        if (err)
            g_error_free(err);
        return false;
    }
    auto paths = search_item_paths(bus, service, account);
    g_object_unref(bus);
    return !paths.empty();
}

bool Keyring::delete_secret(std::string_view service, std::string_view account)
{
    if (service.empty() || account.empty())
        return false;
    GError* err = nullptr;
    GDBusConnection* bus = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &err);
    if (err || !bus) {
        if (err)
            g_error_free(err);
        return false;
    }
    auto paths = search_item_paths(bus, service, account);
    for (auto const& path : paths) {
        GError* del_err = nullptr;
        GVariant* res = g_dbus_connection_call_sync(
            bus,
            SECRET_BUS_NAME,
            path.c_str(),
            SECRET_ITEM_IFACE,
            "Delete",
            nullptr,
            G_VARIANT_TYPE("(o)"),
            G_DBUS_CALL_FLAGS_NONE,
            5000,
            nullptr,
            &del_err);
        if (del_err)
            g_error_free(del_err);
        if (res)
            g_variant_unref(res);
    }
    g_object_unref(bus);
    return true;
}

}
