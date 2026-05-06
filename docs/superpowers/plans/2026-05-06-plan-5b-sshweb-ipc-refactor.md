# Plan 5b: SSHWebServer IPC Refactor — Async out-of-process SSH + Connection Pooling

> **For agentic workers:** Use `superpowers:executing-plans` or `superpowers:subagent-driven-development` to implement step-by-step. Each task has a clear verification step. Do not batch tasks — verify before moving on.

---

## Goal

Replace the synchronous, in-process `libssh2` call inside `ResourceLoader::handle_sshweb_load_request` with an async IPC call to the `SSHWebServer` helper process via `LibSSHWebClient::Client`. Add per-host SSH connection pooling inside `ConnectionFromClient` so that repeated fetches from the same origin (fonts, scripts, images loaded by an ssh-web page) reuse a single open SSH session rather than reconnecting per-resource.

By the end of 5b: navigating to `ssh-web://localhost:32443/` no longer blocks the WebContent event loop; resources sub-loaded from that origin are fetched over the same SSH connection; and `SSHWebServer` survives multi-client scenarios (multiple tabs).

---

## Current State (as of 2026-05-06)

Inspected files and their status:

- **`Libraries/LibWeb/Loader/ResourceLoader.cpp`** (lines 420-454, 626-692): The dispatch block at line 440 calls `handle_sshweb_load_request` synchronously and waits for a result inline. The implementation at line 626 directly calls `SSHWeb::Connection::open(...)` and `connection->execute_command(...)` — pure synchronous libssh2, no IPC. The `#include` block at lines 20-26 imports `LibSSHWeb/URL.h`, `LibSSHWeb/Packfile.h`, `Services/SSHWebServer/Connection.h`, and `Services/SSHWebServer/KnownHosts.h` — all in-process.
- **`Libraries/LibWeb/Loader/ResourceLoader.h`** (lines 83-102): `handle_sshweb_load_request` declared; `SSHWebLoadResult` struct defined. Both guard-wrapped with `#ifdef LADYBIRD_ENABLE_SSHWEB`. No `SSHWebClient::Client` member anywhere.
- **`Libraries/LibSSHWebClient/Client.h`**: Fully declared. `Client` extends `IPC::ConnectionToServer<SSHWebClientEndpoint, SSHWebServerEndpoint>` and `SSHWebClientEndpoint`. Has `execute(url, command, OnComplete)`, `tofu_prompt`, `request_chunk`, `request_finished`, and `HashMap<u64, PendingRequest> m_pending`. Structurally complete.
- **`Libraries/LibSSHWebClient/Client.cpp`**: Fully implemented. `execute()` assigns a request_id and calls `IPCProxy::async_start_request(...)`. `request_chunk` accumulates bytes, calls `on_complete` on `is_final`. `request_finished` handles error strings. `tofu_prompt` auto-accepts (logs + calls `async_tofu_decision`). **The IPC send/receive paths are wired and correct.**
- **`Libraries/LibSSHWebClient/CMakeLists.txt`**: Compiles both `.ipc` files, builds `LibSSHWebClient`, links `LibCore LibIPC LibURL`. Does not yet link `LibSSHWebClient` into `LibWeb` — that linkage is missing.
- **`Services/SSHWebServer/SSHWebServer.ipc`**: Defines `init_transport`, `connect_new_client`, `start_request`, `stop_request`, `tofu_decision`. Complete.
- **`Services/SSHWebServer/SSHWebClient.ipc`**: Defines `tofu_prompt`, `request_chunk`, `request_finished`. Complete.
- **`Services/SSHWebServer/ConnectionFromClient.h`**: Fully declared. All IPC overrides declared. Has `ConnectionMap` typedef.
- **`Services/SSHWebServer/ConnectionFromClient.cpp`**: `start_request` is implemented — opens a new `SSHWeb::Connection` per request (no pooling), runs the command, fires `async_request_chunk` + `async_request_finished`. `connect_new_client()` returns an empty `IPC::TransportHandle {}` (stub — multi-client not yet supported). `die()` calls `EventLoop::quit(0)` (kills the whole server on first disconnect — wrong for multi-client). No connection pool.
- **`Services/SSHWebServer/Service.cpp`**: Service mode fully wired via `IPC::take_over_accepted_client_from_system_server<ConnectionFromClient>(mach_server_name)`. Passes `--service` flag and `--mach-server-name`. One-shot: a single client, then the process exits.
- **`Libraries/LibWebView/HelperProcess.{h,cpp}`**: `launch_request_server_process()` at line 202 is the template. It calls `launch_server_process<Requests::RequestClient>("RequestServer"sv, arguments)`. No `launch_sshweb_server_process()` exists yet.
- **`Libraries/LibWebView/Application.{h,cpp}`**: Has `launch_request_server()` / `m_request_server_client`. No `launch_sshweb_server()` or `m_sshweb_client` member. `launch_request_server` is called at Application startup (line 513).
- **`Libraries/LibWebView/ProcessType.h`**: Enum has `Browser, WebContent, WebWorker, RequestServer, ImageDecoder`. No `SSHWebServer` entry.
- **`Libraries/LibWebView/ProcessManager.cpp`**: `process_type_from_name` maps string names to enum — no `"SSHWebServer"` case. Adding it requires both files.

