# sshweb-browser Fork Notes

This fork tracks `LadybirdBrowser/ladybird` and adds support for the SSH-Web protocol behind the `LADYBIRD_ENABLE_SSHWEB` build flag (default `ON`).

## Build flag

```bash
# Default — full sshweb-browser
cmake -DLADYBIRD_ENABLE_SSHWEB=ON ...

# Vanilla Ladybird (no SSH-Web changes compiled in)
cmake -DLADYBIRD_ENABLE_SSHWEB=OFF ...
```

`OFF` exists for upstream-bisecting and to keep the fork as a strict superset of Ladybird.

## Added components

| Path | Eventual purpose |
|---|---|
| `Libraries/LibSSHWeb/` | Shared types: protocol version, manifest schema, IPC messages, URL parsing. |
| `Services/SSHWebServer/` | Out-of-process SSH-Web client: connection pool, key store, capabilities cache. |
| `Tests/LibSSHWeb/` | Unit tests for `LibSSHWeb`. |

Plan 1 status: scaffolding only — `LibSSHWeb` currently contains only `Version.h`, and `SSHWebServer` is a libssh2-linked stub that prints version info and exits. Protocol logic arrives in Plans 2-8.

## Upstream rebase cadence

Monthly. After each rebase, run `cmake -DLADYBIRD_ENABLE_SSHWEB=OFF ...` and confirm the upstream test suite still passes — we should not regress vanilla Ladybird.

## Versioning

The fork's version tracks the SSH-Web protocol version, not Ladybird's. The constant `SSHWeb::implementation_version` in `Libraries/LibSSHWeb/Version.h` is authoritative.

## Contributing back

We aim to upstream changes that are useful regardless of SSH-Web (e.g., generic improvements to LibIPC or LibURL). Fork-specific changes live only in the fork.
