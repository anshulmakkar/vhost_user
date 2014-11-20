#include <stdio.h>
#include <stdlib.h>
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
#include "vhost_user.h"
//#include "vhost_user_transport.h"
//#include "vring.h"
/* Transport Server app(thread) for sending RDMA data to the client. 
 * It connects to the transport client and sends data to it.
 */

extern struct app_context  *transport_ctx;
extern struct app_data data;

/* rdma_write: Executed only in server transport thread for sending data using RDMA */
static void rdma_write(struct app_context *ctx, struct app_data *data)
{
    ctx->sge_list.addr = (uintptr_t)ctx->buf;
    ctx->sge_list.length = ctx->size;
    ctx->sge_list.lkey = ctx->mr->lkey;
    
    ctx->wr.wr.rdma.remote_addr = data->remote_connection->vaddr;
    ctx->wr.wr.rdma.rkey        = data->remote_connection->rkey;
    ctx->wr.wr_id = RDMA_WRID;
    ctx->wr.sg_list = &ctx->sge_list;
    ctx->wr.num_sge = 1;
    ctx->wr.opcode = IBV_WR_RDMA_WRITE;
    ctx->wr.send_flags = IBV_SEND_SIGNALED;
    ctx->wr.next = NULL;

    struct ibv_send_wr * bad_wr;
    fprintf(stdout, "posing the buffer for rdma_writ \n");
    TEST_NZ(ibv_post_send(ctx->qp, &ctx->wr, &bad_wr),
            "ibv_post_send_failed.");
}

/* rdma_transfer: Executed only in server transport thread for sending data using RDMA */
void rdma_transfer(uint8_t * buf, size_t size)
{
   uint8_t *ptr = transport_ctx->buf;
   memcpy(ptr, buf, size);
   rdma_write(transport_ctx, &data);
}

static int send_packet(struct VHostUser* vhost_user,void * p, size_t size)
{
    int r = 0;
    uint32_t rx_idx = VHOST_CLIENT_VRING_IDX_RX;
    fprintf(stderr, "vhost_client.c: send_packet: put_vring %lx\n", 
            vhost_user->vring_table.vring[rx_idx].desc);
    if (vhost_user->vring_table.vring[rx_idx].desc)
    {
        r = put_vring(&vhost_user->vring_table, rx_idx, (void *)p, size);
        if (r != 0)
        {
            fprintf(stdout, "send_packet: put_vring failed \n");
            return -1;
        }
        fprintf(stderr, "vhost_client.c: send_packet:put_vring and call kick \n");
        return kick(&vhost_user->vring_table, rx_idx);
    }
    else
        fprintf(stdout, "cant send packet ad [rx_idx].desc not set \n");
    return 0;
}

/* rdma_read: Reads the data from the buffer that has been transferred by 
 * transport server app which may be
 * be executing on different physical machine. (SDN-1). 
 * It then sends this received data to the VM to completem VM to VM communication.
 * Only executes in the client transport thread for receiving data using RDMA 
 */
void rdma_read_send(PThreadArgs *pthread_args, uint8_t * buf, size_t size)
{
    if(send_packet(pthread_args->vhost_user, (void *)buf, size) != 0)
        fprintf(stdout, "Send packet failed \n");
}

/* qp_change_state_rtr
 * Change the qp status to RTR (ready to receive)
 */
int qp_change_state_rtr(struct ibv_qp *qp, struct app_data *data)
{
    struct ibv_qp_attr *attr;

    attr = malloc(sizeof(struct ibv_qp_attr));
    memset(attr, 0, sizeof(struct ibv_qp_attr));

    attr->qp_state = IBV_QPS_RTR;
    attr->path_mtu = IBV_MTU_2048;
    attr->dest_qp_num = data->remote_connection->qpn;
    attr->rq_psn      = data->remote_connection->psn;
    attr->max_dest_rd_atomic = 1;
    attr->min_rnr_timer = 12;
    attr->ah_attr.is_global = 0;
    attr->ah_attr.dlid = data->remote_connection->lid;
    attr->ah_attr.sl = sl;
    attr->ah_attr.src_path_bits = 0;
    attr->ah_attr.port_num = data->ib_port;

    TEST_NZ(ibv_modify_qp(qp, attr, 
                          IBV_QP_STATE              |
                          IBV_QP_AV                 |
                          IBV_QP_PATH_MTU           |
                          IBV_QP_DEST_QPN           |
                          IBV_QP_RQ_PSN             |
                          IBV_QP_MAX_DEST_RD_ATOMIC |
                          IBV_QP_MIN_RNR_TIMER),
            "Could not modify QP to RTR state");
    free(attr);
    return 0;
}

/* qp_change_state_rts
 * change qp status to RTS (Read to send)
 * QP status has to be RTR before changing it to RTS 
 */
