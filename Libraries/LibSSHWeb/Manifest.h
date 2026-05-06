/*
 * Copyright (c) 2026, the sshweb-browser developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Error.h>
#include <AK/Optional.h>
#include <AK/String.h>
#include <AK/StringView.h>
#include <AK/Vector.h>

namespace SSHWeb {

struct ManifestSite {
    String name;
    String host;
    Optional<String> description;
};

struct ManifestCommand {
    String name;
    Optional<String> description;
    Vector<String> routes;
    Vector<String> supports;
};

struct ManifestAuth {
    Vector<String> modes;
    Vector<String> key_types;
};

struct ManifestMCPTool {
    String name;
    String auth;
    Optional<String> description;
};

struct ManifestMCP {
    String version;
    Vector<ManifestMCPTool> tools;
};

struct ManifestProxyCache {
    Vector<String> allow; // hostnames, e.g. "fonts.googleapis.com"
};

struct CapabilitiesManifest {
    String protocol;
    ManifestSite site;
    Vector<ManifestCommand> commands;
    ManifestAuth auth;
    Optional<ManifestMCP> mcp;
    Optional<ManifestProxyCache> proxy_cache;

    static ErrorOr<CapabilitiesManifest> parse(StringView json);
    ErrorOr<String> serialize() const;
};

}
