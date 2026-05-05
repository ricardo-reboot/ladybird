/*
 * Copyright (c) 2026, the sshweb-browser developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Format.h>
#include <LibMain/Main.h>
#include <LibSSHWeb/Version.h>
#include <libssh2.h>

ErrorOr<int> ladybird_main(Main::Arguments arguments)
{
    (void)arguments;

    if (auto rc = libssh2_init(0); rc != 0)
        return Error::from_string_literal("libssh2_init failed");

    outln("SSHWebServer starting (protocol={}, impl={}, libssh2={})",
        SSHWeb::protocol_version,
        SSHWeb::implementation_version,
        libssh2_version(0));

    // Plan 1 only verifies the process boots and links libssh2.
    // The event loop and IPC server bind land in Plan 5.
    libssh2_exit();
    return 0;
}
