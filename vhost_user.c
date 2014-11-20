#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/mman.h>
//#include "lib/virtio/virtio_vring.h"
//#include "vhost.h"
#include "vhost_user.h"
//#include "vhost_user_transport.h"

#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))
#define VRING_IDX_NONE          ((uint16_t)-1)
#if 0
const unsigned char arp_request[60] = { 0x34, 0x36, 0x00, 0x0c, 0x22,
        0x38, /* DST MAC - broadcast */
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, /* SRC MAC -01:02:03:04:05:06 */
        0x08, 0x06, /* Eth Type - ARP */
        0x00, 0x01, /* Ethernet */
        0x08, 0x00, /* Protocol - IP */
        0x06, /* HW size */
        0x04, /* Protocol size */
        0x00, 0x01, /* Request */
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, /* Sender MAC */
        0x0a, 0x0a, 0x0a, 0x01, /* Sender IP - 10.10.10.1*/
        0x34, 0x36, 0x00, 0x0c, 0x22, 0x38, /* Target MAC */
        0x0a, 0x0a, 0x0a, 0x02, /* Target IP - 10.10.10.2*/
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* Padding */
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
#endif
const unsigned char arp_request[60] = { 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, /* DST MAC - broadcast */
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, /* SRC MAC -01:02:03:04:05:06 */
        0x08, 0x06, /* Eth Type - ARP */
        0x00, 0x01, /* Ethernet */
        0x08, 0x00, /* Protocol - IP */
        0x06, /* HW size */
        0x04, /* Protocol size */
        0x00, 0x01, /* Request */
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, /* Sender MAC */
        0x0a, 0x0a, 0x0a, 0x01, /* Sender IP - 10.10.10.1*/
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* Target MAC */
        0x0a, 0x0a, 0x0a, 0x02}; /* Target IP - 10.10.10.2*/
        //0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* Padding */
        //0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };


#define VHOST_CLIENT_TEST_MESSAGE        (arp_request)
#define VHOST_CLIENT_TEST_MESSAGE_LEN    (sizeof(arp_request))
int g_send_lock = 0;
//#define MEMB_SIZE(t,m)      (sizeof(((t*)0)->m))
static int receive_sock_server(FdNode* node);
static int accept_sock_server(FdNode* node);
typedef int (*MsgHandler)(VHostUser* vhost_server, VHostUserMsg* msg);
struct app_context  *transport_ctx;
struct app_data data;
static int avail_handler_server(void* context, void* buf, size_t size);
static uintptr_t map_handler(void* context, uint64_t addr);

void dump_mem(uint8_t *address, int length) 
{
    int i = 0; //used to keep track of line lengths
    char *line = (char*)address; //used to print char version of data
    unsigned char ch; // also used to print char version of data
    fprintf(stdout, "\n");
    fprintf(stdout, "%08X | ", (uint8_t*)address); //Print the address we are pulling from
    while (length-- > 0) 
    {
        fprintf(stdout, "%02X ", (unsigned char)*address++); //Print each char
        if (!(++i % 16) || (length == 0 && i % 16)) 
        { //If we come to the end of a line...
            //If this is the last line, print some fillers.
            if (length == 0)
            {
                while (i++ % 16) 
                { 
                    fprintf(stdout, "__ "); 
                }     
            }
            fprintf(stdout, "| ");
            while ((uint8_t*)line < (uint8_t*)address) 
            {
                // Print the character version
                ch = *line++;
                fprintf(stdout, "%c", (ch < 33 || ch == 255) ? 0x2E : ch);
            }
            // If we are not on the last line, prefix the next line with the address.
            if (length > 0) 
            {
                fprintf(stdout, "\n%08X | ", (int)address); 
            }
        }
    }
    puts("");
}

const char* cmd_from_vhostmsg(const VHostUserMsg* msg)
{
    switch (msg->request) {
    case VHOST_USER_NONE:
        return "VHOST_USER_NONE";
    case VHOST_USER_GET_FEATURES:
        return "VHOST_USER_GET_FEATURES";
    case VHOST_USER_SET_FEATURES:
        return "VHOST_USER_SET_FEATURES";
    case VHOST_USER_SET_OWNER:
        return "VHOST_USER_SET_OWNER";
    case VHOST_USER_RESET_OWNER:
        return "VHOST_USER_RESET_OWNER";
    case VHOST_USER_SET_MEM_TABLE:
        return "VHOST_USER_SET_MEM_TABLE";
    case VHOST_USER_SET_LOG_BASE:
        return "VHOST_USER_SET_LOG_BASE";
    case VHOST_USER_SET_LOG_FD:
        return "VHOST_USER_SET_LOG_FD";
    case VHOST_USER_SET_VRING_NUM:
        return "VHOST_USER_SET_VRING_NUM";
    case VHOST_USER_SET_VRING_ADDR:
        return "VHOST_USER_SET_VRING_ADDR";
    case VHOST_USER_SET_VRING_BASE:
        return "VHOST_USER_SET_VRING_BASE";
    case VHOST_USER_GET_VRING_BASE:
        return "VHOST_USER_GET_VRING_BASE";
    case VHOST_USER_SET_VRING_KICK:
        return "VHOST_USER_SET_VRING_KICK";
    case VHOST_USER_SET_VRING_CALL:
        return "VHOST_USER_SET_VRING_CALL";
    case VHOST_USER_SET_VRING_ERR:
        return "VHOST_USER_SET_VRING_ERR";
    //case VHOST_USER_ECHO:
    //    return "VHOST_USER_ECHO";
    case VHOST_USER_MAX:
        return "VHOST_USER_MAX";
    }

    return "UNDEFINED";
}


void dump_buffer(uint8_t* p, size_t len)
{
    int i;
    fprintf(stdout,
            "....");
    for(i=0;i<len;i++) {
        if(i%16 == 0)fprintf(stdout,"\n");
        fprintf(stdout,"%.2x ",p[i]);
    }
    fprintf(stdout,"\n");
}

void dump_vring(struct vring_desc* desc, struct vring_avail* avail,struct vring_used* used)
{
    int idx;

    fprintf(stdout,"desc:\n");
    for(idx=0;idx<VHOST_VRING_SIZE;idx++){
        fprintf(stdout, "%d: 0x% %d 0x%x %d\n",
                idx,
                desc[idx].addr, desc[idx].len,
                desc[idx].flags, desc[idx].next);
    }

    fprintf(stdout,"avail:\n");
    for(idx=0;idx<VHOST_VRING_SIZE;idx++){
       int desc_idx = avail->ring[idx];
       fprintf(stdout, "%d: %d\n",idx, desc_idx);

       //dump_buffer((uint8_t*)desc[desc_idx].addr, desc[desc_idx].len);
    }
}

void dump_vhost_vring(struct vhost_vring* vring)
{
    fprintf(stdout, "kickfd: 0x%x, callfd: 0x%x\n", vring->kickfd, vring->callfd);
    dump_vring(vring->desc, &vring->avail, &vring->used);
}


void dump_vhostmsg(const VHostUserLocalMsg* msg)
{
    int i = 0;
    //fprintf(stdout, "Cmd: %s (0x%x)\n", cmd_from_vhostmsg(&msg->request), msg->request.request);
    //fprintf(stdout, "Flags: 0x%x\n", msg->request.flags);
    // command specific `dumps`
    switch (msg->request.request) 
    {
        case VHOST_USER_GET_FEATURES:
            fprintf(stdout, "VHOST_USER_GET_FEATURES u64: %lx\n", msg->request.u64);
        break;
        case VHOST_USER_SET_FEATURES:
            fprintf(stdout, "VHOST_USER_GET_FEATURES u64: %lx\n", msg->request.u64);
        break;
        case VHOST_USER_SET_OWNER:
            fprintf(stdout, "VHOST_USER_SET_OWNER \n");
        break;
        case VHOST_USER_RESET_OWNER:
            fprintf(stdout, "VHOST_USER_RESET_OWNER \n");
        break;
        case VHOST_USER_SET_MEM_TABLE:
            fprintf(stdout, "VHOST_USER_SET_MEM_TABLE nregions \n");
            //%d sz of req %d\n", msg->request.memory.nregions, sizeof(VHostUserMsg));
#if 0 
            dump_mem((uint8_t *)(&msg->request), sizeof(VHostUserMsg));
            for (i = 0; i < msg->request.memory.nregions; i++) 
            {
                fprintf(stdout, "region: \n\tgpa = %lx\n\t \
                        size = %lx\n\tua = %lx\n",
                        msg->request.memory.regions[i].guest_phys_addr,
                        msg->request.memory.regions[i].memory_size,
                        msg->request.memory.regions[i].userspace_addr);
            }
#endif
        break;
        case VHOST_USER_SET_LOG_BASE:
            fprintf(stdout, "VHOST_USER_SET_LOG_BASE u64: %lx\n", msg->request.u64);
        break;
        case VHOST_USER_SET_LOG_FD:
            fprintf(stderr, "VHOS_USER_SET_LOG_FD \n");
        break;
        case VHOST_USER_SET_VRING_NUM:
            fprintf(stdout, "VHOST_USER_SET_VRING_NUM state: %d %d\n", msg->request.state.index, msg->request.state.num);
        break;
        case VHOST_USER_SET_VRING_ADDR:
            fprintf(stdout, "VHOST_USER_SET_VRING_ADDR addr. Init Done.....:\n\tidx = %d\n\tflags = 0x%lx\n \
                \tdua = %lx\n \
                \tuua = %lx\n \
                \taua = %lx\n \
                \tlga = %x\n", msg->request.addr.index, msg->request.addr.flags,
                msg->request.addr.desc_user_addr, msg->request.addr.used_user_addr,
                msg->request.addr.avail_user_addr, msg->request.addr.log_guest_addr);
        break;
        case VHOST_USER_SET_VRING_BASE:
            fprintf(stdout, "VHOST_USER_SET_VRING_BASE state: %d %d\n", msg->request.state.index, msg->request.state.num);
        break;
        case VHOST_USER_GET_VRING_BASE:
            fprintf(stdout, "VHOST_USER_GET_VRING_BASE state: %d %d\n", msg->request.state.index, msg->request.state.num);
        break;
        case VHOST_USER_SET_VRING_KICK:
        case VHOST_USER_SET_VRING_CALL:
        case VHOST_USER_SET_VRING_ERR:
            //fprintf(stdout, "//KICK_CALL_ERR %lx\n", msg->request.u64);
        break;
        case VHOST_USER_NONE:
        case VHOST_USER_MAX:
            fprintf(stdout, "VHOST_USER_MAX_NONE, %d\n",msg->request.request);
            break;
        case VHOST_USER_ECHO:
            //fprintf(stdout, "VHOST_USER_ECHO \n");
            break;
       default:
            fprintf(stdout, "DEFAULT \n");
    }
    //fprintf(stdout, ".........................................\n");
}

int shm_fds[VHOST_USER_MEMORY_MAX_NREGIONS];

int kick(VringTable* vring_table, uint32_t v_idx)
{
    uint64_t kick_it = 1;
    int kickfd = vring_table->vring[v_idx].kickfd;

    write(kickfd, &kick_it, sizeof(kick_it));
    fsync(kickfd);

    return 0;
}

void* init_shm_from_fd(int fd, size_t size) {
    void *result = mmap(0, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (result == MAP_FAILED) {
        perror("mmap");
        result = 0;
    }
    return result;
}

int sync_shm(void* ptr, size_t size)
{
    return msync(ptr, size, MS_SYNC | MS_INVALIDATE);
}

int init_fd_list(FdList *fd_list, uint32_t ms)
{
    int idx;
    fd_list->nfds = 0;
    fd_list->fdmax = -1;

    for (idx = 0; idx < FD_LIST_SIZE; idx++)
    {
        fd_list->read_fds[idx].fd = -1;
        fd_list->read_fds[idx].context = 0;
        fd_list->read_fds[idx].handler = 0;

        fd_list->write_fds[idx].fd = -1;
        fd_list->write_fds[idx].context = 0;
        fd_list->write_fds[idx].handler = 0;

    }
    fd_list->ms = 1;
}

FdNode * find_fd_node_by_fd(FdNode *fds, int fd)
{
    int idx;
    for (idx = 0; idx < FD_LIST_SIZE; idx++)
    {
        if (fds[idx].fd == fd)
            return &(fds[idx]);
    }
    return 0;
}

int add_fd_list(FdList *fd_list, FdType type, int fd, void *context,
                FdHandler handler)
{
    FdNode *fds = (type == FD_READ) ? fd_list->read_fds : fd_list->write_fds;
    /* find first node which is empty */
    FdNode *fd_node = find_fd_node_by_fd(fds, -1);

    if(!fd_node)
    {
        fprintf(stderr, "No empty node in the fd list \n");
        return -1;        
    }
    fd_node->fd = fd;
    fd_node->context = context;
    fd_node->handler = handler;
}

int del_fd_list(FdList* fd_list, FdType type, int fd)
{
    FdNode* fds = (type == FD_READ) ? fd_list->read_fds : fd_list->write_fds;
    FdNode* fd_node = find_fd_node_by_fd(fds, fd);

    if (!fd_node) {
        fprintf(stderr, "Fd (%d) not found fd list\n", fd);
        return -1;
    }

    fd_node->fd = -1;
    fd_node->context = 0;
    fd_node->handler = 0;

    return 0;
}

int vhost_user_receive(int sock, struct VHostUserMsg *msg, int *fds, int *nfds)
{
    struct msghdr msgh;
    struct iovec iov[1];
    int ret;

    int fd_size = sizeof(int) * VHOST_USER_MEMORY_MAX_NREGIONS;
    char control[CMSG_SPACE(fd_size)];
    struct cmsghdr *cmsg;

    memset(&msgh, 0, sizeof(msgh));
    memset(control, 0 , sizeof(control));
    *nfds = 0;

    iov[0].iov_base = (void *)msg;
    iov[0].iov_len = sizeof(struct VHostUserMsg);

    msgh.msg_iov = iov;
    msgh.msg_iovlen = 1;
    msgh.msg_control = control;
    msgh.msg_controllen = sizeof(control);

    ret = recvmsg(sock, &msgh, 0);
    //fprintf(stdout, "size of msg received = %d\n", sizeof(VHostUserMsg));
    //dump_mem((uint8_t *)msg, sizeof(VHostUserMsg));
    if (ret > 0)
    {
        if (msgh.msg_flags & (MSG_TRUNC | MSG_CTRUNC))  
            ret = -1;
        else
        {
            // Copy file descriptors
            cmsg = CMSG_FIRSTHDR(&msgh);
            if (cmsg && cmsg->cmsg_len > 0 &&
                cmsg->cmsg_level == SOL_SOCKET &&
                cmsg->cmsg_type == SCM_RIGHTS) 
            {
                if (fd_size >= cmsg->cmsg_len - CMSG_LEN(0)) 
                {
                    fd_size = cmsg->cmsg_len - CMSG_LEN(0);
                    memcpy(fds, CMSG_DATA(cmsg), fd_size);
                    *nfds = fd_size / sizeof(int);
                }
            }
        }
    }
    if (ret < 0 && errno != EAGAIN)
    {
        perror("recvmsg failed \n");
    }
    return ret;
}

static int receive_sock_server(FdNode *node)
{
    VHostUser *vhost_user = (VHostUser *)node->context;
    int sock  = node->fd;
    VHostUserLocalMsg msg;
    int r;
    //fprintf(stderr, "receive_sock_server \n");
    //msg.fd_num = sizeof(msg.fds)/sizeof(int);
    msg.fd_num = sizeof(msg.fds)/sizeof(int);

    r = vhost_user_receive(sock, &msg.request, msg.fds, &msg.fd_num);
    if (r < 0)
        perror("receice_sock_server error \n");
    else if (r == 0)
    {
        del_fd_list(&vhost_user->fd_list, FD_READ, sock);
        close(sock);
    }
    dump_vhostmsg(&msg);
    r = 0;

    //handle the packet to the registerd server backend.
    if (vhost_user->handlers.in_handler)
    {
        void * ctx = vhost_user->handlers.context;
        r = vhost_user->handlers.in_handler(ctx, &msg);
        if (r < 0)
        {
            fprintf(stdout, "Error Processing message %s\n",
                    cmd_from_vhostmsg(&msg.request));

        }
    }
    else
        fprintf(stdout, "No handler registerd \n");
}

static int accept_sock_server(FdNode *node)
{
    int sock;
    struct sockaddr_un un;
    socklen_t len = sizeof(un);
    VHostUser *vhost_user = (VHostUser *)node->context;
    //accept connection
    if ((sock = accept(vhost_user->sock, (struct sockaddr*)&un, &len)) == -1)
    {
        perror("error in accept ");
    }

    add_fd_list(&vhost_user->fd_list, FD_READ, sock, (void *)vhost_user, 
                receive_sock_server);
   return 1;
}

int vhost_user_open_socket(VHostUser * vhost_server)
{
    int sock;
    struct sockaddr_un un;

    unlink(vhost_server->path);
    if ((vhost_server->sock = socket(AF_UNIX, SOCK_STREAM, 0)) == -1)
    {
        perror("socket creation failed \n");
        return -1;
    }
    
    un.sun_family = AF_UNIX;
    strncpy(un.sun_path, vhost_server->path, sizeof(un.sun_path));
    if (bind(vhost_server->sock, (struct sockaddr *)&un,
             sizeof(struct sockaddr_un)) == -1)
    {
        perror("bind error \n");
        return -1;
    }
    if (listen(vhost_server->sock, 1) == -1)
    {
        perror("listen sock error \n");
        return -1;
    }
    init_fd_list(&vhost_server->fd_list, FD_LIST_SELECT_5);
    add_fd_list(&vhost_server->fd_list, FD_READ, vhost_server->sock, 
                (void *) vhost_server, accept_sock_server);
    assert(fcntl(sock, F_SETFL, O_NONBLOCK) == 0);
    return sock;
}


int vhost_user_accept(int sock)
{
    int newsock;

    if((newsock = accept(sock, NULL, NULL)) ==  1)
        assert(errno == EAGAIN);
    else
        assert(fcntl(newsock, F_SETFL, O_NONBLOCK) == 0);
    return newsock;
}


void *vhost_user_map_guest_memory(int fd, int size)
{
    void *ptr = mmap(0, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    return ptr == MAP_FAILED ? 0 : ptr;
}

int vhost_user_sync_shm(void *ptr, size_t size)
{
    return msync(ptr, size, MS_SYNC | MS_INVALIDATE);
}

int run_vhost_user(VHostUser *vhost_user)
{
    
}

int process_fd_set(FdList *fd_list, FdType type, fd_set *fdset)
{
    int idx;
    FdNode *fds = (type == FD_READ) ? fd_list->read_fds :  fd_list->write_fds;

    for (idx = 0; idx < FD_LIST_SIZE; idx++)
    {
        FdNode * node = &(fds[idx]);
        if (FD_ISSET(node->fd, fdset))
        {
            if (node->handler)
                node->handler(node);
        }
    }
    return 0;
}

static int fd_set_from_fd_list(FdList* fd_list, FdType type, fd_set* fdset)
{
    int idx;
    FdNode* fds = (type == FD_READ) ? fd_list->read_fds : fd_list->write_fds;

    FD_ZERO(fdset);

    for (idx = 0; idx < FD_LIST_SIZE; idx++) {
        int fd = fds[idx].fd;
        if (fd != -1) {
            FD_SET(fd,fdset);
            fd_list->fdmax = MAX(fd, fd_list->fdmax);
        }
    }

    return 0;
}

int traverse_fd_list(FdList * fd_list)
{
    fd_set read_fdset, write_fdset;
    struct timeval tv = { .tv_sec = fd_list->ms/1000,
                          .tv_usec = (fd_list->ms%1000)*1000 };
    int r;
    fd_list->fdmax = -1;

    fd_set_from_fd_list(fd_list, FD_READ, &read_fdset);
    fd_set_from_fd_list(fd_list, FD_WRITE, &write_fdset);
    r = select(fd_list->fdmax + 1, &read_fdset, &write_fdset, 0, &tv);
    if (r == -1)
    {
        perror("select call \n");
    }
    else if (r == 0)
    {
        //no ready fds, timout
    }
    else
    {
        int rr = process_fd_set(fd_list, FD_READ, &read_fdset);
        int wr = process_fd_set(fd_list, FD_WRITE, &write_fdset);
        //amakkar if (r != rr + wr)
            //printf("error in select. read + writes not eq. total \n");
    }
      
}

static uintptr_t _map_user_addr(VHostUser * vhost_user, uint64_t addr)
{
    uintptr_t result = 0;
    int idx;

    //fprintf(stdout, "_map_user_addr nregions= %d \n", vhost_user->memory.nregions);

    for (idx = 0; idx < vhost_user->memory.nregions; idx++)
    {
        VHostUserLocalMemoryRegion * region = &vhost_user->memory.regions[idx];
#if 0
        fprintf(stdout, "map_user_addr adddr:\n\tidx = %d\n\t \
                \tregion = %lx\n \
                \tdua = %lx\n \
                \tmem_size = %lx\n \
                \tmmap_addr = %lx\n \
                \taddr = %lx\n", idx, region, region->userspace_addr, region->memory_size, 
                                 region->mmap_addr, addr);
#endif
        if (region->userspace_addr <= addr
            && addr < (region->userspace_addr + region->memory_size))
        {
            result = region->mmap_addr + addr - region->userspace_addr;    
            break;
        }    
    }
    return result;
}

static uintptr_t _map_guest_addr(VHostUser * vhost_user, uint64_t addr)
{
    uintptr_t result = 0;
    int idx;
    fprintf(stderr, "map guest addr \n");

    for (idx = 0; idx < vhost_user->memory.nregions; idx++)
    {
        VHostUserLocalMemoryRegion *region = &vhost_user->memory.regions[idx];
        
        if (region->guest_phys_addr <= addr 
            && addr < (region->guest_phys_addr + region->memory_size))
            {
                result = region->mmap_addr + addr - region->guest_phys_addr;
                break;
            }
    }
    return result;
}

static int _get_features(VHostUser *vhost_user, VHostUserLocalMsg *msg)
{
    fprintf(stdout, "_get_features \n");
    return 1;
}

static int _set_features(VHostUser *vhost_user, VHostUserLocalMsg *msg)
{
    fprintf(stdout, "_set_features \n");
    return 0;
}

static int _set_owner(VHostUser *vhost_user, VHostUserLocalMsg *msg)
{
    fprintf(stdout, "_set_owner \n");
    return 0;
}

static int _reset_owner(VHostUser *vhost_user, VHostUserLocalMsg *msg)
{
    fprintf(stdout, "_reset_owner \n");
    return 0;
}

static int _set_mem_table(VHostUser *vhost_user, VHostUserLocalMsg *msg)
{
    int idx;
    fprintf(stdout, "_set_mem_table . nregions = %d\n", msg->request.memory.nregions);

    vhost_user->memory.nregions = 0;
    for (idx = 0; idx < msg->request.memory.nregions; idx++)
    {
        fprintf(stdout, "n regions = %d\n", msg->request.memory.nregions);
        if (msg->fds[idx] > 0)
        {
            VHostUserLocalMemoryRegion *region = &vhost_user->memory.regions[idx];

            region->guest_phys_addr = msg->request.memory.regions[idx].guest_phys_addr;
            region->memory_size = msg->request.memory.regions[idx].memory_size;
            region->userspace_addr = msg->request.memory.regions[idx].userspace_addr;
            fprintf(stdout, "idx = %d and msg->fd_num = %d\n", idx, msg->fd_num);
            assert(idx < msg->fd_num);
            assert(msg->fds[idx] > 0);

            region->mmap_addr = (uintptr_t) init_shm_from_fd(msg->fds[idx], 
                                region->memory_size);
            vhost_user->memory.nregions++;
        }
    }
    return 0;
}
static int _set_log_base(VHostUser *vhost_user, VHostUserLocalMsg *msg)
{
    fprintf(stdout, "_set_log_base \n");
    return 0;
}

static int _set_log_fd(VHostUser *vhost_user, VHostUserLocalMsg *msg)
{
    fprintf(stdout, "set_log_fd \n");
    return 0;
}

static int _set_vring_num(VHostUser *vhost_user, VHostUserLocalMsg *msg)
{
    //fprintf(stdout, "_set_vring_num \n");
    int idx = msg->request.state.index;
    assert(idx < VHOST_CLIENT_VRING_NUM);
    vhost_user->vring_table.vring[idx].num = msg->request.state.num;
    return 0;
}

static int _set_vring_addr(VHostUser *vhost_user, VHostUserLocalMsg *msg)
{
    int idx = msg->request.addr.index;
    fprintf(stdout, "_set_vring_Addr idx=......%d \n", idx);
    assert(idx < VHOST_CLIENT_VRING_NUM);

    vhost_user->vring_table.vring[idx].desc = 
            (struct vring_desc *) _map_user_addr(vhost_user, 
                                                 msg->request.addr.desc_user_addr); 
    vhost_user->vring_table.vring[idx].avail = 
            (struct vring_avail *) _map_user_addr(vhost_user,
                                                  msg->request.addr.avail_user_addr);
    vhost_user->vring_table.vring[idx].used = 
            (struct vring_used *) _map_user_addr(vhost_user, 
                                                 msg->request.addr.used_user_addr);

    vhost_user->vring_table.vring[idx].last_used_idx = 
            vhost_user->vring_table.vring[idx].used->idx;
    fprintf(stdout, "return _set_vring_Addr \n");

    /* initialization done. Set the global send lock to 1 */
    g_send_lock = 1;
    return 0;
}

static int _set_vring_base(VHostUser *vhost_user, VHostUserLocalMsg *msg)
{
    fprintf(stdout, "_set_vring_base \n");

    int idx = msg->request.state.index;
    assert(idx<VHOST_CLIENT_VRING_NUM);

    vhost_user->vring_table.vring[idx].last_avail_idx = msg->request.state.num;

    return 0;
}


static int _get_vring_base(VHostUser *vhost_user, VHostUserLocalMsg *msg)
{
    fprintf(stdout, "_get_vring_base \n");

    int idx = msg->request.state.index;

    assert(idx<VHOST_CLIENT_VRING_NUM);
    msg->request.state.num = vhost_user->vring_table.vring[idx].last_avail_idx;
    msg->request.size = MEMB_SIZE(VHostUserMsg, state);
    return 1; //should reply back.
}

static int _poll_avail_vring(VHostUser * vhost_user, int idx)
{
    uint32_t count = 0;

    //if vring is already set, process the vring.
    if (vhost_user->vring_table.vring[idx].desc)
    {
        count = process_avail_vring(&vhost_user->vring_table, idx);
    }
    return count;
}



static int _kick_server(FdNode * node)
{
    VHostUser * vhost_user = (VHostUser*) node->context;
    int kickfd = node->fd;
    ssize_t r;
    uint64_t kick_it = 0;
    fprintf(stdout, "_kick_server \n");
    r = read(kickfd, &kick_it, sizeof(kick_it));

    if (r < 0)
        perror("recv kick \n");
    else if (r == 0)
        del_fd_list(&vhost_user->fd_list, FD_READ, kickfd);
    else
        _poll_avail_vring(vhost_user, VHOST_CLIENT_VRING_IDX_TX);
    return 0;
}

static int _set_vring_kick(VHostUser* vhost_server, VHostUserLocalMsg* msg)
{
    int idx = msg->request.u64 & VHOST_USER_VRING_IDX_MASK;
    int validfd = (msg->request.u64 & VHOST_USER_VRING_NOFD_MASK) == 0;

    assert(idx<VHOST_CLIENT_VRING_NUM);
    if (validfd) {
        assert(msg->fd_num == 1);

        vhost_server->vring_table.vring[idx].kickfd = msg->fds[0];


        if (idx == VHOST_CLIENT_VRING_IDX_TX) {
            add_fd_list(&vhost_server->fd_list, FD_READ,
                    vhost_server->vring_table.vring[idx].kickfd,
                    (void*) vhost_server, _kick_server);
            fprintf(stdout, "Listening for kicks on 0x%x\n", 
                    vhost_server->vring_table.vring[idx].kickfd);
        }
        vhost_server->is_polling = 0;
    } else {
        fprintf(stdout, "Got empty kickfd. Start polling.\n");
        vhost_server->is_polling = 1;
    }

    return 0;
}

static int _set_vring_call(VHostUser* vhost_server, VHostUserLocalMsg* msg)
{
    fprintf(stdout, "%s\n", __FUNCTION__);

    int idx = msg->request.u64 & VHOST_USER_VRING_IDX_MASK;
    int validfd = (msg->request.u64 & VHOST_USER_VRING_NOFD_MASK) == 0;

    //amakkar don't do assert(idx<VHOST_CLIENT_VRING_NUM);
    if (validfd) 
    {
        // amakkar don't do. assert(msg->fd_num == 1);
        vhost_server->vring_table.vring[idx].callfd = msg->fds[0];
        fprintf(stdout, "Got callfd 0x%x\n", vhost_server->vring_table.vring[idx].callfd);
    }
    return 0;
}

static int _set_user_echo(VHostUser* vhost_server, VHostUserLocalMsg* msg)
{
    return 0;
}

static int _set_vring_err(VHostUser* vhost_server, VHostUserLocalMsg* msg)
{
    fprintf(stdout, "%s\n", __FUNCTION__);
    return 0;
}

static MsgHandler msg_handlers[VHOST_USER_MAX] = {
        0,                  // VHOST_USER_NONE
        _get_features,      // VHOST_USER_GET_FEATURES
        _set_features,      // VHOST_USER_SET_FEATURES
        _set_owner,         // VHOST_USER_SET_OWNER
        _reset_owner,       // VHOST_USER_RESET_OWNER
        _set_mem_table,     // VHOST_USER_SET_MEM_TABLE
        _set_log_base,      // VHOST_USER_SET_LOG_BASE
        _set_log_fd,        // VHOST_USER_SET_LOG_FD
        _set_vring_num,     // VHOST_USER_SET_VRING_NUM
        _set_vring_addr,    // VHOST_USER_SET_VRING_ADDR
        _set_vring_base,    // VHOST_USER_SET_VRING_BASE
        _get_vring_base,    // VHOST_USER_GET_VRING_BASE
        _set_vring_kick,    // VHOST_USER_SET_VRING_KICK
        _set_vring_call,    // VHOST_USER_SET_VRING_CALL
        _set_vring_err,     // VHOST_USER_SET_VRING_ERR
        _set_user_echo,
        };

static uintptr_t map_handler(void *context, uint64_t addr)
{
    VHostUser * vhost_user = (VHostUser *)context;
    fprintf(stdout, "map_handler \n");
    return _map_guest_addr(vhost_user, addr);
}

static int avail_handler_server(void *context, void *buf, size_t size)
{
    VHostUser * vhost_user = (VHostUser *)context;
    fprintf(stderr, "avail_hander \n");
    memcpy(vhost_user->buffer, buf, size);
    vhost_user->buffer_size = size;
    dump_buffer(buf, size);
    /* important point amakkar. Call the RDMA module to the RDMA 
     *transfer to remote backend.
     */
    fprintf(stdout, "Calling RDMA transfer \n");
    //rdma_transfer(buf, size);
}

static int process_desc(VringTable * vring_table, uint32_t v_idx, uint32_t a_idx)
{
    struct vring_desc *desc = vring_table->vring[v_idx].desc;
    struct vring_avail *avail = vring_table->vring[v_idx].avail;
    struct vring_used *used = vring_table->vring[v_idx].used;
    unsigned int num = vring_table->vring[v_idx].num;

    ProcessHandler * handler = &vring_table->handler;
    uint16_t u_idx = vring_table->vring[v_idx].last_used_idx % num;
    uint16_t d_idx = avail->ring[a_idx];
    uint32_t i, len = 0;
    size_t buf_size = ETH_PACKET_SIZE;
    uint8_t buf[buf_size];
    struct virtio_net_hdr *hdr = 0;
    size_t hdr_len = sizeof(struct virtio_net_hdr);

    i = d_idx;

    for(;;)
    {
        void * cur = 0;
        uint32_t cur_len = desc[i].len;

        //map the address
        if (handler && handler->map_handler)
        {
            cur = (void *)handler->map_handler(handler->context, desc[i].addr);
        }
        else
        {
            cur = (void *) (uintptr_t) desc[i].addr;
        }
        if (len + cur_len < buf_size)
            memcpy(buf + len, cur, cur_len);
        else 
            break;
        len += cur_len;
        
        if(desc[i].flags & VIRTIO_DESC_F_NEXT)
            i = desc[i].next;
        else
            break;
    }
    if (!len)
    {
        fprintf(stderr, "packet of 0 length. Return \n");
        return -1;
    }

    //add it to used ring.
    used->ring[u_idx].id = d_idx;
    used->ring[u_idx].len = len;
    
    //check the header
    hdr = (struct virtio_net_hdr *)buf;

    if ((hdr->flags !=0) || (hdr->gso_type != 0) || (hdr->hdr_len != 0)
         || (hdr->gso_size != 0) || (hdr->csum_start != 0)
         || (hdr->csum_offset != 0))
    {
        fprintf(stderr, "wrong flags \n");
    }

    //consume the packet
    if (handler && handler->avail_handler)
    {
        if (handler->avail_handler(handler->context, buf + hdr_len, len - hdr_len) != 0)
        {
            fprintf(stderr, "error in avail_handler for processing current packet \n");
            //TODO, drop the packet.
        }
        
    }
    else
        fprintf(stderr, "avail_handler handler not avaialable \n");
            
    return 0;
}


static int process_avail_vring(VringTable *vring_table, uint32_t v_idx)
{
    struct vring_avail *avail = vring_table->vring[v_idx].avail;
    struct vring_used *used = vring_table->vring[v_idx].used;
    uint32_t num = vring_table->vring[v_idx].num;

    uint32_t count = 0;
    uint16_t a_idx = vring_table->vring[v_idx].last_avail_idx % num;

    //loop all available descriptors
    for(;;)
    {
        //we reached the end of the avail
        if (vring_table->vring[v_idx].last_avail_idx == avail->idx)
            break;
        process_desc(vring_table, v_idx, a_idx);

        a_idx = (a_idx + 1) % num;

        vring_table->vring[v_idx].last_avail_idx++;
        vring_table->vring[v_idx].last_used_idx++;
        count++;
    }
    used->idx = vring_table->vring[v_idx].last_used_idx;
    return count;
}

static int in_msg_server(void* context, VHostUserLocalMsg* msg)
{
    VHostUser* vhost_server = (VHostUser*) context;
    int result = 0;
    //fprintf(stdout, "in_msg_server req = %d\n", msg->request.request);
    assert(msg->request.request > VHOST_USER_NONE && msg->request.request < VHOST_USER_MAX);

    if (msg_handlers[msg->request.request]) {
    fprintf(stdout, " Handling : %s\n msg.", cmd_from_vhostmsg(&msg->request));
        result = msg_handlers[msg->request.request](vhost_server, msg);
    }
    return result;
}

static void handle_rdma_transport_receive(PThreadArgs *pthread_args)
{
    /* transport specific initializations */
    //init_transport_context();
    struct app_context 		*transport_ctx = NULL;
	char                    *ib_devname = NULL;
	int                   	iters = 1000;
	int                  	scnt, ccnt;
	int                   	duplex = 0;
	struct ibv_qp			*qp;
    int cnt = 0;
    uint8_t *buffer;
    buffer = malloc(60);
#if 0
	struct app_data	 	 data = {
		.port	    		= 18515,
		.ib_port    		= 1,
		.size       		= 65536,
		.tx_depth   		= 100,
		.servername 		= NULL,
		.remote_connection 	= NULL,
		.ib_dev     		= NULL
		
	};
#endif
    /*server name to which we are going to connect */
    fprintf(stdout, "server name = %s\n", pthread_args->server_name);
    data.port = 18515;
    data.ib_port = 1;
    data.size = 65536;
    data.tx_depth = 100;
    data.servername = pthread_args->server_name;
    data.remote_connection = NULL;
    data.ib_dev = NULL;
    
    pid = getpid();
    srand48(pid * time(NULL));

    TEST_Z(transport_ctx = init_context(&data), "Could not create context. init_ctx\n");
    set_local_ib_connection(transport_ctx, &data);

    /* Now create new thread for transport . It will open a separate port
     * for communicatin with other end. Present main thread is already
     * listening for kicks from the QEMU. New thread will communicate to 
     * remote app for rdma transfers
     */
    TEST_N(data.sockfd = tcp_client_connect(&data),
           "TCP server listen (TCP) failed");
    fprintf(stdout, "returnded sockfd = %d\n", data.sockfd);
  
    TEST_NZ(tcp_exch_ib_connection_info(&data),
            "could not exchange connection, tcp_exch_ib_connection");

    /* change the state of qp to ready_to_send */ 
    qp_change_state_rtr(transport_ctx->qp, &data);
    
    //uint8_t  *chPtr = (char *)data.local_connection.vaddr; 	
    //uin8
    /* amakkar: wait for guest to come up and main thread to initialize the vrings
     * worse hack. Just for POC. Remove it.
     */
    memcpy(buffer, arp_request, 42); 
    sleep(180);
    /* print the buffer received using rdma transfer */
    while(1)
    {
        /* amakkar: assume main vhost_user has been initialized in main thread, before 
         * its being used in the call below. Use memory barrier to ensure that. Might 
         * cause unexpected results.
         */ 
        /* amakkar, declare chPtr as volatile, as it referes to vaddr and the
         * content at this address may be modified by the trasnport thread.
         * we want below condition to be evaluated everytime. Also, use some form of 
         * lock here while reading.. We need to sync so as to ensure we are reading 
         * the correct packet and the content is not overwritten while we re reading.
         */
        sleep(1);
        if (g_send_lock)
        {
            /* amakkar: to check the receive path only */
            rdma_read_send(pthread_args, buffer, 
                            VHOST_CLIENT_TEST_MESSAGE_LEN);
        }
        else
            fprintf(stdout, "locked \n");
        #if 0
        if(*(chPtr + 10) != 0)
        {
            dump_buffer((uint8_t *)chPtr, 15);
            fprintf(stdout, "%d\n", *(chPtr + 10));
            rdma_read_send(pthread_args->vhost_user,chPtr, 42);
            *(chPtr + 10) = 0;
        }
        #endif
		//if(strlen(chPtr) > 0){
		//break;
		//}
    }

}

static void handle_rdma_transport()
{
    /* transport specific initializations */
    //init_transport_context();
    struct app_context 		*transport_ctx = NULL;
	char                    *ib_devname = NULL;
	int                   	iters = 1000;
	int                  	scnt, ccnt;
	int                   	duplex = 0;
	struct ibv_qp			*qp;
    int cnt = 0;
#if 0
	struct app_data	 	 data = {
		.port	    		= 18515,
		.ib_port    		= 1,
		.size       		= 65536,
		.tx_depth   		= 100,
		.servername 		= NULL,
		.remote_connection 	= NULL,
		.ib_dev     		= NULL
		
	};
#endif
    data.port = 18515;
    data.ib_port = 1;
    data.size = 65536;
    data.tx_depth = 100;
    data.servername = NULL;
    data.remote_connection = NULL;
    data.ib_dev = NULL;
    
    pid = getpid();
    srand48(pid * time(NULL));

    TEST_Z(transport_ctx = init_context(&data), "Could not create context. init_ctx\n");
    set_local_ib_connection(transport_ctx, &data);

    /* Now create new thread for transport . It will open a separate port
     * for communicatin with other end. Present main thread is already
     * listening for kicks from the QEMU. New thread will communicate to 
     * remote app for rdma transfers
     */
    TEST_N(data.sockfd = tcp_server_listen(&data),
           "TCP server listen (TCP) failed");
    fprintf(stdout, "returnded sockfd = %d\n", data.sockfd);
  
    TEST_NZ(tcp_exch_ib_connection_info(&data),
            "could not exchange connection, tcp_exch_ib_connection");

    /* change the state of qp to ready_to_send */ 
    qp_change_state_rts(transport_ctx->qp, &data);
}

int put_vring(VringTable *vring_table, uint32_t v_idx, void * buf, size_t size)
{
    fprintf(stdout, "put_vring\n"); 
    struct vring_desc *desc = vring_table->vring[v_idx].desc;
    struct vring_avail * avail = vring_table->vring[v_idx].avail;
    unsigned int num = vring_table->vring[v_idx].num;
    ProcessHandler * handler = &vring_table->handler;
    fprintf(stdout, "put_vrin 1\n"); 

    uint16_t a_idx = vring_table->vring[v_idx].last_avail_idx;
    void *dest_buf = 0;
    struct virtio_net_hdr *hdr = 0;
    size_t hdr_len = sizeof(struct virtio_net_hdr);
    fprintf(stdout, "put_vrin 2, v_idx=%d\n", v_idx); 
    fprintf(stdout, "a_idx = %d, desc=%lx\n", a_idx,vring_table->vring[v_idx].desc);
    #if 0
    if(size > desc[a_idx].len)
    {
        fprintf(stdout, "put_vring wrong size size=%d, desc[a_idx].len=%d, a_idx=%d\n", size, desc[a_idx].len, a_idx);
        return -1;
    }
    #endif
    fprintf(stdout, "put_vring, move_available_head \n"); 
    /* move available head */
    vring_table->vring[v_idx].last_avail_idx = desc[a_idx].next;
    
    handler->map_handler = map_handler;
    /* map the address */
    if (handler && handler->map_handler)
    {
        /* maps the guest address corresponding to the available ring. Avail
         * ring is maintained in the guest.
         */
        dest_buf = (void *)handler->map_handler(handler->context, desc[a_idx].addr);
    }
    else
    {
        dest_buf = (void *) (uintptr_t) desc[a_idx].addr;
    }
    
    fprintf(stdout, "put_vring, initialize the hdr fields to all 0s \n"); 
    /* intialize the hdr fields to all 0s */
    hdr = dest_buf;
    hdr->flags = 0;
    hdr->gso_type = 0;
    hdr->hdr_len = 0;
    hdr->gso_size = 0;
    hdr->csum_start = 0;
    hdr->csum_offset = 0;

    /* at present support of only single buffer per packet */
    fprintf(stdout, "present support of only single buffer per packet.Ready to sync.\n");
    memcpy(dest_buf + hdr_len, buf, size);
    dump_buffer(dest_buf, size);
    desc[a_idx].len = hdr_len + size;
    desc[a_idx].flags = 0;
    desc[a_idx].next = VRING_IDX_NONE;

    /* add to available.*/
    avail->ring[avail->idx % num] = a_idx;
    avail->idx++;
    fprintf(stdout, "put_vring, synching memory \n");
    sync_shm(dest_buf, size);
    sync_shm((void *)&(avail), sizeof(struct vring_avail));
    
    fprintf(stdout, "put_vring, return 0 \n");
    return 0;
}

static int vhost_user_poll_handler(void *context)
{
    VHostUser *vhost_user = (VHostUser *)context;
    int tx_idx = VHOST_CLIENT_VRING_IDX_TX;
    int rx_idx = VHOST_CLIENT_VRING_IDX_RX;
    //amakkar if(vhost_user->vring_table.vring[rx_idx].desc)
    if(vhost_user->vring_table.vring[rx_idx].desc)
    {
        /* handle TX ring. TX ring from clients (QEMUs) perspective i.e client
         * wants to transfer and server(this vhost_user app) is receiving.
         */
        _poll_avail_vring(vhost_user, tx_idx);
        if(vhost_user->buffer_size)
        {
            fprintf(stdout, "also adding it to recv q \n");
            put_vring(&vhost_user->vring_table, rx_idx, vhost_user->buffer, 
                      vhost_user->buffer_size);
            /* signle to client ie QEMU that data is available */
            kick(&vhost_user->vring_table, rx_idx);

            /*mark the buffer empty */
            vhost_user->buffer_size = 0;
        } 
    }
    else
    {
        /* processing RX ring. RX ring from the clients(QEMUs) perspective i.e
         * this app (vhost_user) app has some data to deliver to the client via 
         * clients RX Qs
         */
        /* for every packet transferred by the client, this server is sending 
         * it back to the client. vhost_Server->buffer_size > 0 
         * whenever any packet is sent from client to this server. As a 
         * result for every packet received this condition becomes true
         * and below code is exectued and packet is placed in the receive
         * Q of the client and client is kicked 
         */
        if(vhost_user->buffer_size)
        {
            put_vring(&vhost_user->vring_table, rx_idx, vhost_user->buffer, 
                      vhost_user->buffer_size);
            /* signle to client ie QEMU that data is available */
            kick(&vhost_user->vring_table, rx_idx);

            /*mark the buffer empty */
            vhost_user->buffer_size = 0;
        }   
    }
    return 0;
}

static AppHandlers vhost_user_handlers = 
{
    .context = 0,
    .in_handler = in_msg_server,
    .poll_handler = vhost_user_poll_handler
};


int loop_server(VHostUser *vhost_user)
{
   traverse_fd_list(&vhost_user->fd_list); 
   if (vhost_user->handlers.poll_handler)
       vhost_user->handlers.poll_handler(vhost_user->handlers.context);

   return 0;
}

/* globals related to trasport */
//pid_t pid;
//int sl = 1;
//int page_size;
/*******************************/

void main(int argc, char *argv[])
{
    int idx = 0;
    PThreadArgs * pthread_args = malloc(sizeof(PThreadArgs));
    pthread_t transport_thread_client_receive;
    pthread_t transport_thread_server_send;
    fprintf(stderr, "vhost_user app started \n");
    struct VHostUser *vhost_server = (VHostUser *)malloc(sizeof(struct VHostUser));
    vhost_user_handlers.context = vhost_server;
    vhost_server->vring_table.handler.context = (void *) vhost_server;
    vhost_server->vring_table.handler.avail_handler = avail_handler_server;
    vhost_server->vring_table.handler.map_handler = map_handler;
    for (idx = 0; idx < VHOST_CLIENT_VRING_NUM; idx++)
    {
        vhost_server->vring_table.vring[idx].kickfd = -1;
        vhost_server->vring_table.vring[idx].callfd = -1;
        vhost_server->vring_table.vring[idx].desc = 0;
        vhost_server->vring_table.vring[idx].avail = 0;
        vhost_server->vring_table.vring[idx].used = 0;
        vhost_server->vring_table.vring[idx].num = 0;
        vhost_server->vring_table.vring[idx].last_avail_idx = 0;
        vhost_server->vring_table.vring[idx].last_used_idx = 0;
    }
    vhost_server->memory.nregions = 0;
    vhost_server->buffer_size = 0;
    vhost_server->is_polling = 0;

    memcpy(&vhost_server->handlers, &vhost_user_handlers, sizeof(AppHandlers));
    strncpy(vhost_server->path, "/home/amakkar/qemu.sock", 256);
    vhost_user_open_socket(vhost_server); 
    int app_running = 1;
    if (argc == 2)
    {
        /* launch the client version for receiving data */
        strncpy(pthread_args->server_name, argv[1], strlen(argv[1]));
        pthread_args->vhost_user = vhost_server;
        fprintf(stdout, "Launching the client version (a new thread )for receiving data using RDMA\n");
        pthread_create(&transport_thread_client_receive, NULL, (void *)handle_rdma_transport_receive, pthread_args);
    }
    else
    {
        /* launch the server version for sending the data received from the VM. */
        fprintf(stdout, "Launching the server version (a new thread )for sending data(received from VM) using RDMA\n");
        pthread_create(&transport_thread_server_send, NULL, (void *)handle_rdma_transport, NULL);
    }

    /********Run the main thread for communication with VM/QEMU************************/
    while (app_running)
        loop_server(vhost_server);


   //server = new_vhost_server("/home/amakkar/qemu.sock");
  // new_vhost_server("/home/amakkar/qemu.sock");
}
