#if HAVE_CONFIG_H
#  include <config.h>
#endif /* HAVE_CONFIG_H */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <netdb.h>

#include <infiniband/verbs.h>
#define RDMA_WRID 3

static int die_test(const char *reason){
	fprintf(stderr, "Err: %s - %s\n ", strerror(errno), reason);
	exit(EXIT_FAILURE);
	return -1;
}

// if x is NON-ZERO, error is printed
#define TEST_NZ(x,y) do { if ((x)) die_test(y); } while (0)

// if x is ZERO, error is printed
#define TEST_Z(x,y) do { if (!(x)) die_test(y); } while (0)

// if x is NEGATIVE, error is printed
#define TEST_N(x,y) do { if ((x)<0) die_test(y); } while (0)

static int page_size;
static int sl = 1;
static pid_t pid;

struct app_context{
	struct ibv_context 		*context;
	struct ibv_pd      		*pd;
	struct ibv_mr      		*mr;
	struct ibv_cq      		*rcq;
	struct ibv_cq      		*scq;
	struct ibv_qp      		*qp;
	struct ibv_comp_channel *ch;
	void               		*buf;
	unsigned            	size;
	int                 	tx_depth;
	struct ibv_sge      	sge_list;
	struct ibv_send_wr  	wr;
};


struct ib_connection {
    int             	lid;
    int            	 	qpn;
    int             	psn;
	unsigned 			rkey;
	unsigned long long 	vaddr;
};

struct app_data {
	int							port;
	int							ib_port;
	unsigned            		size;
	int                 		tx_depth;
	int 		    			sockfd;
	char						*servername;
	struct ib_connection		local_connection;
	struct ib_connection 		*remote_connection;
	struct ibv_device			*ib_dev;

};

/*declaring globally as its also needed in the main thread for writing. */
//extern struct app_context  *transport_ctx = NULL;
//struct app_data data;
#if 0
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
#endif
