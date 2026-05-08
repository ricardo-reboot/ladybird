/*
 * Copyright (c) 2026, the sshweb-browser developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/StringView.h>

namespace SSHWeb {

// High-level error categories. Detailed messages flow through AK::Error;
// this enum is for code that needs to branch on the kind of failure
// (e.g. the address bar showing different copy for a network error vs a host-key mismatch).
enum class SSHWebError {
    InvalidURL,
    ConnectionRefused,
    HandshakeFailed,
    HostKeyUnknown,
    HostKeyMismatch,
    AuthenticationFailed,
    ProtocolVersionMismatch,
    InvalidManifest,
    ProxyDenied,
    Cancelled,
};

constexpr StringView to_string(SSHWebError error)
{
    switch (error) {
    case SSHWebError::InvalidURL:               return "InvalidURL"sv;
    case SSHWebError::ConnectionRefused:        return "ConnectionRefused"sv;
    case SSHWebError::HandshakeFailed:          return "HandshakeFailed"sv;
    case SSHWebError::HostKeyUnknown:           return "HostKeyUnknown"sv;
    case SSHWebError::HostKeyMismatch:          return "HostKeyMismatch"sv;
    case SSHWebError::AuthenticationFailed:     return "AuthenticationFailed"sv;
    case SSHWebError::ProtocolVersionMismatch:  return "ProtocolVersionMismatch"sv;
    case SSHWebError::InvalidManifest:          return "InvalidManifest"sv;
    case SSHWebError::ProxyDenied:              return "ProxyDenied"sv;
    case SSHWebError::Cancelled:                return "Cancelled"sv;
    }
    VERIFY_NOT_REACHED();
}

}