int qp_change_state_rts(struct ibv_qp *qp, struct app_data *data)
{
    /* as mentioned above, first qp status has to be change to RTR (receive) */
    qp_change_state_rtr(qp, data);

    struct ibv_qp_attr * attr;
    attr = malloc(sizeof(struct ibv_qp_attr));
    memset(attr, 0, sizeof(struct ibv_qp_attr));

    attr->qp_state = IBV_QPS_RTS;
    attr->timeout = 14;
    attr->retry_cnt = 7;
    attr->rnr_retry = 7;
    attr->sq_psn = data->local_connection.psn;
    attr->max_rd_atomic = 1;

    TEST_NZ(ibv_modify_qp(qp, attr, 
                          IBV_QP_STATE      |
                          IBV_QP_TIMEOUT    |
                          IBV_QP_RETRY_CNT  | 
                          IBV_QP_RNR_RETRY  |
                          IBV_QP_SQ_PSN     |
                          IBV_QP_MAX_QP_RD_ATOMIC),
            "Could not modify QP to RTS state");
    free(attr);
    return 0;
}
int tcp_exch_ib_connection_info(struct app_data *data)
{
    char msg[sizeof("0000:000000:000000:00000000:0000000000000000")];
    int parsed;
    int cnt;

    struct ib_connection *local = &data->local_connection;

    sprintf(msg, "%04x:%06x:%06x:%08x:%016Lx",
            local->lid, local->qpn, local->psn, local->rkey, local->vaddr);


    fprintf(stdout, "writing to sockfd = %d\n", data->sockfd);

    if (cnt = write(data->sockfd, msg, sizeof(msg)) != sizeof(msg))
    {
        fprintf(stderr, "could not send the conn_details to peer %d\n", cnt);
        return -1;
    }
    if (read(data->sockfd, msg, sizeof(msg)) != sizeof(msg))
    {
        fprintf(stderr, "could not read conn_details from peer\n");
        return -1;
    }
    if (!data->remote_connection)
        free(data->remote_connection);

    TEST_Z(data->remote_connection = malloc(sizeof(struct ib_connection)),
           "Could not allocate mem for remote connection");

    struct ib_connection *remote = data->remote_connection;
    parsed = sscanf(msg, "%x:%x:%x:%x:%Lx", &remote->lid, &remote->qpn,
                         &remote->psn, &remote->rkey, &remote->vaddr);

    if (parsed != 5)
    {
        fprintf(stderr, "Could not parse msg from peer\n");
        free(data->remote_connection);
    }
    return 0;
}

int qp_change_state_init(struct ibv_qp *qp, struct app_data *data)
{
    struct ibv_qp_attr *attr;
    attr = malloc(sizeof(struct ibv_qp_attr));
    memset(attr, 0, sizeof(struct ibv_qp_attr));

    attr->qp_state = IBV_QPS_INIT;
    attr->pkey_index = 0;
    attr->port_num = data->ib_port;
    attr->qp_access_flags = IBV_ACCESS_REMOTE_WRITE;

    TEST_NZ(ibv_modify_qp(qp, attr, 
                          IBV_QP_STATE         |
                          IBV_QP_PKEY_INDEX    |
                          IBV_QP_PORT          |
                          IBV_QP_ACCESS_FLAGS),
            "could not modify QP to INIT");                
     return 0;
}

/* tcp_client_connect
 * Creates connection to the server. Its called only from the lcient instance.
 */
int tcp_client_connect(struct app_data *data)
{
    struct addrinfo *res, *t;
    struct addrinfo hints = {
                             .ai_family = AF_UNSPEC,
                             .ai_socktype = SOCK_STREAM
                            };
    char *service;
    int n;
    int sockfd = -1;
    struct sockaddr_in sin;

    TEST_N(asprintf(&service, "%d", data->port),
           "Error writing port-number to port-string");
    TEST_N(getaddrinfo(data->servername, service, &hints, &res),
           "getaddrinfo threw error");

    for (t = res; t; t = t->ai_next)
    {
        TEST_N(sockfd = socket(t->ai_family, t->ai_socktype, t->ai_protocol),
               "Could not create cleint socket");
        fprintf(stdout, "sockfd = %d\n", sockfd);
        TEST_N(connect(sockfd, t->ai_addr, t->ai_addrlen),
               "could not connect to server");
    }
    freeaddrinfo(res);
    return sockfd;
}
/* tcp_server_listen
 * Creates a TCP server socket which listens for incoming connection.
 * Its called only from the server instance.
 */
