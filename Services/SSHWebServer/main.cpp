/*
 * Copyright (c) 2026, the sshweb-browser developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibMain/Main.h>
#include <Services/SSHWebServer/Service.h>

ErrorOr<int> ladybird_main(Main::Arguments arguments)
{
    return SSHWebService::run(move(arguments));
}
