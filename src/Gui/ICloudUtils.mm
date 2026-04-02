// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 FreeCAD contributors
// SPDX-FileNotice: Part of the FreeCAD project.

/******************************************************************************
 *                                                                            *
 *   FreeCAD is free software: you can redistribute it and/or modify          *
 *   it under the terms of the GNU Lesser General Public License as           *
 *   published by the Free Software Foundation, either version 2.1            *
 *   of the License, or (at your option) any later version.                   *
 *                                                                            *
 *   FreeCAD is distributed in the hope that it will be useful,               *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty              *
 *   of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.                  *
 *   See the GNU Lesser General Public License for more details.              *
 *                                                                            *
 *   You should have received a copy of the GNU Lesser General Public         *
 *   License along with FreeCAD. If not, see https://www.gnu.org/licenses     *
 *                                                                            *
 ******************************************************************************/

#include "PreCompiled.h"

#import <Foundation/Foundation.h>

#include <QApplication>
#include <QProgressDialog>

#include "ICloudUtils.h"

namespace Gui {

bool ensureICloudFileAvailable(const QString& filePath, QWidget* parent)
{
    @autoreleasepool {
        NSURL* fileURL = [NSURL fileURLWithPath:filePath.toNSString()];

        // Check if this is an iCloud ubiquitous item
        NSNumber* isUbiquitous = nil;
        [fileURL getResourceValue:&isUbiquitous forKey:NSURLIsUbiquitousItemKey error:nil];
        if (!isUbiquitous || !isUbiquitous.boolValue) {
            return true;  // Not an iCloud file, nothing to do
        }

        // Check download status
        NSString* downloadStatus = nil;
        [fileURL getResourceValue:&downloadStatus
                           forKey:NSURLUbiquitousItemDownloadingStatusKey
                            error:nil];

        if ([downloadStatus isEqualToString:NSURLUbiquitousItemDownloadingStatusCurrent]
            || [downloadStatus isEqualToString:NSURLUbiquitousItemDownloadingStatusDownloaded]) {
            return true;  // Already available locally
        }

        // File is evicted — trigger download
        NSError* error = nil;
        if (![[NSFileManager defaultManager] startDownloadingUbiquitousItemAtURL:fileURL
                                                                          error:&error]) {
            return false;  // Download couldn't be started
        }

        // Show progress dialog while waiting for download
        QProgressDialog progress(
            QObject::tr("Downloading \"%1\" from iCloud...")
                .arg(QFileInfo(filePath).fileName()),
            QObject::tr("Cancel"),
            0, 0,  // indeterminate
            parent
        );
        progress.setWindowModality(Qt::WindowModal);
        progress.setMinimumDuration(500);

        while (true) {
            // Refresh resource values (they're cached per-URL)
            [fileURL removeCachedResourceValueForKey:NSURLUbiquitousItemDownloadingStatusKey];

            downloadStatus = nil;
            [fileURL getResourceValue:&downloadStatus
                               forKey:NSURLUbiquitousItemDownloadingStatusKey
                                error:nil];

            if ([downloadStatus isEqualToString:NSURLUbiquitousItemDownloadingStatusCurrent]
                || [downloadStatus isEqualToString:NSURLUbiquitousItemDownloadingStatusDownloaded]) {
                progress.close();
                return true;
            }

            if (progress.wasCanceled()) {
                return false;
            }

            QApplication::processEvents();
            [NSThread sleepForTimeInterval:0.1];
        }
    }
}

} // namespace Gui
