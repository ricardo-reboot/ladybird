/*
 * Copyright (c) 2026, the sshweb-browser developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/CharacterTypes.h>
#include <AK/StringBuilder.h>
#include <LibSSHWeb/URL.h>

namespace SSHWeb {

static constexpr StringView SCHEME_PREFIX = "ssh-web://"sv;

ErrorOr<URL> URL::parse(StringView input)
{
    if (!input.starts_with(SCHEME_PREFIX))
        return Error::from_string_literal("Not an ssh-web URL");

    auto rest = input.substring_view(SCHEME_PREFIX.length());
    if (rest.is_empty())
        return Error::from_string_literal("ssh-web URL has no host");

    auto slash_index = rest.find('/');
    StringView authority = slash_index.has_value() ? rest.substring_view(0, *slash_index) : rest;
    StringView path = slash_index.has_value() ? rest.substring_view(*slash_index) : "/"sv;

    if (authority.is_empty())
        return Error::from_string_literal("ssh-web URL has no host");

    StringView host_view;
    u16 port = DEFAULT_PORT;
    auto colon_index = authority.find(':');
    if (colon_index.has_value()) {
        host_view = authority.substring_view(0, *colon_index);
        auto port_view = authority.substring_view(*colon_index + 1);
        if (port_view.is_empty())
            return Error::from_string_literal("ssh-web URL has empty port");
        for (auto c : port_view) {
            if (!is_ascii_digit(c))
                return Error::from_string_literal("ssh-web URL port is not numeric");
        }
        auto parsed_port = port_view.to_number<u32>();
        if (!parsed_port.has_value() || *parsed_port == 0 || *parsed_port > 65535)
            return Error::from_string_literal("ssh-web URL port out of range");
        port = static_cast<u16>(*parsed_port);
    } else {
        host_view = authority;
    }

    if (host_view.is_empty())
        return Error::from_string_literal("ssh-web URL has no host");

    return URL {
        .host = TRY(String::from_utf8(host_view)),
        .port = port,
        .path = TRY(String::from_utf8(path)),
    };
}

String URL::serialize() const
{
    StringBuilder builder;
    builder.append(SCHEME_PREFIX);
    builder.append(host);
    if (port != DEFAULT_PORT) {
        builder.append(':');
        builder.appendff("{}", port);
    }
    if (path.is_empty() || !path.starts_with('/'))
        builder.append('/');
    builder.append(path);
    return MUST(builder.to_string());
}

}
