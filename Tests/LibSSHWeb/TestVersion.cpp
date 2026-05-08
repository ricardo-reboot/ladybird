/*
 * Copyright (c) 2026, the sshweb-browser developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibSSHWeb/Version.h>
#include <LibTest/TestCase.h>

TEST_CASE(protocol_version_matches_spec)
{
    EXPECT_EQ(SSHWeb::protocol_version, "ssh-web/0.1"sv);
}

TEST_CASE(implementation_version_is_semver)
{
    auto v = SSHWeb::implementation_version;
    EXPECT(!v.is_empty());
    EXPECT(v.contains('.'));
}
