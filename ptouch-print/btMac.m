//
//  btMac.m
//  ptouch-print
//
//  Bluetooth Classic (RFCOMM / SPP) transport for P-Touch printers such as
//  the PT-P910BT. macOS no longer connects the /dev/cu.PT-* serial ports it
//  creates for these printers, so talk to the printer's "SPP SERVER" RFCOMM
//  channel directly through IOBluetooth instead.
//

#import <Foundation/Foundation.h>
#import <IOBluetooth/IOBluetooth.h>
#include <dlfcn.h>
#include <mach-o/dyld.h>
#include <spawn.h>
#include <sys/wait.h>
#include "include/btMac.h"

extern char **environ;

@interface PTBluetoothLink : NSObject <IOBluetoothRFCOMMChannelDelegate>
@property(nonatomic) IOBluetoothDevice *device;
@property(nonatomic) IOBluetoothRFCOMMChannel *channel;
@property(nonatomic) NSMutableData *rx;
@property(nonatomic) NSMutableData *tx;
@property(nonatomic) BOOL openDone;
@property(nonatomic) IOReturn openStatus;
@property(nonatomic) BOOL closed;
@end

@implementation PTBluetoothLink

- (instancetype)init {
  self = [super init];
  _rx = [NSMutableData data];
  _tx = [NSMutableData data];
  return self;
}

- (void)rfcommChannelData:(IOBluetoothRFCOMMChannel *)channel data:(void *)data length:(size_t)length {
  [self.rx appendBytes:data length:length];
}

- (void)rfcommChannelOpenComplete:(IOBluetoothRFCOMMChannel *)channel status:(IOReturn)error {
  self.openStatus = error;
  self.openDone = YES;
}

- (void)rfcommChannelClosed:(IOBluetoothRFCOMMChannel *)channel {
  self.closed = YES;
}

@end

static PTBluetoothLink *gLink;
static int gDebug;

static void spin(NSTimeInterval seconds) {
  [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode beforeDate:[NSDate dateWithTimeIntervalSinceNow:seconds]];
}

/* Paired printers whose name starts with "PT-", or matches name/address. */
static IOBluetoothDevice *findPrinter(const char *want) {
  NSString *wanted = want ? @(want) : nil;
  for (IOBluetoothDevice *d in [IOBluetoothDevice pairedDevices]) {
    NSString *name = d.name ?: @"";
    if (wanted) {
      if ([name rangeOfString:wanted options:NSCaseInsensitiveSearch].location != NSNotFound ||
          [d.addressString caseInsensitiveCompare:[wanted stringByReplacingOccurrencesOfString:@":" withString:@"-"]] == NSOrderedSame) {
        return d;
      }
    } else if ([name hasPrefix:@"PT-"]) {
      return d;
    }
  }
  return nil;
}

@interface PTSDPWaiter : NSObject
@property(nonatomic) BOOL done;
@end

@implementation PTSDPWaiter
- (void)sdpQueryComplete:(IOBluetoothDevice *)device status:(IOReturn)status {
  self.done = YES;
}
@end

/* RFCOMM channel of the printer's Serial Port Profile service, or 0. The
   PT-P910BT also offers an Apple iAP service on another RFCOMM channel,
   which must not be used. Match by the SPP service class UUID (0x1101),
   and only fall back to the service name. */
static BluetoothRFCOMMChannelID sppChannelIn(NSArray *services) {
  NSArray *spp = @[[IOBluetoothSDPUUID uuid16:kBluetoothSDPUUID16ServiceClassSerialPort]];
  BluetoothRFCOMMChannelID byName = 0;
  for (IOBluetoothSDPServiceRecord *s in services) {
    BluetoothRFCOMMChannelID ch = 0;
    if ([s getRFCOMMChannelID:&ch] != kIOReturnSuccess) {
      continue;
    }
    NSString *name = [s getServiceName];
    BOOL isSPP = [s hasServiceFromArray:spp];
    if (gDebug) {
      fprintf(stderr, "service '%s' on RFCOMM channel %d%s\n", name ? name.UTF8String : "?", ch, isSPP ? " (SPP)" : "");
    }
    if (isSPP) {
      return ch;
    }
    if (byName == 0 && name && [name rangeOfString:@"SPP" options:NSCaseInsensitiveSearch].location != NSNotFound) {
      byName = ch;
    }
  }
  return byName;
}

static BluetoothRFCOMMChannelID findSPPChannel(IOBluetoothDevice *d) {
  /* the cached service records are usually good enough; a fresh SDP
     query takes several seconds */
  BluetoothRFCOMMChannelID ch = sppChannelIn(d.services);
  if (ch != 0) {
    return ch;
  }
  PTSDPWaiter *waiter = [PTSDPWaiter new];
  if ([d performSDPQuery:waiter] == kIOReturnSuccess) {
    for (int i = 0; i < 150 && !waiter.done; i++) {
      spin(0.1);
    }
  }
  return sppChannelIn(d.services);
}

