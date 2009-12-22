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

#import "SC101Driver.h"
#import "SC101Device.h"

extern "C" {
#import <sys/errno.h>
#import <netinet/in.h>
#import "psan_wireformat.h"
};


static const OSSymbol *gSC101DriverSummonKey;
static const OSSymbol *gSC101DeviceIDKey;

static void socketUpcallHandler(socket_t so, void* cookie, int waitf);


// Define my superclass
#define super IOService

// REQUIRED! This macro defines the class's constructors, destructors,
// and several other methods I/O Kit requires. Do NOT use super as the
// second parameter. You must use the literal name of the superclass.
OSDefineMetaClassAndStructors(net_habitue_driver_SC101, IOService)


/**********************************************************************************************************************************/
#pragma mark IOService stubs
/**********************************************************************************************************************************/

bool net_habitue_driver_SC101::start(IOService *provider)
{
  KINFO("Starting");
  
  gSC101DriverSummonKey = OSSymbol::withCString(kSC101DriverSummonKey);
  gSC101DeviceIDKey = OSSymbol::withCString(kSC101DeviceIDKey);
  
  if (!super::start(provider))
    return false;

  /* set up Event Loop to single thread all work */
  if (!setupEventLoop())
  {
    KINFO("%s: Failed to set up event loop!", getName());
    return false;
  }
  
  /* set up single shared UDP socket for all communications */
  if (!setupSocket())
  {
    KINFO("%s: Failed to set up socket!", getName());
    return false;
  }

  /* create and initialize array for tracking outstanding requests by 16-bit seq# */
  if (!(outstanding = (struct outstanding **)IONew(struct outstanding *, INT16_MAX)))
  {
    KINFO("%s: Failed to alloc outstanding array", getName());
    return false;
  }
  bzero(outstanding, INT16_MAX * sizeof(struct outstanding *));
  
  TAILQ_INIT(&_timeoutHead);
  
  /* there is no particular reason for this to be random */
  UInt64 now;
  clock_get_uptime(&now);
  _seq = now % INT16_MAX;
  KINFO("Sequence#: %d", _seq);
  
  registerService();
  
  return true;
}


void net_habitue_driver_SC101::stop(IOService *provider)
{
  KINFO("Stopping");

  cleanupEventLoop();
  cleanupSocket();

  for (int i = 0; i < INT16_MAX; i++)
  {
    if (outstanding[i] == NULL)
      continue;
    
    KINFO("killing outstanding[%d]", i);
    
    struct outstanding *out = outstanding[i];
    unregisterPacketHandler(out);
    out->timeoutHandler(out->target, out, out->ctx);
  }
  
  IODelete(outstanding, struct outstanding *, INT16_MAX);
  
  super::stop(provider);
}


/* this is magic! :-)
 * these requests actually come from userspace (privileged account) and I'm (ab)using
 * the interface as a general communication mechanism for convenience rather than
 * writing up a full userclient interface, which I will probably need to do eventually
 */
IOReturn net_habitue_driver_SC101::setProperties(OSObject *properties)
{
  OSDictionary *dict = OSDynamicCast(OSDictionary, properties);
  
  if (!dict)
    return kIOReturnBadArgument;
  
  OSDictionary *summon = OSDynamicCast(OSDictionary, dict->getObject(gSC101DriverSummonKey));
  
  if (!summon)
    return kIOReturnBadArgument;
  
  KINFO("summoning nub");
  addClient(summon);
  
  return kIOReturnSuccess;
}


IOWorkLoop *net_habitue_driver_SC101::getWorkLoop()
{
  return _workLoop;
}


void net_habitue_driver_SC101::addClient(OSDictionary *table)
{
  net_habitue_device_SC101 *nub = NULL;
  OSString *id = OSDynamicCast(OSString, table->getObject(gSC101DeviceIDKey));

  OSIterator *childIterator = getClientIterator();
  
  if (childIterator)
  {
    OSObject *child;
    
    while ((child = childIterator->getNextObject()))
    {
      net_habitue_device_SC101 *candidate = OSDynamicCast(net_habitue_device_SC101, child);

      /* TODO(iwade) try to use matchPropertyTable() here? */
      if (!candidate || !candidate->getID()->isEqualTo(id))
        continue;
      
      nub = candidate;
      break;
    }
    
    childIterator->release();
  }
  
  if (!nub)
  {
    nub = OSTypeAlloc(net_habitue_device_SC101);
    nub->init(table);
    if (!nub->attach(this))
      KINFO("attach failed");
    nub->registerService();
    nub->release();
  }
}


/**********************************************************************************************************************************/
#pragma mark Setup Functions
/**********************************************************************************************************************************/


