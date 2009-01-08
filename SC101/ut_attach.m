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

#import <Foundation/Foundation.h>
#import <IOKit/IOKitLib.h>


#define SC101ServiceName "net_habitue_driver_SC101"


int attach(char *idString, int readSize, int writeSize)
{
  NSMutableDictionary *summonNub = [NSMutableDictionary dictionary];
  NSDictionary *properties = [NSDictionary dictionaryWithObject:summonNub forKey:@"SummonNub"];
  
  [summonNub setObject:[NSString stringWithCString:idString encoding:NSUTF8StringEncoding] forKey:@"ID"];
  if (readSize > 0)
    [summonNub setObject:[NSNumber numberWithInt:readSize] forKey:@"IOMaxReadSize"];
  if (writeSize > 0)
    [summonNub setObject:[NSNumber numberWithInt:writeSize] forKey:@"IOMaxWriteSize"];
  
  io_service_t driverObject = IO_OBJECT_NULL;
  kern_return_t ioStatus = kIOReturnSuccess;
  int ret = 1;

  if (!(driverObject = IOServiceGetMatchingService(kIOMasterPortDefault, IOServiceNameMatching(SC101ServiceName))))
    goto cleanup;
  
  if ((ioStatus = IORegistryEntrySetCFProperties(driverObject, properties)) != kIOReturnSuccess)
    goto cleanup;
  
  ret = 0;
  
cleanup:
  if (ioStatus == kIOReturnNotPrivileged)
    fprintf(stderr, "root access required, try again using sudo.\n");
  else if (ioStatus != kIOReturnSuccess)
    fprintf(stderr, "ioStatus = 0x%08x", ioStatus);  
  
  if (driverObject)
    IOObjectRelease(driverObject);
  else
    fprintf(stderr, "SC101 driver not loaded.\n");

  return ret;
}


int usage()
{
  fprintf(stderr, "Usage: ut_attach [OPTIONS] <DEVICE_ID>\n");
  fprintf(stderr, "Options:\n");
  fprintf(stderr, "  -r LEN      maximum IO read size\n");
  fprintf(stderr, "  -w LEN      maximum IO write size\n");
  return 1;
}


int main(int argc, char *argv[])
{
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  int ret = 0;
  int readSize = -1;
  int writeSize = -1;
  int ch;

  while ((ch = getopt(argc, argv, "r:w:")) != -1)
  {
    switch (ch) {
      case 'r':
        readSize = atoi(optarg);
        break;
      case 'w':
        writeSize = atoi(optarg);
        break;
      default:
        usage();
    }
  }

  argc -= optind;
  argv += optind;
  
  if (argc == 1)
    ret = attach(argv[0], readSize, writeSize);
  else
    ret = usage();

  [pool release];
  
  return ret;
}
