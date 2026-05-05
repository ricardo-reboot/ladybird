/*
 * Copyright (c) 2026, the sshweb-browser developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Error.h>
#include <AK/String.h>
#include <AK/StringView.h>

namespace SSHWeb {

// The default TCP port for SSH-Web. Concatenation of SSH (22) and HTTPS (443).
constexpr u16 DEFAULT_PORT = 22443;

// A parsed `ssh-web://host[:port][/path]` URL.
//
// SSH-Web URLs deliberately exclude userinfo (identity is the user's keypair, not in the URL),
// query strings, and fragments. The current spec admits only host, port, and path.
struct URL {
    String host;
    u16 port { DEFAULT_PORT };
    String path { "/"_string };

    static ErrorOr<URL> parse(StringView input);
    String serialize() const;
};

}