int tcp_server_listen(struct app_data *data)
{
    struct addrinfo *res, *t;
    struct addrinfo hints = {
                              .ai_flags = AI_PASSIVE,
                              .ai_family = AF_UNSPEC,
                              .ai_socktype = SOCK_STREAM
                            };
    char *service;
    int sockfd = -1;
    int n, connfd;
    struct sockaddr_in sin;
    int cnt = 0;

    TEST_N(asprintf(&service, "%d", data->port), 
           "Error writing port-number to port-string");

    TEST_N(n = getaddrinfo(NULL, service, &hints, &res),
           "getaddrinfo failed");

    TEST_N(sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol),
           "could not create server socket");

    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &n, sizeof(n));

    TEST_N(bind(sockfd, res->ai_addr, res->ai_addrlen),
           "could not bind addr to socket");

    TEST_N(listen(sockfd, 5),
           "failed to listen on sockfd");

    TEST_N(connfd = accept(sockfd, NULL, 0),
           "failed to setup accept");
    
    freeaddrinfo(res);

    return connfd;
}

/* set_local_ib_connection
 * sets all relevant attributes needed for an IB connection which are then sent
 * to peer via TCP connection.
 * Information needed to exchange data over IB are :
 * lid: Local Identifier, 16 bit address assigned to end node by subnet manager.
 * qpn: Q pair number, identifies qpn with the channel adapther (HCA i.e IB hardware)
 * psn: Packet sequence number, used to verify correct delivery seq of packages.
 * rkey: Remote Key, together with 'vaddr' identifies and grants access to memory region.
 * vaddr: virtual address, memory address that peer can later write to.
 */
void set_local_ib_connection(struct app_context *ctx, struct app_data *data)
{
    struct ibv_port_attr attr;
    TEST_NZ(ibv_query_port(ctx->context, data->ib_port, &attr),
            "could not get port attributes, ibv_query_port");

    data->local_connection.lid = attr.lid;
    data->local_connection.qpn = ctx->qp->qp_num;
    data->local_connection.psn = lrand48() & 0xffffff;
    data->local_connection.rkey = ctx->mr->rkey;
    data->local_connection.vaddr = (uintptr_t)ctx->buf + ctx->size;
}

/* init_context
 * this method initializes the IB context.
 * It creates structures for: ProtectionDomain, MemoryRegion, CompletionChannel,
 * Completions Qs and QP.
 */
struct app_context * init_context(struct app_data *data)
{
    //struct app_conext *ctx;
    struct ibv_device *ib_dev;
    page_size = sysconf(_SC_PAGESIZE);

    /*ctx is declared globally and its malloced now. Memory is in heap and heap 
     * area is shared between processes */
    transport_ctx = malloc(sizeof(struct app_context));
    memset(transport_ctx, 0, sizeof(struct app_context));
    
    transport_ctx->size = data->size; //65536
    transport_ctx->tx_depth = data->tx_depth; //100

    TEST_NZ(posix_memalign(&transport_ctx->buf, page_size, transport_ctx->size *2), 
                           "could not allocate working buffer ctx->buf");

    memset(transport_ctx->buf, 0, transport_ctx->size * 2);

    struct ibv_device **dev_list;
    int num = 0;

    TEST_Z(dev_list = ibv_get_device_list(&num), "No IB Device available");

    /* use first device in the ib device list */
    TEST_Z(data->ib_dev = dev_list[0], 
           "IB device list could not be assigned");

    TEST_Z(transport_ctx->context = ibv_open_device(data->ib_dev), 
           "ibv_open_device_failed");

    TEST_Z(transport_ctx->pd = ibv_alloc_pd(transport_ctx->context),
           "ibv_alloc_pd failed");

    TEST_Z(transport_ctx->mr = ibv_reg_mr(transport_ctx->pd, transport_ctx->buf, 
                                          transport_ctx->size * 2,
                                          IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_LOCAL_WRITE),
           "ibv_reg_mr failed");
    TEST_Z(transport_ctx->ch = ibv_create_comp_channel(transport_ctx->context), 
           "could not create completion channel");

    TEST_Z(transport_ctx->rcq = ibv_create_cq(transport_ctx->context, 1, NULL, NULL, 0), 
           "could not create receive q");

    TEST_Z(transport_ctx->scq = ibv_create_cq(transport_ctx->context, transport_ctx->tx_depth, 
                                              transport_ctx, transport_ctx->ch, 0),
           "could not create send q");

    struct ibv_qp_init_attr qp_init_attr = {
        .send_cq = transport_ctx->scq,
        .recv_cq = transport_ctx->rcq,
        .qp_type = IBV_QPT_RC,
        .cap = {
                .max_send_wr = transport_ctx->tx_depth,
                .max_recv_wr = 1,
                .max_send_sge = 1,
                .max_recv_sge = 1,
                .max_inline_data = 0
               }
    };

    TEST_Z(transport_ctx->qp = ibv_create_qp(transport_ctx->pd, &qp_init_attr),
           "could not create qp");

    qp_change_state_init(transport_ctx->qp, data);
    return transport_ctx;
}
