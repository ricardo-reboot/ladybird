/*
 * Copyright (c) 2026, Ricardo Moura <ricardo@bugscave.dev>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/JsonObject.h>
#include <LibWebView/WebUI/IdentitiesUI.h>

namespace WebView {

void IdentitiesUI::register_interfaces()
{
    if (auto result = m_store.load(); result.is_error())
        warnln("IdentitiesUI: failed to load identities: {}", result.error());

    register_interface("loadIdentities"sv, [this](auto const&) {
        load_identities();
    });
    register_interface("createIdentity"sv, [this](auto const& data) {
        create_identity(data);
    });
    register_interface("deleteIdentity"sv, [this](auto const& data) {
        delete_identity(data);
    });
    register_interface("renameIdentity"sv, [this](auto const& data) {
        rename_identity(data);
    });
}

void IdentitiesUI::load_identities()
{
    async_send_message("renderIdentities"sv, m_store.serialize());
}

void IdentitiesUI::create_identity(JsonValue const& data)
{
    String name_string;
    String passphrase;

    if (data.is_string()) {
        name_string = data.as_string();
    } else if (data.is_object()) {
        auto const& obj = data.as_object();
        name_string = obj.get_string("name"sv).value_or("Unnamed"_string);
        passphrase = obj.get_string("passphrase"sv).value_or(String {});
    } else {
        warnln("IdentitiesUI::createIdentity: expected string or object");
        return;
    }

    auto result = m_store.create(move(name_string), move(passphrase));
    if (result.is_error()) {
        warnln("IdentitiesUI: failed to create identity: {}", result.error());
        JsonObject error_obj;
        error_obj.set("error"_string, MUST(String::formatted("{}", result.error())));
        async_send_message("identityError"sv, move(error_obj));
        return;
    }

    async_send_message("renderIdentities"sv, m_store.serialize());
}

void IdentitiesUI::delete_identity(JsonValue const& data)
{
    if (!data.is_string()) {
        warnln("IdentitiesUI::deleteIdentity: expected string id");
        return;
    }

    auto id_string = data.as_string();
    auto result = m_store.remove(id_string);
    if (result.is_error()) {
        warnln("IdentitiesUI: failed to delete identity: {}", result.error());
        JsonObject error_obj;
        error_obj.set("error"_string, MUST(String::formatted("{}", result.error())));
        async_send_message("identityError"sv, move(error_obj));
        return;
    }

    async_send_message("renderIdentities"sv, m_store.serialize());
}

void IdentitiesUI::rename_identity(JsonValue const& data)
{
    if (!data.is_object()) {
        warnln("IdentitiesUI::renameIdentity: expected object with id and name");
        return;
    }

    auto const& obj = data.as_object();
    auto id = obj.get_string("id"sv);
    auto name = obj.get_string("name"sv);
    if (!id.has_value() || !name.has_value()) {
        warnln("IdentitiesUI::renameIdentity: missing id or name");
        return;
    }

    auto result = m_store.rename(id.value(), name.value());
    if (result.is_error()) {
        warnln("IdentitiesUI: failed to rename identity: {}", result.error());
        JsonObject error_obj;
        error_obj.set("error"_string, MUST(String::formatted("{}", result.error())));
        async_send_message("identityError"sv, move(error_obj));
        return;
    }

    async_send_message("renderIdentities"sv, m_store.serialize());
}

}
