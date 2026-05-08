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
    // page_id is recorded so that on_tofu_prompt can target the right UI window.
    void execute(URL::URL const& url, ByteString command, OnComplete on_complete, u64 page_id = 0);

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

    // Called once (on the IPC thread) after fetch_capabilities_async() receives
    // and stores the manifest. Set this before calling fetch_capabilities_async().
    Function<void(SSHWeb::CapabilitiesManifest const&)> on_manifest_ready;

    // === Plan 7B TOFU ===
    // Fired when the server sends a tofu_prompt. The WebContent glue sets this
    // to route the prompt to the UI process for user interaction.
    // Signature: (page_id, prompt_id, host, port, key_type, fingerprint_sha256)
    Function<void(u64, u64, ByteString, u16, ByteString, ByteString)> on_tofu_prompt;
    // === End Plan 7B TOFU ===

    // Set the active identity for all subsequent SSH requests.
    void set_active_identity(ByteString identity_id, ByteString identity_dir, ByteString passphrase);

    // Drop all pooled SSH connections so the next request re-authenticates.
    void clear_ssh_pool();

private:
    // SSHWebClientEndpoint overrides — called by the server back at us.
    virtual void tofu_prompt(u64 prompt_id, ByteString host, u16 port, ByteString key_type, ByteString fingerprint_sha256) override;
    virtual void request_chunk(u64 request_id, ByteBuffer data, bool is_final) override;
    virtual void request_finished(u64 request_id, ByteString error) override;

    struct PendingRequest {
        ByteBuffer accumulated;
        OnComplete on_complete;
        u64 page_id { 0 };
    };

    struct ActiveIdentity {
        ByteString id;
        ByteString dir;
        ByteString passphrase;
    };

    HashMap<u64, PendingRequest> m_pending;
    u64 m_next_request_id { 1 };
    u64 m_last_page_id { 0 };   // page_id of the most recent execute() call
    Optional<SSHWeb::CapabilitiesManifest> m_manifest;
    Optional<ActiveIdentity> m_active_identity;
    bool m_capabilities_fetch_in_flight { false };
};

}
