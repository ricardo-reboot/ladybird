/*
 * Copyright (c) 2026, Ricardo Moura <ricardo@bugscave.dev>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <LibWebView/IdentityStore.h>
#include <LibWebView/WebUI.h>

namespace WebView {

class IdentitiesUI final : public WebUI {
    WEB_UI(IdentitiesUI);

private:
    virtual void register_interfaces() override;

    void load_identities();
    void create_identity(JsonValue const&);
    void delete_identity(JsonValue const&);
    void rename_identity(JsonValue const&);

    IdentityStore m_store;
};

}
