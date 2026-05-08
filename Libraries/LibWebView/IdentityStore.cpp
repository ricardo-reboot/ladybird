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
#include <AK/Random.h>
#include <AK/StringBuilder.h>
#include <AK/Time.h>
#include <LibCore/DirIterator.h>
#include <LibCore/Directory.h>
#include <LibCore/File.h>
#include <LibCore/Process.h>
#include <LibCore/StandardPaths.h>
#include <LibCore/System.h>
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

ErrorOr<ByteBuffer> IdentityStore::parse_openssh_ed25519_pubkey(StringView file_content)
{
    // Format: "ssh-ed25519 <base64> <comment>\n"
    auto parts = file_content.split_view(' ');
    if (parts.size() < 2 || parts[0] != "ssh-ed25519"sv)
        return Error::from_string_literal("Not an ed25519 public key");

    auto decoded = TRY(decode_base64(parts[1]));
    // Wire format: [uint32 type_len]["ssh-ed25519"][uint32 key_len][32-byte key]
    // = 4 + 11 + 4 + 32 = 51 bytes minimum
    if (decoded.size() < 51)
        return Error::from_string_literal("Public key blob too short");

    auto key_offset = 4 + 11 + 4; // skip type-length-prefix + "ssh-ed25519" + key-length-prefix
    auto key_bytes = decoded.bytes().slice(key_offset, 32);
    auto result = TRY(ByteBuffer::create_uninitialized(32));
    memcpy(result.data(), key_bytes.data(), 32);
    return result;
}

ErrorOr<void> IdentityStore::load()
{
    m_identities.clear();

    TRY(ensure_storage_directory());

    Core::DirIterator it(storage_directory(), Core::DirIterator::SkipParentAndBaseDir);
    while (it.has_next()) {
        auto entry_name = it.next_path();
        auto meta_path = ByteString::formatted("{}/{}/meta.json", storage_directory(), entry_name);
        auto pub_path  = ByteString::formatted("{}/{}/ed25519.pub", storage_directory(), entry_name);

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
        auto pub_content = TRY(pub_file.value()->read_until_eof());
        auto pub_content_str = StringView(pub_content.bytes());
        auto pub_key = parse_openssh_ed25519_pubkey(pub_content_str);
        if (pub_key.is_error())
            continue;

        auto openssh_line = MUST(String::from_utf8(pub_content_str.trim_whitespace()));

        m_identities.append(SSHWebIdentity {
            .id = id.release_value(),
            .name = name.release_value(),
            .created_at = *created_at,
            .public_key = pub_key.release_value(),
            .public_key_openssh = move(openssh_line),
            .encrypted = is_encrypted,
        });
    }

    return {};
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

    auto key_path = ByteString::formatted("{}/ed25519", dir);
    bool is_encrypted = !passphrase.is_empty();

    Vector<ByteString> args;
    args.append("-t");
    args.append("ed25519");
    args.append("-N");
    args.append(passphrase.to_byte_string());
    args.append("-C");
    args.append(name.to_byte_string());
    args.append("-f");
    args.append(key_path);

    auto process = TRY(Core::Process::spawn("/usr/bin/ssh-keygen"sv, ReadonlySpan<ByteString>(args)));
    auto exit_code = TRY(process.wait_for_termination());
    if (exit_code != 0) {
        (void)Core::System::rmdir(dir);
        return Error::from_string_literal("ssh-keygen failed");
    }

    // Read the generated public key to extract raw bytes for fingerprinting.
    auto pub_path = ByteString::formatted("{}/ed25519.pub", dir);
    auto pub_file = TRY(Core::File::open(pub_path, Core::File::OpenMode::Read));
    auto pub_content = TRY(pub_file->read_until_eof());
    auto pub_content_str = StringView(pub_content.bytes());
    auto public_key_bytes = TRY(parse_openssh_ed25519_pubkey(pub_content_str));
    auto openssh_line = TRY(String::from_utf8(pub_content_str.trim_whitespace()));

    // Set restrictive permissions on private key.
    TRY(Core::System::chmod(key_path, 0600));

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
        .public_key_openssh = move(openssh_line),
        .encrypted = is_encrypted,
    });

    return id;
}

ErrorOr<void> IdentityStore::remove(String const& id)
{
    auto dir = ByteString::formatted("{}/{}", storage_directory(), id);

    for (auto const* filename : { "ed25519", "ed25519.pub", "meta.json" }) {
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

ErrorOr<bool> IdentityStore::verify_passphrase(String const& id, String const& passphrase)
{
    auto key_path = ByteString::formatted("{}/{}/ed25519", storage_directory(), id);

    Vector<ByteString> args;
    args.append("-y");
    args.append("-f");
    args.append(key_path);
    args.append("-P");
    args.append(passphrase.to_byte_string());

    auto process = TRY(Core::Process::spawn("/usr/bin/ssh-keygen"sv, ReadonlySpan<ByteString>(args)));
    auto exit_code = TRY(process.wait_for_termination());
    return exit_code == 0;
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
        obj.set("public_key_openssh"_string, ident.public_key_openssh);
        obj.set("encrypted"_string, JsonValue(ident.encrypted));
        MUST(arr.append(move(obj)));
    }
    return arr;
}

}
