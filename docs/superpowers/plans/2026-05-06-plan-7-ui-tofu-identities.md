# Plan 7 — UI, TOFU Modal, Identities

**Date:** 2026-05-06  
**Parallel subplans:** 7A (Address Bar), 7B (TOFU Modal), 7C (Identities)

---

## Background (read before any subplan)

`ssh-web://` pages are loaded via `ResourceLoader::dispatch_sshweb_load_request` in
`Libraries/LibWeb/Loader/ResourceLoader.cpp`. The SSH session is managed by a per-WebContent
helper process, `Services/SSHWebServer/`. The browser side speaks to it through
`Libraries/LibSSHWebClient/Client.{h,cpp}` which holds an `Optional<SSHWeb::CapabilitiesManifest>`
(site name, proxy allowlist) after the async `fetch_capabilities_async()` call.

The AppKit toolbar lives in `UI/AppKit/Interface/TabController.mm`. The URL bar is a
`LocationSearchField` (subclass of `NSSearchField`) owned by `location_toolbar_item`. URL changes
reach the bar through `onURLChange:` → `setLocationFieldText:`. Modal dialogs use
`beginSheetModalForWindow:completionHandler:` (see `on_request_alert` in
`UI/AppKit/Interface/LadybirdWebView.mm` line 560).

---

## 7A — Address Bar SSH-Web Indicator

### Goal

When the active page has an `ssh-web://` origin, the URL bar should (a) show a subtle background
tint, (b) display a "SSH-Web · <site name>" prefix pill to the left of the URL text, and (c) offer
a small identity-switcher button to the right of the URL text. The indicator must clear immediately
when navigating away. No tint or pill on regular HTTP/S pages.

### Files to touch

- `/Users/ricardo.moura/projects/misc/sshttpd/sshweb-browser/UI/AppKit/Interface/TabController.mm` — main edit surface: `setLocationFieldText:`, `onURLChange:`, and `location_toolbar_item` property.
- `/Users/ricardo.moura/projects/misc/sshttpd/sshweb-browser/UI/AppKit/Interface/TabController.h` — add public method `-(void)onSSHWebManifestLoaded:(NSString*)siteName` and ivar for the tint layer.
- `/Users/ricardo.moura/projects/misc/sshttpd/sshweb-browser/UI/AppKit/Interface/LadybirdWebView.mm` — wire the new `on_sshweb_manifest_ready` callback (see IPC additions below).
- `/Users/ricardo.moura/projects/misc/sshttpd/sshweb-browser/Libraries/LibSSHWebClient/Client.h` / `Client.cpp` — add `Function<void(SSHWeb::CapabilitiesManifest const&)> on_manifest_ready` callback field; fire it from `fetch_capabilities_async` after `self->m_manifest = ...`.
- `/Users/ricardo.moura/projects/misc/sshttpd/sshweb-browser/Libraries/LibWebView/ViewImplementation.h` / `ViewImplementation.cpp` — add `Function<void(String const& site_name)> on_sshweb_manifest_ready` on the view bridge; populate it from `SSHWebClient::Client::on_manifest_ready` inside `ResourceLoader` or `WebContent::PageClient` (whichever owns the Client ref in process).

### Key API additions

```cpp
// LibSSHWebClient/Client.h
Function<void(SSHWeb::CapabilitiesManifest const&)> on_manifest_ready;

// LibWebView/ViewImplementation.h
Function<void(String const& site_name)> on_sshweb_manifest_ready;
```

```objc
// TabController.h
- (void)onSSHWebManifestLoaded:(NSString*)siteName;
- (void)clearSSHWebIndicator;
```

### Step-by-step tasks

**Step 1 — Callback chain from Client to ViewImplementation**

1. In `Client.h` add the `on_manifest_ready` function field (nullable, no-op if unset).
2. In `Client.cpp::fetch_capabilities_async` lambda, after `self->m_manifest = parsed.release_value()`, fire `if (self->on_manifest_ready) self->on_manifest_ready(*self->m_manifest);`.
3. In `ViewImplementation.h` add `on_sshweb_manifest_ready`.
4. In `ResourceLoader.cpp` (or wherever `m_sshweb_client` is created and stored), wire:
   ```cpp
   m_sshweb_client->on_manifest_ready = [this](auto const& manifest) {
       if (m_page)
           m_page->client().page_did_sshweb_manifest_load(manifest.site.name);
   };
   ```
