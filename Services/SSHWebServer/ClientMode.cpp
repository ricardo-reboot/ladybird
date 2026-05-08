/*
 * Copyright (c) 2026, the sshweb-browser developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Format.h>
#include <LibSSHWeb/Manifest.h>
#include <LibSSHWeb/URL.h>
#include <Services/SSHWebServer/ClientMode.h>
#include <Services/SSHWebServer/Connection.h>
#include <Services/SSHWebServer/Identity.h>
#include <Services/SSHWebServer/KnownHosts.h>
#include <stdio.h>

namespace SSHWebService {

static bool prompt_tofu(StringView host, u16 port, SSHWeb::HostKey const& key)
{
    outln("The authenticity of host '{}:{}' can't be established.", host, port);
    outln("{} key fingerprint is SHA256:{}", key.type, key.fingerprint_sha256);
    out("Connect and add to known_hosts? [y/N]: ");
    fflush(stdout);

    char buf[16];
    auto* line = fgets(buf, sizeof(buf), stdin);
    if (!line)
        return false;
    return line[0] == 'y' || line[0] == 'Y';
}

ErrorOr<int> run_client_mode(ClientModeOptions const& options)
{
    auto url = TRY(SSHWeb::URL::parse(options.url));
    auto known_hosts = TRY(SSHWeb::KnownHosts::with_default_root());

    SSHWeb::TOFUDecisionCallback decision;
    if (options.accept_host_key) {
        decision = [](StringView, u16, SSHWeb::HostKey const&) { return true; };
    } else {
        decision = [](StringView host, u16 port, SSHWeb::HostKey const& key) {
            return prompt_tofu(host, port, key);
        };
    }

    Optional<SSHWeb::Identity> identity;
    if (!options.identity_label.is_empty()) {
        auto store = TRY(SSHWeb::IdentityStore::with_default_root());
        identity = TRY(store.get(options.identity_label));
        outln("Using identity '{}' (key: {})", identity->label, identity->private_key_path);
    }

    auto connection = TRY(SSHWeb::Connection::open(url.host, url.port, known_hosts, move(decision), move(identity)));
    auto response = TRY(connection->execute_command(options.command));

    if (options.command.starts_with("capabilities"sv)) {
        auto parsed = SSHWeb::CapabilitiesManifest::parse(StringView { response });
        if (parsed.is_error()) {
            outln("Received {} bytes (failed to parse as manifest: {})", response.size(), parsed.error());
            outln("Raw response:");
            out("{}", StringView { response });
        } else {
            outln("Capabilities manifest:");
            outln("  protocol: {}", parsed.value().protocol);
            outln("  site: {} ({})", parsed.value().site.name, parsed.value().site.host);
            outln("  commands: {}", parsed.value().commands.size());
            for (auto const& cmd : parsed.value().commands)
                outln("    - {}", cmd.name);
        }
    } else {
        out("{}", StringView { response });
    }

    return 0;
}

}
