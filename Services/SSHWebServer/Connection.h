/*
 * Copyright (c) 2026, the sshweb-browser developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/ByteBuffer.h>
#include <AK/Error.h>
#include <AK/Function.h>
#include <AK/NonnullOwnPtr.h>
#include <AK/OwnPtr.h>
#include <AK/String.h>
#include <Services/SSHWebServer/KnownHosts.h>

struct _LIBSSH2_SESSION;
typedef struct _LIBSSH2_SESSION LIBSSH2_SESSION;

namespace SSHWeb {

// Caller responds to a TOFU prompt by returning whether to trust the new host key.
// `key` is the fingerprint we just received over the wire.
using TOFUDecisionCallback = Function<bool(StringView host, u16 port, HostKey const& key)>;

class Connection {
public:
    static ErrorOr<NonnullOwnPtr<Connection>> open(
        StringView host,
        u16 port,
        KnownHosts& known_hosts,
        TOFUDecisionCallback tofu_decision);

    ~Connection();

    // Run a single command (e.g. "capabilities", "receive-pack /") and read the entire response.
    ErrorOr<ByteBuffer> execute_command(StringView command);

    HostKey const& host_key() const { return m_host_key; }

private:
    Connection(int socket_fd, LIBSSH2_SESSION* session, HostKey host_key);

    int m_socket_fd { -1 };
    LIBSSH2_SESSION* m_session { nullptr };
    HostKey m_host_key;
};

}