5. Add `page_did_sshweb_manifest_load(String site_name)` to the WebContent → UI IPC endpoint (`Libraries/LibWebView/WebContentClient.ipc` → handle in `WebContentClient.cpp` → fire `on_sshweb_manifest_ready` on the view bridge).

**Verification step 1:** Add a `dbgln` in the lambda and navigate to an `ssh-web://` test origin. Confirm the site name appears in the log.

**Step 2 — Wire into AppKit**

1. In `LadybirdWebView.mm`, inside the big `init` where all `on_*` callbacks are set, add:
   ```objc
   m_web_view_bridge->on_sshweb_manifest_ready = [weak_self](auto const& site_name) {
       LadybirdWebView* self = weak_self;
       if (self == nil) return;
       auto* ns_name = Ladybird::string_to_ns_string(site_name);
       [[self observer] onSSHWebManifestLoaded:ns_name];
   };
   ```
2. In `Tab.mm` conform `Tab` to the updated `LadybirdWebViewObserver` protocol; implement `onSSHWebManifestLoaded:` to forward to `[[self tabController] onSSHWebManifestLoaded:siteName]`.

**Step 3 — URL bar visual changes in TabController**

1. Extract the `LocationSearchField` creation into a helper so we can keep a typed pointer `_location_search_field` on `TabController`.
2. Implement `onSSHWebManifestLoaded:`:
   - Store `_ssh_web_site_name` ivar.
   - Set `[_location_search_field setBackgroundColor:[NSColor colorWithRed:0.0 green:0.35 blue:0.55 alpha:0.08]]` (a subtle SSH-Web tint).
   - Prepend an `NSAttributedString` pill "SSH-Web · <name> " in a secondary text style to the attributed URL value already built by `setLocationFieldText:`.
3. Implement `clearSSHWebIndicator`:
   - Clear `_ssh_web_site_name`.
   - Restore default `backgroundColor`.
4. Call `clearSSHWebIndicator` at the start of `onLoadStart:isRedirect:`.
5. In `onURLChange:` if `_ssh_web_site_name` is set, re-apply the pill after `setLocationFieldText:`.

**Verification step 3:** Navigate to an `ssh-web://` site — bar should tint and show pill. Navigate to `https://example.com` — bar should clear immediately.

**Step 4 — Identity switcher button (stub for 7C)**

Add a small `NSButton` (lock+person icon, system image `NSImageNameUserTemplate`) to the right of the location search field, hidden unless `_ssh_web_site_name != nil`. Button target/action is `@selector(showIdentitySwitcher:)` — a no-op stub in this subplan. 7C will implement the popover.

**Verification step 4:** Button appears on `ssh-web://` and disappears on normal URLs.

### Risks / things to watch for

- `on_manifest_ready` fires on an IPC background thread; ensure all AppKit mutations happen on the main thread (dispatch to main queue if needed).
- `setLocationFieldText:` is called from `onURLChange:` which may interleave with the manifest arrival. The pill is only re-applied if `_ssh_web_site_name` is already set, so ordering is safe.
- The `LocationSearchField` overrides `intrinsicContentSize`; adding a left/right decoration view should use `NSSearchField`'s `leftView` / `rightView` slots rather than constraints to avoid breaking autocomplete geometry.

### Out of scope

- Animating the tint transition.
- Showing which identity is in use inside the pill (deferred to 7C).
- Subdomain / multi-hop display.
- Dark-mode colour tuning (use system semantic colours only).

---

## 7B — TOFU Modal Dialog

### Goal

When `SSHWebServer` encounters an unknown host key, it currently auto-accepts (Plan 5 stub).
Plan 7B replaces that with a real async round-trip: the server sends `tofu_prompt` over IPC, the
UI process presents a full-window sheet (Chrome SSL-warning style) with three choices — **Trust
permanently** (writes to `KnownHosts`), **Trust this session** (in-memory only), and **Cancel**
(kills the load). The server then receives `tofu_decision` and proceeds or aborts.

