/*
 * Copyright (c) 2026, the sshweb-browser developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/StringView.h>

namespace SSHWeb {

// The SSH-Web protocol version this build implements.
// Tracks the spec at sshttpd/spec/SSH-WEB-SPEC.md.
constexpr StringView protocol_version = "ssh-web/0.1"sv;

// The sshweb-browser implementation version.
// Bumped on every release; tracks protocol_version's major/minor.
constexpr StringView implementation_version = "0.0.1"sv;

}
