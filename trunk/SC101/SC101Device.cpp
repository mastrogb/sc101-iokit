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

#include <IOKit/IOBufferMemoryDescriptor.h>
#include <IOKit/IOKitKeys.h>

#include "SC101Device.h"

extern "C" {
#include <netinet/in.h>
#include "psan_wireformat.h"
};

#define kCFBundleIdentifierKey "CFBundleIdentifier"

static const OSSymbol *gSC101DeviceIDKey;
static const OSSymbol *gSC101DeviceIOMaxReadSizeKey;
static const OSSymbol *gSC101DeviceIOMaxWriteSizeKey;
static const OSSymbol *gSC101DevicePartitionAddressKey;
static const OSSymbol *gSC101DeviceRootAddressKey;
static const OSSymbol *gSC101DevicePartNumberKey;
static const OSSymbol *gSC101DeviceVersionKey;
static const OSSymbol *gSC101DeviceLabelKey;
static const OSSymbol *gSC101DeviceSizeKey;

// Define my superclass
#define super IOBlockStorageDevice

// REQUIRED! This macro defines the class's constructors, destructors,
// and several other methods I/O Kit requires. Do NOT use super as the
// second parameter. You must use the literal name of the superclass.
OSDefineMetaClassAndStructors(net_habitue_device_SC101, IOBlockStorageDevice)


/**********************************************************************************************************************************/
#pragma mark IOService stubs
/**********************************************************************************************************************************/

bool net_habitue_device_SC101::init(OSDictionary *properties)
{
  KDEBUG("init");

  OSString *id = OSDynamicCast(OSString, properties->getObject("ID"));
  if (!id)
    return false;
  
  if (!super::init(properties))
    return false;
  
  if (!gSC101DeviceIDKey)
    gSC101DeviceIDKey = OSSymbol::withCString(kSC101DeviceIDKey);
  if (!gSC101DeviceIOMaxReadSizeKey)
    gSC101DeviceIOMaxReadSizeKey = OSSymbol::withCString(kSC101DeviceIOMaxReadSizeKey);
  if (!gSC101DeviceIOMaxWriteSizeKey)
    gSC101DeviceIOMaxWriteSizeKey = OSSymbol::withCString(kSC101DeviceIOMaxWriteSizeKey);
  if (!gSC101DevicePartitionAddressKey)
    gSC101DevicePartitionAddressKey = OSSymbol::withCString(kSC101DevicePartitionAddressKey);
  if (!gSC101DeviceRootAddressKey)
    gSC101DeviceRootAddressKey = OSSymbol::withCString(kSC101DeviceRootAddressKey);
  if (!gSC101DevicePartNumberKey)
    gSC101DevicePartNumberKey = OSSymbol::withCString(kSC101DevicePartNumberKey);
  if (!gSC101DeviceVersionKey)
    gSC101DeviceVersionKey = OSSymbol::withCString(kSC101DeviceVersionKey);
  if (!gSC101DeviceLabelKey)
    gSC101DeviceLabelKey = OSSymbol::withCString(kSC101DeviceLabelKey);
  if (!gSC101DeviceSizeKey)
    gSC101DeviceSizeKey = OSSymbol::withCString(kSC101DeviceSizeKey);

  OSNumber *ioMaxReadSize = OSDynamicCast(OSNumber, properties->getObject(gSC101DeviceIOMaxReadSizeKey));
  
  if (!ioMaxReadSize ||
      ioMaxReadSize->unsigned64BitValue() < SECTOR_SIZE ||
      ioMaxReadSize->unsigned64BitValue() > MAX_IO_READ_SIZE ||
      ioMaxReadSize->unsigned64BitValue() & (ioMaxReadSize->unsigned64BitValue() - 1))
  {
    ioMaxReadSize = OSNumber::withNumber(DEFAULT_IO_READ_SIZE, 64);
    
    if (ioMaxReadSize)
    {
      setProperty(gSC101DeviceIOMaxReadSizeKey, ioMaxReadSize);
      ioMaxReadSize->release();
    }
  }
  
  OSNumber *ioMaxWriteSize = OSDynamicCast(OSNumber, properties->getObject(gSC101DeviceIOMaxWriteSizeKey));
  
  if (!ioMaxWriteSize ||
      ioMaxWriteSize->unsigned64BitValue() < SECTOR_SIZE ||
      ioMaxWriteSize->unsigned64BitValue() > MAX_IO_WRITE_SIZE ||
      ioMaxWriteSize->unsigned64BitValue() & (ioMaxWriteSize->unsigned64BitValue() - 1))
  {
    ioMaxWriteSize = OSNumber::withNumber(DEFAULT_IO_WRITE_SIZE, 64);
    
    if (ioMaxWriteSize)
    {
      setProperty(gSC101DeviceIOMaxWriteSizeKey, ioMaxWriteSize);
      ioMaxWriteSize->release();
    }
  }
  
  nanoseconds_to_absolutetime(1000000000ULL * 60, &_resolveInterval);
  
  _mediaStateAttached = false;
  _mediaStateChanged = true;
  
  STAILQ_INIT(&_pendingHead);
  _pendingCount = 0;
  STAILQ_INIT(&_outstandingHead);
  _outstandingCount = 0;
  
  return true;
}