### Files to touch

- `/Users/ricardo.moura/projects/misc/sshttpd/sshweb-browser/Services/SSHWebServer/ConnectionFromClient.cpp` — replace the auto-accept `TOFUDecisionCallback` with one that sends `async_tofu_prompt` and suspends.
- `/Users/ricardo.moura/projects/misc/sshttpd/sshweb-browser/Services/SSHWebServer/ConnectionFromClient.h` — add `HashMap<u64, Function<void(bool)>> m_pending_tofu` and `u64 m_next_prompt_id`.
- `/Users/ricardo.moura/projects/misc/sshttpd/sshweb-browser/Services/SSHWebServer/Connection.h` / `Connection.cpp` — `TOFUDecisionCallback` must become asynchronous (callback-based); audit whether the current sync signature blocks the event loop.
- `/Users/ricardo.moura/projects/misc/sshttpd/sshweb-browser/Libraries/LibSSHWebClient/Client.h` / `Client.cpp` — add `Function<void(u64, ByteString, u16, ByteString, ByteString)> on_tofu_prompt`; replace the auto-accept stub in `tofu_prompt()`.
- `/Users/ricardo.moura/projects/misc/sshttpd/sshweb-browser/Libraries/LibWebView/ViewImplementation.h` — add `Function<void(u64, String host, u16 port, String key_type, String fingerprint)> on_tofu_prompt`.
- `/Users/ricardo.moura/projects/misc/sshttpd/sshweb-browser/UI/AppKit/Interface/LadybirdWebView.mm` — implement `on_tofu_prompt` wiring → `[self.observer onTOFUPrompt:...]`.
- `/Users/ricardo.moura/projects/misc/sshttpd/sshweb-browser/UI/AppKit/Interface/Tab.mm` + `Tab.h` — implement `onTOFUPrompt:host:port:keyType:fingerprint:promptId:`.
- New file: `/Users/ricardo.moura/projects/misc/sshttpd/sshweb-browser/UI/AppKit/Interface/TOFUSheetController.{h,mm}` — `NSWindowController` subclass that owns the sheet NSPanel.

### Key API additions

```objc
// LadybirdWebViewObserver protocol (Tab.h)
- (void)onTOFUPrompt:(u64)promptId
                host:(NSString*)host
                port:(uint16_t)port
             keyType:(NSString*)keyType
         fingerprint:(NSString*)fingerprint;
```

```cpp
// LibSSHWebClient/Client.h
Function<void(u64 prompt_id, ByteString host, u16 port,
              ByteString key_type, ByteString fingerprint_sha256)> on_tofu_prompt;

// LibWebView/ViewImplementation.h
Function<void(u64 prompt_id, String host, u16 port,
              String key_type, String fingerprint_sha256)> on_tofu_prompt;
// Also needs a way to send the decision back:
Function<void(u64 prompt_id, bool accepted)> send_tofu_decision;
```

```cpp
// SSHWebServer/ConnectionFromClient.h
u64 m_next_prompt_id { 1 };
HashMap<u64, Function<void(bool)>> m_pending_tofu;
```

### Step-by-step tasks

**Step 1 — Make SSHWebServer emit the real prompt**

1. In `ConnectionFromClient.cpp::start_request`, replace the inline `TOFUDecisionCallback` lambda that returns `true` with one that:
   - Allocates a `prompt_id = m_next_prompt_id++`.
   - Registers a suspended continuation in `m_pending_tofu[prompt_id]`.
   - Calls `async_tofu_prompt(prompt_id, host, port, key_type, fingerprint)` on the client.
   - **Blocks** by running the IPC event loop until `m_pending_tofu[prompt_id]` is resolved.

   > **Risk:** `SSHWeb::Connection::open` is currently called synchronously from `start_request`.
   > If the event loop is not re-entrant this will deadlock. Audit `Connection::open` — if it runs
   > `ssh-keyscan`-style synchronous I/O there may need to be a thread or async wrapper.
   > The safest short-term fix is to run the SSH connect on a background thread and send the
   > `tofu_decision` result back on the IPC thread. See risks section.

