/*
 * Copyright (c) 2026, the sshweb-browser developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/ByteString.h>
#include <LibCore/DirIterator.h>
#include <LibCore/Process.h>
#include <LibCore/StandardPaths.h>
#include <LibCore/System.h>
#include <LibFileSystem/FileSystem.h>
#include <Services/SSHWebServer/Identity.h>

namespace SSHWeb {

IdentityStore::IdentityStore(String root_dir)
    : m_root_dir(move(root_dir))
{
}

ErrorOr<IdentityStore> IdentityStore::with_default_root()
{
    auto config = Core::StandardPaths::config_directory();
    auto sshweb = TRY(String::formatted("{}/sshweb", config));
    if (!FileSystem::exists(sshweb))
        TRY(Core::System::mkdir(sshweb, 0700));
    auto root = TRY(String::formatted("{}/identities", sshweb));
    if (!FileSystem::exists(root))
        TRY(Core::System::mkdir(root, 0700));
    return IdentityStore { move(root) };
}

ErrorOr<Vector<String>> IdentityStore::list_labels() const
{
    Vector<String> labels;
    Core::DirIterator it { m_root_dir.to_byte_string(), Core::DirIterator::Flags::SkipDots };
    while (it.has_next()) {
        auto entry = it.next_path();
        auto full = TRY(String::formatted("{}/{}", m_root_dir, entry));
        if (FileSystem::is_directory(full))
            labels.append(TRY(String::from_utf8(entry.view())));
    }
    return labels;
}

ErrorOr<Identity> IdentityStore::get(StringView label) const
{
    auto dir = TRY(String::formatted("{}/{}", m_root_dir, label));
    if (!FileSystem::is_directory(dir))
        return Error::from_string_literal("Identity not found");
    auto priv = TRY(String::formatted("{}/ed25519", dir));
    auto pub = TRY(String::formatted("{}/ed25519.pub", dir));
    if (!FileSystem::exists(priv) || !FileSystem::exists(pub))
        return Error::from_string_literal("Identity directory missing key files");
    return Identity {
        .label = TRY(String::from_utf8(label)),
        .private_key_path = priv,
        .public_key_path = pub,
    };
}

ErrorOr<Identity> IdentityStore::generate(StringView label)
{
    auto dir = TRY(String::formatted("{}/{}", m_root_dir, label));
    if (FileSystem::exists(dir))
        return Error::from_string_literal("Identity with that label already exists");
    TRY(Core::System::mkdir(dir, 0700));
    auto priv = TRY(String::formatted("{}/ed25519", dir));
    auto comment = TRY(String::formatted("sshweb:{}", label));

    Vector<ByteString> args;
    args.append("-t");
    args.append("ed25519");
    args.append("-N");
    args.append("");
    args.append("-C");
    args.append(comment.to_byte_string());
    args.append("-f");
    args.append(priv.to_byte_string());

    auto process = TRY(Core::Process::spawn("/usr/bin/ssh-keygen"sv, ReadonlySpan<ByteString>(args)));
    auto exit_code = TRY(process.wait_for_termination());
    if (exit_code != 0)
        return Error::from_string_literal("ssh-keygen exited non-zero");

    return get(label);
}

}