---

## Step-by-Step Implementation Tasks

### Task 1 — Register SSHWebServer as a known process type

**Files:**
- `/Users/ricardo.moura/projects/misc/sshttpd/sshweb-browser/Libraries/LibWebView/ProcessType.h`
- `/Users/ricardo.moura/projects/misc/sshttpd/sshweb-browser/Libraries/LibWebView/ProcessManager.cpp`

**Changes:**
1. In `ProcessType.h`, add `SSHWebServer,` to the `ProcessType` enum after `ImageDecoder`.
2. In `ProcessManager.cpp`, add to `process_type_from_name`:
   ```cpp
   if (name == "SSHWebServer"sv)
       return ProcessType::SSHWebServer;
   ```
   And add to `process_name_from_type`:
   ```cpp
   case ProcessType::SSHWebServer:
       return "SSHWebServer"sv;
   ```

**Verification:** `grep -n SSHWebServer` in both files shows the new cases. Build compiles without warnings.

---

### Task 2 — Add `launch_sshweb_server_process()` to HelperProcess

**Files:**
- `/Users/ricardo.moura/projects/misc/sshttpd/sshweb-browser/Libraries/LibWebView/HelperProcess.h`
- `/Users/ricardo.moura/projects/misc/sshttpd/sshweb-browser/Libraries/LibWebView/HelperProcess.cpp`

**Changes:**

In `HelperProcess.h`, add (inside the `#ifdef LADYBIRD_ENABLE_SSHWEB` guard or unconditionally alongside the other declarations):
```cpp
WEBVIEW_API ErrorOr<NonnullRefPtr<SSHWebClient::Client>> launch_sshweb_server_process();
```
Include `<LibSSHWebClient/Client.h>` (or forward-declare and include in .cpp).

In `HelperProcess.cpp`, add after `launch_request_server_process()`:
```cpp
#ifdef LADYBIRD_ENABLE_SSHWEB
ErrorOr<NonnullRefPtr<SSHWebClient::Client>> launch_sshweb_server_process()
{
    Vector<ByteString> arguments;
    arguments.append("--service"sv);

    if (auto server = mach_server_name(); server.has_value()) {
        arguments.append("--mach-server-name"sv);
        arguments.append(server.value());
    }

    return launch_server_process<SSHWebClient::Client>("SSHWebServer"sv, move(arguments));
}
#endif
```

Note: `SSHWebClient::Client` already implements `InitTransport` (via `IPC::ConnectionToServer`). The `launch_server_process` template calls `client->send_sync<typename ClientType::InitTransport>(getpid())` — confirm `Client::InitTransport` is the alias for `Messages::SSHWebServer::InitTransport` (it is, per `Client.h` line 31).