2. Implement `tofu_decision(u64 prompt_id, bool accepted)` in `ConnectionFromClient.cpp` — look up `m_pending_tofu[prompt_id]`, call the stored callback, erase the entry.

**Verification step 1:** Connect to a new `ssh-web://` host with no known-hosts entry. Observe `async_tofu_prompt` fires (add `dbgln`) and that `tofu_decision` is received.

**Step 2 — Thread the callback through to the UI**

1. In `Client.cpp::tofu_prompt`, instead of auto-accepting, call `if (on_tofu_prompt) on_tofu_prompt(prompt_id, host, port, key_type, fingerprint_sha256); else async_tofu_decision(prompt_id, true);`.
2. In `ViewImplementation` (or `WebContent::PageClient`) wire `SSHWebClient::Client::on_tofu_prompt` → fire `page_did_sshweb_tofu_prompt` IPC to the UI process.
3. Add `page_did_sshweb_tofu_prompt(u64 prompt_id, String host, u16 port, String key_type, String fingerprint)` to `Libraries/LibWebView/WebContentClient.ipc`.
4. Handle it in `WebContentClient.cpp` → set `on_tofu_prompt` on the view bridge.
5. Also add `send_sshweb_tofu_decision(u64 prompt_id, bool accepted)` to `Libraries/LibWebView/ViewImplementation` → calls back into WebContent → SSHWebClient → `async_tofu_decision`.

**Verification step 2:** Wire a temporary auto-accept through the full new chain, confirm the page loads.

**Step 3 — TOFUSheetController (AppKit modal)**

Create `UI/AppKit/Interface/TOFUSheetController.{h,mm}`:

```objc
@interface TOFUSheetController : NSObject
- (instancetype)initWithHost:(NSString*)host
                        port:(uint16_t)port
                     keyType:(NSString*)keyType
                 fingerprint:(NSString*)fingerprint;
// Presents as sheet on `window`. Calls `completion` exactly once.
// decision: 0=cancel, 1=session, 2=permanent
- (void)presentOnWindow:(NSWindow*)window
             completion:(void(^)(int decision))completion;
@end
```

Implementation builds an `NSPanel` (or `NSAlert` subclass) with:
- Title: "Untrusted Host Key"
- Informative text: "ssh-web is connecting to **<host>:<port>**.\nKey type: <key_type>\nFingerprint: <fingerprint_sha256>"
- Three buttons: "Trust Permanently", "Trust This Session", "Cancel"
- Uses `[NSApp beginSheet:modalForWindow:...]`.

**Step 4 — Wire TOFUSheetController into Tab.mm**

In `Tab.mm::onTOFUPrompt:host:port:keyType:fingerprint:promptId:`:
```objc
auto* sheet = [[TOFUSheetController alloc] initWithHost:host port:port keyType:keyType fingerprint:fingerprint];
[sheet presentOnWindow:self completion:^(int decision) {
    bool permanent = (decision == 2);
    bool accepted  = (decision != 0);
    if (permanent && accepted) {
        // Write to KnownHosts via IPC or a direct C++ call on the UI process side.
        // For MVP: send accepted=YES and let the server handle persistence via KnownHosts
        // (it already has the key material). Pass a `permanent` flag in tofu_decision.
    }
    [[self tabController] sendTOFUDecision:promptId accepted:accepted];
}];
```

> **Note:** The existing `SSHWebServer.ipc` `tofu_decision` only takes `(u64 prompt_id, bool accepted)`. For MVP, "trust permanently" vs "trust session" can be a second bool: `tofu_decision(u64 prompt_id, bool accepted, bool permanent)`. The server writes `KnownHosts::record()` only when `permanent=true`. Add this to the IPC file.

**Verification step 4:** Navigate to an unknown `ssh-web://` host. Confirm the sheet appears, "Cancel" aborts the load, "Trust This Session" loads the page but does not write a `known_hosts` file, "Trust Permanently" loads the page and writes the file.

### Risks / things to watch for

