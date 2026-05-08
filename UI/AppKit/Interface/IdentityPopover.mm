/*
 * Copyright (c) 2026, Ricardo Moura <ricardo@bugscave.dev>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibWebView/IdentityStore.h>
#include <LibWebView/ViewImplementation.h>
#include <LibURL/Parser.h>

#import <Interface/IdentityPopover.h>
#import <Interface/LadybirdWebView.h>
#import <Interface/Tab.h>
#import <objc/runtime.h>

#if !__has_feature(objc_arc)
#    error "This project requires ARC"
#endif

static constexpr CGFloat const POPOVER_WIDTH = 280;
static constexpr CGFloat const ROW_HEIGHT = 40;
static constexpr CGFloat const BUTTON_ROW_HEIGHT = 32;
static constexpr CGFloat const PADDING = 12;
static constexpr CGFloat const SEPARATOR_HEIGHT = 1;

// Tracks which identity is active. Empty string = anonymous.
static NSString* s_active_identity_id = @"";
static NSString* s_active_passphrase = @"";

@interface IdentityPopover ()
@property (nonatomic, strong) NSPopover* popover;
@property (nonatomic, weak) Tab* tab;
@property (nonatomic, weak) NSView* anchorView;
@property (nonatomic, strong) NSTextField* nameField;
@property (nonatomic, strong) NSSecureTextField* passphraseField;
@end

@implementation IdentityPopover
{
    WebView::IdentityStore m_store;
}

- (instancetype)init
{
    if (self = [super init]) {
        _popover = [[NSPopover alloc] init];
        _popover.behavior = NSPopoverBehaviorTransient;
        _popover.delegate = self;
    }
    return self;
}

- (void)showRelativeToView:(NSView*)view tab:(Tab*)tab
{
    if (_popover.isShown) {
        [_popover close];
        return;
    }

    _tab = tab;
    _anchorView = view;

    // Load identities from disk.
    (void)m_store.load();

    auto* contentVC = [[NSViewController alloc] init];
    auto* contentView = [self buildContentView];
    contentVC.view = contentView;
    _popover.contentViewController = contentVC;

    [_popover showRelativeToRect:view.bounds ofView:view preferredEdge:NSRectEdgeMaxY];
}

- (void)close
{
    if (_popover.isShown)
        [_popover close];
}

#pragma mark - NSPopoverDelegate

- (void)popoverWillClose:(NSNotification*)notification
{
    (void)notification;
}

#pragma mark - Content building

- (NSView*)buildContentView
{
    auto* stack = [[NSStackView alloc] init];
    stack.orientation = NSUserInterfaceLayoutOrientationVertical;
    stack.alignment = NSLayoutAttributeLeading;
    stack.spacing = 0;

    // "Anonymous" row — always first.
    auto* anonRow = [self buildIdentityRow:@"Anonymous"
                              fingerprint:@"No SSH key"
                               identityId:@""
                                 isActive:[s_active_identity_id isEqualToString:@""]
                              isEncrypted:NO];
    [stack addArrangedSubview:anonRow];

    auto& identities = m_store.identities();
    if (!identities.is_empty()) {
        [stack addArrangedSubview:[self buildSeparator]];

        for (auto const& ident : identities) {
            auto* rawName = [[NSString alloc] initWithBytes:ident.name.bytes().data()
                                                     length:ident.name.bytes().size()
                                                   encoding:NSUTF8StringEncoding];
            auto* name = ident.encrypted
                ? [NSString stringWithFormat:@"\U0001F512 %@", rawName]
                : rawName;
            auto fp = WebView::IdentityStore::fingerprint(ident.public_key);
            auto* fullFp = [[NSString alloc] initWithBytes:fp.bytes().data()
                                                    length:fp.bytes().size()
                                                  encoding:NSUTF8StringEncoding];
            // Show first 8 hex bytes (23 chars: "ab:cd:ef:12:34:56:78:9a") + ellipsis
            auto* fingerprint = fullFp.length > 23
                ? [NSString stringWithFormat:@"%@...", [fullFp substringToIndex:23]]
                : fullFp;
            auto* identId = [[NSString alloc] initWithBytes:ident.id.bytes().data()
                                                     length:ident.id.bytes().size()
                                                   encoding:NSUTF8StringEncoding];
            BOOL active = [s_active_identity_id isEqualToString:identId];
            BOOL encrypted = ident.encrypted;
            auto* row = [self buildIdentityRow:name fingerprint:fingerprint identityId:identId isActive:active isEncrypted:encrypted];
            [stack addArrangedSubview:row];
        }
    }

    [stack addArrangedSubview:[self buildSeparator]];

    // Action buttons.
    auto* generateBtn = [self buildActionButton:@"Generate New Keypair..."
                                         action:@selector(generateNewIdentity:)
                                          image:@"plus.circle"];
    [stack addArrangedSubview:generateBtn];

    auto* manageBtn = [self buildActionButton:@"Manage Identities..."
                                       action:@selector(openManageIdentities:)
                                        image:@"gearshape"];
    [stack addArrangedSubview:manageBtn];

    // Size constraints.
    CGFloat totalHeight = ROW_HEIGHT; // anonymous
    if (!identities.is_empty())
        totalHeight += SEPARATOR_HEIGHT + (ROW_HEIGHT * identities.size());
    totalHeight += SEPARATOR_HEIGHT + (BUTTON_ROW_HEIGHT * 2) + (PADDING * 2);

    auto* container = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, POPOVER_WIDTH, totalHeight)];
    stack.translatesAutoresizingMaskIntoConstraints = NO;
    [container addSubview:stack];
    [NSLayoutConstraint activateConstraints:@[
        [stack.topAnchor constraintEqualToAnchor:container.topAnchor constant:PADDING],
        [stack.leadingAnchor constraintEqualToAnchor:container.leadingAnchor],
        [stack.trailingAnchor constraintEqualToAnchor:container.trailingAnchor],
        [stack.bottomAnchor constraintEqualToAnchor:container.bottomAnchor constant:-PADDING],
    ]];

    return container;
}

- (NSView*)buildIdentityRow:(NSString*)name
                fingerprint:(NSString*)fingerprint
                 identityId:(NSString*)identityId
                   isActive:(BOOL)isActive
                isEncrypted:(BOOL)isEncrypted
{
    auto* row = [[NSView alloc] init];
    row.translatesAutoresizingMaskIntoConstraints = NO;
    [NSLayoutConstraint activateConstraints:@[
        [row.heightAnchor constraintEqualToConstant:ROW_HEIGHT],
        [row.widthAnchor constraintEqualToConstant:POPOVER_WIDTH],
    ]];

    // Checkmark on the left.
    auto* check = [NSTextField labelWithString:isActive ? @"✓" : @""];
    check.font = [NSFont systemFontOfSize:13 weight:NSFontWeightMedium];
    check.textColor = [NSColor systemBlueColor];
    check.translatesAutoresizingMaskIntoConstraints = NO;
    [row addSubview:check];

    // Name label.
    auto* nameLabel = [NSTextField labelWithString:name];
    nameLabel.font = [NSFont systemFontOfSize:13 weight:isActive ? NSFontWeightSemibold : NSFontWeightRegular];
    nameLabel.translatesAutoresizingMaskIntoConstraints = NO;
    [row addSubview:nameLabel];

    // Fingerprint label (smaller, gray).
    auto* fpLabel = [NSTextField labelWithString:fingerprint];
    fpLabel.font = [NSFont systemFontOfSize:10];
    fpLabel.textColor = [NSColor secondaryLabelColor];
    fpLabel.translatesAutoresizingMaskIntoConstraints = NO;
    [row addSubview:fpLabel];

    [NSLayoutConstraint activateConstraints:@[
        [check.leadingAnchor constraintEqualToAnchor:row.leadingAnchor constant:PADDING],
        [check.centerYAnchor constraintEqualToAnchor:row.centerYAnchor],
        [check.widthAnchor constraintEqualToConstant:16],

        [nameLabel.leadingAnchor constraintEqualToAnchor:check.trailingAnchor constant:8],
        [nameLabel.topAnchor constraintEqualToAnchor:row.topAnchor constant:4],
        [nameLabel.trailingAnchor constraintLessThanOrEqualToAnchor:row.trailingAnchor constant:-PADDING],

        [fpLabel.leadingAnchor constraintEqualToAnchor:nameLabel.leadingAnchor],
        [fpLabel.topAnchor constraintEqualToAnchor:nameLabel.bottomAnchor constant:1],
        [fpLabel.trailingAnchor constraintLessThanOrEqualToAnchor:row.trailingAnchor constant:-PADDING],
    ]];

    // Click gesture.
    auto* click = [[NSClickGestureRecognizer alloc] initWithTarget:self action:@selector(identityRowClicked:)];
    [row addGestureRecognizer:click];
    objc_setAssociatedObject(row, "identityId", identityId, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    objc_setAssociatedObject(row, "encrypted", @(isEncrypted), OBJC_ASSOCIATION_RETAIN_NONATOMIC);

    // Hover tracking.
    auto* area = [[NSTrackingArea alloc] initWithRect:NSZeroRect
                                              options:NSTrackingMouseEnteredAndExited | NSTrackingActiveInKeyWindow | NSTrackingInVisibleRect
                                                owner:self
                                             userInfo:@{@"view": row}];
    [row addTrackingArea:area];

    return row;
}

- (NSView*)buildSeparator
{
    auto* sep = [[NSBox alloc] init];
    sep.boxType = NSBoxSeparator;
    sep.translatesAutoresizingMaskIntoConstraints = NO;
    [NSLayoutConstraint activateConstraints:@[
        [sep.widthAnchor constraintEqualToConstant:POPOVER_WIDTH],
        [sep.heightAnchor constraintEqualToConstant:SEPARATOR_HEIGHT],
    ]];
    return sep;
}

- (NSView*)buildActionButton:(NSString*)title action:(SEL)action image:(NSString*)symbolName
{
    auto* row = [[NSView alloc] init];
    row.translatesAutoresizingMaskIntoConstraints = NO;
    [NSLayoutConstraint activateConstraints:@[
        [row.heightAnchor constraintEqualToConstant:BUTTON_ROW_HEIGHT],
        [row.widthAnchor constraintEqualToConstant:POPOVER_WIDTH],
    ]];

    auto* icon = [[NSImageView alloc] init];
    [icon setImage:[NSImage imageWithSystemSymbolName:symbolName accessibilityDescription:@""]];
    [icon setContentTintColor:[NSColor secondaryLabelColor]];
    icon.translatesAutoresizingMaskIntoConstraints = NO;
    [row addSubview:icon];

    auto* label = [NSTextField labelWithString:title];
    label.font = [NSFont systemFontOfSize:12];
    label.textColor = [NSColor secondaryLabelColor];
    label.translatesAutoresizingMaskIntoConstraints = NO;
    [row addSubview:label];

    [NSLayoutConstraint activateConstraints:@[
        [icon.leadingAnchor constraintEqualToAnchor:row.leadingAnchor constant:PADDING + 16 + 4],
        [icon.centerYAnchor constraintEqualToAnchor:row.centerYAnchor],
        [icon.widthAnchor constraintEqualToConstant:14],
        [icon.heightAnchor constraintEqualToConstant:14],

        [label.leadingAnchor constraintEqualToAnchor:icon.trailingAnchor constant:6],
        [label.centerYAnchor constraintEqualToAnchor:row.centerYAnchor],
        [label.trailingAnchor constraintLessThanOrEqualToAnchor:row.trailingAnchor constant:-PADDING],
    ]];

    auto* click = [[NSClickGestureRecognizer alloc] initWithTarget:self action:action];
    [row addGestureRecognizer:click];

    auto* area = [[NSTrackingArea alloc] initWithRect:NSZeroRect
                                              options:NSTrackingMouseEnteredAndExited | NSTrackingActiveInKeyWindow | NSTrackingInVisibleRect
                                                owner:self
                                             userInfo:@{@"view": row}];
    [row addTrackingArea:area];

    return row;
}

#pragma mark - Actions

- (void)notifyIdentityChange:(NSString*)identityId passphrase:(NSString*)passphrase
{
    if (_tab == nil)
        return;
    auto idBytes = ByteString([identityId UTF8String] ?: "", [identityId lengthOfBytesUsingEncoding:NSUTF8StringEncoding]);
    ByteString dirBytes;
    if (idBytes.length() > 0)
        dirBytes = ByteString::formatted("{}/{}", WebView::IdentityStore::storage_directory(), idBytes);
    auto passBytes = ByteString([passphrase UTF8String] ?: "", [passphrase lengthOfBytesUsingEncoding:NSUTF8StringEncoding]);
    auto& view = [[_tab web_view] view];
    view.set_active_sshweb_identity(move(idBytes), move(dirBytes), move(passBytes));
    view.reload();
}

- (void)identityRowClicked:(NSClickGestureRecognizer*)gesture
{
    auto* row = gesture.view;
    id identityId = objc_getAssociatedObject(row, "identityId");
    if (![identityId isKindOfClass:[NSString class]])
        return;

    auto* idStr = (NSString*)identityId;
    NSNumber* encrypted = objc_getAssociatedObject(row, "encrypted");

    if (encrypted.boolValue) {
        // Prompt for passphrase before selecting.
        [self promptPassphraseForIdentity:idStr];
    } else {
        s_active_identity_id = idStr;
        s_active_passphrase = @"";
        [self notifyIdentityChange:idStr passphrase:@""];
        [_popover close];
    }
}

- (void)promptPassphraseForIdentity:(NSString*)identityId
{
    // Keep popover open but show an NSAlert with secure text field.
    // Need to close popover first because transient popover + modal alert conflict.
    [_popover close];

    auto* alert = [[NSAlert alloc] init];
    alert.messageText = @"Enter Passphrase";
    alert.informativeText = @"This identity is passphrase-protected.";
    [alert addButtonWithTitle:@"Unlock"];
    [alert addButtonWithTitle:@"Cancel"];
    alert.alertStyle = NSAlertStyleInformational;

    auto* input = [[NSSecureTextField alloc] initWithFrame:NSMakeRect(0, 0, 260, 24)];
    input.placeholderString = @"Passphrase";
    alert.accessoryView = input;

    // Focus the passphrase field.
    [alert.window setInitialFirstResponder:input];

    auto response = [alert runModal];
    if (response != NSAlertFirstButtonReturn)
        return;

    auto* passphrase = input.stringValue;
    if (passphrase.length == 0)
        return;

    // Verify passphrase via ssh-keygen.
    auto akId = MUST(String::from_utf8(StringView { [identityId UTF8String], [identityId lengthOfBytesUsingEncoding:NSUTF8StringEncoding] }));
    auto akPass = MUST(String::from_utf8(StringView { [passphrase UTF8String], [passphrase lengthOfBytesUsingEncoding:NSUTF8StringEncoding] }));

    auto result = m_store.verify_passphrase(akId, akPass);
    if (result.is_error() || !result.value()) {
        auto* errorAlert = [[NSAlert alloc] init];
        errorAlert.messageText = @"Wrong Passphrase";
        errorAlert.informativeText = @"The passphrase you entered is incorrect.";
        [errorAlert addButtonWithTitle:@"OK"];
        errorAlert.alertStyle = NSAlertStyleWarning;
        [errorAlert runModal];
        return;
    }

    s_active_identity_id = identityId;
    s_active_passphrase = [passphrase copy];
    [self notifyIdentityChange:identityId passphrase:passphrase];
}

- (void)generateNewIdentity:(NSClickGestureRecognizer*)gesture
{
    (void)gesture;
    [self showCreateForm];
}

- (void)showCreateForm
{
    static constexpr CGFloat FORM_WIDTH = 280;
    static constexpr CGFloat FIELD_HEIGHT = 24;

    auto* stack = [[NSStackView alloc] init];
    stack.orientation = NSUserInterfaceLayoutOrientationVertical;
    stack.alignment = NSLayoutAttributeLeading;
    stack.spacing = 8;

    // Title
    auto* title = [NSTextField labelWithString:@"New Identity"];
    title.font = [NSFont systemFontOfSize:13 weight:NSFontWeightSemibold];
    title.translatesAutoresizingMaskIntoConstraints = NO;
    [stack addArrangedSubview:title];

    // Name field
    _nameField = [[NSTextField alloc] init];
    _nameField.placeholderString = @"Display name (e.g. Work, Personal)";
    _nameField.font = [NSFont systemFontOfSize:12];
    _nameField.translatesAutoresizingMaskIntoConstraints = NO;
    [NSLayoutConstraint activateConstraints:@[
        [_nameField.widthAnchor constraintEqualToConstant:FORM_WIDTH - (PADDING * 2)],
        [_nameField.heightAnchor constraintEqualToConstant:FIELD_HEIGHT],
    ]];
    [stack addArrangedSubview:_nameField];

    // Passphrase field
    _passphraseField = [[NSSecureTextField alloc] init];
    _passphraseField.placeholderString = @"Passphrase (optional)";
    _passphraseField.font = [NSFont systemFontOfSize:12];
    _passphraseField.translatesAutoresizingMaskIntoConstraints = NO;
    [NSLayoutConstraint activateConstraints:@[
        [_passphraseField.widthAnchor constraintEqualToConstant:FORM_WIDTH - (PADDING * 2)],
        [_passphraseField.heightAnchor constraintEqualToConstant:FIELD_HEIGHT],
    ]];
    [stack addArrangedSubview:_passphraseField];

    // Button row
    auto* buttonRow = [[NSStackView alloc] init];
    buttonRow.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    buttonRow.spacing = 8;

    auto* cancelBtn = [NSButton buttonWithTitle:@"Cancel" target:self action:@selector(cancelCreateForm:)];
    cancelBtn.bezelStyle = NSBezelStyleRounded;
    cancelBtn.font = [NSFont systemFontOfSize:12];

    auto* createBtn = [NSButton buttonWithTitle:@"Create" target:self action:@selector(confirmCreateForm:)];
    createBtn.bezelStyle = NSBezelStyleRounded;
    createBtn.font = [NSFont systemFontOfSize:12];
    createBtn.keyEquivalent = @"\r";

    [buttonRow addArrangedSubview:cancelBtn];
    [buttonRow addArrangedSubview:createBtn];
    buttonRow.translatesAutoresizingMaskIntoConstraints = NO;
    [NSLayoutConstraint activateConstraints:@[
        [buttonRow.widthAnchor constraintEqualToConstant:FORM_WIDTH - (PADDING * 2)],
    ]];

    // Right-align buttons
    buttonRow.alignment = NSLayoutAttributeTrailing;
    [stack addArrangedSubview:buttonRow];

    CGFloat totalHeight = (PADDING * 2) + 18 + 8 + FIELD_HEIGHT + 8 + FIELD_HEIGHT + 8 + 28;
    auto* container = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, FORM_WIDTH, totalHeight)];
    stack.translatesAutoresizingMaskIntoConstraints = NO;
    [container addSubview:stack];
    [NSLayoutConstraint activateConstraints:@[
        [stack.topAnchor constraintEqualToAnchor:container.topAnchor constant:PADDING],
        [stack.leadingAnchor constraintEqualToAnchor:container.leadingAnchor constant:PADDING],
        [stack.trailingAnchor constraintEqualToAnchor:container.trailingAnchor constant:-PADDING],
        [stack.bottomAnchor constraintEqualToAnchor:container.bottomAnchor constant:-PADDING],
    ]];

    auto* vc = [[NSViewController alloc] init];
    vc.view = container;
    _popover.contentViewController = vc;

    // Focus the name field after the popover redraws.
    dispatch_async(dispatch_get_main_queue(), ^{
        [self.nameField becomeFirstResponder];
    });
}

- (void)cancelCreateForm:(id)sender
{
    (void)sender;
    // Switch back to identity list view.
    auto* contentVC = [[NSViewController alloc] init];
    contentVC.view = [self buildContentView];
    _popover.contentViewController = contentVC;
}

- (void)confirmCreateForm:(id)sender
{
    (void)sender;
    auto* name = _nameField.stringValue;
    if (name.length == 0) {
        [_nameField becomeFirstResponder];
        return;
    }
    auto* passphrase = _passphraseField.stringValue;

    auto akName = MUST(String::from_utf8(StringView { [name UTF8String], [name lengthOfBytesUsingEncoding:NSUTF8StringEncoding] }));
    auto akPass = MUST(String::from_utf8(StringView { [passphrase UTF8String], [passphrase lengthOfBytesUsingEncoding:NSUTF8StringEncoding] }));

    auto result = m_store.create(move(akName), move(akPass));
    if (!result.is_error()) {
        auto id = result.release_value();
        auto* nsId = [[NSString alloc] initWithBytes:id.bytes().data()
                                              length:id.bytes().size()
                                            encoding:NSUTF8StringEncoding];
        s_active_identity_id = nsId;
        s_active_passphrase = [passphrase copy];
        [self notifyIdentityChange:nsId passphrase:passphrase];
    }
    [_popover close];
}

- (void)openManageIdentities:(NSClickGestureRecognizer*)gesture
{
    (void)gesture;
    [_popover close];
    auto url = URL::Parser::basic_parse("about:sshweb-identities"sv);
    if (url.has_value() && _tab != nil)
        [_tab.web_view loadURL:url.value()];
}

#pragma mark - Hover effects

- (void)mouseEntered:(NSEvent*)event
{
    auto* userInfo = event.trackingArea.userInfo;
    id view = userInfo[@"view"];
    if ([view isKindOfClass:[NSView class]]) {
        auto* v = (NSView*)view;
        v.wantsLayer = YES;
        v.layer.backgroundColor = [NSColor quaternaryLabelColor].CGColor;
    }
}

- (void)mouseExited:(NSEvent*)event
{
    auto* userInfo = event.trackingArea.userInfo;
    id view = userInfo[@"view"];
    if ([view isKindOfClass:[NSView class]]) {
        auto* v = (NSView*)view;
        v.layer.backgroundColor = [NSColor clearColor].CGColor;
    }
}

@end