**Verification:** Compiles. `nm` or `grep` shows `launch_sshweb_server_process` in the object file.

---

### Task 3 — Wire `launch_sshweb_server()` into Application startup

**Files:**
- `/Users/ricardo.moura/projects/misc/sshttpd/sshweb-browser/Libraries/LibWebView/Application.h`
- `/Users/ricardo.moura/projects/misc/sshttpd/sshweb-browser/Libraries/LibWebView/Application.cpp`

**Changes in `Application.h`:**
Add inside `#ifdef LADYBIRD_ENABLE_SSHWEB`:
```cpp
static SSHWebClient::Client& sshweb_server_client() { return *the().m_sshweb_client; }
```
And in the private member section:
```cpp
#ifdef LADYBIRD_ENABLE_SSHWEB
RefPtr<SSHWebClient::Client> m_sshweb_client;
ErrorOr<void> launch_sshweb_server();
#endif
```

**Changes in `Application.cpp`:**
1. In `initialize_client_processes()` (near line 513, after `TRY(launch_request_server())`):
   ```cpp
   #ifdef LADYBIRD_ENABLE_SSHWEB
   TRY(launch_sshweb_server());
   #endif
   ```
2. Add the method:
   ```cpp
   #ifdef LADYBIRD_ENABLE_SSHWEB
   ErrorOr<void> Application::launch_sshweb_server()
   {
       m_sshweb_client = TRY(launch_sshweb_server_process());
       return {};
   }
   #endif
   ```

**Verification:** Run Ladybird with `LADYBIRD_ENABLE_SSHWEB` defined; confirm `SSHWebServer` process appears in `ps aux`. `dbgln` in `ConnectionFromClient` constructor should fire.

---

### Task 4 — Add SSH connection pool to `ConnectionFromClient`

**Files:**
- `/Users/ricardo.moura/projects/misc/sshttpd/sshweb-browser/Services/SSHWebServer/ConnectionFromClient.h`
- `/Users/ricardo.moura/projects/misc/sshttpd/sshweb-browser/Services/SSHWebServer/ConnectionFromClient.cpp`

**Changes in `ConnectionFromClient.h`:**
Add to the private section:
```cpp
// host:port → open SSHWeb::Connection. Lazily populated by start_request.
HashMap<ByteString, NonnullOwnPtr<SSHWeb::Connection>> m_ssh_pool;

static ByteString pool_key(StringView host, u16 port)
{
    return ByteString::formatted("{}:{}", host, port);
}
```

**Changes in `ConnectionFromClient.cpp` — `start_request`:**
Replace the `Connection::open(...)` call with a pool lookup:
```cpp
auto key = pool_key(sshweb_url.host.bytes_as_string_view(), sshweb_url.port);
SSHWeb::Connection* connection = nullptr;
if (auto it = m_ssh_pool.find(key); it != m_ssh_pool.end()) {
    connection = it->value.ptr();
} else {
    auto conn_or_error = SSHWeb::Connection::open(
        sshweb_url.host.bytes_as_string_view(),
        sshweb_url.port,
        known_hosts,
        move(decision),
        {});
    if (conn_or_error.is_error()) {
        async_request_finished(request_id,
            ByteString::formatted("ssh connect: {}", conn_or_error.error()));
        return;
    }
    auto inserted = m_ssh_pool.set(key, conn_or_error.release_value());
    connection = m_ssh_pool.find(key)->value.ptr();
}
```
Then proceed with `connection->execute_command(command.view())` as before.

Also fix `die()` — change from `Core::EventLoop::current().quit(0)` to proper client removal from a static `ConnectionMap`. For now a minimal fix:
```cpp
void ConnectionFromClient::die()
{
    m_ssh_pool.clear(); // Close all connections for this client.
    // Don't quit the event loop — other clients may remain.
    // The process exits naturally when the last client disconnects.
}
```
(Full multi-client support needs a static `ConnectionMap` like `RequestServer/ConnectionFromClient` uses; add it if time allows, otherwise the single-exit behavior is acceptable for 5b scope.)