IOWorkLoop *net_habitue_device_SC101::getWorkLoop()
{
  return ((net_habitue_driver_SC101 *)getProvider())->getWorkLoop();
}

bool net_habitue_device_SC101::attach(IOService *provider)
{
  if (!super::attach(provider))
    return false;

  resolve();

  return true;
}


IOReturn net_habitue_device_SC101::doAsyncReadWrite(IOMemoryDescriptor *buffer, UInt32 block, UInt32 nblks, IOStorageCompletion completion)
{
  OSData *addr = OSDynamicCast(OSData, getProperty(gSC101DevicePartitionAddressKey));
  addr->retain();
  
  prepareAndDoAsyncReadWrite(addr, buffer, block, nblks, completion);

  return kIOReturnSuccess;
}


IOReturn net_habitue_device_SC101::doEjectMedia(void)
{
  _mediaStateAttached = false;
  _mediaStateChanged = true;
  
  // detach(getProvider());
  
  return kIOReturnSuccess;
}


IOReturn net_habitue_device_SC101::doFormatMedia(UInt64 byteCapacity)
{
  return kIOReturnUnsupported;
}

UInt32 net_habitue_device_SC101::doGetFormatCapacities(UInt64 *capacities, UInt32 capacitiesMaxCount) const
{
  return 0;
}

IOReturn net_habitue_device_SC101::doLockUnlockMedia(bool doLock)
{
  return kIOReturnUnsupported;
}


IOReturn net_habitue_device_SC101::doSynchronizeCache(void)
{
  return kIOReturnSuccess;
}


char *net_habitue_device_SC101::getVendorString(void)
{
  return "Netgear";
}


char *net_habitue_device_SC101::getProductString(void)
{
  OSData *partNumber = OSDynamicCast(OSData, getProperty(gSC101DevicePartNumberKey));
  char *productString = "Unknown";
  
  if (partNumber)
  {
    if (partNumber->isEqualTo(kSC101PartNumber, sizeof(kSC101PartNumber)))
      productString = "SC101";
    else if (partNumber->isEqualTo(kSC101TPartNumber, sizeof(kSC101TPartNumber)))
      productString = "SC101T";
  }
  
  return productString;
}


char *net_habitue_device_SC101::getRevisionString(void)
{
  OSString *version = OSDynamicCast(OSString, getProperty(gSC101DeviceVersionKey));
  
  return (char *)(version ? version->getCStringNoCopy() : "");
}


char *net_habitue_device_SC101::getAdditionalDeviceInfoString(void)
{
  OSString *label = OSDynamicCast(OSString, getProperty(gSC101DeviceLabelKey));
  
  return (char *)(label ? label->getCStringNoCopy() : "");
}


IOReturn net_habitue_device_SC101::reportBlockSize(UInt64 *blockSize)
{
  *blockSize = SECTOR_SIZE;
  
  return kIOReturnSuccess;
}


IOReturn net_habitue_device_SC101::reportEjectability(bool *isEjectable)
{
  *isEjectable = true;
  
  return kIOReturnSuccess;
}


IOReturn net_habitue_device_SC101::reportLockability(bool *isLockable)
{
  *isLockable = false;

  return kIOReturnSuccess;
}


IOReturn net_habitue_device_SC101::reportMaxReadTransfer(UInt64 blockSize, UInt64 *max)
{
  *max = ACCEPT_IO_READ_SIZE;
  
  return kIOReturnSuccess;
}


