/*
 * Copyright (c) 2026, the sshweb-browser developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/MemoryStream.h>
#include <LibCompress/Zlib.h>
#include <LibSSHWeb/Packfile.h>

namespace SSHWeb {

// Parse the variable-length type+size header used by Git packfiles.
// Format: first byte is `tttssss` with continuation in bit 7,
// where `ttt` is the object type and `ssss` is the low 4 bits of size.
// Subsequent bytes are 7 bits of size each, with continuation in bit 7.
//
// Advances `cursor` past the header. Stores type in *out_type.
// Returns the decoded object size in bytes (uncompressed).
static ErrorOr<size_t> parse_object_header(ReadonlyBytes data, size_t& cursor, PackfileObjectType& out_type)
{
    if (cursor >= data.size())
        return Error::from_string_literal("packfile: truncated object header");

    auto byte = data[cursor++];
    out_type = static_cast<PackfileObjectType>((byte >> 4) & 0x07);
    size_t size = byte & 0x0f;
    size_t shift = 4;

    while (byte & 0x80) {
        if (cursor >= data.size())
            return Error::from_string_literal("packfile: truncated size encoding");
        byte = data[cursor++];
        size |= static_cast<size_t>(byte & 0x7f) << shift;
        shift += 7;
    }
    return size;
}

ErrorOr<Vector<PackfileObject>> decode_packfile(ReadonlyBytes bytes)
{
    // Minimum valid packfile: 12-byte header + 20-byte SHA-1 trailer = 32 bytes.
    if (bytes.size() < 32)
        return Error::from_string_literal("packfile too small");

    if (memcmp(bytes.data(), "PACK", 4) != 0)
        return Error::from_string_literal("packfile: missing PACK signature");

    auto big_endian_u32 = [](u8 const* p) {
        return (static_cast<u32>(p[0]) << 24) | (static_cast<u32>(p[1]) << 16)
            | (static_cast<u32>(p[2]) << 8) | static_cast<u32>(p[3]);
    };
    u32 version = big_endian_u32(bytes.data() + 4);
    if (version != 2)
        return Error::from_string_literal("packfile: unsupported version");
    u32 object_count = big_endian_u32(bytes.data() + 8);

    Vector<PackfileObject> objects;
    size_t cursor = 12; // past the header
    auto end = bytes.size() - 20; // exclude trailing SHA-1

    for (u32 i = 0; i < object_count; ++i) {
        if (cursor >= end)
            return Error::from_string_literal("packfile: ran out of bytes mid-object");

        PackfileObjectType type = PackfileObjectType::Unknown;
        size_t expected_size = TRY(parse_object_header(bytes, cursor, type));

        // Decompress the next zlib stream starting at `cursor`. Streaming
        // ZlibDecompressor reads only what it needs and stops at end-of-stream,
        // which lets us advance `cursor` to the next object's header.
        auto stream = make<FixedMemoryStream>(bytes.slice(cursor, end - cursor));
        auto* stream_ptr = stream.ptr();
        auto decompressor = TRY(Compress::ZlibDecompressor::create(move(stream)));

        ByteBuffer body;
        TRY(body.try_resize(expected_size));
        size_t read_so_far = 0;
        while (read_so_far < expected_size) {
            auto chunk = TRY(decompressor->read_some(body.bytes().slice(read_so_far)));
            if (chunk.is_empty())
                break;
            read_so_far += chunk.size();
        }
        if (read_so_far != expected_size)
            return Error::from_string_literal("packfile: short read on zlib stream");

        // Advance cursor by however many compressed bytes the stream consumed.
        cursor += stream_ptr->offset();

        objects.append(PackfileObject {
            .type = type,
            .data = move(body),
        });
    }

    return objects;
}

ErrorOr<ByteBuffer> first_blob_in_packfile(ReadonlyBytes bytes)
{
    // Plan 5 MVP: only decode the first object. sshttpd's writer emits
    // blobs in directory-walk order (alphabetical), and the first file in
    // a typical site root is index.html, so this returns the document we
    // want for the common case.
    //
    // Multi-object packfile traversal is harder than it looks because the
    // streaming zlib decompressor reads ahead into its buffer, making the
    // cursor advance ambiguous. Plan 6 / a follow-up will revisit this
    // (likely by using the zlib total_in counter or a custom byte-by-byte
    // input adapter).
    if (bytes.size() < 32 || memcmp(bytes.data(), "PACK", 4) != 0)
        return Error::from_string_literal("packfile: missing PACK signature");

    size_t cursor = 12; // past PACK header
    auto end = bytes.size() - 20; // exclude trailing SHA-1

    PackfileObjectType type = PackfileObjectType::Unknown;
    size_t expected_size = TRY(parse_object_header(bytes, cursor, type));

    if (type != PackfileObjectType::Blob)
        return Error::from_string_literal("packfile: first object is not a blob");

    auto stream = make<FixedMemoryStream>(bytes.slice(cursor, end - cursor));
    auto decompressor = TRY(Compress::ZlibDecompressor::create(move(stream)));

    ByteBuffer body;
    TRY(body.try_resize(expected_size));
    size_t read_so_far = 0;
    while (read_so_far < expected_size) {
        auto chunk = TRY(decompressor->read_some(body.bytes().slice(read_so_far)));
        if (chunk.is_empty())
            break;
        read_so_far += chunk.size();
    }
    if (read_so_far != expected_size)
        return Error::from_string_literal("packfile: short read on zlib stream");

    return body;
}

}
