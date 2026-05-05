/*
 * Copyright (c) 2026, the sshweb-browser developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/ByteBuffer.h>
#include <AK/Function.h>
#include <AK/HashMap.h>
#include <LibIPC/ConnectionToServer.h>
#include <LibURL/URL.h>
#include <Services/SSHWebServer/SSHWebClientEndpoint.h>
#include <Services/SSHWebServer/SSHWebServerEndpoint.h>

namespace SSHWebClient {

// Client-side handle for the SSHWebServer helper process.
//
// Mirrors Requests::RequestClient (LibRequests):
//   - inherits ConnectionToServer<OurEndpoint, RemoteEndpoint> for outgoing
//     IPC and so we receive callbacks at our own endpoint
//   - inherits SSHWebClientEndpoint to implement the receive callbacks
class Client final
    : public IPC::ConnectionToServer<SSHWebClientEndpoint, SSHWebServerEndpoint>
    , public SSHWebClientEndpoint {
    C_OBJECT_ABSTRACT(Client);

public:
    using InitTransport = Messages::SSHWebServer::InitTransport;
    using OnComplete = Function<void(ErrorOr<ByteBuffer>)>;

    explicit Client(NonnullOwnPtr<IPC::Transport>);
    virtual ~Client() override;

    // Issue a command against an ssh-web:// URL. on_complete is invoked
    // exactly once with the accumulated response bytes or an error.
    void execute(URL::URL const& url, ByteString command, OnComplete on_complete);

private:
    // SSHWebClientEndpoint overrides — called by the server back at us.
    virtual void tofu_prompt(u64 prompt_id, ByteString host, u16 port, ByteString key_type, ByteString fingerprint_sha256) override;
    virtual void request_chunk(u64 request_id, ByteBuffer data, bool is_final) override;
    virtual void request_finished(u64 request_id, ByteString error) override;

    struct PendingRequest {
        ByteBuffer accumulated;
        OnComplete on_complete;
    };

    HashMap<u64, PendingRequest> m_pending;
    u64 m_next_request_id { 1 };
};

}
