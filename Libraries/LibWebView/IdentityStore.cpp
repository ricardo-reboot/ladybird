/*
 * Copyright (c) 2026, Ricardo Moura <ricardo@bugscave.dev>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Base64.h>
#include <AK/Hex.h>
#include <AK/JsonObject.h>
#include <AK/JsonParser.h>
#include <AK/JsonValue.h>
#include <AK/LexicalPath.h>
#include <AK/Random.h>
#include <AK/StringBuilder.h>
#include <AK/Time.h>
#include <LibCore/DirIterator.h>
#include <LibCore/Directory.h>
#include <LibCore/File.h>
#include <LibCore/StandardPaths.h>
#include <LibCore/System.h>
#include <LibCrypto/Cipher/AES.h>
#include <LibCrypto/Curves/EdwardsCurve.h>
#include <LibCrypto/Hash/PBKDF2.h>
#include <LibCrypto/Hash/SHA2.h>
#include <LibWebView/IdentityStore.h>

namespace WebView {

static constexpr u32 PBKDF2_ITERATIONS = 600000;
static constexpr size_t SALT_LENGTH = 16;
static constexpr size_t IV_LENGTH = 12;
static constexpr size_t TAG_LENGTH = 16;

ByteString IdentityStore::storage_directory()
{
    return ByteString::formatted("{}/sshweb-browser/identities", Core::StandardPaths::config_directory());
}

ErrorOr<void> IdentityStore::ensure_storage_directory()
{
    TRY(Core::Directory::create(storage_directory(), Core::Directory::CreateDirectories::Yes));
    return {};
}

ErrorOr<void> IdentityStore::load()
{
    m_identities.clear();

    TRY(ensure_storage_directory());

    Core::DirIterator it(storage_directory(), Core::DirIterator::SkipParentAndBaseDir);
    while (it.has_next()) {
        auto entry_name = it.next_path();
        auto meta_path = ByteString::formatted("{}/{}/meta.json", storage_directory(), entry_name);
        auto pub_path  = ByteString::formatted("{}/{}/public",    storage_directory(), entry_name);

        auto meta_file = Core::File::open(meta_path, Core::File::OpenMode::Read);
        if (meta_file.is_error())
            continue;
        auto meta_bytes = TRY(meta_file.value()->read_until_eof());
        auto meta_str = StringView(meta_bytes.bytes());
        auto json_or_err = JsonValue::from_string(meta_str);
        if (json_or_err.is_error() || !json_or_err.value().is_object())
            continue;
        auto const& obj = json_or_err.value().as_object();

        auto id = obj.get_string("id"sv);
        auto name = obj.get_string("name"sv);
        auto created_at = obj.get_integer<i64>("created_at"sv);
        if (!id.has_value() || !name.has_value() || !created_at.has_value())
            continue;

        bool is_encrypted = obj.get_bool("encrypted"sv).value_or(false);

        auto pub_file = Core::File::open(pub_path, Core::File::OpenMode::Read);
        if (pub_file.is_error())
            continue;
        auto pub_bytes = TRY(pub_file.value()->read_until_eof());
        if (pub_bytes.size() != 32)
            continue;

        m_identities.append(SSHWebIdentity {
            .id = id.release_value(),
            .name = name.release_value(),
            .created_at = *created_at,
            .public_key = move(pub_bytes),
            .encrypted = is_encrypted,
        });
    }

    return {};
}

static ErrorOr<ByteBuffer> derive_key(ReadonlyBytes passphrase, ReadonlyBytes salt)
{
    Crypto::Hash::PBKDF2 pbkdf2(Crypto::Hash::HashKind::SHA256);
    return pbkdf2.derive_key(passphrase, salt, PBKDF2_ITERATIONS, 32);
}

static ErrorOr<void> write_file_atomic(ByteString const& dir, char const* name, ReadonlyBytes data, mode_t mode = 0644)
{
    auto tmp = ByteString::formatted("{}/{}.tmp", dir, name);
    auto final_path = ByteString::formatted("{}/{}", dir, name);
    {
        auto f = TRY(Core::File::open(tmp, Core::File::OpenMode::Write, mode));
        TRY(f->write_until_depleted(data));
    }
    TRY(Core::System::rename(tmp, final_path));
    return {};
}

ErrorOr<String> IdentityStore::create(String name, String passphrase)
{
    TRY(ensure_storage_directory());

    auto id = generate_random_uuid();
    auto dir = ByteString::formatted("{}/{}", storage_directory(), id);
    TRY(Core::Directory::create(dir, Core::Directory::CreateDirectories::Yes));

    Crypto::Curves::Ed25519 ed25519;
    auto private_seed = TRY(ed25519.generate_private_key());
    auto public_key_bytes = TRY(ed25519.generate_public_key(private_seed));

    bool is_encrypted = !passphrase.is_empty();

    if (is_encrypted) {
        // Generate random salt and IV.
        u8 salt_buf[SALT_LENGTH];
        fill_with_random(salt_buf);
        ReadonlyBytes salt { salt_buf, SALT_LENGTH };

        u8 iv_buf[IV_LENGTH];
        fill_with_random(iv_buf);
        ReadonlyBytes iv { iv_buf, IV_LENGTH };

        // Derive 256-bit key from passphrase.
        auto key = TRY(derive_key(passphrase.bytes(), salt));

        // Encrypt private seed with AES-256-GCM.
        Crypto::Cipher::AESGCMCipher cipher(key.bytes());
        auto encrypted = TRY(cipher.encrypt(private_seed, iv, {}, TAG_LENGTH));

        // Write encrypted private key + crypto parameters.
        TRY(write_file_atomic(dir, "private", encrypted.ciphertext, 0600));
        TRY(write_file_atomic(dir, "private.salt", salt));
        TRY(write_file_atomic(dir, "private.iv", iv));
        TRY(write_file_atomic(dir, "private.tag", encrypted.tag));
    } else {
        TRY(write_file_atomic(dir, "private", private_seed, 0600));
    }

    TRY(write_file_atomic(dir, "public", public_key_bytes));

    // Build meta.json.
    auto now = AK::UnixDateTime::now().seconds_since_epoch();
    JsonObject meta;
    meta.set("id"_string, id);
    meta.set("name"_string, name);
    meta.set("created_at"_string, JsonValue(now));
    meta.set("encrypted"_string, JsonValue(is_encrypted));

    auto meta_json = JsonValue(move(meta)).serialized();
    TRY(write_file_atomic(dir, "meta.json", meta_json.bytes()));

    m_identities.append(SSHWebIdentity {
        .id = id,
        .name = move(name),
        .created_at = now,
        .public_key = move(public_key_bytes),
        .encrypted = is_encrypted,
    });

    return id;
}

ErrorOr<void> IdentityStore::remove(String const& id)
{
    auto dir = ByteString::formatted("{}/{}", storage_directory(), id);

    for (auto const* filename : { "private", "private.salt", "private.iv", "private.tag", "public", "meta.json" }) {
        auto path = ByteString::formatted("{}/{}", dir, filename);
        auto result = Core::System::unlink(path);
        if (result.is_error() && result.error().code() != ENOENT)
            return result.release_error();
    }
    TRY(Core::System::rmdir(dir));

    m_identities.remove_all_matching([&id](SSHWebIdentity const& ident) {
        return ident.id == id;
    });

    return {};
}

ErrorOr<void> IdentityStore::rename(String const& id, String new_name)
{
    auto dir = ByteString::formatted("{}/{}", storage_directory(), id);
    auto meta_path = ByteString::formatted("{}/meta.json", dir);

    auto meta_file = TRY(Core::File::open(meta_path, Core::File::OpenMode::Read));
    auto meta_bytes = TRY(meta_file->read_until_eof());
    auto json = TRY(JsonValue::from_string(StringView(meta_bytes.bytes())));
    if (!json.is_object())
        return Error::from_string_literal("meta.json is not an object");

    auto obj = json.as_object();
    obj.set("name"_string, new_name);

    auto serialized = JsonValue(move(obj)).serialized();
    TRY(write_file_atomic(dir, "meta.json", serialized.bytes()));

    for (auto& ident : m_identities) {
        if (ident.id == id) {
            ident.name = move(new_name);
            break;
        }
    }

    return {};
}

bool IdentityStore::is_encrypted(String const& id) const
{
    for (auto const& ident : m_identities) {
        if (ident.id == id)
            return ident.encrypted;
    }
    return false;
}

ErrorOr<ByteBuffer> IdentityStore::decrypt_private_key(String const& id, String const& passphrase)
{
    auto dir = ByteString::formatted("{}/{}", storage_directory(), id);

    auto ciphertext_file = TRY(Core::File::open(ByteString::formatted("{}/private", dir), Core::File::OpenMode::Read));
    auto ciphertext = TRY(ciphertext_file->read_until_eof());

    auto salt_file = TRY(Core::File::open(ByteString::formatted("{}/private.salt", dir), Core::File::OpenMode::Read));
    auto salt = TRY(salt_file->read_until_eof());

    auto iv_file = TRY(Core::File::open(ByteString::formatted("{}/private.iv", dir), Core::File::OpenMode::Read));
    auto iv = TRY(iv_file->read_until_eof());

    auto tag_file = TRY(Core::File::open(ByteString::formatted("{}/private.tag", dir), Core::File::OpenMode::Read));
    auto tag = TRY(tag_file->read_until_eof());

    auto key = TRY(derive_key(passphrase.bytes(), salt.bytes()));

    Crypto::Cipher::AESGCMCipher cipher(key.bytes());
    auto plaintext = cipher.decrypt(ciphertext.bytes(), iv.bytes(), {}, tag.bytes());
    if (plaintext.is_error())
        return Error::from_string_literal("Wrong passphrase");

    return plaintext.release_value();
}

String IdentityStore::fingerprint(ReadonlyBytes public_key)
{
    auto digest = Crypto::Hash::SHA256::hash(public_key.data(), public_key.size());
    auto all_bytes = digest.bytes();
    ReadonlyBytes hash_bytes { all_bytes.data(), min<size_t>(16u, all_bytes.size()) };
    auto hex = encode_hex(hash_bytes);
    StringBuilder sb;
    for (size_t i = 0; i < hex.length(); i += 2) {
        if (i != 0)
            sb.append(':');
        sb.append(hex.substring_view(i, 2));
    }
    return MUST(sb.to_string());
}

JsonArray IdentityStore::serialize() const
{
    JsonArray arr;
    for (auto const& ident : m_identities) {
        JsonObject obj;
        obj.set("id"_string, ident.id);
        obj.set("name"_string, ident.name);
        obj.set("created_at"_string, JsonValue(ident.created_at));
        obj.set("fingerprint"_string, fingerprint(ident.public_key));
        obj.set("encrypted"_string, JsonValue(ident.encrypted));
        MUST(arr.append(move(obj)));
    }
    return arr;
}

}
