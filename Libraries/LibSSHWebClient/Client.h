/*
 * Copyright (c) 2026, the sshweb-browser developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/ByteBuffer.h>
#include <AK/Function.h>
#include <AK/HashMap.h>
#include <AK/Optional.h>
#include <LibIPC/ConnectionToServer.h>
#include <LibSSHWeb/Manifest.h>
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

    // Capabilities manifest — populated after connect via set_manifest().
    void set_manifest(SSHWeb::CapabilitiesManifest manifest) { m_manifest = move(manifest); }
    Optional<SSHWeb::CapabilitiesManifest> const& manifest() const { return m_manifest; }

    // Kick an async capabilities fetch for the given origin URL. The manifest
    // is stored on arrival. Safe to call multiple times; subsequent calls are
    // ignored once the manifest is loaded (or a fetch is already in-flight).
    void fetch_capabilities_async(URL::URL const& origin_url);

    // Returns true if the given host is in the server's proxy-cache.allow list.
    // Exact match only (subdomain matching is a future enhancement).
    // Returns false if the manifest has not been loaded yet — callers should
    // treat this conservatively (block the request).
    bool is_host_allowlisted(StringView host) const;

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
    Optional<SSHWeb::CapabilitiesManifest> m_manifest;
    bool m_capabilities_fetch_in_flight { false };
};

}