IOReturn net_habitue_device_SC101::reportMaxWriteTransfer(UInt64 blockSize, UInt64 *max)
{
  *max = ACCEPT_IO_WRITE_SIZE;

  return kIOReturnSuccess;
}


IOReturn net_habitue_device_SC101::reportMaxValidBlock(UInt64 *maxBlock)
{
  OSNumber *size = OSDynamicCast(OSNumber, getProperty(gSC101DeviceSizeKey));  
  *maxBlock = (size ? size->unsigned64BitValue() / SECTOR_SIZE - 1 : 0);

  return kIOReturnSuccess;
}


IOReturn net_habitue_device_SC101::reportMediaState(bool *mediaPresent, bool *changed)
{
  *mediaPresent = _mediaStateAttached;
  *changed = _mediaStateChanged;

  if (_mediaStateChanged)
  {
    KDEBUG("media now %s", _mediaStateAttached ? "present" : "missing");
    _mediaStateChanged = false;
  }

  return kIOReturnSuccess;
}


IOReturn net_habitue_device_SC101::reportPollRequirements(bool *pollIsRequired, bool *pollIsExpensive)
{
  *pollIsRequired = true;
  *pollIsExpensive = false;
  
  return kIOReturnSuccess;
}


IOReturn net_habitue_device_SC101::reportRemovability(bool *isRemovable)
{
  *isRemovable = true;
  
  return kIOReturnSuccess;
}


IOReturn net_habitue_device_SC101::reportWriteProtection(bool *isWriteProtected)
{
#ifdef WRITEPROTECT
  KDEBUG("true");
  *isWriteProtected = true;
#else
  *isWriteProtected = false;
#endif
  
  return kIOReturnSuccess;
}


/**********************************************************************************************************************************/
#pragma mark functions
/**********************************************************************************************************************************/


// 
#define POWER_OF_2(n) ({ \
  int __power = 1; \
  while ((1 << __power) < n) \
    __power++; \
  __power; \
})


// least significant bit set in value
#define LSB(n) ((n) & ~((n) - 1))


static UInt64 getUInt48(unsigned char *buf)
{
  // TODO(iwade) endianess?
  UInt64 ret = 0;
  for (int i = 0; i < 6; i++)
    ret = (ret << 8) | buf[i];
  return ret;
}


static struct {
  UInt32 timeout_ms;
  UInt32 tries;
} retry_plan[] = {
  {  250,  1 },
  {  500,  1 },
  { 1000,  1 },
  { 3000, 20 },
  {    0,  0 }
};


static UInt32 getNextTimeoutMS(UInt32 attempt, bool isWrite)
{
  for (int i = 0, a = 0; retry_plan[i].tries; i++)
  {
    a += retry_plan[i].tries;
    
    if (attempt >= a)
      continue;
    
    return retry_plan[i].timeout_ms;
  }
  
#ifdef RETRY_INDEFINITELY_DELAY_MS
  if (isWrite)
    return RETRY_INDEFINITELY_DELAY_MS;
#endif
  
  return 0;
}


static bool mbuf_buffer(IOMemoryDescriptor *buffer, UInt32 skip_buffer, mbuf_t m, UInt32 skip_mbuf, UInt32 copy)
{
  UInt32 offset = 0;
  bool isWrite = (buffer->getDirection() == kIODirectionOut);
  
  if (buffer->prepare() != kIOReturnSuccess)
  {
    KDEBUG("buffer prepare failed");
    return false;
  }
  
  if (isWrite && mbuf_pkthdr_len(m) < skip_mbuf + copy)
    mbuf_pkthdr_setlen(m, skip_mbuf + copy);
  
  for (; m; m = mbuf_next(m))
  {
    if (isWrite && mbuf_len(m) < skip_mbuf + copy && mbuf_trailingspace(m))
      mbuf_setlen(m, min(mbuf_maxlen(m), skip_mbuf + copy));
    
    UInt32 available = mbuf_len(m);
    
    //KDEBUG("available=%d, skip_mbuf=%d", available, skip_mbuf);
    
    if (skip_mbuf >= available)
    {
      skip_mbuf -= available;
      continue;
    }
    
    UInt8 *buf = (UInt8 *)mbuf_data(m) + skip_mbuf;
    IOByteCount len = copy;                       // remaining requested
    len = min(len, available - skip_mbuf);        // available in mbuf
    len = min(len, buffer->getLength() - offset); // available in iomd    
    IOByteCount wrote = 0;
    
    if (!len)
    {
      KDEBUG("no space, %d-%d, %d-%d", available, skip_mbuf, buffer->getLength(), offset);
      break;
    }
    
    //KDEBUG("COPY: skip_buffer=%d, offset=%d, len=%d (remaining=%d)", skip_buffer, offset, len, copy);
    if (isWrite)
      wrote = buffer->readBytes(skip_buffer + offset, buf, len);
    else
      wrote = buffer->writeBytes(skip_buffer + offset, buf, len);

    if (wrote != len)
    {
      KDEBUG("short IO");
      break;
    }
    
    offset += len;
    copy -= len;
    skip_mbuf = 0;
  }
  
  if (buffer->complete() != kIOReturnSuccess)
  {
    KDEBUG("buffer complete failed");
    return false;
  }

  if (copy > 0)
  {
    KDEBUG("failed to copy requested data: %d remaining", copy);
    return false;
  }
  
  return true;
}


