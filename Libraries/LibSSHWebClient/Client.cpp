/*
 * Copyright (c) 2026, the sshweb-browser developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Format.h>
#include <AK/NonnullRefPtr.h>
#include <LibSSHWebClient/Client.h>
#include <LibSSHWeb/Manifest.h>

namespace SSHWebClient {

Client::Client(NonnullOwnPtr<IPC::Transport> transport)
    : IPC::ConnectionToServer<SSHWebClientEndpoint, SSHWebServerEndpoint>(*this, move(transport))
{
}

Client::~Client() = default;

void Client::execute(URL::URL const& url, ByteString command, OnComplete on_complete, u64 page_id)
{
    auto request_id = m_next_request_id++;
    m_last_page_id = page_id;
    m_pending.set(request_id, PendingRequest { .accumulated = {}, .on_complete = move(on_complete), .page_id = page_id });
    IPCProxy::async_start_request(request_id, url, move(command));
}

// === Plan 7B TOFU ===
void Client::tofu_prompt(u64 prompt_id, ByteString host, u16 port, ByteString key_type, ByteString fingerprint_sha256)
{
    dbgln("[SSHWebClient] tofu_prompt for {}:{} ({} {})", host, port, key_type, fingerprint_sha256);
    if (on_tofu_prompt) {
        // Route to UI process via the callback set by WebContent/main.cpp.
        on_tofu_prompt(m_last_page_id, prompt_id, move(host), port, move(key_type), move(fingerprint_sha256));
    } else {
        // Fallback: auto-accept (should not happen in production with UI wired up).
        dbgln("[SSHWebClient] no on_tofu_prompt handler — auto-accepting");
        IPCProxy::async_tofu_decision(prompt_id, true, true);
    }
}
// === End Plan 7B TOFU ===

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

void Client::fetch_capabilities_async(URL::URL const& origin_url)
{
    if (m_manifest.has_value() || m_capabilities_fetch_in_flight)
        return;
    m_capabilities_fetch_in_flight = true;
    // Capture a strong ref so the lambda is safe even if the caller drops its
    // reference before the capabilities response arrives.
    NonnullRefPtr<Client> self = *this;
    execute(origin_url, "capabilities", [self](ErrorOr<ByteBuffer> result) mutable {
        self->m_capabilities_fetch_in_flight = false;
        if (result.is_error()) {
            dbgln("[SSHWebClient] capabilities fetch failed: {}", result.error());
            return;
        }
        auto raw = result.release_value();
        auto parsed = SSHWeb::CapabilitiesManifest::parse(StringView { raw.bytes() });
        if (parsed.is_error()) {
            dbgln("[SSHWebClient] capabilities parse failed: {}", parsed.error());
            return;
        }
        self->m_manifest = parsed.release_value();
        auto allow_count = self->m_manifest->proxy_cache.has_value()
            ? self->m_manifest->proxy_cache->allow.size()
            : 0u;
        dbgln("[SSHWebClient] manifest loaded: site={}, proxy-cache.allow={} host(s)",
            self->m_manifest->site.name, allow_count);
        if (self->on_manifest_ready)
            self->on_manifest_ready(*self->m_manifest);
    });
}

bool Client::is_host_allowlisted(StringView host) const
{
    if (!m_manifest.has_value())
        return false;
    if (!m_manifest->proxy_cache.has_value())
        return false;
    for (auto const& allowed : m_manifest->proxy_cache->allow) {
        auto allowed_view = allowed.bytes_as_string_view();
        if (allowed_view == host)
            return true;
        // Subdomain match: "foo.fonts.googleapis.com" matches "fonts.googleapis.com"
        if (host.ends_with(allowed_view) && host.length() > allowed_view.length() && host[host.length() - allowed_view.length() - 1] == '.')
            return true;
    }
    return false;
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
