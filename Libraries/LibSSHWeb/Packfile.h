/*
 * Copyright (c) 2026, the sshweb-browser developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/ByteBuffer.h>
#include <AK/Error.h>
#include <AK/Span.h>

namespace SSHWeb {

// Parsed Git packfile object.
enum class PackfileObjectType {
    Blob = 3,
    Tree = 2,
    Unknown = 0,
};

struct PackfileObject {
    PackfileObjectType type;
    ByteBuffer data; // zlib-decompressed object body
};

// Parses the PACK v2 packfile in `bytes` and returns all decompressed objects.
//
// Format (per Git's pack-format docs):
//   - 4 bytes: "PACK" magic
//   - 4 bytes: version (big-endian uint32; we expect 2)
//   - 4 bytes: object count (big-endian uint32)
//   - N objects, each:
//       * variable-length header encoding (type, size)
//       * zlib-compressed object data
//   - Trailing 20-byte SHA-1 of everything before
//
// Plan 5 MVP only handles non-delta objects (blob, tree). Returns the
// objects in pack order.
ErrorOr<Vector<PackfileObject>> decode_packfile(ReadonlyBytes bytes);

// Convenience: decode and return the first blob in the packfile, or an
// error if none exists. Used by ResourceLoader's MVP path since our
// test fixture serves a single file.
ErrorOr<ByteBuffer> first_blob_in_packfile(ReadonlyBytes bytes);

}
