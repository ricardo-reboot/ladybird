/*
 * Copyright (c) 2026, the sshweb-browser developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/JsonArray.h>
#include <AK/JsonObject.h>
#include <AK/JsonValue.h>
#include <LibSSHWeb/Manifest.h>
#include <LibSSHWeb/Version.h>

namespace SSHWeb {

static ErrorOr<String> required_string(JsonObject const& obj, StringView key)
{
    auto value = obj.get(key);
    if (!value.has_value() || !value->is_string())
        return Error::from_string_literal("Missing or non-string field");
    return value->as_string();
}

static Optional<String> optional_string(JsonObject const& obj, StringView key)
{
    auto value = obj.get(key);
    if (!value.has_value() || !value->is_string())
        return {};
    return value->as_string();
}

static ErrorOr<Vector<String>> parse_string_array(JsonValue const& v)
{
    if (!v.is_array())
        return Error::from_string_literal("Expected array");
    Vector<String> out;
    for (auto const& element : v.as_array().values()) {
        if (!element.is_string())
            return Error::from_string_literal("Array contains non-string element");
        out.append(element.as_string());
    }
    return out;
}

ErrorOr<CapabilitiesManifest> CapabilitiesManifest::parse(StringView json)
{
    auto value = TRY(JsonValue::from_string(json));
    if (!value.is_object())
        return Error::from_string_literal("Manifest is not a JSON object");
    auto const& root = value.as_object();

    CapabilitiesManifest manifest;

    manifest.protocol = TRY(required_string(root, "protocol"sv));
    if (manifest.protocol != protocol_version)
        return Error::from_string_literal("Manifest protocol version mismatch");

    auto site_value = root.get("site"sv);
    if (!site_value.has_value() || !site_value->is_object())
        return Error::from_string_literal("Manifest missing 'site' object");
    auto const& site_obj = site_value->as_object();
    manifest.site.name = TRY(required_string(site_obj, "name"sv));
    manifest.site.host = TRY(required_string(site_obj, "host"sv));
    manifest.site.description = optional_string(site_obj, "description"sv);

    auto commands_value = root.get("commands"sv);
    if (!commands_value.has_value() || !commands_value->is_object())
        return Error::from_string_literal("Manifest missing 'commands' object");
    Optional<Error> command_error;
    commands_value->as_object().for_each_member([&](String const& name, JsonValue const& command_value) {
        if (command_error.has_value())
            return;
        if (!command_value.is_object()) {
            command_error = Error::from_string_literal("Command entry is not an object");
            return;
        }
        auto const& command_obj = command_value.as_object();
        ManifestCommand command;
        command.name = name;
        command.description = optional_string(command_obj, "description"sv);
        if (auto routes = command_obj.get("routes"sv); routes.has_value()) {
            // Tolerate either array (e.g. receive-pack: ["/", "/about"]) or object
            // (e.g. api-call: {"GET /api/posts": {...}}). For the object form, harvest the keys.
            if (routes->is_array()) {
                auto parsed = parse_string_array(*routes);
                if (parsed.is_error()) { command_error = parsed.release_error(); return; }
                command.routes = parsed.release_value();
            } else if (routes->is_object()) {
                routes->as_object().for_each_member([&](StringView key, JsonValue const&) {
                    command.routes.append(MUST(String::from_utf8(key)));
                });
            }
        }
        if (auto supports = command_obj.get("supports"sv); supports.has_value() && supports->is_array()) {
            auto parsed = parse_string_array(*supports);
            if (parsed.is_error()) { command_error = parsed.release_error(); return; }
            command.supports = parsed.release_value();
        }
        manifest.commands.append(move(command));
    });
    if (command_error.has_value())
        return command_error.release_value();

    auto auth_value = root.get("auth"sv);
    if (!auth_value.has_value() || !auth_value->is_object())
        return Error::from_string_literal("Manifest missing 'auth' object");
    auto const& auth_obj = auth_value->as_object();
    if (auto modes = auth_obj.get("modes"sv); modes.has_value())
        manifest.auth.modes = TRY(parse_string_array(*modes));
    if (auto key_types = auth_obj.get("key-types"sv); key_types.has_value())
        manifest.auth.key_types = TRY(parse_string_array(*key_types));

    // Parse proxy-cache allowlist from commands["proxy-call"]["allowed-origins"].
    // The server serializes this as part of the commands object (not a top-level key).
    if (commands_value.has_value() && commands_value->is_object()) {
        if (auto proxy_call = commands_value->as_object().get("proxy-call"sv); proxy_call.has_value() && proxy_call->is_object()) {
            if (auto allowed = proxy_call->as_object().get("allowed-origins"sv); allowed.has_value() && allowed->is_array()) {
                ManifestProxyCache pc;
                for (auto const& element : allowed->as_array().values()) {
                    if (element.is_string())
                        pc.allow.append(element.as_string());
                }
                manifest.proxy_cache = move(pc);
            }
        }
    }

    if (auto mcp_value = root.get("mcp"sv); mcp_value.has_value() && mcp_value->is_object()) {
        auto const& mcp_obj = mcp_value->as_object();
        ManifestMCP mcp;
        mcp.version = TRY(required_string(mcp_obj, "version"sv));
        if (auto tools = mcp_obj.get("tools"sv); tools.has_value() && tools->is_array()) {
            for (auto const& tool_value : tools->as_array().values()) {
                if (!tool_value.is_object())
                    return Error::from_string_literal("MCP tool is not an object");
                auto const& tool_obj = tool_value.as_object();
                ManifestMCPTool tool;
                tool.name = TRY(required_string(tool_obj, "name"sv));
                tool.auth = TRY(required_string(tool_obj, "auth"sv));
                tool.description = optional_string(tool_obj, "description"sv);
                mcp.tools.append(move(tool));
            }
        }
        manifest.mcp = move(mcp);
    }

    return manifest;
}

ErrorOr<String> CapabilitiesManifest::serialize() const
{
    JsonObject root;
    root.set("protocol"sv, protocol);

    JsonObject site_obj;
    site_obj.set("name"sv, site.name);
    site_obj.set("host"sv, site.host);
    if (site.description.has_value())
        site_obj.set("description"sv, *site.description);
    root.set("site"sv, move(site_obj));

    JsonObject commands_obj;
    for (auto const& command : commands) {
        JsonObject command_obj;
        if (command.description.has_value())
            command_obj.set("description"sv, *command.description);
        if (!command.routes.is_empty()) {
            JsonArray routes;
            for (auto const& r : command.routes)
                routes.must_append(r);
            command_obj.set("routes"sv, move(routes));
        }
        if (!command.supports.is_empty()) {
            JsonArray supports;
            for (auto const& s : command.supports)
                supports.must_append(s);
            command_obj.set("supports"sv, move(supports));
        }
        commands_obj.set(command.name, move(command_obj));
    }
    root.set("commands"sv, move(commands_obj));

    JsonObject auth_obj;
    JsonArray modes;
    for (auto const& m : auth.modes)
        modes.must_append(m);
    auth_obj.set("modes"sv, move(modes));
    JsonArray key_types;
    for (auto const& k : auth.key_types)
        key_types.must_append(k);
    auth_obj.set("key-types"sv, move(key_types));
    root.set("auth"sv, move(auth_obj));

    if (mcp.has_value()) {
        JsonObject mcp_obj;
        mcp_obj.set("version"sv, mcp->version);
        JsonArray tools;
        for (auto const& tool : mcp->tools) {
            JsonObject tool_obj;
            tool_obj.set("name"sv, tool.name);
            tool_obj.set("auth"sv, tool.auth);
            if (tool.description.has_value())
                tool_obj.set("description"sv, *tool.description);
            tools.must_append(move(tool_obj));
        }
        mcp_obj.set("tools"sv, move(tools));
        root.set("mcp"sv, move(mcp_obj));
    }

    return root.serialized();
}

}
