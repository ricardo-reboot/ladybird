/*
 * Copyright (c) 2026, the sshweb-browser developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Format.h>
#include <LibCore/ArgsParser.h>
#include <LibSSHWeb/Version.h>
#include <Services/SSHWebServer/ClientMode.h>
#include <Services/SSHWebServer/Identity.h>
#include <Services/SSHWebServer/Service.h>
#include <libssh2.h>

namespace SSHWebService {

ErrorOr<int> run(Main::Arguments arguments)
{
    StringView connect_url;
    StringView command;
    StringView identity_label;
    StringView generate_identity;
    bool accept_host_key = false;
    bool list_identities = false;

    Core::ArgsParser parser;
    parser.set_general_help("SSH-Web client/service");
    parser.add_option(connect_url, "Connect to an ssh-web:// URL (one-shot client mode)", "connect", 0, "url");
    parser.add_option(command, "Command to run after connecting", "command", 0, "cmd");
    parser.add_option(identity_label, "Use this identity (publickey auth)", "identity", 0, "label");
    parser.add_option(generate_identity, "Generate a new ed25519 identity with this label", "generate-identity", 0, "label");
    parser.add_option(list_identities, "List all known identities and exit", "list-identities", 0);
    parser.add_option(accept_host_key, "Auto-accept unknown host keys (scripted/test use only)", "accept-host-key", 0);
    parser.parse(arguments);

    if (auto rc = libssh2_init(0); rc != 0)
        return Error::from_string_literal("libssh2_init failed");

    int exit_code = 0;
    if (!generate_identity.is_empty()) {
        auto store = SSHWeb::IdentityStore::with_default_root();
        if (store.is_error()) { warnln("error: {}", store.error()); libssh2_exit(); return 1; }
        auto generated = store.value().generate(generate_identity);
        if (generated.is_error()) { warnln("error: {}", generated.error()); libssh2_exit(); return 1; }
        outln("Generated identity '{}' at {}", generated.value().label, generated.value().private_key_path);
    } else if (list_identities) {
        auto store = SSHWeb::IdentityStore::with_default_root();
        if (store.is_error()) { warnln("error: {}", store.error()); libssh2_exit(); return 1; }
        auto labels = store.value().list_labels();
        if (labels.is_error()) { warnln("error: {}", labels.error()); libssh2_exit(); return 1; }
        if (labels.value().is_empty()) {
            outln("No identities. Generate one with --generate-identity <label>.");
        } else {
            outln("Identities (in {}):", store.value().root_dir());
            for (auto const& label : labels.value())
                outln("  {}", label);
        }
    } else if (connect_url.is_empty() && command.is_empty()) {
        outln("SSHWebServer starting (protocol={}, impl={}, libssh2={})",
            SSHWeb::protocol_version,
            SSHWeb::implementation_version,
            libssh2_version(0));
    } else if (!connect_url.is_empty() && !command.is_empty()) {
        ClientModeOptions opts {
            .url = connect_url,
            .command = command,
            .identity_label = identity_label,
            .accept_host_key = accept_host_key,
        };
        auto result = run_client_mode(opts);
        if (result.is_error()) {
            warnln("error: {}", result.error());
            exit_code = 1;
        } else {
            exit_code = result.value();
        }
    } else {
        warnln("error: --connect and --command must be used together");
        exit_code = 2;
    }

    libssh2_exit();
    return exit_code;
}

}
