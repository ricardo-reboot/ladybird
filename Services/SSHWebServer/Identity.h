/*
 * Copyright (c) 2026, the sshweb-browser developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Error.h>
#include <AK/String.h>
#include <AK/StringView.h>
#include <AK/Vector.h>

namespace SSHWeb {

// One identity = a labeled ed25519 keypair stored on disk.
//
// Layout:
//   <root>/<label>/
//     ed25519        (OpenSSH-format private key, mode 0600)
//     ed25519.pub    (OpenSSH-format public key)
struct Identity {
    String label;
    String private_key_path;
    String public_key_path;
    String passphrase;
};

class IdentityStore {
public:
    explicit IdentityStore(String root_dir);

    // Default location: <config>/sshweb/identities/
    static ErrorOr<IdentityStore> with_default_root();

    ErrorOr<Vector<String>> list_labels() const;
    ErrorOr<Identity> get(StringView label) const;

    // Generate a fresh ed25519 keypair under <label>. Returns the resulting Identity.
    // Implemented by shelling out to ssh-keygen (-t ed25519 -N "" -C <label>).
    ErrorOr<Identity> generate(StringView label);

    String root_dir() const { return m_root_dir; }

private:
    String m_root_dir;
};

}
