/*
 * Copyright (c) 2026, the sshweb-browser developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Format.h>
#include <AK/Vector.h>
#include <LibCore/EventLoop.h>
#include <LibIPC/TransportHandle.h>
#include <LibSSHWeb/URL.h>
#include <Services/SSHWebServer/Connection.h>
#include <Services/SSHWebServer/ConnectionFromClient.h>
#include <Services/SSHWebServer/KnownHosts.h>
#include <pthread.h>

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

// === Plan 7B TOFU ===
void ConnectionFromClient::tofu_decision(u64 prompt_id, bool accepted, bool permanent)
{
    auto it = m_pending_tofu.find(prompt_id);
    if (it == m_pending_tofu.end()) {
        dbgln("[SSHWebServer] tofu_decision for unknown prompt_id {}", prompt_id);
        return;
    }
    auto* pending = it->value.ptr();

    // If the user chose "Trust Permanently", persist to known_hosts now (on the IPC thread,
    // before signalling the background thread so there's no race on the file).
    if (accepted && permanent) {
        auto known_hosts_or_error = SSHWeb::KnownHosts::with_default_root();
        if (!known_hosts_or_error.is_error()) {
            auto kh = known_hosts_or_error.release_value();
            if (auto err = kh.record(pending->host, pending->port, pending->key); err.is_error())
                dbgln("[SSHWebServer] tofu_decision: failed to record known_host: {}", err.error());
        }
    }

    {
        Threading::MutexLocker locker(pending->mutex);
        pending->accepted = accepted;
        pending->permanent = permanent;
        pending->resolved = true;
        pending->cond.signal();
    }
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

    // Plan 5b: pool lookup — reuse an open connection if one exists for this host:port.
    auto key = pool_key(sshweb_url.host.bytes_as_string_view(), sshweb_url.port);
    if (auto it = m_ssh_pool.find(key); it != m_ssh_pool.end()) {
        // Connection already open — execute command synchronously on the IPC thread.
        SSHWeb::Connection* connection = it->value.ptr();
        auto bytes_or_error = connection->execute_command(command.view());
        if (bytes_or_error.is_error()) {
            async_request_finished(request_id, ByteString::formatted("execute: {}", bytes_or_error.error()));
            return;
        }
        auto bytes = bytes_or_error.release_value();
        async_request_chunk(request_id, bytes.bytes(), true);
        async_request_finished(request_id, ByteString {});
        return;
    }

    // === Plan 7B TOFU ===
    // New connection needed — move Connection::open to a background thread so the TOFU
    // round-trip (which requires an IPC event-loop spin on this same thread) doesn't deadlock.

    // Capture the IPC event loop so we can deferred_invoke from the background thread.
    auto event_loop_ref = Core::EventLoop::current_weak();

    // We keep a strong RefPtr to ourselves so the background thread can safely call async_*.
    NonnullRefPtr<ConnectionFromClient> self = *this;

    // Capture values by value for the thread lambda.
    ByteString host = sshweb_url.host.to_byte_string();
    u16 port = sshweb_url.port;
    ByteString pool_key_str = key;

    struct ThreadArgs {
        NonnullRefPtr<ConnectionFromClient> self;
        NonnullRefPtr<Core::WeakEventLoopReference> event_loop_ref;
        u64 request_id;
        ByteString host;
        u16 port;
        ByteString pool_key_str;
        ByteString command;
    };

    auto* args = new ThreadArgs {
        move(self),
        move(event_loop_ref),
        request_id,
        move(host),
        port,
        move(pool_key_str),
        move(command),
    };

    pthread_t thread;
    pthread_create(&thread, nullptr, [](void* raw) -> void* {
        auto* args = static_cast<ThreadArgs*>(raw);
        NonnullRefPtr<ConnectionFromClient> self = args->self;
        auto event_loop_ref = move(args->event_loop_ref);
        u64 request_id = args->request_id;
        ByteString host = move(args->host);
        u16 port = args->port;
        ByteString pool_key_str = move(args->pool_key_str);
        ByteString command = move(args->command);
        delete args;

        // Open known_hosts — each open attempt gets a fresh view.
        auto known_hosts_or_error = SSHWeb::KnownHosts::with_default_root();
        if (known_hosts_or_error.is_error()) {
            auto msg = ByteString::formatted("known_hosts: {}", known_hosts_or_error.error());
            auto ev = event_loop_ref->take();
            if (ev) ev->deferred_invoke([self, request_id, msg]() mutable {
                self->async_request_finished(request_id, msg);
            });
            return nullptr;
        }
        auto known_hosts = known_hosts_or_error.release_value();

        // Build the TOFU callback. Called synchronously from Connection::open on this
        // background thread whenever a new (unknown) host key is encountered.
        SSHWeb::TOFUDecisionCallback tofu_decision_cb = [&](StringView cb_host, u16 cb_port, SSHWeb::HostKey const& received_key) -> bool {
            // Allocate the PendingTOFU entry on the heap. Ownership will be transferred
            // to m_pending_tofu (on the IPC thread) after registration.
            auto* entry = new PendingTOFU();
            entry->host = ByteString(cb_host);
            entry->port = cb_port;
            entry->key = received_key;

            // We use a small struct on the stack to synchronise registration between
            // the background thread and the IPC thread's deferred_invoke.
            struct RegSync {
                Threading::Mutex mutex;
                Threading::ConditionVariable cond { mutex };
                bool done { false };
                u64 prompt_id { 0 };
                PendingTOFU* pending { nullptr };   // raw ptr; safe because we wait for done
            } reg;

            auto ev = event_loop_ref->take();
            if (!ev) {
                delete entry;
                return false;
            }

            // On the IPC thread: register the entry, fire async_tofu_prompt, and wake us.
            ev->deferred_invoke([&self, entry, &reg]() mutable {
                reg.prompt_id = self->m_next_prompt_id++;
                entry->resolved = false;
                self->m_pending_tofu.set(reg.prompt_id, adopt_own(*entry));
                reg.pending = entry;    // safe: OwnPtr still holds it

                // Fire async_tofu_prompt BEFORE signalling so the IPC message is queued
                // before the background thread potentially calls deferred_invoke for cleanup.
                self->async_tofu_prompt(reg.prompt_id,
                    ByteString(entry->host),
                    entry->port,
                    ByteString(entry->key.type.bytes_as_string_view()),
                    ByteString(entry->key.fingerprint_sha256.bytes_as_string_view()));

                Threading::MutexLocker locker(reg.mutex);
                reg.done = true;
                reg.cond.signal();
            });

            // Wait until the IPC thread has registered the entry and given us the pointer.
            {
                Threading::MutexLocker locker(reg.mutex);
                reg.cond.wait_while([&reg] { return !reg.done; });
            }

            // Now block until tofu_decision() signals us (via pending->cond).
            // Access to pending is safe: only the background thread and IPC thread touch it,
            // and by this point the IPC thread has finished with the reg sync.
            PendingTOFU* pending = reg.pending;
            u64 prompt_id = reg.prompt_id;

            {
                Threading::MutexLocker locker(pending->mutex);
                pending->cond.wait_while([pending] { return !pending->resolved; });
            }

            bool result = pending->accepted;

            // Erase the pending entry on the IPC thread (async — safe since pending->resolved=true).
            // Capture self by value to ensure the refcount outlives the thread.
            NonnullRefPtr<ConnectionFromClient> self_copy = self;
            ev->deferred_invoke([self_copy, prompt_id]() mutable {
                self_copy->m_pending_tofu.remove(prompt_id);
            });

            return result;
        };

        auto connection_or_error = SSHWeb::Connection::open(
            host,
            port,
            known_hosts,
            move(tofu_decision_cb),
            {});

        if (connection_or_error.is_error()) {
            auto msg = ByteString::formatted("ssh connect: {}", connection_or_error.error());
            auto ev = event_loop_ref->take();
            if (ev) ev->deferred_invoke([self, request_id, msg]() mutable {
                self->async_request_finished(request_id, msg);
            });
            return nullptr;
        }

        auto owned_connection = connection_or_error.release_value();
        auto bytes_or_error = owned_connection->execute_command(command.view());

        if (bytes_or_error.is_error()) {
            auto msg = ByteString::formatted("execute: {}", bytes_or_error.error());
            auto ev = event_loop_ref->take();
            if (ev) ev->deferred_invoke([self, request_id, msg]() mutable {
                self->async_request_finished(request_id, msg);
            });
            return nullptr;
        }

        auto bytes = bytes_or_error.release_value();

        // Store connection in pool and send response — must happen on the IPC thread.
        auto ev = event_loop_ref->take();
        if (ev) ev->deferred_invoke([self, request_id, pool_key_str, bytes = move(bytes), owned_connection = move(owned_connection)]() mutable {
            self->m_ssh_pool.set(pool_key_str, move(owned_connection));
            self->async_request_chunk(request_id, bytes.bytes(), true);
            self->async_request_finished(request_id, ByteString {});
        });

        return nullptr;
    }, args);
    pthread_detach(thread);
    // === End Plan 7B TOFU ===
}

}
