/*
 * Copyright (c) 2026, the sshweb-browser developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibSSHWeb/Manifest.h>
#include <LibTest/TestCase.h>

static constexpr auto MINIMAL_MANIFEST = R"(
{
    "protocol": "ssh-web/0.1",
    "site": {
        "name": "Example",
        "host": "example.com"
    },
    "commands": {
        "receive-pack": {
            "description": "Fetch site content as packfile",
            "routes": ["/"],
            "supports": ["delta", "incremental"]
        }
    },
    "auth": {
        "modes": ["anonymous"],
        "key-types": ["ed25519"]
    }
}
)"sv;

TEST_CASE(parse_minimal)
{
    auto manifest = MUST(SSHWeb::CapabilitiesManifest::parse(MINIMAL_MANIFEST));
    EXPECT_EQ(manifest.protocol, "ssh-web/0.1"sv);
    EXPECT_EQ(manifest.site.name, "Example"sv);
    EXPECT_EQ(manifest.site.host, "example.com"sv);
    EXPECT_EQ(manifest.commands.size(), 1u);
    EXPECT_EQ(manifest.commands[0].name, "receive-pack"sv);
    EXPECT_EQ(manifest.commands[0].routes.size(), 1u);
    EXPECT_EQ(manifest.commands[0].routes[0], "/"sv);
    EXPECT_EQ(manifest.auth.modes.size(), 1u);
    EXPECT_EQ(manifest.auth.modes[0], "anonymous"sv);
}

TEST_CASE(parse_with_mcp_tools)
{
    constexpr auto json = R"(
    {
        "protocol": "ssh-web/0.1",
        "site": { "name": "S", "host": "s.example" },
        "commands": {},
        "auth": { "modes": ["identified"], "key-types": ["ed25519"] },
        "mcp": {
            "version": "1.0",
            "tools": [
                { "name": "submit", "auth": "identified", "description": "Submit something" },
                { "name": "vote", "auth": "identified" }
            ]
        }
    }
    )"sv;

    auto manifest = MUST(SSHWeb::CapabilitiesManifest::parse(json));
    EXPECT(manifest.mcp.has_value());
    EXPECT_EQ(manifest.mcp->version, "1.0"sv);
    EXPECT_EQ(manifest.mcp->tools.size(), 2u);
    EXPECT_EQ(manifest.mcp->tools[0].name, "submit"sv);
    EXPECT_EQ(manifest.mcp->tools[0].auth, "identified"sv);
    EXPECT(manifest.mcp->tools[0].description.has_value());
    EXPECT_EQ(*manifest.mcp->tools[0].description, "Submit something"sv);
    EXPECT_EQ(manifest.mcp->tools[1].name, "vote"sv);
    EXPECT(!manifest.mcp->tools[1].description.has_value());
}

TEST_CASE(reject_invalid_json)
{
    auto result = SSHWeb::CapabilitiesManifest::parse("{ not valid"sv);
    EXPECT(result.is_error());
}

TEST_CASE(reject_missing_protocol)
{
    constexpr auto json = R"(
    {
        "site": { "name": "S", "host": "s.example" },
        "commands": {},
        "auth": { "modes": ["anonymous"], "key-types": ["ed25519"] }
    }
    )"sv;
    auto result = SSHWeb::CapabilitiesManifest::parse(json);
    EXPECT(result.is_error());
}

TEST_CASE(reject_wrong_protocol_version)
{
    constexpr auto json = R"(
    {
        "protocol": "ssh-web/9.9",
        "site": { "name": "S", "host": "s.example" },
        "commands": {},
        "auth": { "modes": ["anonymous"], "key-types": ["ed25519"] }
    }
    )"sv;
    auto result = SSHWeb::CapabilitiesManifest::parse(json);
    EXPECT(result.is_error());
}

TEST_CASE(serialize_round_trip)
{
    auto manifest = MUST(SSHWeb::CapabilitiesManifest::parse(MINIMAL_MANIFEST));
    auto serialized = MUST(manifest.serialize());
    auto reparsed = MUST(SSHWeb::CapabilitiesManifest::parse(serialized.bytes_as_string_view()));
    EXPECT_EQ(reparsed.protocol, manifest.protocol);
    EXPECT_EQ(reparsed.site.host, manifest.site.host);
    EXPECT_EQ(reparsed.commands.size(), manifest.commands.size());
}
