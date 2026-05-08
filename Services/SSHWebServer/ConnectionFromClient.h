/*
 * Copyright (c) 2026, the sshweb-browser developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/ByteString.h>
#include <AK/Function.h>
#include <AK/HashMap.h>
#include <LibIPC/ConnectionFromClient.h>
#include <LibThreading/ConditionVariable.h>
#include <LibThreading/Mutex.h>
#include <Services/SSHWebServer/Connection.h>
#include <Services/SSHWebServer/KnownHosts.h>
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
    virtual void start_request(u64 request_id, URL::URL url, ByteString command, ByteString identity_id, ByteString identity_dir, ByteString passphrase) override;
    virtual Messages::SSHWebServer::StopRequestResponse stop_request(u64 request_id) override;
    virtual void tofu_decision(u64 prompt_id, bool accepted, bool permanent) override;
    virtual void clear_ssh_pool() override;

    // host:port -> open SSHWeb::Connection. Lazily populated by start_request.
    HashMap<ByteString, NonnullOwnPtr<SSHWeb::Connection>> m_ssh_pool;

    static ByteString pool_key(StringView host, u16 port, StringView identity_id = {})
    {
        return ByteString::formatted("{}:{}:{}", host, port, identity_id);
    }

    // === Plan 7B TOFU ===
    // One entry per outstanding TOFU prompt. The background thread that called
    // Connection::open blocks on `cond` waiting for the UI decision.
    struct PendingTOFU {
        Threading::Mutex mutex;
        Threading::ConditionVariable cond { mutex };
        // Set by tofu_decision() on the IPC thread; read by the background thread.
        bool resolved { false };
        bool accepted { false };
        bool permanent { false };
        // Host-key details needed to persist the entry if permanent=true.
        ByteString host;
        u16 port { 0 };
        SSHWeb::HostKey key;
    };

    HashMap<u64, OwnPtr<PendingTOFU>> m_pending_tofu;
    u64 m_next_prompt_id { 1 };
    // === End Plan 7B TOFU ===
};

}