OSString *net_habitue_device_SC101::getID()
{
  return OSDynamicCast(OSString, getProperty(gSC101DeviceIDKey));
}


void net_habitue_device_SC101::setIcon(OSString *resourceFile)
{
  OSString *identifier = OSDynamicCast(OSString, getProvider()->getProperty(kCFBundleIdentifierKey));
  OSDictionary *iconDict = OSDictionary::withCapacity(2);
  
  if (iconDict)
  {
    iconDict->setObject(kCFBundleIdentifierKey, identifier);
    iconDict->setObject(kIOBundleResourceFileKey, resourceFile);
    setProperty(kIOMediaIconKey, iconDict);
    iconDict->release();
  }
}


void net_habitue_device_SC101::resolve()
{
  KDEBUG("resolving");
  clock_get_uptime(&_lastResolve);
  
  struct sockaddr_in addr;
  bzero(&addr, sizeof(addr));
  addr.sin_len = sizeof(addr);
  addr.sin_family = AF_INET;
  addr.sin_port = htons(PSAN_PORT);
  addr.sin_addr.s_addr = INADDR_BROADCAST;

  struct psan_resolve_t req;
  bzero(&req, sizeof(req));
  req.ctrl.cmd = PSAN_RESOLVE;
  req.ctrl.seq = ((net_habitue_driver_SC101 *)getProvider())->getSequenceNumber();
  strncpy(req.id, getID()->getCStringNoCopy(), sizeof(req.id));
  
  struct outstanding *out = IONew(struct outstanding, 1);
  if (!out)
    ; // TODO(iwade) handle
  bzero(out, sizeof(*out));
  out->seq = ntohs(req.ctrl.seq);
  out->len = sizeof(struct psan_resolve_response_t);
  out->cmd = PSAN_RESOLVE_RESPONSE;
  out->packetHandler = OSMemberFunctionCast(PacketHandler, this, &net_habitue_device_SC101::handleResolvePacket);
  out->timeoutHandler = OSMemberFunctionCast(TimeoutHandler, this, &net_habitue_device_SC101::handleResolveTimeout);
  out->target = this;
  out->timeout_ms = RESOLVE_TIMEOUT_MS;
  
  mbuf_t m;

  if (mbuf_allocpacket(MBUF_WAITOK, sizeof(struct psan_resolve_t), NULL, &m) != 0)
    KDEBUG("mbuf_allocpacket failed!"); // TODO(iwade) handle

  if (mbuf_copyback(m, 0, sizeof(req), &req, MBUF_WAITOK) != 0)
    KDEBUG("mbuf_copyback failed!"); // TODO(iwade) handle
  
  ((net_habitue_driver_SC101 *)getProvider())->sendPacket(&addr, m, out);
}


void net_habitue_device_SC101::retryResolve()
{
  UInt64 now;
  clock_get_uptime(&now);
  
  if (now - _lastResolve > _resolveInterval &&
      now - _lastReply > _resolveInterval)
  {
    resolve();
  }
}


