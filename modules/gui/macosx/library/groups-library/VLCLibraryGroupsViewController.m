/*****************************************************************************
 * VLCLibraryGroupsViewController.m: MacOS X interface module
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

#import "VLCLibraryGroupsViewController.h"

#import "library/VLCLibraryTableView.h"
#import "library/VLCLibraryUIUnits.h"
#import "library/VLCLibraryWindow.h"

@implementation VLCLibraryGroupsViewController

- (instancetype)initWithLibraryWindow:(VLCLibraryWindow *)libraryWindow
{
    self = [super init];
    if (self) {
        [self setupPropertiesFromLibraryWindow:libraryWindow];
        [self setupListViewModeViews];
    }
    return self;
}

- (void)setupPropertiesFromLibraryWindow:(VLCLibraryWindow *)libraryWindow
{
    NSParameterAssert(libraryWindow);
    _libraryWindow = libraryWindow;
    _libraryTargetView = libraryWindow.libraryTargetView;
    _emptyLibraryView = libraryWindow.emptyLibraryView;
    _placeholderImageView = libraryWindow.placeholderImageView;
    _placeholderLabel = libraryWindow.placeholderLabel;
}

- (void)setupListViewModeViews
{
    _groupsTableViewScrollView = [[NSScrollView alloc] init];
    _selectedGroupTableViewScrollView = [[NSScrollView alloc] init];
    _groupsTableView = [[VLCLibraryTableView alloc] init];
    _selectedGroupTableView = [[VLCLibraryTableView alloc] init];
    _listViewSplitView = [[NSSplitView alloc] init];

    self.groupsTableViewScrollView.translatesAutoresizingMaskIntoConstraints = NO;
    self.selectedGroupTableViewScrollView.translatesAutoresizingMaskIntoConstraints = NO;
    self.listViewSplitView.translatesAutoresizingMaskIntoConstraints = NO;
    self.groupsTableView.translatesAutoresizingMaskIntoConstraints = NO;
    self.selectedGroupTableView.translatesAutoresizingMaskIntoConstraints = NO;

    self.groupsTableViewScrollView.hasHorizontalScroller = NO;
    self.groupsTableViewScrollView.borderType = NSNoBorder;

    self.selectedGroupTableViewScrollView.hasHorizontalScroller = NO;
    self.selectedGroupTableViewScrollView.borderType = NSNoBorder;

    self.groupsTableViewScrollView.documentView = self.groupsTableView;
    self.selectedGroupTableViewScrollView.documentView = self.selectedGroupTableView;

    self.listViewSplitView.vertical = YES;
    self.listViewSplitView.dividerStyle = NSSplitViewDividerStyleThin;
    [self.listViewSplitView addArrangedSubview:self.groupsTableViewScrollView];
    [self.listViewSplitView addArrangedSubview:self.selectedGroupTableViewScrollView];

    NSTableColumn * const groupsColumn = [[NSTableColumn alloc] initWithIdentifier:@"groups"];
    NSTableColumn * const selectedGroupColumn =
        [[NSTableColumn alloc] initWithIdentifier:@"selectedGroup"];

    [self.groupsTableView addTableColumn:groupsColumn];
    [self.selectedGroupTableView addTableColumn:selectedGroupColumn];

    self.groupsTableView.headerView = nil;
    self.selectedGroupTableView.headerView = nil;

    self.groupsTableView.rowHeight = VLCLibraryUIUnits.mediumTableViewRowHeight;
    self.selectedGroupTableView.rowHeight = VLCLibraryUIUnits.mediumTableViewRowHeight;
}

@end