bool net_habitue_driver_SC101::setupEventLoop(void)
{
  /* create local workloop */
  _workLoop = IOWorkLoop::workLoop();

  IOWorkLoop *workLoop = getWorkLoop();
  
  if (!workLoop)
  {
    KINFO("%s: Failed to getWorkLoop()", getName());
    return false;
  }
  
  /* set up command gate */
  _commandGate = IOCommandGate::commandGate(this);
  
  if (!_commandGate)
  {
    KINFO("%s: Failed to create command gate!", getName());
    return false;
  }
  
  if (workLoop->addEventSource(_commandGate) != kIOReturnSuccess)
  {
    KINFO("%s: Failed to add command gate to work loop!", getName());
    return false;
  }
  
  /* set up interrupt event source */
  _interruptSource = IOInterruptEventSource::interruptEventSource(this,
                                                                  OSMemberFunctionCast(IOInterruptEventAction, this, &net_habitue_driver_SC101::handleInterrupt));
  
  if (!_interruptSource)
  {
    KINFO("%s: Failed to create interrupt event source!", getName());
    return false;
  }
  
  if (workLoop->addEventSource(_interruptSource) != kIOReturnSuccess)
  {
    KINFO("%s: Failed to add interrupt event source to work loop!", getName());
    return false;
  }
  
  /* set up timer event source */
  _timerSource = IOTimerEventSource::timerEventSource(this, 
                                                      OSMemberFunctionCast(IOTimerEventSource::Action, this, &net_habitue_driver_SC101::timeoutOccurred));
  
  if (!_timerSource)
  {
    KINFO("%s: Failed to create timer event source!", getName());
    return false;
  }
  
  if (workLoop->addEventSource(_timerSource) != kIOReturnSuccess)
  {
    KINFO("%s: Failed to add timer event source to work loop!", getName());
    return false;
  }
  
  return true;
}


void net_habitue_driver_SC101::cleanupEventLoop(void)
{
  if (_timerSource)
  {
    _timerSource->cancelTimeout();
    getWorkLoop()->removeEventSource(_timerSource);
    _timerSource->release();
    _timerSource = NULL;
  }
  
  if (_interruptSource)
  {
    _interruptSource->disable();
    getWorkLoop()->removeEventSource(_interruptSource);
    _interruptSource->release();
    _interruptSource = NULL;
  }
  
  if (_commandGate)
  {
    getWorkLoop()->removeEventSource(_commandGate);
    _commandGate->release();
    _commandGate = NULL;
  }  
}


bool net_habitue_driver_SC101::setupSocket(void)
{
  errno_t error;
  int on = 1;
  int rcvbufsize = RCVBUF_SIZE;
  int sndbufsize = SNDBUF_SIZE;
  
  if ((error = sock_socket(AF_INET, SOCK_DGRAM, 0, socketUpcallHandler, _interruptSource, &_so)))
    goto out;
  
  if ((error = sock_setsockopt(_so, SOL_SOCKET, SO_BROADCAST, &on, sizeof(on))))
    goto out;
  
  if ((error = sock_setsockopt(_so, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on))))
    goto out;
  
  if ((error = sock_setsockopt(_so, SOL_SOCKET, SO_RCVBUF, &rcvbufsize, sizeof(rcvbufsize))))
    goto out;

  if ((error = sock_setsockopt(_so, SOL_SOCKET, SO_SNDBUF, &sndbufsize, sizeof(sndbufsize))))
    goto out;
  
  struct sockaddr_in addr;
  bzero(&addr, sizeof(addr));
  addr.sin_len = sizeof(addr);
  addr.sin_family = AF_INET;
  addr.sin_port = htons(PSAN_PORT);
  addr.sin_addr.s_addr = INADDR_ANY;
  
  /* first, try to bind known port .. unnecessary, but makes firewalling easier */
  error = sock_bind(_so, (struct sockaddr *)&addr);
  
  if (error == EADDRINUSE)
  {
    /* fall back to random port */
    addr.sin_port = htons(0);
    error = sock_bind(_so, (struct sockaddr *)&addr);
  }
  
  if (error)
    goto out;
  
  sock_getsockname(_so, (struct sockaddr *)&addr, sizeof(addr));
  KINFO("%s: Listening on port *:%u", getName(), ntohs(addr.sin_port));
  
  return true;
  
out:
  if (error)
    KINFO("Error: %d", error);
  sock_close(_so);  

  return false;
}


void net_habitue_driver_SC101::cleanupSocket(void)
{
  sock_close(_so);
}


/**********************************************************************************************************************************/
#pragma mark Interrupt Handlers
/**********************************************************************************************************************************/


static void socketUpcallHandler(socket_t so, void* cookie, int waitf)
{
  ((IOInterruptEventSource *)cookie)->interruptOccurred(NULL, NULL, NULL);
}


void net_habitue_driver_SC101::handleInterrupt(IOInterruptEventSource *sender, int count)
{
  for (int i = 0; i < count; i++)
    receivePacket();
}


void net_habitue_driver_SC101::timeoutOccurred(IOTimerEventSource *sender)
{
  processTimeout();
}


/**********************************************************************************************************************************/
#pragma mark Core Functions
/**********************************************************************************************************************************/


uint16_t net_habitue_driver_SC101::getSequenceNumber(void)
{
  if (_seq >= INT16_MAX)
    _seq = 0;

  return htons(_seq++);
}


