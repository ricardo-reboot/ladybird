/*
 * Copyright (c) 2026, the sshweb-browser developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Base64.h>
#include <AK/ByteString.h>
#include <Services/SSHWebServer/Connection.h>
#include <libssh2.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace SSHWeb {

Connection::Connection(int socket_fd, LIBSSH2_SESSION* session, HostKey host_key)
    : m_socket_fd(socket_fd)
    , m_session(session)
    , m_host_key(move(host_key))
{
}

Connection::~Connection()
{
    if (m_session) {
        libssh2_session_disconnect(m_session, "shutdown");
        libssh2_session_free(m_session);
    }
    if (m_socket_fd >= 0)
        close(m_socket_fd);
}

static ErrorOr<int> tcp_connect(StringView host, u16 port)
{
    addrinfo hints {};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    auto host_byte_string = TRY(String::from_utf8(host)).to_byte_string();
    auto port_byte_string = ByteString::formatted("{}", port);

    addrinfo* result = nullptr;
    if (getaddrinfo(host_byte_string.characters(), port_byte_string.characters(), &hints, &result) != 0)
        return Error::from_string_literal("getaddrinfo failed");

    int fd = -1;
    for (auto* ai = result; ai != nullptr; ai = ai->ai_next) {
        fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0)
            continue;
        if (connect(fd, ai->ai_addr, ai->ai_addrlen) == 0)
            break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(result);
    if (fd < 0)
        return Error::from_string_literal("TCP connect failed");
    return fd;
}

static String fingerprint_sha256(LIBSSH2_SESSION* session)
{
    auto* fp = libssh2_hostkey_hash(session, LIBSSH2_HOSTKEY_HASH_SHA256);
    if (!fp)
        return ""_string;
    ReadonlyBytes bytes { reinterpret_cast<u8 const*>(fp), 32 };
    return MUST(encode_base64(bytes));
}

ErrorOr<NonnullOwnPtr<Connection>> Connection::open(
    StringView host,
    u16 port,
    KnownHosts& known_hosts,
    TOFUDecisionCallback tofu_decision)
{
    int sock = TRY(tcp_connect(host, port));
    auto* session = libssh2_session_init();
    if (!session) {
        close(sock);
        return Error::from_string_literal("libssh2_session_init failed");
    }

    if (libssh2_session_handshake(session, sock) != 0) {
        libssh2_session_free(session);
        close(sock);
        return Error::from_string_literal("SSH handshake failed");
    }

    int key_type_raw = 0;
    size_t key_len = 0;
    auto* key_bytes = libssh2_session_hostkey(session, &key_len, &key_type_raw);
    (void)key_bytes;
    StringView type_view;
    switch (key_type_raw) {
    case LIBSSH2_HOSTKEY_TYPE_ED25519: type_view = "ed25519"sv; break;
    case LIBSSH2_HOSTKEY_TYPE_ECDSA_256: type_view = "ecdsa-sha2-nistp256"sv; break;
    case LIBSSH2_HOSTKEY_TYPE_ECDSA_384: type_view = "ecdsa-sha2-nistp384"sv; break;
    case LIBSSH2_HOSTKEY_TYPE_ECDSA_521: type_view = "ecdsa-sha2-nistp521"sv; break;
    case LIBSSH2_HOSTKEY_TYPE_RSA: type_view = "rsa"sv; break;
    default: type_view = "unknown"sv; break;
    }

    HostKey received {
        .type = TRY(String::from_utf8(type_view)),
        .fingerprint_sha256 = fingerprint_sha256(session),
    };

    auto known = TRY(known_hosts.lookup(host, port));
    if (known.has_value()) {
        if (known->key.fingerprint_sha256 != received.fingerprint_sha256) {
            libssh2_session_disconnect(session, "host key mismatch");
            libssh2_session_free(session);
            close(sock);
            return Error::from_string_literal("Host key mismatch");
        }
    } else {
        if (!tofu_decision(host, port, received)) {
            libssh2_session_disconnect(session, "user declined TOFU");
            libssh2_session_free(session);
            close(sock);
            return Error::from_string_literal("Host key not trusted");
        }
        TRY(known_hosts.record(host, port, received));
    }

    // Anonymous mode: skip authentication entirely.
    // Plan 4 will add publickey auth here.

    return adopt_own(*new Connection { sock, session, move(received) });
}

ErrorOr<ByteBuffer> Connection::execute_command(StringView command)
{
    auto* channel = libssh2_channel_open_session(m_session);
    if (!channel)
        return Error::from_string_literal("libssh2_channel_open_session failed");

    auto command_z = TRY(String::from_utf8(command)).to_byte_string();
    if (libssh2_channel_exec(channel, command_z.characters()) != 0) {
        libssh2_channel_free(channel);
        return Error::from_string_literal("libssh2_channel_exec failed");
    }

    ByteBuffer buffer;
    char read_buf[4096];
    for (;;) {
        ssize_t n = libssh2_channel_read(channel, read_buf, sizeof(read_buf));
        if (n == 0)
            break;
        if (n < 0) {
            libssh2_channel_free(channel);
            return Error::from_string_literal("libssh2_channel_read failed");
        }
        TRY(buffer.try_append(reinterpret_cast<u8 const*>(read_buf), static_cast<size_t>(n)));
    }
    libssh2_channel_close(channel);
    libssh2_channel_free(channel);
    return buffer;
}

}
