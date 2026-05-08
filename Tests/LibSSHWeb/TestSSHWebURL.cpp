/*
 * Copyright (c) 2026, the sshweb-browser developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibSSHWeb/URL.h>
#include <LibTest/TestCase.h>

TEST_CASE(parse_minimal)
{
    auto url = MUST(SSHWeb::URL::parse("ssh-web://example.com"sv));
    EXPECT_EQ(url.host, "example.com"sv);
    EXPECT_EQ(url.port, 22443);
    EXPECT_EQ(url.path, "/"sv);
}

TEST_CASE(parse_with_explicit_port)
{
    auto url = MUST(SSHWeb::URL::parse("ssh-web://example.com:1234"sv));
    EXPECT_EQ(url.host, "example.com"sv);
    EXPECT_EQ(url.port, 1234);
    EXPECT_EQ(url.path, "/"sv);
}

TEST_CASE(parse_with_path)
{
    auto url = MUST(SSHWeb::URL::parse("ssh-web://news.ycombinator.com/item/12345"sv));
    EXPECT_EQ(url.host, "news.ycombinator.com"sv);
    EXPECT_EQ(url.port, 22443);
    EXPECT_EQ(url.path, "/item/12345"sv);
}

TEST_CASE(parse_with_explicit_port_and_path)
{
    auto url = MUST(SSHWeb::URL::parse("ssh-web://h.example:2222/a/b"sv));
    EXPECT_EQ(url.host, "h.example"sv);
    EXPECT_EQ(url.port, 2222);
    EXPECT_EQ(url.path, "/a/b"sv);
}

TEST_CASE(reject_wrong_scheme)
{
    auto result = SSHWeb::URL::parse("https://example.com"sv);
    EXPECT(result.is_error());
}

TEST_CASE(reject_no_host)
{
    auto result = SSHWeb::URL::parse("ssh-web://"sv);
    EXPECT(result.is_error());
}

TEST_CASE(reject_invalid_port)
{
    auto result = SSHWeb::URL::parse("ssh-web://example.com:abc"sv);
    EXPECT(result.is_error());
}

TEST_CASE(reject_port_out_of_range)
{
    auto result = SSHWeb::URL::parse("ssh-web://example.com:99999"sv);
    EXPECT(result.is_error());
}

TEST_CASE(serialize_round_trip_minimal)
{
    auto url = MUST(SSHWeb::URL::parse("ssh-web://example.com"sv));
    EXPECT_EQ(url.serialize(), "ssh-web://example.com/"sv);
}

TEST_CASE(serialize_omits_default_port)
{
    SSHWeb::URL url { .host = "example.com"_string, .port = 22443, .path = "/"_string };
    EXPECT_EQ(url.serialize(), "ssh-web://example.com/"sv);
}

TEST_CASE(serialize_includes_explicit_port)
{
    SSHWeb::URL url { .host = "example.com"_string, .port = 2222, .path = "/x"_string };
    EXPECT_EQ(url.serialize(), "ssh-web://example.com:2222/x"sv);
}
