/*
 * Copyright (c) 2026, the sshweb-browser developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Error.h>
#include <AK/StringView.h>

namespace SSHWebService {

struct ClientModeOptions {
    StringView url;
    StringView command;
    bool accept_host_key { false };
};

ErrorOr<int> run_client_mode(ClientModeOptions const& options);

}
