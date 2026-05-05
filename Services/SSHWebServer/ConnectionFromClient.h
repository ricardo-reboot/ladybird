/*
 * Copyright (c) 2026, the sshweb-browser developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/HashMap.h>
#include <LibIPC/ConnectionFromClient.h>
#include <Services/SSHWebServer/SSHWebClientEndpoint.h>
#include <Services/SSHWebServer/SSHWebServerEndpoint.h>

namespace SSHWebService {

// One instance per connected WebContent (or test client). Implements the
// SSHWebServer endpoint and pushes responses back via the SSHWebClient
// proxy methods (async_request_chunk, async_request_finished).
//
// Plan 5 simplification:
//   - One SSH connection opened per start_request, then closed.
//   - TOFU auto-accepts (no prompt round-trip yet).
//   - Single-chunk responses (no streaming).
class ConnectionFromClient final
    : public IPC::ConnectionFromClient<SSHWebClientEndpoint, SSHWebServerEndpoint> {
    C_OBJECT(ConnectionFromClient);

public:
    using ConnectionMap = HashMap<int, NonnullRefPtr<ConnectionFromClient>>;

    virtual ~ConnectionFromClient() override;
    virtual void die() override;

private:
    explicit ConnectionFromClient(NonnullOwnPtr<IPC::Transport>);

    virtual Messages::SSHWebServer::InitTransportResponse init_transport(int peer_pid) override;
    virtual Messages::SSHWebServer::ConnectNewClientResponse connect_new_client() override;
    virtual void start_request(u64 request_id, URL::URL url, ByteString command) override;
    virtual Messages::SSHWebServer::StopRequestResponse stop_request(u64 request_id) override;
    virtual void tofu_decision(u64 prompt_id, bool accepted) override;
};

}
