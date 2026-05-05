/*
 * Copyright (c) 2026, the sshweb-browser developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/JsonObject.h>
#include <AK/JsonValue.h>
#include <AK/Time.h>
#include <LibCore/File.h>
#include <LibCore/StandardPaths.h>
#include <LibCore/System.h>
#include <LibFileSystem/FileSystem.h>
#include <Services/SSHWebServer/KnownHosts.h>

namespace SSHWeb {

KnownHosts::KnownHosts(String root_dir)
    : m_root_dir(move(root_dir))
{
}

ErrorOr<KnownHosts> KnownHosts::with_default_root()
{
    auto config = Core::StandardPaths::config_directory();
    auto root = TRY(String::formatted("{}/sshweb/known_hosts", config));
    if (!FileSystem::exists(root))
        TRY(Core::System::mkdir(root, 0700));
    return KnownHosts { move(root) };
}

ErrorOr<String> KnownHosts::path_for(StringView host, u16 port) const
{
    return String::formatted("{}/{}_{}.json", m_root_dir, host, port);
}

ErrorOr<Optional<KnownHostEntry>> KnownHosts::lookup(StringView host, u16 port) const
{
    auto path = TRY(path_for(host, port));
    if (!FileSystem::exists(path))
        return Optional<KnownHostEntry> {};

    auto file = TRY(Core::File::open(path, Core::File::OpenMode::Read));
    auto bytes = TRY(file->read_until_eof());
    auto json = TRY(JsonValue::from_string(StringView { bytes }));
    if (!json.is_object())
        return Error::from_string_literal("known_hosts entry is not a JSON object");
    auto const& obj = json.as_object();

    auto type = obj.get("type"sv);
    auto fp = obj.get("fingerprint_sha256"sv);
    auto first_seen = obj.get("first_seen"sv);
    if (!type.has_value() || !type->is_string())
        return Error::from_string_literal("known_hosts entry missing 'type'");
    if (!fp.has_value() || !fp->is_string())
        return Error::from_string_literal("known_hosts entry missing 'fingerprint_sha256'");

    return KnownHostEntry {
        .key = HostKey {
            .type = type->as_string(),
            .fingerprint_sha256 = fp->as_string(),
        },
        .first_seen = first_seen.has_value() && first_seen->is_string() ? first_seen->as_string() : ""_string,
    };
}

ErrorOr<void> KnownHosts::record(StringView host, u16 port, HostKey const& key)
{
    JsonObject obj;
    obj.set("type"sv, key.type);
    obj.set("fingerprint_sha256"sv, key.fingerprint_sha256);
    auto now = TRY(String::formatted("{}", AK::UnixDateTime::now().milliseconds_since_epoch()));
    obj.set("first_seen"sv, now);

    auto path = TRY(path_for(host, port));
    auto file = TRY(Core::File::open(path, Core::File::OpenMode::Write | Core::File::OpenMode::Truncate));
    auto serialized = obj.serialized();
    TRY(file->write_until_depleted(serialized.bytes()));
    return {};
}

ErrorOr<void> KnownHosts::forget(StringView host, u16 port)
{
    auto path = TRY(path_for(host, port));
    if (FileSystem::exists(path))
        TRY(Core::System::unlink(path));
    return {};
}

}