- **Synchronous SSH connect:** `Connection::open` is called on the IPC dispatch thread. If libssh blocks during key exchange waiting for a `tofu_decision` that requires an IPC round-trip, this will deadlock. Mitigation: move `Connection::open` (and the blocking libssh handshake) to a detached thread; resume the request on the IPC thread after the `tofu_decision` callback fires.
- **Multiple tabs, same host:** Two tabs can each trigger a TOFU prompt for the same host simultaneously. For MVP, allow both prompts to show independently; last-decision wins for permanent storage.
- **IPC ordering:** `tofu_prompt` is a fire-and-forget (`=|`). The response `tofu_decision` travels in the reverse direction. Ensure the server does not process a second `start_request` for the same connection while a TOFU prompt is outstanding (the pool key blocks re-use naturally since the connection isn't open yet).

### Out of scope

- Certificate pinning or key rotation detection (show a harder warning if key changes).
- Exporting or inspecting the known-hosts store from the UI.
- Android / Qt UI frontends (this plan is AppKit-only).

---

## 7C — Identity Management

### Goal

Users can generate named ed25519 keypairs, list and delete them, and map a keypair to a specific
`ssh-web://` origin ("for site.example use identity 'alice'"). Two surfaces: (1) a settings page
at `ssh-web://settings/identities` rendered as an internal HTML page, and (2) a quick-switcher
popover attached to the address-bar button added in 7A.

`IdentityStore` and `IdentityStore::generate()` (which shells out to `ssh-keygen`) already exist
in `Services/SSHWebServer/Identity.{h,cpp}`. The missing pieces are:
- IPC to list/generate/delete identities and set per-origin mappings.
- A per-origin mapping store (new file).
- The internal HTML settings page.
- The AppKit quick-switcher popover.

### Files to touch

- `/Users/ricardo.moura/projects/misc/sshttpd/sshweb-browser/Services/SSHWebServer/SSHWebServer.ipc` — add identity management messages.
- `/Users/ricardo.moura/projects/misc/sshttpd/sshweb-browser/Services/SSHWebServer/ConnectionFromClient.h` / `ConnectionFromClient.cpp` — implement new IPC handlers.
- New file: `/Users/ricardo.moura/projects/misc/sshttpd/sshweb-browser/Services/SSHWebServer/IdentityMapping.{h,cpp}` — persistent per-origin→label map (simple JSON file at `~/.config/sshweb/identity_map.json`).
- `/Users/ricardo.moura/projects/misc/sshttpd/sshweb-browser/Libraries/LibSSHWebClient/Client.h` / `Client.cpp` — add methods mirroring the new IPC messages.
- `/Users/ricardo.moura/projects/misc/sshttpd/sshweb-browser/Libraries/LibWeb/Loader/ResourceLoader.cpp` — intercept `ssh-web://settings/identities` before the normal `dispatch_sshweb_load_request` and serve the internal HTML page.
- `/Users/ricardo.moura/projects/misc/sshttpd/sshweb-browser/Libraries/LibWeb/Loader/GeneratedPagesLoader.{h,cpp}` — add `load_sshweb_settings_identities_page()`.
- New resource file: `UI/AppKit/Resources/sshweb-settings-identities.html` (or under `Libraries/LibWeb/`, whichever resource root is in use).
- `/Users/ricardo.moura/projects/misc/sshttpd/sshweb-browser/UI/AppKit/Interface/TabController.mm` — implement `showIdentitySwitcher:` (popover).
- New file: `/Users/ricardo.moura/projects/misc/sshttpd/sshweb-browser/UI/AppKit/Interface/IdentitySwitcherPopover.{h,mm}`.

### Key API additions

```c
// SSHWebServer.ipc additions
list_identities() => (Vector<ByteString> labels)
generate_identity(ByteString label) => (bool success, ByteString error)
delete_identity(ByteString label) => (bool success, ByteString error)
get_identity_for_origin(ByteString origin) => (Optional<ByteString> label)
set_identity_for_origin(ByteString origin, ByteString label) =|
clear_identity_for_origin(ByteString origin) =|
```

```cpp
// Services/SSHWebServer/IdentityMapping.h
class IdentityMapping {
public:
    static ErrorOr<IdentityMapping> with_default_path();
    Optional<String> label_for_origin(StringView origin) const;
    ErrorOr<void> set(StringView origin, StringView label);
    ErrorOr<void> clear(StringView origin);
private:
    HashMap<String, String> m_map;   // origin → label
    String m_path;
    ErrorOr<void> persist() const;
};
```

```cpp
// LibSSHWebClient/Client.h (new public methods)
ErrorOr<Vector<String>> list_identities();
ErrorOr<Identity> generate_identity(StringView label);
ErrorOr<void> delete_identity(StringView label);
Optional<String> identity_for_origin(StringView origin);
void set_identity_for_origin(StringView origin, StringView label);
```

### Step-by-step tasks

**Step 1 — IdentityMapping store**

Create `Services/SSHWebServer/IdentityMapping.{h,cpp}`. JSON layout: `{ "ssh-web://host:port": "alice" }`. Use `AK::JsonObject` for parsing/serialising (already in tree). Add to `CMakeLists.txt`.

**Verification step 1:** Unit-test with a temp path: set, get, clear, persist+reload.

**Step 2 — IPC messages in SSHWebServer**

Add the six messages listed above to `SSHWebServer.ipc`. Implement handlers in `ConnectionFromClient.cpp`:
- `list_identities` → `IdentityStore::with_default_root()` then `list_labels()`.
- `generate_identity(label)` → `store.generate(label)`.
- `delete_identity(label)` → `FileSystem::remove_all(store.root_dir() + "/" + label)`.
- `get_identity_for_origin / set_identity_for_origin / clear_identity_for_origin` → delegate to `IdentityMapping`.

**Verification step 2:** Send `list_identities` IPC from a test client, confirm empty vector on clean install.

**Step 3 — LibSSHWebClient convenience wrappers**

Add the six synchronous wrapper methods to `Client.h/cpp`. These are thin IPC forwarders. Note: `generate_identity` may take several seconds (ssh-keygen subprocess) — for the MVP it is acceptable to block the calling thread since generation is user-initiated from the settings page, not on a network path.

**Step 4 — Internal settings page route**

In `ResourceLoader::load_resource` (or wherever `dispatch_sshweb_load_request` is called), before the normal path, intercept:

```cpp
if (url.scheme() == "ssh-web"sv) {
    auto sshweb_url = SSHWeb::URL::parse(url.serialize());
    if (!sshweb_url.is_error() && sshweb_url.value().path == "/settings/identities"sv) {
        // Serve internal HTML
        auto html = MUST(load_sshweb_settings_identities_page());
        // ... synthesize headers, call on_data_received / on_complete
        return nullptr;
    }
}
```

The URL `ssh-web://settings/identities` (host = "settings") will not be routed to any real SSH server because the interception happens before `dispatch_sshweb_load_request`.

**Step 5 — Settings HTML page**

Write `load_sshweb_settings_identities_page()` in `GeneratedPagesLoader.cpp`. It returns a self-contained HTML string. The page:
- Calls `window.__sshweb_identities_api.listIdentities()` (a JS binding injected by the browser, or falls back to a `<form>` POST to a synthetic endpoint — see risks).
- For MVP, the simplest approach: generate the HTML server-side (in C++) by calling `IdentityStore::list_labels()` directly when serving the page, embedding the current list as static HTML. Actions (generate, delete, set mapping) use `<form action="ssh-web://settings/identities/action" method="POST">`, and `ResourceLoader` intercepts those too.

Alternatively (simpler for a one-session worker): generate the full page in the C++ handler with the identity list already embedded, and handle POST submissions as new load requests. This avoids needing JS bindings entirely.

**Verification step 5:** Navigate to `ssh-web://settings/identities` — page renders with identity list.

**Step 6 — POST action handler**

In `ResourceLoader`, intercept `ssh-web://settings/identities/action` POST. Parse the form body (plain `application/x-www-form-urlencoded`). Supported actions: `action=generate&label=alice`, `action=delete&label=alice`, `action=set_mapping&origin=ssh-web://host:port&label=alice`. After acting, redirect to `ssh-web://settings/identities` (HTTP 303).

**Verification step 6:** Generate a new identity from the settings page, reload the page, confirm it appears in the list.

**Step 7 — Quick-switcher popover (AppKit)**

`TabController::showIdentitySwitcher:` (wired to the button added in 7A step 4):

```objc
auto* popover = [[IdentitySwitcherPopover alloc]
    initWithOrigin:Ladybird::string_to_ns_string(current_ssh_web_origin)
         webClient:ssh_web_client_ref];
[popover showRelativeToRect:[_identity_button bounds]
                     ofView:_identity_button
             preferredEdge:NSRectEdgeMaxY];
```

`IdentitySwitcherPopover.mm` fetches `list_identities()` and `identity_for_origin()` synchronously (acceptable — small local IPC), shows an `NSPopover` with a `NSTableView` of labels (radio-style), a "New…" button that prompts for a label and calls `generate_identity`, and a "None" row.

**Verification step 7:** On an `ssh-web://` page, click the lock/person button — popover lists identities, selecting one updates the mapping.

### Risks / things to watch for

- **`generate_identity` latency:** `ssh-keygen` takes 0.5–2 s. Run it asynchronously in the popover (show a spinner) or accept the brief UI hang for MVP settings-page flow.
- **Settings page JS API:** If the page needs live updates without reload, a JS binding is required. For MVP, stick to the redirect-after-POST pattern (no JS bindings needed).
- **`ssh-web://settings` as a fake host:** The SSH connection code will never see this host because the route is intercepted first. Ensure the interception happens strictly *before* `dispatch_sshweb_load_request` and before any `fetch_capabilities_async` call to avoid a failed capabilities fetch log noise.
- **Per-origin key selection in SSHWebServer:** `ConnectionFromClient::start_request` must call `IdentityMapping::label_for_origin(origin)` and pass the keypair path to `Connection::open`. Currently `Connection::open` takes an identity path as `{}` (empty = default). Ensure `Connection.h` already accepts `Optional<String> identity_path` or add it.

### Out of scope

- Passphrase-protected keys (ed25519 keys are generated with `-N ""` = no passphrase).
- Importing existing keys.
- SSH agent integration.
- Per-identity known-hosts namespacing.
- Android / Qt frontends.

---

## Cross-cutting risks

| Risk | Affects | Mitigation |
|---|---|---|
| **Manifest cache timing:** `on_sshweb_manifest_ready` fires after `on_url_change`. The address bar may briefly show no pill on fast loads. | 7A | Re-apply pill in `onSSHWebManifestLoaded:` regardless of current URL; clear only on `onLoadStart:`. |
| **IPC endpoint churn:** 7A, 7B, 7C all add messages to `WebContentClient.ipc` and/or `SSHWebServer.ipc`. Agents must not add conflicting message IDs. | All | Each agent owns a distinct message namespace: 7A adds `page_did_sshweb_manifest_load`; 7B adds `page_did_sshweb_tofu_prompt` + `tofu_decision` extension; 7C adds the six identity management messages to `SSHWebServer.ipc` only. No overlap. |
| **`SSHWebClient::Client` shared object:** 7A, 7B, and 7C all add fields/callbacks to `Client.h`. These are purely additive; merge conflicts are low risk but agents should pull from main before starting. | All | Each subplan touches a distinct section of `Client.h`. No method signature clashes if the IPC namespace rule above is followed. |
| **Blocking SSH connect vs. async TOFU:** 7B restructures `ConnectionFromClient::start_request` from sync to async. 7C adds identity selection *before* `Connection::open`. 7B must land (or its Connection threading refactor must land) before 7C's identity-path changes to `Connection::open` are safe to use. | 7B before 7C integration | Agents can develop independently but final integration of 7C's `Connection::open` identity-path change needs 7B's threading to be in place. |
| **Address bar button (7A) needed by 7C:** The identity-switcher button added in 7A step 4 is the entry point for 7C's popover. 7C can stub `showIdentitySwitcher:` but cannot test the full flow until 7A step 4 is merged. | 7A before 7C popover | 7C can develop and test the settings page route (steps 1–6) fully independently. Only step 7 (the popover) depends on 7A. |
