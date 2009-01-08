#pragma once
#include "config.h"

#include <IOKit/storage/IOStorage.h>
#include <IOKit/storage/IOBlockStorageDevice.h>

#include "SC101Driver.h"


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
#define kSC101PartNumber ((UInt8[3]){ 0, 0, 101 })
#define kSC101TPartNumber ((UInt8[3]){ 0, 0, 102 })

struct outstanding_io {
  OSData *addr;
  
  IOMemoryDescriptor *buffer;
  UInt32 block;
  UInt32 nblks;
  IOStorageCompletion completion;
  
  int attempt;
  int timeout_ms;
  struct outstanding outstanding;
  
  STAILQ_ENTRY(outstanding_io) entries;
};

STAILQ_HEAD(outstandingIOQueue, outstanding_io);

class net_habitue_device_SC101 : public IOBlockStorageDevice
  {
    OSDeclareDefaultStructors(net_habitue_device_SC101);
  public:
    virtual bool init(OSDictionary *dictionary = 0);
    virtual bool attach(IOService *provider);
    virtual IOReturn doAsyncReadWrite(IOMemoryDescriptor *buffer, UInt32 block, UInt32 nblks, IOStorageCompletion completion);
    virtual IOReturn doEjectMedia(void);
    virtual IOReturn doFormatMedia(UInt64 byteCapacity);
    virtual UInt32 doGetFormatCapacities(UInt64 *capacities, UInt32 capacitiesMaxCount) const;
    virtual IOReturn doLockUnlockMedia(bool doLock);
    virtual IOReturn doSynchronizeCache(void);
    virtual char* getVendorString(void);
    virtual char* getProductString(void);
    virtual char* getRevisionString(void);
    virtual char* getAdditionalDeviceInfoString(void);
    virtual IOReturn reportBlockSize(UInt64 *blockSize);
    virtual IOReturn reportEjectability(bool *isEjectable);
    virtual IOReturn reportLockability(bool *isLockable);
    virtual IOReturn reportMaxReadTransfer(UInt64 blockSize, UInt64 *max);
    virtual IOReturn reportMaxWriteTransfer(UInt64 blockSize, UInt64 *max);
    virtual IOReturn reportMaxValidBlock(UInt64 *maxBlock);
    virtual IOReturn reportMediaState(bool *mediaPresent, bool *changed);
    virtual IOReturn reportPollRequirements(bool *pollIsRequired, bool *pollIsExpensive);
    virtual IOReturn reportRemovability(bool *isRemovable);
    virtual IOReturn reportWriteProtection(bool *isWriteProtected);

    OSString *getID();
    IOWorkLoop *getWorkLoop();
  protected:
    /* initial setup functions */
    void resolve();
    void retryResolve();
    void handleResolvePacket(struct sockaddr_in *addr, mbuf_t m, size_t len, struct outstanding *out, void *ctx);
    void handleResolveTimeout(struct outstanding *out, void *ctx);
    void disk();
    void diskCompletion(void *parameter, IOReturn status, UInt64 actualByteCount);
    void partition(UInt8 partitions);
    void partitionCompletion(void *parameter, IOReturn status, UInt64 actualByteCount);

    /* main IO functions */
    void handleAsyncIOPacket(struct sockaddr_in *addr, mbuf_t m, size_t len, struct outstanding *out, void *ctx);
    void handleAsyncIOTimeout(struct outstanding *out, void *ctx);    
    void prepareAndDoAsyncReadWrite(OSData *addr, IOMemoryDescriptor *buffer, UInt32 block, UInt32 nblks, IOStorageCompletion completion);
    void submitIO(struct outstanding_io *io);
    void doSubmitIO(struct outstanding_io *io);
    void completeIO(struct outstanding_io *io);
    void queueIO(struct outstanding_io *io);
    void dequeueAndSubmitIO();
    void deblock(IOMemoryDescriptor *buffer, UInt32 block, UInt32 nblks, IOStorageCompletion completion);
    
    void setIcon(OSString *resourceFile);
    
    bool _mediaStateAttached;
    bool _mediaStateChanged;

    UInt64 _resolveInterval;
    UInt64 _lastReply;
    UInt64 _lastResolve;    
    
    struct outstandingIOQueue _pendingHead;
    UInt32 _pendingCount;
    struct outstandingIOQueue _outstandingHead;
    UInt32 _outstandingCount;
  };
