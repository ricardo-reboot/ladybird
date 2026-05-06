/*
 * Copyright (c) 2026, the sshweb-browser developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#import <Cocoa/Cocoa.h>
#include <AK/Types.h>

// === Plan 7B TOFU ===
// Presents a sheet asking the user whether to trust an unknown SSH host key.
// decision values passed to the completion block:
//   0 = Cancel  (connection aborted)
//   1 = Trust this session only
//   2 = Trust permanently  (writes to known_hosts)
@interface TOFUSheetController : NSObject

- (instancetype)initWithHost:(NSString*)host
                        port:(uint16_t)port
                     keyType:(NSString*)keyType
                 fingerprint:(NSString*)fingerprint;

// Presents the sheet on `window`.  Calls `completion` exactly once.
- (void)presentOnWindow:(NSWindow*)window
             completion:(void (^)(int decision))completion;

@end
// === End Plan 7B TOFU ===
