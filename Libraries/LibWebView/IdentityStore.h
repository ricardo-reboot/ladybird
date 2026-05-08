/*
 * Copyright (c) 2026, Ricardo Moura <ricardo@bugscave.dev>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

// SSH-Web identity store.
//
// Each identity is an Ed25519 keypair with a display name and a creation timestamp.
// Identities are stored under:
//   Core::StandardPaths::config_directory() + "/sshweb-browser/identities/<id>/"
// On macOS this resolves to ~/Library/Preferences/sshweb-browser/identities/<id>/.
//
// Disk layout (one directory per identity):
//   <id>/private   — raw 32-byte Ed25519 private key seed
//   <id>/public    — raw 32-byte Ed25519 public key
//   <id>/meta.json — {"id":"...","name":"...","created_at":<unix-secs>}
//
// TODO: Keys are stored in plaintext. Encrypted-at-rest is a future plan.

#include <AK/ByteBuffer.h>
#include <AK/Error.h>
#include <AK/JsonArray.h>
#include <AK/String.h>
#include <AK/Vector.h>

namespace WebView {

struct SSHWebIdentity {
    String id;           // UUID used as directory name
    String name;         // User-supplied display name
    i64 created_at {};   // Unix timestamp (seconds since epoch)
    ByteBuffer public_key; // Raw 32-byte Ed25519 public key
};

class IdentityStore {
public:
    // Load all identities from disk.  Call once at startup.
    ErrorOr<void> load();

    // Return all loaded identities.
    Vector<SSHWebIdentity> const& identities() const { return m_identities; }

    // Create a new identity with the given display name, write it to disk atomically,
    // and append it to the in-memory list.  Returns the new identity's id.
    ErrorOr<String> create(String name);

    // Delete the identity with the given id from disk and from the in-memory list.
    ErrorOr<void> remove(String const& id);

    // Serialize the identity list as a JsonArray suitable for sending to the WebUI.
    JsonArray serialize() const;

    // Hex-encoded SHA-256 fingerprint of a public key (first 16 bytes for display).
    static String fingerprint(ReadonlyBytes public_key);

private:
    static ByteString storage_directory();
    static ErrorOr<void> ensure_storage_directory();

    Vector<SSHWebIdentity> m_identities;
};

}
