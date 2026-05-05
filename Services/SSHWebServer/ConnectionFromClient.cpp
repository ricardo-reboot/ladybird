/*
 * Copyright (c) 2026, the sshweb-browser developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Format.h>
#include <LibCore/EventLoop.h>
#include <LibSSHWeb/URL.h>
#include <Services/SSHWebServer/Connection.h>
#include <Services/SSHWebServer/ConnectionFromClient.h>
#include <Services/SSHWebServer/KnownHosts.h>

namespace SSHWebService {

ConnectionFromClient::ConnectionFromClient(NonnullOwnPtr<IPC::Transport> transport)
    : IPC::ConnectionFromClient<SSHWebClientEndpoint, SSHWebServerEndpoint>(*this, move(transport), 1)
{
}

ConnectionFromClient::~ConnectionFromClient() = default;

void ConnectionFromClient::die()
{
    // Plan 5 has a single client at a time. When it disconnects, exit.
    // Plan 5b will track multiple connections and only remove the dead one.
    Core::EventLoop::current().quit(0);
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
    // Plan 5 simplification: we don't yet support spawning sub-clients for
    // additional tabs. Plan 5b will implement this by handing back a
    // freshly-paired transport handle.
    return IPC::TransportHandle {};
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
    auto connection = connection_or_error.release_value();

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