**Verification:** Hit `ssh-web://localhost:32443/` and observe index.html loads. Then navigate to a page with an image or font served via proxy-call and confirm in `dbgln` output that the second request reuses the same pool entry (no second `Connection::open` log line).

---

### Task 5 — Replace synchronous in-process call with async IPC in ResourceLoader

This is the main payoff task.

**Files:**
- `/Users/ricardo.moura/projects/misc/sshttpd/sshweb-browser/Libraries/LibWeb/Loader/ResourceLoader.h`
- `/Users/ricardo.moura/projects/misc/sshttpd/sshweb-browser/Libraries/LibWeb/Loader/ResourceLoader.cpp`

**Changes in `ResourceLoader.h`:**
- Remove `handle_sshweb_load_request` declaration and `SSHWebLoadResult` struct (they become internal to the async callback).
- Add `#include <LibSSHWebClient/Client.h>` inside the `#ifdef LADYBIRD_ENABLE_SSHWEB` guard.
- Add a private method declaration:
  ```cpp
  #ifdef LADYBIRD_ENABLE_SSHWEB
  void dispatch_sshweb_load_request(
      LoadRequest const& request,
      RefPtr<SharedFunction<void(bool, Requests::RequestTimingInfo const&, StringView)>> on_complete,
      RefPtr<SharedFunction<void(HTTP::HeaderList const&, Optional<u32>, StringView const&)>> on_headers_received,
      RefPtr<SharedFunction<void(ReadonlyBytes)>> on_data_received);
  #endif
  ```

**Changes in `ResourceLoader.cpp`:**

1. Replace the `#include` block for in-process SSH (lines 20-26) with:
   ```cpp
   #ifdef LADYBIRD_ENABLE_SSHWEB
   #    include <LibSSHWebClient/Client.h>
   #    include <LibSSHWeb/URL.h>
   #    include <LibWebView/Application.h>
   #endif
   ```

2. Replace the synchronous dispatch block (lines 440-454):
   ```cpp
   if (request_scheme_is_sshweb || (initiated_from_sshweb && url.scheme().is_one_of("http"sv, "https"sv))) {
       dispatch_sshweb_load_request(request, on_complete, on_headers_received, on_data_received);
       return nullptr;
   }
   ```

