/*****************************************************************************
 * VLCStatusNotifierView.m: MacOS X interface module
 *****************************************************************************
 * Copyright (C) 2024 VLC authors and VideoLAN
 *
 * Authors: Claudio Cambra <developer@claudiocambra.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

#import "VLCStatusNotifierView.h"

#import "extensions/NSString+Helpers.h"
#import "library/VLCLibraryModel.h"

NSString * const VLCStatusNotifierViewActivated = @"VLCStatusNotifierViewActivated";
NSString * const VLCStatusNotifierViewDeactivated = @"VLCStatusNotifierViewDeactivated";

@interface VLCStatusNotifierView ()

@property NSUInteger loadingCount;
@property BOOL permanentDiscoveryMessageActive;
@property NSMutableSet<NSString *> *longNotifications;
@property NSMutableArray<NSString *> *messages;

@property (readonly) NSString *loadingLibraryItemsMessage;
@property (readonly) NSString *libraryItemsLoadedMessage;
@property (readonly) NSString *discoveringMediaMessage;
@property (readonly) NSString *discoveryCompletedMessage;
@property (readonly) NSString *discoveryFailedMessage;

@end

@implementation VLCStatusNotifierView

- (void)awakeFromNib
{
    self.loadingCount = 0;
    self.permanentDiscoveryMessageActive = NO;
    self.longNotifications = NSMutableSet.set;
    self.messages = NSMutableArray.array;

    _loadingLibraryItemsMessage = _NS("Loading library items");
    _libraryItemsLoadedMessage = _NS("Library items loaded");
    _discoveringMediaMessage = _NS("Discovering media");
    _discoveryCompletedMessage = _NS("Media discovery completed");

    self.label.stringValue = _NS("Idle");
    self.progressIndicator.hidden = YES;
    self.infoImageView.hidden = NO;

    NSNotificationCenter * const defaultCenter = NSNotificationCenter.defaultCenter;
    [defaultCenter addObserver:self selector:@selector(updateStatus:) name:nil object:nil];
}

- (void)displayStartLoad
{
    if (self.loadingCount == 0) {
        self.infoImageView.hidden = YES;
        self.progressIndicator.hidden = NO;
        [self.progressIndicator startAnimation:self];
    }
    self.loadingCount++;
}

- (void)displayFinishLoad
{
    self.loadingCount--;
    if (self.loadingCount == 0) {
        [self.progressIndicator stopAnimation:self];
        self.progressIndicator.hidden = YES;
        self.infoImageView.hidden = NO;
    }
}

- (void)addMessage:(NSString *)message
{
    if (self.messages.count == 0) {
        [NSNotificationCenter.defaultCenter postNotificationName:VLCStatusNotifierViewActivated object:self];
    }
    [self.messages addObject:message];
    self.label.stringValue = [self.messages componentsJoinedByString:@"\n"];
}

- (void)removeMessage:(NSString *)message
{
    [self.messages removeObject:message];
    if (self.messages.count == 0) {
        [NSNotificationCenter.defaultCenter postNotificationName:VLCStatusNotifierViewDeactivated object:self];
        return;
    }

    self.label.stringValue = [self.messages componentsJoinedByString:@"\n"];
}

- (void)updateStatus:(NSNotification *)notification
{
    NSString * const notificationName = notification.name;
    if (![notificationName hasPrefix:@"VLC"]) {
        return;
    }

    if ([notificationName isEqualToString:VLCLibraryModelDiscoveryStarted]) {
        [self addMessage:self.discoveringMediaMessage];
        [self removeMessage:self.discoveryCompletedMessage];
        [self removeMessage:self.discoveryFailedMessage];
        self.permanentDiscoveryMessageActive = YES;
        [self displayStartLoad];
    } else if ([notificationName isEqualToString:VLCLibraryModelDiscoveryCompleted]) {
        self.permanentDiscoveryMessageActive = NO;
        [self displayFinishLoad];
        [self presentTransientMessage:self.discoveryCompletedMessage];
        [self removeMessage:self.discoveringMediaMessage];
    } else if ([notificationName isEqualToString:VLCLibraryModelDiscoveryFailed]) {
        self.permanentDiscoveryMessageActive = NO;
        [self displayFinishLoad];
        [self presentTransientMessage:self.discoveryFailedMessage];
        [self removeMessage:self.discoveringMediaMessage];
    } else if ([notificationName containsString:VLCLongNotificationNameStartSuffix] && ![self.longNotifications containsObject:notificationName]) {
        if (self.longNotifications.count == 0) {
            [self addMessage:self.loadingLibraryItemsMessage];
            [self removeMessage:self.libraryItemsLoadedMessage];
            [self displayStartLoad];
        }
        [self.longNotifications addObject:notificationName];
    } else if ([notificationName containsString:VLCLongNotificationNameFinishSuffix]) {
        NSString * const loadingNotification =
            [notificationName stringByReplacingOccurrencesOfString:VLCLongNotificationNameFinishSuffix withString:VLCLongNotificationNameStartSuffix];
        [self.longNotifications removeObject:loadingNotification];
        if (self.longNotifications.count == 0) {
            [self displayFinishLoad];
            [self presentTransientMessage:self.libraryItemsLoadedMessage];
            [self removeMessage:self.loadingLibraryItemsMessage];
        }
    }
}

- (void)presentTransientMessage:(NSString *)message
{
    [self addMessage:message];
    [self performSelector:@selector(removeMessage:) withObject:message afterDelay:2.0];
}

@end