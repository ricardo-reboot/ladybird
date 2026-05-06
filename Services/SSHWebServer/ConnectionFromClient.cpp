/*
 * Copyright (c) 2026, the sshweb-browser developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Format.h>
#include <AK/Vector.h>
#include <LibIPC/TransportHandle.h>
#include <LibSSHWeb/URL.h>
#include <Services/SSHWebServer/Connection.h>
#include <Services/SSHWebServer/ConnectionFromClient.h>
#include <Services/SSHWebServer/KnownHosts.h>

namespace SSHWebService {

// Static registry keeping per-WebContent ConnectionFromClient instances alive
// for the lifetime of the SSHWebServer process. Mirrors RequestServer's
// pattern. Each entry corresponds to one connected WebContent process.
static Vector<NonnullRefPtr<ConnectionFromClient>>& secondary_connections()
{
    static Vector<NonnullRefPtr<ConnectionFromClient>> s_connections;
    return s_connections;
}

ConnectionFromClient::ConnectionFromClient(NonnullOwnPtr<IPC::Transport> transport)
    : IPC::ConnectionFromClient<SSHWebClientEndpoint, SSHWebServerEndpoint>(*this, move(transport), 1)
{
}

ConnectionFromClient::~ConnectionFromClient() = default;

void ConnectionFromClient::die()
{
    // Plan 5b: clear the SSH pool so all connections for this client are
    // closed. Do NOT quit the event loop — other clients may still be
    // connected. The process exits naturally when the last client disconnects.
    m_ssh_pool.clear();
}

Messages::SSHWebServer::InitTransportResponse ConnectionFromClient::init_transport([[maybe_unused]] int peer_pid)
{
#ifdef AK_OS_WINDOWS
    transport().set_peer_pid(peer_pid);
#endif
    return getpid();
}

Messages::SSHWebServer::ConnectNewClientResponse ConnectionFromClient::connect_new_client()
{
    // Plan 5b: hand the caller (a WebContent process) a freshly-paired
    // transport. The local end is held by a new ConnectionFromClient kept
    // alive in secondary_connections(); the remote end is returned over IPC.
    auto paired_or_error = IPC::Transport::create_paired();
    if (paired_or_error.is_error()) {
        dbgln("SSHWebServer::connect_new_client: create_paired failed: {}", paired_or_error.error());
        return IPC::TransportHandle {};
    }
    auto paired = paired_or_error.release_value();
    auto remote_handle = move(paired.remote_handle);
    auto new_client = adopt_ref(*new ConnectionFromClient(move(paired.local)));
    secondary_connections().append(new_client);
    return remote_handle;
}

Messages::SSHWebServer::StopRequestResponse ConnectionFromClient::stop_request(u64)
{
    // Plan 5 simplification: requests are synchronous from the service's
    // point of view, so by the time the client could send stop_request the
    // request is already done. Always return success.
    return true;
}

void ConnectionFromClient::tofu_decision(u64, bool)
{
    // Plan 5 simplification: we auto-accept on the server side too, so
    // we don't currently send tofu_prompt and don't expect to receive
    // tofu_decision. Plan 7 will implement the async dance.
}

void ConnectionFromClient::start_request(u64 request_id, URL::URL url, ByteString command)
{
    // Pull host/port out of the LibURL URL.
    auto sshweb_url_or_error = SSHWeb::URL::parse(url.serialize());
    if (sshweb_url_or_error.is_error()) {
        async_request_finished(request_id, ByteString::formatted("invalid ssh-web URL: {}", sshweb_url_or_error.error()));
        return;
    }
    auto sshweb_url = sshweb_url_or_error.release_value();

    // Open known_hosts at the default location.
    auto known_hosts_or_error = SSHWeb::KnownHosts::with_default_root();
    if (known_hosts_or_error.is_error()) {
        async_request_finished(request_id, ByteString::formatted("known_hosts: {}", known_hosts_or_error.error()));
        return;
    }
    auto known_hosts = known_hosts_or_error.release_value();

    // Plan 5b: pool lookup — reuse an open connection if one exists for this host:port.
    auto key = pool_key(sshweb_url.host.bytes_as_string_view(), sshweb_url.port);
    SSHWeb::Connection* connection = nullptr;
    if (auto it = m_ssh_pool.find(key); it != m_ssh_pool.end()) {
        connection = it->value.ptr();
    } else {
        // Plan 5: auto-accept TOFU. Plan 7 will route through the UI process.
        SSHWeb::TOFUDecisionCallback decision = [](StringView, u16, SSHWeb::HostKey const&) -> bool {
            return true;
        };

        auto connection_or_error = SSHWeb::Connection::open(
            sshweb_url.host.bytes_as_string_view(),
            sshweb_url.port,
            known_hosts,
            move(decision),
            {});
        if (connection_or_error.is_error()) {
            async_request_finished(request_id, ByteString::formatted("ssh connect: {}", connection_or_error.error()));
            return;
        }
        m_ssh_pool.set(key, connection_or_error.release_value());
        connection = m_ssh_pool.find(key)->value.ptr();
    }

    auto bytes_or_error = connection->execute_command(command.view());
    if (bytes_or_error.is_error()) {
        async_request_finished(request_id, ByteString::formatted("execute: {}", bytes_or_error.error()));
        return;
    }

    auto bytes = bytes_or_error.release_value();
    async_request_chunk(request_id, bytes.bytes(), true);
    async_request_finished(request_id, ByteString {});
}

}