void net_habitue_device_SC101::handleResolvePacket(struct sockaddr_in *addr, mbuf_t m, size_t len, struct outstanding *out, void *ctx)
{
  clock_get_uptime(&_lastReply);
  
  if (mbuf_len(m) < out->len &&
      mbuf_pullup(&m, out->len) != 0)
  {
    KDEBUG("pullup failed");
    return;
  }
  
  KDEBUG("resolve succeeded!");
  
  struct psan_resolve_response_t *res = (struct psan_resolve_response_t *)mbuf_data(m);
  
  struct sockaddr_in part;
  bzero(&part, sizeof(part));
  part.sin_len = sizeof(part);
  part.sin_family = AF_INET;
  part.sin_port = htons(PSAN_PORT);
  part.sin_addr = res->ip4;
  
  OSData *partData = OSData::withBytes(&part, sizeof(part));
  if (partData)
  {
    setProperty(gSC101DevicePartitionAddressKey, partData);
    partData->release();
  }
  
  OSData *rootData = OSData::withBytes(addr, sizeof(*addr));
  if (rootData)
  {
    setProperty(gSC101DeviceRootAddressKey, rootData);
    rootData->release();
  }
  
  IODelete(out, struct outstanding, 1);
  
  mbuf_freem(m);

  if (!getProperty(gSC101DeviceSizeKey))
    disk();
}


void net_habitue_device_SC101::handleResolveTimeout(struct outstanding *out, void *ctx)
{
  KDEBUG("resolve timed out, no such ID '%s'?", getID()->getCStringNoCopy());
  
  IODelete(out, struct outstanding, 1);
  
  // TODO(iwade) detach if never successfully resolved.
}


/* read the first sector on the root address for model, firmware and partition info */
void net_habitue_device_SC101::disk()
{
  UInt32 block = 0;
  UInt32 nblks = 1;
  
  IOBufferMemoryDescriptor *buffer = IOBufferMemoryDescriptor::withCapacity(nblks * SECTOR_SIZE, kIODirectionIn);
  if (!buffer)
    ; // TODO(iwade) handle
  
  IOStorageCompletion completion;
  completion.target = this;
  completion.action = OSMemberFunctionCast(IOStorageCompletionAction, this, &net_habitue_device_SC101::diskCompletion);
  completion.parameter = buffer;

  OSData *addr = OSDynamicCast(OSData, getProperty(gSC101DeviceRootAddressKey));
  addr->retain();

  prepareAndDoAsyncReadWrite(addr, buffer, block, nblks, completion);
}


void net_habitue_device_SC101::diskCompletion(void *parameter, IOReturn status, UInt64 actualByteCount)
{
  IOBufferMemoryDescriptor *buffer = (IOBufferMemoryDescriptor *)parameter;
  
  if (status != kIOReturnSuccess || actualByteCount != sizeof(psan_get_response_disk_t))
  {
    KDEBUG("disk query on %s failed", getID()->getCStringNoCopy());
    goto cleanup;
  }
  
  struct psan_get_response_disk_t *disk = (struct psan_get_response_disk_t *)buffer->getBytesNoCopy();
  
  OSData *partNumber = OSData::withBytes(disk->part_number, sizeof(disk->part_number));
  if (partNumber)
  {
    OSString *resourceFile = NULL;

    if (partNumber->isEqualTo(kSC101PartNumber, sizeof(kSC101PartNumber)))
      resourceFile = OSString::withCString("SC101.icns");
    else if (partNumber->isEqualTo(kSC101TPartNumber, sizeof(kSC101TPartNumber)))
      resourceFile = OSString::withCString("SC101T.icns");
    
    if (resourceFile)
    {
      setIcon(resourceFile);
      resourceFile->release();
    }
    
    setProperty(gSC101DevicePartNumberKey, partNumber);    
    partNumber->release();
  }
  
  OSString *version = OSString::withCString(disk->version);
  if (version)
  {
    setProperty(gSC101DeviceVersionKey, version);
    version->release();
  }
  
  partition(disk->partitions);
  
cleanup:
  buffer->release();
}


void net_habitue_device_SC101::partition(UInt8 partitions)
{
  UInt32 block = 1;
  UInt32 nblks = partitions;
  
  IOBufferMemoryDescriptor *buffer = IOBufferMemoryDescriptor::withCapacity(nblks * SECTOR_SIZE, kIODirectionIn);
  if (!buffer)
    ; // TODO(iwade) handle
  
  IOStorageCompletion completion;
  completion.target = this;
  completion.action = OSMemberFunctionCast(IOStorageCompletionAction, this, &net_habitue_device_SC101::partitionCompletion);
  completion.parameter = buffer;
  
  OSData *addr = OSDynamicCast(OSData, getProperty(gSC101DeviceRootAddressKey));
  addr->retain();

  prepareAndDoAsyncReadWrite(addr, buffer, block, nblks, completion);
}

