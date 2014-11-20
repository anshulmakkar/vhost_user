#include"vring.h"
#include"vhost.h"
#include "vhost_user_transport.h"
// TODO: these are NET specific
#define VHOST_CLIENT_VRING_IDX_RX   0
#define VHOST_CLIENT_VRING_IDX_TX   1
#define VHOST_CLIENT_VRING_NUM      2
#define ETH_PACKET_SIZE         (1518)
#define BUFFER_SIZE             (sizeof(struct virtio_net_hdr) + ETH_PACKET_SIZE)
enum { VHOST_USER_MEMORY_MAX_NREGIONS = 8 };

#define MEMB_SIZE(t,m)      (sizeof(((t*)0)->m))

// vhost_user request types
typedef enum VHostUserRequest
{
    VHOST_USER_NONE = 0,
    VHOST_USER_GET_FEATURES = 1,
    VHOST_USER_SET_FEATURES = 2,
    VHOST_USER_SET_OWNER = 3,
    VHOST_USER_RESET_OWNER = 4,
    VHOST_USER_SET_MEM_TABLE = 5,
    VHOST_USER_SET_LOG_BASE = 6,
    VHOST_USER_SET_LOG_FD = 7,
    VHOST_USER_SET_VRING_NUM = 8,
    VHOST_USER_SET_VRING_ADDR = 9,
    VHOST_USER_SET_VRING_BASE = 10,
    VHOST_USER_GET_VRING_BASE = 11,
    VHOST_USER_SET_VRING_KICK = 12,
    VHOST_USER_SET_VRING_CALL = 13,
    VHOST_USER_SET_VRING_ERR = 14,
    VHOST_USER_NET_SET_BACKEND = 15,
    VHOST_USER_ECHO = 16,
    VHOST_USER_MAX
}VHostUserRequest;

struct VHostUserMemoryRegion
{
  uint64_t guest_phys_addr;
  uint64_t memory_size;
  uint64_t userspace_addr;
};
typedef struct VHostUserMemoryRegion VHostUserMemoryRegion;

struct VHostUserLocalMemoryRegion
{
  uint64_t guest_phys_addr;
  uint64_t memory_size;
  uint64_t userspace_addr;
  uint64_t mmap_addr;
};
typedef struct VHostUserLocalMemoryRegion VHostUserLocalMemoryRegion;
struct VHostUserMemory
{
  uint32_t nregions;
  uint32_t padding;
  struct VHostUserMemoryRegion regions[VHOST_USER_MEMORY_MAX_NREGIONS];
};
typedef struct VHostUserMemory VHostUserMemory;

struct VHostUserLocalMemory
{
    uint32_t nregions;
    VHostUserLocalMemoryRegion regions[VHOST_USER_MEMORY_MAX_NREGIONS];
};
typedef struct VHostUserLocalMemory VHostUserLocalMemory;

struct VHostUserMsg
{
  VHostUserRequest request;
#define VHOST_USER_VERSION_MASK     (0x3)
#define VHOST_USER_REPLY_MASK       (0x1<<2)
  uint32_t flags;
  uint32_t size;
  union {
#define VHOST_USER_VRING_IDX_MASK   (0xff)
#define VHOST_USER_VRING_NOFD_MASK  (0x1<<8)
    uint64_t u64;
    int fd;
    // defined in vhost.h
    struct vhost_vring_state state;
    struct vhost_vring_addr addr;
    struct vhost_vring_file file;
    struct VHostUserMemory memory;
  };
} __attribute__((packed));
typedef struct VHostUserMsg VHostUserMsg;
#define FD_LIST_SIZE  10

struct VHostUserLocalMsg
{
    VHostUserMsg request;
    size_t fd_num;
    int fds[VHOST_USER_MEMORY_MAX_NREGIONS];
};
typedef struct VHostUserLocalMsg VHostUserLocalMsg;

typedef int (*FdHandler)(struct FdNodeStruct *node);
struct FdNodeStruct
{
    int fd;
    void *context;
    FdHandler handler;
};

typedef struct FdNodeStruct FdNode;

typedef struct 
{
    size_t nfds;
    int fdmax;
    FdNode read_fds[FD_LIST_SIZE];
    FdNode write_fds[FD_LIST_SIZE];
    uint32_t ms;    
}FdList;

typedef enum {
    FD_READ, 
    FD_WRITE
}FdType;

#define FD_LIST_SELECT_POLL (0) //poll and exit
#define FD_LIST_SELECT_5    (200) //5 times per sec

int init_fd_list(FdList *fd_list, uint32_t ms);
int add_fd_list(FdList *fd_list, FdType type, int fd, void *context, 
                FdHandler handler);
int del_fd_list(FdList *fd_list, FdType type, int fd);
int traverse_fd_list(FdList *fd_list);

typedef int (*InMsgHandler)(void* context, struct VHostUserLocalMsg* msg);
typedef int (*PollHandler)(void* context);

struct AppHandlers {
    void* context;
    InMsgHandler in_handler;
    PollHandler poll_handler;
};
typedef struct AppHandlers AppHandlers;

struct VHostUser
{
    char path[256];
    VHostUserLocalMemory memory;
    VringTable vring_table;
    int is_polling;
    uint8_t buffer[BUFFER_SIZE];
    uint32_t buffer_size;
    int status;
    int sock;
    FdList fd_list;
    AppHandlers handlers;
};

struct PThreadArgs
{
    char server_name[255];
    struct VHostUser *vhost_user;
};
typedef struct PThreadArgs PThreadArgs;

typedef struct VHostUser VHostUser;
int vhost_user_open_socket(VHostUser *);
int vhost_user_accept(int sock);
int vhost_user_send(int sock, struct VHostUserMsg *msg);
int vhost_user_receive(int sock, struct VHostUserMsg *msg, int *fds, int *nfds);
void* vhost_user_map_guest_memory(int fd, int size);
int vhost_user_unmap_guest_memory(void *ptr, int size);
int vhost_user_sync_shm(void *ptr, size_t size);
int put_vring(VringTable *vring_table, uint32_t v_idx, void * buf, size_t size);
/***********transport function declaration */
static int die(const char *reason);

int tcp_server_listen(struct app_data *data);

struct app_context * init_context(struct app_data *data);
//static void destroy_ctx(struct app_context *ctx);

void set_local_ib_connection(struct app_context *ctx, struct app_data *data);
//static void print_ib_connection(char *conn_name, struct ib_connection *conn);

int tcp_exch_ib_connection_info(struct app_data *data);

int connect_ctx(struct app_context *ctx, struct app_data *data);

static int qp_change_state_init(struct ibv_qp *qp, struct app_data *data);
int qp_change_state_rtr(struct ibv_qp *qp, struct app_data *data);
int qp_change_state_rts(struct ibv_qp *qp, struct app_data *data);
void rdma_transfer(uint8_t * buf, size_t size);
static void handle_rdma_transport();
static void handle_rdma_transport_receive(PThreadArgs *pthread_args);
static int send_packet(struct VHostUser* vhost_user, void* p, size_t size);
