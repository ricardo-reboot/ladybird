/*
 * Copyright (c) 2026, the sshweb-browser developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Random.h>
#include <AK/String.h>
#include <LibCore/System.h>
#include <LibTest/TestCase.h>
#include <Services/SSHWebServer/KnownHosts.h>

static String make_temp_dir()
{
    auto path = MUST(String::formatted("/tmp/sshweb-known-hosts-test-{}", get_random<u64>()));
    MUST(Core::System::mkdir(path, 0700));
    return path;
}

TEST_CASE(empty_store_has_no_entries)
{
    auto dir = make_temp_dir();
    SSHWeb::KnownHosts store { dir };
    auto entry = MUST(store.lookup("example.com"sv, 22443));
    EXPECT(!entry.has_value());
}

TEST_CASE(record_then_lookup)
{
    auto dir = make_temp_dir();
    SSHWeb::KnownHosts store { dir };
    MUST(store.record("example.com"sv, 22443,
        SSHWeb::HostKey { .type = "ed25519"_string, .fingerprint_sha256 = "AAAAA"_string }));
    auto entry = MUST(store.lookup("example.com"sv, 22443));
    EXPECT(entry.has_value());
    EXPECT_EQ(entry->key.type, "ed25519"sv);
    EXPECT_EQ(entry->key.fingerprint_sha256, "AAAAA"sv);
}

TEST_CASE(lookup_is_per_host_port)
{
    auto dir = make_temp_dir();
    SSHWeb::KnownHosts store { dir };
    MUST(store.record("example.com"sv, 22443,
        SSHWeb::HostKey { .type = "ed25519"_string, .fingerprint_sha256 = "AAAAA"_string }));

    auto same_host_diff_port = MUST(store.lookup("example.com"sv, 22444));
    EXPECT(!same_host_diff_port.has_value());
    auto diff_host_same_port = MUST(store.lookup("other.example"sv, 22443));
    EXPECT(!diff_host_same_port.has_value());
}

TEST_CASE(record_persists_across_instances)
{
    auto dir = make_temp_dir();
    {
        SSHWeb::KnownHosts store { dir };
        MUST(store.record("h.example"sv, 1234,
            SSHWeb::HostKey { .type = "ed25519"_string, .fingerprint_sha256 = "ZZZZZ"_string }));
    }
    SSHWeb::KnownHosts store2 { dir };
    auto entry = MUST(store2.lookup("h.example"sv, 1234));
    EXPECT(entry.has_value());
    EXPECT_EQ(entry->key.fingerprint_sha256, "ZZZZZ"sv);
}

TEST_CASE(rerecord_overwrites_with_new_fingerprint)
{
    auto dir = make_temp_dir();
    SSHWeb::KnownHosts store { dir };
    MUST(store.record("h.example"sv, 22443,
        SSHWeb::HostKey { .type = "ed25519"_string, .fingerprint_sha256 = "OLD"_string }));
    MUST(store.record("h.example"sv, 22443,
        SSHWeb::HostKey { .type = "ed25519"_string, .fingerprint_sha256 = "NEW"_string }));
    auto entry = MUST(store.lookup("h.example"sv, 22443));
    EXPECT_EQ(entry->key.fingerprint_sha256, "NEW"sv);
}

TEST_CASE(forget_removes_entry)
{
    auto dir = make_temp_dir();
    SSHWeb::KnownHosts store { dir };
    MUST(store.record("h.example"sv, 22443,
        SSHWeb::HostKey { .type = "ed25519"_string, .fingerprint_sha256 = "X"_string }));
    MUST(store.forget("h.example"sv, 22443));
    auto entry = MUST(store.lookup("h.example"sv, 22443));
    EXPECT(!entry.has_value());
}
