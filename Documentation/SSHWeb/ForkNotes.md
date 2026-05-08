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

## Smoke testing against sshttpd (after Plan 3)

Build sshttpd from the sibling repo, then drive it from sshweb-browser:

```bash
# In the sshttpd repo
cd /Users/ricardo.moura/projects/misc/sshttpd
go build -o sshttpd ./cmd/sshttpd

# Configure a local fixture
mkdir -p /tmp/sshweb-fixture/keys /tmp/sshweb-fixture/site
cat > /tmp/sshweb-fixture/sshttpd.conf <<'EOF'
site localhost {
    port 32443
    host-key /tmp/sshweb-fixture/keys/host_ed25519
    root /tmp/sshweb-fixture/site
    commands { receive-pack / }
    auth { anonymous [receive-pack] }
}
EOF
echo '<h1>fixture</h1>' > /tmp/sshweb-fixture/site/index.html

# Start sshttpd
./sshttpd -config /tmp/sshweb-fixture/sshttpd.conf &

# Connect from sshweb-browser
cd /Users/ricardo.moura/projects/misc/sshttpd/sshweb-browser
./Build/release/bin/SSHWebServer \
    --connect ssh-web://localhost:32443 \
    --command capabilities \
    --accept-host-key
```

The browser prints the parsed `CapabilitiesManifest`. Subsequent connections re-use the host key.

Known-hosts location is platform-dependent (per `Core::StandardPaths::config_directory()`):
- macOS: `~/Library/Preferences/sshweb/known_hosts/`
- Linux: `~/.config/sshweb/known_hosts/`

## Upstream rebase cadence

Monthly. After each rebase, run `cmake -DLADYBIRD_ENABLE_SSHWEB=OFF ...` and confirm the upstream test suite still passes — we should not regress vanilla Ladybird.

## Versioning

The fork's version tracks the SSH-Web protocol version, not Ladybird's. The constant `SSHWeb::implementation_version` in `Libraries/LibSSHWeb/Version.h` is authoritative.

## Contributing back

We aim to upstream changes that are useful regardless of SSH-Web (e.g., generic improvements to LibIPC or LibURL). Fork-specific changes live only in the fork.
