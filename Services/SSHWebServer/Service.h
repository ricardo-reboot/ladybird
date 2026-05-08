/*
 * Copyright (c) 2026, the sshweb-browser developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Error.h>
#include <LibMain/Main.h>

namespace SSHWebService {

// Boots libssh2, runs the requested mode, and shuts libssh2 down.
// In Plan 3 the only supported mode is one-shot client (driven by --connect / --command flags).
// Plan 5 will add a long-lived IPC service mode here.
ErrorOr<int> run(Main::Arguments arguments);

}