3. Replace `handle_sshweb_load_request` and `parse_sshweb_proxy_response` (lines 577-692) with the new async dispatcher:
   ```cpp
   #ifdef LADYBIRD_ENABLE_SSHWEB
   void ResourceLoader::dispatch_sshweb_load_request(
       LoadRequest const& request,
       RefPtr<SharedFunction<void(bool, Requests::RequestTimingInfo const&, StringView)>> on_complete,
       RefPtr<SharedFunction<void(HTTP::HeaderList const&, Optional<u32>, StringView const&)>> on_headers_received,
       RefPtr<SharedFunction<void(ReadonlyBytes)>> on_data_received)
   {
       auto const& request_url = request.url().value();
       bool is_proxy_call = request_url.scheme().is_one_of("http"sv, "https"sv);

       String origin_url_string;
       if (is_proxy_call) {
           auto page = request.page();
           if (!page) {
               on_complete->function()(false, {}, "ssh-web proxy-call: no Page on request"sv);
               return;
           }
           auto document = page->top_level_traversable()->active_document();
           if (!document) {
               on_complete->function()(false, {}, "ssh-web proxy-call: no active document"sv);
               return;
           }
           origin_url_string = document->url().serialize();
       } else {
           origin_url_string = request_url.serialize();
       }

       auto sshweb_url_or_error = SSHWeb::URL::parse(origin_url_string);
       if (sshweb_url_or_error.is_error()) {
           on_complete->function()(false, {}, "ssh-web: invalid URL"sv);
           return;
       }

       ByteString command;
       if (is_proxy_call) {
           command = ByteString::formatted("proxy-call GET {}", request_url.serialize());
       } else {
           auto sshweb_url = sshweb_url_or_error.value();
           auto path = sshweb_url.path.is_empty() ? "/"_string : sshweb_url.path;
           command = ByteString::formatted("receive-pack {}", path);
       }

       auto& client = WebView::Application::sshweb_server_client();
       client.execute(request_url, move(command),
           [is_proxy_call, request_url, on_complete, on_headers_received, on_data_received]
           (ErrorOr<ByteBuffer> result) mutable {
               if (result.is_error()) {
                   auto msg = ByteString::formatted("ssh-web: {}", result.error());
                   on_complete->function()(false, {}, StringView(msg));
                   return;
               }
               auto raw = result.release_value();
               if (is_proxy_call) {
                   // parse_sshweb_proxy_response logic inline or extracted as free function
                   auto parsed = parse_sshweb_proxy_response(move(raw));
                   if (parsed.is_error()) {
                       auto msg = ByteString::formatted("ssh-web: {}", parsed.error());
                       on_complete->function()(false, {}, StringView(msg));
                       return;
                   }
                   auto r = parsed.release_value();
                   on_headers_received->function()(r.headers, r.status_code, {});
                   on_data_received->function()(r.body.bytes());
                   on_complete->function()(true, {}, {});
               } else {
                   // receive-pack: extract blob, synthesize headers
                   auto blob_or_error = SSHWeb::first_blob_in_packfile(raw.bytes());
                   if (blob_or_error.is_error()) {
                       auto msg = ByteString::formatted("ssh-web: {}", blob_or_error.error());
                       on_complete->function()(false, {}, StringView(msg));
                       return;
                   }
                   auto body = blob_or_error.release_value();
                   auto path_view = request_url.serialize_path();
                   StringView mime = Core::guess_mime_type_based_on_filename(path_view);
                   if (mime == "application/octet-stream"sv) mime = "text/html"sv;
                   auto headers = HTTP::HeaderList::create({});
                   headers->append(HTTP::Header::isomorphic_encode("Content-Type"sv, mime));
                   on_headers_received->function()(headers, 200, {});
                   on_data_received->function()(body.bytes());
                   on_complete->function()(true, {}, {});
               }
           });
   }
   #endif
   ```
   Keep `parse_sshweb_proxy_response` as a static free function (it's already static at line 577) — just move its declaration above the new dispatcher.

4. Update the `CMakeLists.txt` for `LibWeb` (find the file that lists `LibWeb` link libraries) to add:
   ```cmake
   if(LADYBIRD_ENABLE_SSHWEB)
       target_link_libraries(LibWeb PRIVATE LibSSHWebClient)
   endif()
   ```
   Also add `LibWebView` as a link dep only if it isn't already (be careful of circular dep — `LibWebView` may already depend on `LibWeb`; use the `Application::sshweb_server_client()` accessor via a forwarding header or move the client storage to `ResourceLoader` itself to avoid the cycle — see Risk #1 below).

**Verification:** Navigate to `ssh-web://localhost:32443/`. Page renders without blocking. Confirm no in-process `Connection::open` log lines. `dbgln` in `Client::execute` and `ConnectionFromClient::start_request` appear in the SSHWebServer process log.

---

### Task 6 — Remove in-process libssh2 linkage from LibWeb

**Files:**
- `Libraries/LibWeb/CMakeLists.txt` (find with `find ... -name CMakeLists.txt | xargs grep -l LibSSHWeb`)

**Changes:** Remove `LibSSHWeb`, `libssh2::libssh2`, `Services/SSHWebServer/Connection.h` includes and link deps from `LibWeb`. All libssh2 usage now lives exclusively in `SSHWebServer`.

**Verification:** Build succeeds with `libssh2` removed from `LibWeb`'s link line.

---

## Suggested Task Ordering

```
Task 1 (ProcessType) 
  → Task 2 (HelperProcess function)
    → Task 3 (Application wiring + launch)
      → Task 4 (Connection pool in ConnectionFromClient)   ← can be parallelized with Task 3
      → Task 5 (ResourceLoader async dispatch)             ← needs Task 3 done first
        → Task 6 (remove in-process linkage)               ← last, after Task 5 confirmed working
```

Tasks 4 and 3 can be done in parallel. Task 5 is the integration point — do it only after Tasks 1-3 are verified.

---

## Risks / Things to Watch For

1. **LibWeb → LibWebView circular dependency.** `LibWebView` depends on `LibWeb`. Using `WebView::Application::sshweb_server_client()` from inside `ResourceLoader.cpp` introduces a `LibWeb → LibWebView` dep. Two safe alternatives: (a) store the `SSHWebClient::Client*` pointer directly on `ResourceLoader` (set it during app init, analogous to how `ResourceLoader` already has a `request_server_client` in some forks), or (b) use a global accessor in `LibSSHWebClient` itself (a singleton pattern). Option (a) is simpler. Check how `ResourceLoader` currently gets its `Requests::RequestClient` — if it goes through `Application`, the cycle may already exist and be acceptable.

2. **IPC comment-line parser gotcha.** The Ladybird IPC compiler (`IPCCompiler`) skips lines beginning with `//`. Do not put a trailing `=|` or `=>` on a commented-out line — it causes cryptic parse failures. The current `.ipc` files are clean; don't change them.

3. **`connect_new_client()` stub.** `ConnectionFromClient::connect_new_client()` returns an empty `IPC::TransportHandle {}`. For multi-tab scenarios (multiple `WebContent` processes), this must return a real paired transport. The `RequestServer` analog uses `IPC::new_client_connection()`. For 5b scope, single-tab is acceptable; mark this as a known stub with a `// Plan 5b: single-tab only` comment.

4. **`libssh2` blocking socket I/O inside the event loop.** `SSHWeb::Connection::open` and `execute_command` are fully blocking (synchronous libssh2 calls). They now block the `SSHWebServer` process's event loop rather than `WebContent`'s, which is the correct trade-off. However, if two requests arrive while one is in-flight, the second queues behind it. For 5b this is acceptable. Plan 6 or a separate task would thread the libssh2 calls off the event loop inside SSHWebServer.

5. **`libssh2` anon-auth listing trick.** `Connection::open` currently passes `Optional<Identity> {}` (no identity). libssh2 requires `libssh2_userauth_list()` to be called even for public-key flows — this must be called before `libssh2_userauth_publickey_fromfile()`. If you test with publickey auth, verify `Identity` wiring in `ConnectionFromClient::start_request` (currently no identity is passed; the connection falls back to whatever the server allows).

6. **`die()` kills the process.** Current `die()` calls `EventLoop::quit(0)`. Fix this in Task 4 even if full multi-client pooling is deferred — otherwise closing one tab crashes the SSHWebServer for all remaining tabs.

7. **`--mach-server-name` on macOS Sequoia.** Mach bootstrap port passing works only if the parent sets up the port before `spawn`. Look at how `RequestServer` does it in the existing `Application::launch_request_server` — the pattern is already established and `Service.cpp` already accepts `--mach-server-name`.

---

## Out of Scope for Plan 5b

- **TOFU UI dialog** — `tofu_prompt` currently auto-accepts in both `Client.cpp` (browser side) and `ConnectionFromClient.cpp` (server side). Wiring a real UI dialog is Plan 7.
- **JS sandbox / CSP enforcement** — Plan 6.
- **Multi-tab `connect_new_client` full implementation** — deferred; acceptable to keep the stub returning empty handle for 5b.
- **Non-blocking/threaded libssh2 inside SSHWebServer** — deferred; acceptable to block the SSHWebServer event loop per-request for now.
- **Identity/publickey auth plumbing through IPC** — not part of 5b; `start_request` always opens unauthenticated (or server-allows-anon) connections.
