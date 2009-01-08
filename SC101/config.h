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

#pragma once

#ifdef DEBUG
// #define QUEUE_MACRO_DEBUG
// #define IOASSERT 1
#define KDEBUG(fmt, ...) do { \
  kprintf("%s: " fmt "\n", __FUNCTION__, ## __VA_ARGS__); \
} while (0)
#else /* DEBUG */
#define KDEBUG(...)
#endif /*DEBUG */

// during debugging it might be useful to totally disable write requests
// #define WRITEPROTECT

#define SECTOR_SIZE (512)

#define RESOLVE_TIMEOUT_MS (1000)

// drives can be configured to turn off when idle on the SC101. (but seemingly not on the SC101T?)
// they can take up to 30s to spin up in that case, so back off on the retries.
#define SPINUP_INTERVAL_MS (10*1000)

// UDP allows for 64k packets, subtract 512b for request header and truncating to the next lowest power of 2
// means the devices can probably support 32k I/Os, but we can choose a lower limit in case of packet loss.
// jumbo frames are not supported, so UDP packets >1500 bytes are split into multiple ethernet frames.
#define MAX_IO_READ_SIZE (16*1024)
#define DEFAULT_IO_READ_SIZE (8*1024)
#define MAX_IO_WRITE_SIZE (16*1024)
#define DEFAULT_IO_WRITE_SIZE (4*1024)

// requests larger than the max read/write sizes above can be accepted from the block layer and split up
// by our deblocking function.
#define ACCEPT_IO_READ_SIZE (1*1024*1024)
#define ACCEPT_IO_WRITE_SIZE (1*1024*1024)

// when reading a lot of data, particularly from multiple devices it might help to have a larger buffer.
#define RCVBUF_SIZE (4*1024*1024)
#define SNDBUF_SIZE (4*1024*1024)

// ZFS reacts adversely (panic) to disks disappearing with uncommitted data, so we may choose to retry writes
// indefinitely in case of (long lived) network problems.
#define RETRY_INDEFINITELY_DELAY_MS (10000)

// maximum number of IOs to send to a particular device before queueing the request
#define MAX_IO_OUTSTANDING (8)
