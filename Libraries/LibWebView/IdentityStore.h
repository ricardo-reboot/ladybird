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
//   <id>/private       — raw 32-byte seed (unencrypted) OR encrypted blob
//   <id>/private.salt  — 16-byte PBKDF2 salt (present only if passphrase-protected)
//   <id>/private.iv    — 12-byte AES-GCM IV (present only if passphrase-protected)
//   <id>/private.tag   — 16-byte AES-GCM auth tag (present only if passphrase-protected)
//   <id>/public        — raw 32-byte Ed25519 public key
//   <id>/meta.json     — {"id":"...","name":"...","created_at":<unix-secs>,"encrypted":bool}

#include <AK/ByteBuffer.h>
#include <AK/Error.h>
#include <AK/JsonArray.h>
#include <AK/String.h>
#include <AK/Vector.h>
#include <LibWebView/Forward.h>

namespace WebView {

struct WEBVIEW_API SSHWebIdentity {
    String id;           // UUID used as directory name
    String name;         // User-supplied display name
    i64 created_at {};   // Unix timestamp (seconds since epoch)
    ByteBuffer public_key; // Raw 32-byte Ed25519 public key
    bool encrypted {};   // True if private key is passphrase-protected
};

class WEBVIEW_API IdentityStore {
public:
    // Load all identities from disk.  Call once at startup.
    ErrorOr<void> load();

    // Return all loaded identities.
    Vector<SSHWebIdentity> const& identities() const { return m_identities; }

    // Create a new identity with the given display name and optional passphrase.
    // If passphrase is non-empty, the private key is encrypted with AES-256-GCM
    // using a key derived via PBKDF2-SHA256. Returns the new identity's id.
    ErrorOr<String> create(String name, String passphrase = {});

    // Delete the identity with the given id from disk and from the in-memory list.
    ErrorOr<void> remove(String const& id);

    // Rename the identity with the given id (updates meta.json on disk).
    ErrorOr<void> rename(String const& id, String new_name);

    // Decrypt and return the private key seed for an encrypted identity.
    // Returns error if passphrase is wrong or identity not found.
    ErrorOr<ByteBuffer> decrypt_private_key(String const& id, String const& passphrase);

    // Check whether an identity is passphrase-protected.
    bool is_encrypted(String const& id) const;

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
