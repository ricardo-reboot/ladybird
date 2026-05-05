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

namespace SSHWeb {

struct HostKey {
    String type;                // "ed25519", "ecdsa-sha2-nistp256", ...
    String fingerprint_sha256;  // base64 SHA-256 of the public key
};

struct KnownHostEntry {
    HostKey key;
    String first_seen;
};

// Persistent TOFU store. One file per (host, port) under root_dir.
// Files are JSON; the file name is `<host>_<port>.json`.
class KnownHosts {
public:
    explicit KnownHosts(String root_dir);

    ErrorOr<Optional<KnownHostEntry>> lookup(StringView host, u16 port) const;
    ErrorOr<void> record(StringView host, u16 port, HostKey const& key);
    ErrorOr<void> forget(StringView host, u16 port);

    // Default location: ~/.config/sshweb/known_hosts/
    static ErrorOr<KnownHosts> with_default_root();

private:
    ErrorOr<String> path_for(StringView host, u16 port) const;
    String m_root_dir;
};

}
