/*
 * Copyright (c) 2026, the sshweb-browser developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

// === Plan 7B TOFU ===

#import <Interface/TOFUSheetController.h>

#if !__has_feature(objc_arc)
#    error "This file requires ARC"
#endif

@interface TOFUSheetController ()

@property (nonatomic, copy) NSString* host;
@property (nonatomic, assign) uint16_t port;
@property (nonatomic, copy) NSString* keyType;
@property (nonatomic, copy) NSString* fingerprint;

// Retain the panel so it stays alive until the sheet is dismissed.
@property (nonatomic, strong) NSPanel* panel;

@end

@implementation TOFUSheetController

- (instancetype)initWithHost:(NSString*)host
                        port:(uint16_t)port
                     keyType:(NSString*)keyType
                 fingerprint:(NSString*)fingerprint
{
    if (self = [super init]) {
        _host = [host copy];
        _port = port;
        _keyType = [keyType copy];
        _fingerprint = [fingerprint copy];
    }
    return self;
}

- (void)presentOnWindow:(NSWindow*)window
             completion:(void (^)(int decision))completion
{
    // Build the alert with three custom buttons.
    NSAlert* alert = [[NSAlert alloc] init];
    [alert setMessageText:@"Untrusted Host Key"];

    NSString* info = [NSString stringWithFormat:
        @"ssh-web is connecting to %@:%hu.\n\nKey type: %@\nFingerprint (SHA-256):\n%@\n\n"
        @"The server's identity cannot be verified because it is not in your known-hosts list. "
        @"Do you want to trust this host key?",
        self.host, self.port, self.keyType, self.fingerprint];
    [alert setInformativeText:info];

    // Button order in NSAlert: first button = default (return key).
    // We put "Trust Permanently" last so Cancel is the safe default.
    [[alert addButtonWithTitle:@"Cancel"] setTag:0];
    [[alert addButtonWithTitle:@"Trust This Session"] setTag:1];
    [[alert addButtonWithTitle:@"Trust Permanently"] setTag:2];

    [alert setAlertStyle:NSAlertStyleWarning];

    // Use a sheet if a window is provided; fall back to modal dialog.
    if (window != nil) {
        [alert beginSheetModalForWindow:window
                      completionHandler:^(NSModalResponse response) {
                          // response is the tag of the button that was clicked.
                          // NSAlert maps buttons to NSAlertFirstButtonReturn (1000), +1, +2 ...
                          // But we set explicit tags, so we just use the tag of the clicked button.
                          // Unfortunately, beginSheetModalForWindow returns the button *return*
                          // values (NSAlertFirstButtonReturn + index), not the tags.
                          // Map index to decision:
                          // NSAlertFirstButtonReturn (1000) = Cancel (index 0)
                          // NSAlertFirstButtonReturn+1 (1001) = Trust This Session (index 1)
                          // NSAlertFirstButtonReturn+2 (1002) = Trust Permanently (index 2)
                          int decision = 0;
                          if (response == NSAlertFirstButtonReturn)
                              decision = 0; // Cancel
                          else if (response == NSAlertFirstButtonReturn + 1)
                              decision = 1; // Trust this session
                          else if (response == NSAlertFirstButtonReturn + 2)
                              decision = 2; // Trust permanently
                          if (completion)
                              completion(decision);
                      }];
    } else {
        NSModalResponse response = [alert runModal];
        int decision = 0;
        if (response == NSAlertFirstButtonReturn)
            decision = 0;
        else if (response == NSAlertFirstButtonReturn + 1)
            decision = 1;
        else if (response == NSAlertFirstButtonReturn + 2)
            decision = 2;
        if (completion)
            completion(decision);
    }
}

@end
// === End Plan 7B TOFU ===
