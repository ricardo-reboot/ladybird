/*
 * Copyright (c) 2026, Ricardo Moura <ricardo@bugscave.dev>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#import <Cocoa/Cocoa.h>

@class Tab;

@interface IdentityPopover : NSObject <NSPopoverDelegate>

- (void)showRelativeToView:(NSView*)view tab:(Tab*)tab;
- (void)close;

@end