/* read the <partition#> sector on the root address for label and size */
void net_habitue_device_SC101::partitionCompletion(void *parameter, IOReturn status, UInt64 actualByteCount)
{
  IOBufferMemoryDescriptor *buffer = (IOBufferMemoryDescriptor *)parameter;
  
  if (status != kIOReturnSuccess || actualByteCount != sizeof(struct psan_get_response_partition_t))
  {
    KDEBUG("partition lookup on %s failed", getID()->getCStringNoCopy());
    goto cleanup;
  }
  
  struct psan_get_response_partition_t *part = (struct psan_get_response_partition_t *)buffer->getBytesNoCopy();
  OSString *id = getID();

  for (UInt32 i = 0; i < actualByteCount / sizeof(struct psan_get_response_partition_t); i++, part++)
  {
    KDEBUG("cmp %s", part->id);
    
    if (strncmp(part->id, id->getCStringNoCopy(), id->getLength() + 1) != 0)
      continue;
    
    KDEBUG("Matched!");
    
    OSString *label = OSString::withCString(part->label);
    if (label)
    {
      setProperty(gSC101DeviceLabelKey, label);
      label->release();
    }
    
    OSNumber *size = OSNumber::withNumber(getUInt48(part->sector_size) << 9, 64);
    if (size)
    {
      setProperty(gSC101DeviceSizeKey, size);
      size->release();
    }
 
    if (1) // TODO(iwade) determine minimum fields needed
    {
      _mediaStateAttached = true;
      _mediaStateChanged = true;
    }

    break;
  }
  
cleanup:
  buffer->release();
}

void net_habitue_device_SC101::handleAsyncIOPacket(struct sockaddr_in *addr, mbuf_t m, size_t len, struct outstanding *out, void *ctx)
{  
  clock_get_uptime(&_lastReply);
  
  struct outstanding_io *io = (struct outstanding_io *)ctx;
  bool isWrite = (io->buffer->getDirection() == kIODirectionOut);
  UInt32 ioLen = (io->nblks * SECTOR_SIZE);
  
  IOStorageCompletion completion = io->completion;
  IOReturn status = kIOReturnError;
  IOByteCount wrote = ioLen;
  
  if (isWrite)
  {
    //KDEBUG("%p write %d %d", io, io->block, io->nblks);

    status = kIOReturnSuccess;
  }
  else
  {
    //KDEBUG("%p read %d %d", io, io->block, io->nblks);

    if (mbuf_buffer(io->buffer, 0, m, sizeof(struct psan_get_response_t), ioLen))
      status = kIOReturnSuccess;
    else
      KDEBUG("mbuf_buffer failed");
  }
  
  if (status != kIOReturnSuccess)
    KDEBUG("%p FAILED", io);

  completeIO(io);
  io->addr->release();
  IODelete(io, struct outstanding_io, 1);
  
  mbuf_freem(m);
  
  IOStorage::complete(completion, status, wrote);
}


void net_habitue_device_SC101::handleAsyncIOTimeout(struct outstanding *out, void *ctx)
{
  struct outstanding_io *io = (struct outstanding_io *)ctx;
  IOStorageCompletion completion = io->completion;
  bool isWrite = (io->buffer->getDirection() == kIODirectionOut);
  
  io->attempt++;
  io->timeout_ms = getNextTimeoutMS(io->attempt, isWrite);
  
  if (io->timeout_ms)
  {
    KDEBUG("retry IO (%p, %d, %d)", io, io->attempt, io->timeout_ms);
    // IOBlockStorageDriver::incrementRetries(isWrite)
    
    doSubmitIO(io);
    return;
  }
  
  KDEBUG("abort IO %p", io);
  // IOBlockStorageDriver::incrementErrors(isWrite)
  
  completeIO(io);
  io->addr->release();
  IODelete(io, struct outstanding_io, 1);

  IOStorage::complete(completion, kIOReturnNotResponding, 0);
}