void net_habitue_driver_SC101::receivePacket(void)
{
  struct sockaddr_in addr;
  bzero(&addr, sizeof(addr));
  addr.sin_len = sizeof(addr);
  
  struct msghdr msghdr;
  bzero(&msghdr, sizeof(msghdr));
  msghdr.msg_name = &addr;
  msghdr.msg_namelen = sizeof(addr);
  
  mbuf_t m;
  errno_t error;
  size_t len = UINT16_MAX;
  
  if ((error = sock_receivembuf(_so, &msghdr, &m, MSG_DONTWAIT, &len)))
  {
    KINFO("%s: error %d from sock_receivembuf", getName(), error);
    return;
  }
  
  if (len < sizeof(struct psan_ctrl_t))
  {
    KINFO("%s: short packet len=%zu", getName(), len);
    return;
  }
  
  handlePacket(&addr, m, len);
}


bool net_habitue_driver_SC101::sendPacket(struct sockaddr_in *dest, mbuf_t m, struct outstanding *out)
{
  if (out)
    registerPacketHandler(out);

  struct msghdr msghdr;
  bzero(&msghdr, sizeof(msghdr));
  msghdr.msg_name = dest;
  msghdr.msg_namelen = sizeof(*dest);
  
  errno_t error;
  size_t sent;
  
  if ((error = sock_sendmbuf(_so, &msghdr, m, 0, &sent)))
  {
    KINFO("Error: sock_sendmbuf() returned %d", error);
    return false;
  }
  
  return true;
}


void net_habitue_driver_SC101::registerPacketHandler(struct outstanding *out)
{
  if (outstanding[out->seq] != NULL)
    KINFO("seq#%d already used!", out->seq); // TODO(iwade) handle
  
  outstanding[out->seq] = out;

  if (out->timeout_ms)
    addTimeout(out);
}


void net_habitue_driver_SC101::unregisterPacketHandler(struct outstanding *out)
{
  outstanding[out->seq] = NULL;

  if (out->timeout_ms)
    removeTimeout(out);
}


void net_habitue_driver_SC101::handlePacket(struct sockaddr_in *addr, mbuf_t m, size_t len)
{
  if (mbuf_len(m) < sizeof(struct psan_ctrl_t) &&
      mbuf_pullup(&m, sizeof(struct psan_ctrl_t)) != 0)
  {
    KINFO("short packet, ignoring.");
    return;
  }
  
  struct psan_ctrl_t *ctrl = (struct psan_ctrl_t *)mbuf_data(m);
  struct outstanding *out = outstanding[ntohs(ctrl->seq)];

  if (!out || ntohs(ctrl->seq) != out->seq || len != out->len || ctrl->cmd != out->cmd)
  {
    if (ctrl->cmd == PSAN_ERROR && out && out->timeout_ms) {
      KINFO("Drive not ready, backing off for %d seconds", SPINUP_INTERVAL_MS/1000);
      
      removeTimeout(out);
      out->timeout_ms = SPINUP_INTERVAL_MS;
      addTimeout(out);
    }
    else if (ctrl->cmd != PSAN_FIND && ctrl->cmd != PSAN_RESOLVE)
    {
      KDEBUG("No matching request for seq#%d,cmd=0x%02x,len=%d expected:cmd=0x%02x,len=%d",
             ntohs(ctrl->seq), ctrl->cmd, len, out?out->cmd:0, out?out->len:-1);    
    }

    mbuf_freem(m);
    
    return;
  }

  unregisterPacketHandler(out);
  
  out->packetHandler(out->target, addr, m, len, out, out->ctx);
}


void net_habitue_driver_SC101::addTimeout(struct outstanding *new_out)
{
  struct outstanding *out;
  
  clock_interval_to_deadline(new_out->timeout_ms, kMillisecondScale, &new_out->timeout);
  
  TAILQ_FOREACH_REVERSE(out, &_timeoutHead, timeoutQueue, entries)
    if (out->timeout < new_out->timeout)
      break;
  
  if (out)
    TAILQ_INSERT_AFTER(&_timeoutHead, out, new_out, entries);
  else
    TAILQ_INSERT_HEAD(&_timeoutHead, new_out, entries);
  
  if (new_out == TAILQ_FIRST(&_timeoutHead))
    updateTimeout();
}


void net_habitue_driver_SC101::removeTimeout(struct outstanding *out)
{
  bool update = false;
  
  if (out == TAILQ_FIRST(&_timeoutHead))
    update = true;
  
  TAILQ_REMOVE(&_timeoutHead, out, entries);

  if (update)
    updateTimeout();
}


void net_habitue_driver_SC101::updateTimeout()
{
  struct outstanding *out = TAILQ_FIRST(&_timeoutHead);
  
  if (out)
    _timerSource->wakeAtTime(*((AbsoluteTime *)&out->timeout));
  else
    _timerSource->cancelTimeout();
}


void net_habitue_driver_SC101::processTimeout()
{
  struct outstanding *out = TAILQ_FIRST(&_timeoutHead);
  
  if (out)
  {
    unregisterPacketHandler(out);
    out->timeoutHandler(out->target, out, out->ctx);
  }
}
