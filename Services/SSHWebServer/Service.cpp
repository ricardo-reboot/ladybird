/*
 * Copyright (c) 2026, the sshweb-browser developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Format.h>
#include <LibSSHWeb/Version.h>
#include <Services/SSHWebServer/Service.h>
#include <libssh2.h>

namespace SSHWebService {

ErrorOr<int> run(Main::Arguments arguments)
{
    (void)arguments;

    if (auto rc = libssh2_init(0); rc != 0)
        return Error::from_string_literal("libssh2_init failed");

    outln("SSHWebServer starting (protocol={}, impl={}, libssh2={})",
        SSHWeb::protocol_version,
        SSHWeb::implementation_version,
        libssh2_version(0));

    libssh2_exit();
    return 0;
}

}