int ptbt_open(const char *want, int debug, char *found_name, size_t found_len) {
  @autoreleasepool {
    gDebug = debug;
    IOBluetoothDevice *d = findPrinter(want);
    if (d == nil) {
      fprintf(stderr, "no paired Bluetooth printer %s%s%sfound - pair it in System Settings > Bluetooth first\n",
              want ? "\"" : "", want ? want : "", want ? "\" " : "");
      return -1;
    }
    snprintf(found_name, found_len, "%s", d.name.UTF8String ?: "");
    fprintf(stderr, "connecting to %s over Bluetooth...\n", found_name);
    BluetoothRFCOMMChannelID ch = findSPPChannel(d);
    if (ch == 0) {
      fprintf(stderr, "%s offers no Bluetooth serial port service - is it switched on and in range?\n", found_name);
      return -1;
    }
    PTBluetoothLink *link = [PTBluetoothLink new];
    link.device = d;
    IOBluetoothRFCOMMChannel *channel = nil;
    IOReturn r = [d openRFCOMMChannelAsync:&channel withChannelID:ch delegate:link];
    if (r != kIOReturnSuccess) {
      fprintf(stderr, "can not open Bluetooth channel %d: 0x%x\n", ch, r);
      return -1;
    }
    for (int i = 0; i < 200 && !link.openDone; i++) {
      spin(0.1);
    }
    if (!link.openDone || link.openStatus != kIOReturnSuccess) {
      if (!link.openDone) {
        fprintf(stderr, "Bluetooth connection to %s timed out - is it switched on and in range?\n", found_name);
      } else {
        fprintf(stderr, "Bluetooth connection to %s failed (0x%x) - is it switched on and in range?\n", found_name, link.openStatus);
      }
      [channel closeChannel];
      return -1;
    }
    link.channel = channel;
    gLink = link;
    return 0;
  }
}

int ptbt_flush(void) {
  if (gLink == nil) {
    return -1;
  }
  NSMutableData *tx = gLink.tx;
  size_t mtu = gLink.channel.getMTU;
  if (mtu == 0) {
    mtu = 127;
  }
  size_t done = 0;
  while (done < tx.length) {
    if (gLink.closed) {
      fprintf(stderr, "Bluetooth connection closed by printer\n");
      return -1;
    }
    size_t n = MIN(mtu, tx.length - done);
    IOReturn r = [gLink.channel writeSync:(uint8_t *)tx.mutableBytes + done length:(UInt16)n];
    if (r != kIOReturnSuccess) {
      fprintf(stderr, "Bluetooth write error 0x%x\n", r);
      return -1;
    }
    done += n;
  }
  tx.length = 0;
  return 0;
}

int ptbt_write(const uint8_t *data, size_t len) {
  if (gLink == nil) {
    return -1;
  }
  [gLink.tx appendBytes:data length:len];
  if (gLink.tx.length >= 4096) {
    return ptbt_flush();
  }
  return 0;
}

int ptbt_read(uint8_t *buf, int len, int timeout_ms, int *tx) {
  *tx = 0;
  if (gLink == nil || ptbt_flush() != 0) {
    return -1;
  }
  NSDate *end = [NSDate dateWithTimeIntervalSinceNow:timeout_ms / 1000.0];
  while ((int)gLink.rx.length < len && !gLink.closed && [end timeIntervalSinceNow] > 0) {
    spin(0.05);
  }
  int n = (int)MIN((NSUInteger)len, gLink.rx.length);
  memcpy(buf, gLink.rx.bytes, (size_t)n);
  [gLink.rx replaceBytesInRange:NSMakeRange(0, (NSUInteger)n) withBytes:NULL length:0];
  *tx = n;
  return 0;
}

void ptbt_discard_input(void) {
  spin(0.05);
  gLink.rx.length = 0;
}

void ptbt_close(void) {
  if (gLink == nil) {
    return;
  }
  ptbt_flush();
  [gLink.channel closeChannel];
  [gLink.device closeConnection];
  gLink = nil;
}

/* macOS only lets a process use Bluetooth if its "responsible" app declares
   it. Terminal.app does not, so a command line tool started from it is
   killed on first Bluetooth use. Re-run ourselves as our own responsible
   process; the Info.plist embedded in this binary then makes macOS ask for
   Bluetooth permission for ptouch-print itself. Returns the child's exit
   status, or -1 if we are already the re-run child (or it failed). */
int ptbt_rerun_as_responsible(int argc, char **argv) {
  if (getenv("PTOUCH_RESPONSIBLE")) {
    return -1;
  }
  int (*setdisclaim)(posix_spawnattr_t *, int) = dlsym(RTLD_DEFAULT, "responsibility_spawnattrs_setdisclaim");
  char path[PATH_MAX];
  uint32_t size = sizeof(path);
  if (setdisclaim == NULL || _NSGetExecutablePath(path, &size) != 0) {
    return -1;
  }
  posix_spawnattr_t attr;
  posix_spawnattr_init(&attr);
  setdisclaim(&attr, 1);
  setenv("PTOUCH_RESPONSIBLE", "1", 1);
  pid_t pid;
  int rc = posix_spawn(&pid, path, NULL, &attr, argv, environ);
  posix_spawnattr_destroy(&attr);
  unsetenv("PTOUCH_RESPONSIBLE");
  if (rc != 0) {
    return -1;
  }
  int status = 0;
  while (waitpid(pid, &status, 0) < 0) {
    if (errno != EINTR) {
      return -1;
    }
  }
  if (WIFEXITED(status)) {
    return WEXITSTATUS(status);
  }
  if (WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT) {
    fprintf(stderr, "ptouch-print was stopped by macOS - allow it in System Settings > Privacy & Security > Bluetooth\n");
  }
  return 128 + (WIFSIGNALED(status) ? WTERMSIG(status) : 0);
}
