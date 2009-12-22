/*
 *  Copyright (C) 2009  Iain Wade <iwade@optusnet.com.au>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef kCFBundleIdentifierKey
#define kCFBundleIdentifierKey "CFBundleIdentifier"
#endif

//
#define kSC101DriverName "net_habitue_driver_SC101"
#define kSC101DeviceName "net_habitue_device_SC101"

//
#define kSC101DriverSummonKey "SummonNub"

// property keys
#define kSC101DeviceIDKey "ID"
#define kSC101DeviceIOMaxReadSizeKey "IOMaxReadSize"
#define kSC101DeviceIOMaxWriteSizeKey "IOMaxWriteSize"
#define kSC101DevicePartitionAddressKey "Partition Address"
#define kSC101DeviceRootAddressKey "Root Address"
#define kSC101DevicePartNumberKey "Part Number"
#define kSC101DeviceVersionKey "Firmware Version"
#define kSC101DeviceLabelKey "Label"
#define kSC101DeviceSizeKey "Size"

// part numbers
#define kSC101PartNumber ((unsigned char[3]){ 0, 0, 101 })
#define kSC101TPartNumber ((unsigned char[3]){ 0, 0, 102 })