void net_habitue_device_SC101::prepareAndDoAsyncReadWrite(OSData *addr, IOMemoryDescriptor *buffer, UInt32 block, UInt32 nblks, IOStorageCompletion completion)
{
  bool isWrite = (buffer->getDirection() == kIODirectionOut);
  const OSSymbol *ioMaxKey = (isWrite ? gSC101DeviceIOMaxWriteSizeKey : gSC101DeviceIOMaxReadSizeKey);
  UInt64 ioMaxSize = OSDynamicCast(OSNumber, getProperty(ioMaxKey))->unsigned64BitValue();
  UInt64 ioSize = (nblks * SECTOR_SIZE);
  
#if WRITEPROTECT
  if (isWrite)
    panic();
#endif
  
  if (ioSize > ioMaxSize || ioSize & (ioSize - 1))
  {
    //KDEBUG("%s size=%llu, deblocking", (isWrite ? "write" : "read"), ioSize);
    deblock(buffer, block, nblks, completion);
    return;
  }
  
  struct outstanding_io *io = IONew(struct outstanding_io, 1);
  if (!io)
    ; // TODO(iwade) handle
  bzero(io, sizeof(*io));
  io->addr = addr;
  io->buffer = buffer;
  io->block = block;
  io->nblks = nblks;
  io->completion = completion;
  io->attempt = 0;
  io->timeout_ms = getNextTimeoutMS(io->attempt, isWrite);
  
  getWorkLoop()->runAction(OSMemberFunctionCast(Action, this, &net_habitue_device_SC101::submitIO), this, io);
}

void net_habitue_device_SC101::submitIO(struct outstanding_io *io)
{
  if (_outstandingCount >= MAX_IO_OUTSTANDING)
  {
    queueIO(io);
    return;
  }
  
  STAILQ_INSERT_TAIL(&_outstandingHead, io, entries);
  _outstandingCount++;
  
  doSubmitIO(io);
}

void net_habitue_device_SC101::doSubmitIO(struct outstanding_io *io)
{
  bool isWrite = (io->buffer->getDirection() == kIODirectionOut);
  UInt32 ioLen = (io->nblks * SECTOR_SIZE);
  mbuf_t m;

  retryResolve();
  
  if (isWrite)
  {
    //KDEBUG("%p write %d %d (%d)", io, io->block, io->nblks, _outstandingCount);
    
    struct psan_put_t req;
    bzero(&req, sizeof(req));
    req.ctrl.cmd = PSAN_PUT;
    req.ctrl.seq = ((net_habitue_driver_SC101 *)getProvider())->getSequenceNumber();
    req.ctrl.len_power = POWER_OF_2(ioLen);
    req.sector = htonl(io->block);
    
    if (mbuf_allocpacket(MBUF_WAITOK, sizeof(req) + ioLen, NULL, &m) != 0)
      KDEBUG("mbuf_allocpacket failed!"); // TODO(iwade) handle

    if (mbuf_copyback(m, 0, sizeof(req), &req, MBUF_WAITOK) != 0)
      KDEBUG("mbuf_copyback failed!"); // TODO(iwade) handle
    
    if (!mbuf_buffer(io->buffer, 0, m, sizeof(req), ioLen))
      KDEBUG("mbuf_buffer failed"); // TODO(iwade) handle

    io->outstanding.seq = ntohs(req.ctrl.seq);
    io->outstanding.len = sizeof(struct psan_put_response_t);
    io->outstanding.cmd = PSAN_PUT_RESPONSE;
  }
  else
  {
    //KDEBUG("%p read %d %d (%d)", io, io->block, io->nblks, _outstandingCount);
    
    struct psan_get_t req;
    bzero(&req, sizeof(req));
    req.ctrl.cmd = PSAN_GET;
    req.ctrl.seq = ((net_habitue_driver_SC101 *)getProvider())->getSequenceNumber();
    req.ctrl.len_power = POWER_OF_2(ioLen);
    req.sector = htonl(io->block);
    
    if (mbuf_allocpacket(MBUF_WAITOK, sizeof(req), NULL, &m) != 0)
      KDEBUG("mbuf_allocpacket failed!"); // TODO(iwade) handle

    if (mbuf_copyback(m, 0, sizeof(req), &req, MBUF_WAITOK) != 0)
      KDEBUG("mbuf_copyback failed!"); // TODO(iwade) handle
    
    io->outstanding.seq = ntohs(req.ctrl.seq);
    io->outstanding.len = sizeof(struct psan_get_response_t) + ioLen;
    io->outstanding.cmd = PSAN_GET_RESPONSE;
  }
  
  io->outstanding.packetHandler = OSMemberFunctionCast(PacketHandler, this, &net_habitue_device_SC101::handleAsyncIOPacket);
  io->outstanding.timeoutHandler = OSMemberFunctionCast(TimeoutHandler, this, &net_habitue_device_SC101::handleAsyncIOTimeout);
  io->outstanding.target = this;
  io->outstanding.ctx = io;
  io->outstanding.timeout_ms = io->timeout_ms;

  ((net_habitue_driver_SC101 *)getProvider())->sendPacket((struct sockaddr_in *)io->addr->getBytesNoCopy(), m, &io->outstanding);
}


