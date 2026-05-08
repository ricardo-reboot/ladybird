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
#include <LibCrypto/Curves/EdwardsCurve.h>
#include <LibCrypto/Hash/SHA2.h>
#include <LibWebView/IdentityStore.h>

namespace WebView {

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
        });
    }

    return {};
}

ErrorOr<String> IdentityStore::create(String name)
{
    TRY(ensure_storage_directory());

    auto id = generate_random_uuid();
    auto dir = ByteString::formatted("{}/{}", storage_directory(), id);
    TRY(Core::Directory::create(dir, Core::Directory::CreateDirectories::Yes));

    // Generate Ed25519 keypair.
    Crypto::Curves::Ed25519 ed25519;
    auto private_seed = TRY(ed25519.generate_private_key());
    auto public_key_bytes = TRY(ed25519.generate_public_key(private_seed));

    // Write private seed atomically.
    auto priv_tmp = ByteString::formatted("{}/private.tmp", dir);
    auto priv_final = ByteString::formatted("{}/private", dir);
    {
        auto f = TRY(Core::File::open(priv_tmp, Core::File::OpenMode::Write, 0600));
        TRY(f->write_until_depleted(private_seed));
    }
    TRY(Core::System::rename(priv_tmp, priv_final));

    // Write public key atomically.
    auto pub_tmp = ByteString::formatted("{}/public.tmp", dir);
    auto pub_final = ByteString::formatted("{}/public", dir);
    {
        auto f = TRY(Core::File::open(pub_tmp, Core::File::OpenMode::Write, 0644));
        TRY(f->write_until_depleted(public_key_bytes));
    }
    TRY(Core::System::rename(pub_tmp, pub_final));

    // Build meta.json.
    auto now = AK::UnixDateTime::now().seconds_since_epoch();
    JsonObject meta;
    meta.set("id"_string, id);
    meta.set("name"_string, name);
    meta.set("created_at"_string, JsonValue(now));

    auto meta_json = JsonValue(move(meta)).serialized();
    auto meta_tmp = ByteString::formatted("{}/meta.json.tmp", dir);
    auto meta_final = ByteString::formatted("{}/meta.json", dir);
    {
        auto f = TRY(Core::File::open(meta_tmp, Core::File::OpenMode::Write, 0644));
        TRY(f->write_until_depleted(meta_json.bytes()));
    }
    TRY(Core::System::rename(meta_tmp, meta_final));

    m_identities.append(SSHWebIdentity {
        .id = id,
        .name = move(name),
        .created_at = now,
        .public_key = move(public_key_bytes),
    });

    return id;
}

ErrorOr<void> IdentityStore::remove(String const& id)
{
    auto dir = ByteString::formatted("{}/{}", storage_directory(), id);

    // Remove files within the directory first, then the directory itself.
    for (auto const* filename : { "private", "public", "meta.json" }) {
        auto path = ByteString::formatted("{}/{}", dir, filename);
        auto result = Core::System::unlink(path);
        // Ignore ENOENT — file may not exist.
        if (result.is_error() && result.error().code() != ENOENT)
            return result.release_error();
    }
    TRY(Core::System::rmdir(dir));

    m_identities.remove_all_matching([&id](SSHWebIdentity const& ident) {
        return ident.id == id;
    });

    return {};
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
        MUST(arr.append(move(obj)));
    }
    return arr;
}

}
