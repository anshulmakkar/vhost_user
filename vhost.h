#include "vring.h"
// vhost_memory structure is used to declare which memory address
// ranges we want to use for DMA. The kernel uses this to create a
// shared memory mapping.
 // Number of vring structures used in Linux vhost. Max 32768.

struct vhost_memory_region {
  uint64_t guest_phys_addr;
  uint64_t memory_size;
  uint64_t userspace_addr;
  uint64_t flags_padding; // no flags currently specified
};

enum { VHOST_MEMORY_MAX_NREGIONS = 8 };

struct vhost_memory {
  uint32_t nregions;
  uint32_t padding;
  struct vhost_memory_region regions[VHOST_MEMORY_MAX_NREGIONS];
};

// vhost is the top-level structure that the application allocates and
// initializes to open a virtio/vhost network device.

struct vhost_vring {
  // eventfd(2) for notifying the kernel (kick) and being notified (call)
  int kickfd, callfd;
  struct vring_desc desc[VHOST_VRING_SIZE] __attribute__((aligned(4)));
  struct vring_avail avail                 __attribute__((aligned(2)));
  struct vring_used used                   __attribute__((aligned(4096)));
  // XXX Hint: Adding this padding seems to reduce impact of heap corruption.
  // So perhaps it's writes to a vring structure that's over-running?
  // char pad[1000000];
};

struct vhost {
  uint64_t features;           // features negotiated with the kernel
  int tapfd;                   // file descriptor for /dev/net/tun
  int vhostfd;                 // file descriptor for /dev/vhost-net
  struct vhost_vring vring[2]; // vring[0] is receive, vring[1] is transmit
};

// These were printed out with a little throw-away C program.
enum {
  VHOST_SET_VRING_NUM   = 0x4008af10,
  VHOST_SET_VRING_BASE  = 0x4008af12,
  VHOST_SET_VRING_KICK  = 0x4008af20,
  VHOST_SET_VRING_CALL  = 0x4008af21,
  VHOST_SET_VRING_ADDR  = 0x4028af11,
  VHOST_SET_MEM_TABLE   = 0x4008af03,
  VHOST_SET_OWNER       = 0x0000af01,
  VHOST_GET_FEATURES    = 0x8008af00,
  VHOST_NET_SET_BACKEND = 0x4008af30
};


// Below are structures imported from Linux headers.
// This is purely to avoid a compile-time dependency on those headers,
// which has been an problem on certain development machines.
// Structures imported from the Linux headers.
struct vhost_vring_state { unsigned int index, num; };
struct vhost_vring_file { unsigned int index; int fd; };
struct vhost_vring_addr {
  unsigned int index, flags;
  uint64_t desc_user_addr, used_user_addr, avail_user_addr, log_guest_addr;
};

struct virtio_net_hdr
{
#define VIRTIO_NET_HDR_F_NEEDS_CSUM     1       // Use csum_start, csum_offset
#define VIRTIO_NET_HDR_F_DATA_VALID    2       // Csum is valid
    uint8_t flags;
#define VIRTIO_NET_HDR_GSO_NONE         0       // Not a GSO frame
#define VIRTIO_NET_HDR_GSO_TCPV4        1       // GSO frame, IPv4 TCP (TSO)
#define VIRTIO_NET_HDR_GSO_UDP          3       // GSO frame, IPv4 UDP (UFO)
#define VIRTIO_NET_HDR_GSO_TCPV6        4       // GSO frame, IPv6 TCP
#define VIRTIO_NET_HDR_GSO_ECN          0x80    // TCP has ECN set
    uint8_t gso_type;
    uint16_t hdr_len;
    uint16_t gso_size;
    uint16_t csum_start;
    uint16_t csum_offset;
};