void net_habitue_device_SC101::completeIO(struct outstanding_io *io)
{
  STAILQ_REMOVE(&_outstandingHead, io, outstanding_io, entries);
  _outstandingCount--;
  
  dequeueAndSubmitIO();
}


void net_habitue_device_SC101::queueIO(struct outstanding_io *io)
{
  STAILQ_INSERT_TAIL(&_pendingHead, io, entries);
  _pendingCount++;
}


void net_habitue_device_SC101::dequeueAndSubmitIO()
{
  struct outstanding_io *io = STAILQ_FIRST(&_pendingHead);
  
  if (io)
  {
    STAILQ_REMOVE(&_pendingHead, io, outstanding_io, entries);
    _pendingCount--;
    
    submitIO(io);
  }
}

/**********************************************************************************************************************************/
#pragma mark Request Splitting functions
/**********************************************************************************************************************************/


struct deblock_master_state {
  IOMemoryDescriptor *buffer;
  UInt32 block;
  UInt32 nblks;
  IOStorageCompletion completion;
  
  UInt32 pending;
  
  IOReturn status;
  UInt64 actualByteCount;
};

struct deblock_state {
  struct deblock_master_state *master;
  IOMemoryDescriptor *buffer;
};


void deblockCompletion(void *target, void *parameter, IOReturn status, UInt64 actualByteCount)
{
  struct deblock_state *state = (struct deblock_state *)parameter;
  struct deblock_master_state *master = state->master;
  
  state->buffer->release();
  IODelete(state, struct deblock_state, 1);
  
  master->pending--;
  master->actualByteCount += actualByteCount;
  if (status != kIOReturnSuccess)
    master->status = status;
  
  if (!master->pending)
  {
    if (master->status != kIOReturnSuccess)
      KDEBUG("deblock FAILED");
    
    IOStorage::complete(master->completion, master->status, master->actualByteCount);
    
    IODelete(master, struct deblock_master_state, 1);
  }
}


void net_habitue_device_SC101::deblock(IOMemoryDescriptor *buffer, UInt32 block, UInt32 nblks, IOStorageCompletion completion)
{
  bool isWrite = (buffer->getDirection() == kIODirectionOut);
  const OSSymbol *ioMaxKey = (isWrite ? gSC101DeviceIOMaxWriteSizeKey : gSC101DeviceIOMaxReadSizeKey);
  UInt64 ioMaxSize = OSDynamicCast(OSNumber, getProperty(ioMaxKey))->unsigned64BitValue();
  UInt64 ioSize = (nblks * SECTOR_SIZE);
  
  struct deblock_master_state *master = IONew(struct deblock_master_state, 1);
  if (!master)
    ; // TODO(iwade) handle
  bzero(master, sizeof(*master));
  master->buffer = buffer;
  master->block = block;
  master->nblks = nblks;
  master->completion = completion;
  master->status = kIOReturnSuccess;
    
  for (UInt64 used = 0, use = min(LSB(ioSize), ioMaxSize);
       used < ioSize;
       used += use, use = min(LSB(ioSize - used), ioMaxSize))
  {
    struct deblock_state *state = IONew(struct deblock_state, 1);
    if (!state)
      ; // TODO(iwade) handle
    bzero(state, sizeof(*state));
    state->master = master;
    state->buffer = IOMemoryDescriptor::withSubRange(buffer, used, use, buffer->getDirection());
    if (!state->buffer)
      ; // TODO(iwade) handle
    
    IOStorageCompletion new_completion;
    new_completion.target = this;
    new_completion.action = deblockCompletion;
    new_completion.parameter = state;
    
    master->pending++;
    
    //KDEBUG("deblock %s used=%llu, use=%llu", (isWrite ? "write" : "read"), used, use);
    
    doAsyncReadWrite(state->buffer, block + used / SECTOR_SIZE, use / SECTOR_SIZE, new_completion);
  }
}
