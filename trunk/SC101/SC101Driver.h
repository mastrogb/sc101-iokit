#pragma once
#include "config.h"

#include <IOKit/IOService.h>
#include <IOKit/IOInterruptEventSource.h>
#include <IOKit/IOTimerEventSource.h>
#include <IOKit/IOCommandGate.h>
#include <IOKit/storage/IOStorage.h>
#include <IOKit/storage/IOBlockStorageDriver.h>
#include <IOKit/storage/IOBlockStorageDevice.h>

extern "C" {
#include <sys/kpi_socket.h>
#include <sys/kpi_mbuf.h>
#include <sys/queue.h>
}

typedef void (*PacketHandler)(OSObject *owner, struct sockaddr_in *addr, mbuf_t m, size_t len, struct outstanding *, void *ctx);
typedef void (*TimeoutHandler)(OSObject *owner, struct outstanding *, void *ctx);

struct outstanding {
  uint8_t cmd;
  int16_t seq;
  uint16_t len;
  
  PacketHandler packetHandler;
  TimeoutHandler timeoutHandler;
  OSObject *target;
  void *ctx;
  
  UInt32 timeout_ms;
  UInt64 timeout; /* auto-filled by addTimeout routine */

  TAILQ_ENTRY(outstanding) entries;
};

TAILQ_HEAD(timeoutQueue, outstanding);

class net_habitue_driver_SC101 : public IOService
  {
    OSDeclareDefaultStructors(net_habitue_driver_SC101)
  public:
    virtual bool start(IOService *provider);
    virtual void stop(IOService *provider);
    virtual IOReturn setProperties(OSObject *properties);
    virtual IOWorkLoop* getWorkLoop();
    
    // called from workloop
    virtual void timeoutOccurred(IOTimerEventSource *sender);
    virtual void handleInterrupt(IOInterruptEventSource *sender, int count);

    // called from device
    uint16_t getSequenceNumber();
    bool sendPacket(struct sockaddr_in *dest, mbuf_t m, struct outstanding *out);
  protected:
    bool setupEventLoop();
    void cleanupEventLoop();
    bool setupSocket();
    void cleanupSocket();
    
    void receivePacket();
    void handlePacket(struct sockaddr_in *addr, mbuf_t m, size_t len);
    void registerPacketHandler(struct outstanding *out);
    void unregisterPacketHandler(struct outstanding *out);
    
    void addTimeout(struct outstanding *out);
    void removeTimeout(struct outstanding *out);
    void updateTimeout();
    void processTimeout();
    
    void addClient(OSDictionary *table);

    IOWorkLoop *_workLoop;
    IOInterruptEventSource *_interruptSource;
    IOTimerEventSource *_timerSource;
    IOCommandGate *_commandGate;
    socket_t _so;
    uint16_t _seq;

    struct outstanding **outstanding;
    struct timeoutQueue _timeoutHead;
  };
