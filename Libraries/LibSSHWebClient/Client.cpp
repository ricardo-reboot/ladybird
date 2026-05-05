/*
 * Copyright (c) 2026, the sshweb-browser developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Format.h>
#include <LibSSHWebClient/Client.h>

namespace SSHWebClient {

Client::Client(NonnullOwnPtr<IPC::Transport> transport)
    : IPC::ConnectionToServer<SSHWebClientEndpoint, SSHWebServerEndpoint>(*this, move(transport))
{
}

Client::~Client() = default;

void Client::execute(URL::URL const& url, ByteString command, OnComplete on_complete)
{
    auto request_id = m_next_request_id++;
    m_pending.set(request_id, PendingRequest { .accumulated = {}, .on_complete = move(on_complete) });
    IPCProxy::async_start_request(request_id, url, move(command));
}

void Client::tofu_prompt(u64 prompt_id, ByteString host, u16 port, ByteString key_type, ByteString fingerprint_sha256)
{
    // Plan 5 simplification: auto-accept. Plan 7 will route this to a real
    // UI dialog in the AppKit / Qt UI process.
    dbgln("[SSHWebClient] auto-accepting host key for {}:{} ({} {})", host, port, key_type, fingerprint_sha256);
    IPCProxy::async_tofu_decision(prompt_id, true);
}

void Client::request_chunk(u64 request_id, ByteBuffer data, bool is_final)
{
    auto it = m_pending.find(request_id);
    if (it == m_pending.end())
        return;
    auto& pending = it->value;
    if (auto result = pending.accumulated.try_append(data.bytes()); result.is_error()) {
        if (pending.on_complete)
            pending.on_complete(result.release_error());
        m_pending.remove(it);
        return;
    }
    if (is_final) {
        auto on_complete = move(pending.on_complete);
        auto bytes = move(pending.accumulated);
        m_pending.remove(it);
        if (on_complete)
            on_complete(move(bytes));
    }
}

void Client::request_finished(u64 request_id, ByteString error)
{
    auto it = m_pending.find(request_id);
    if (it == m_pending.end())
        return;
    auto on_complete = move(it->value.on_complete);
    auto bytes = move(it->value.accumulated);
    m_pending.remove(it);
    if (!on_complete)
        return;
    if (!error.is_empty()) {
        on_complete(Error::from_string_view(StringView { error }));
        return;
    }
    on_complete(move(bytes));
}

}
