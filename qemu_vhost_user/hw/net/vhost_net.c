/*
 * vhost-net support
 *
 * Copyright Red Hat, Inc. 2010
 *
 * Authors:
 *  Michael S. Tsirkin <mst@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#include "net/net.h"
#include "net/tap.h"
#include "net/vhost-user.h"

#include "hw/virtio/virtio-net.h"
#include "net/vhost_net.h"
#include "qemu/error-report.h"

#include "config.h"

#ifdef CONFIG_VHOST_NET
#include <linux/vhost.h>
#include <sys/socket.h>
#include <linux/kvm.h>
#include <fcntl.h>
#include <linux/virtio_ring.h>
#include <netpacket/packet.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <netinet/in.h>

#include <stdio.h>

#include "hw/virtio/vhost.h"
#include "hw/virtio/virtio-bus.h"

struct vhost_net {
    struct vhost_dev dev;
    struct vhost_virtqueue vqs[2];
    int backend;
    NetClientState *nc;
};

unsigned vhost_net_get_features(struct vhost_net *net, unsigned features)
{
    /* Clear features not supported by host kernel. */
    if (!(net->dev.features & (1 << VIRTIO_F_NOTIFY_ON_EMPTY))) {
        features &= ~(1 << VIRTIO_F_NOTIFY_ON_EMPTY);
    }
    if (!(net->dev.features & (1 << VIRTIO_RING_F_INDIRECT_DESC))) {
        features &= ~(1 << VIRTIO_RING_F_INDIRECT_DESC);
    }
    if (!(net->dev.features & (1 << VIRTIO_RING_F_EVENT_IDX))) {
        features &= ~(1 << VIRTIO_RING_F_EVENT_IDX);
    }
    if (!(net->dev.features & (1 << VIRTIO_NET_F_MRG_RXBUF))) {
        features &= ~(1 << VIRTIO_NET_F_MRG_RXBUF);
    }
    return features;
}

void vhost_net_ack_features(struct vhost_net *net, unsigned features)
{
    net->dev.acked_features = net->dev.backend_features;
    if (features & (1 << VIRTIO_F_NOTIFY_ON_EMPTY)) {
        net->dev.acked_features |= (1 << VIRTIO_F_NOTIFY_ON_EMPTY);
    }
    if (features & (1 << VIRTIO_RING_F_INDIRECT_DESC)) {
        net->dev.acked_features |= (1 << VIRTIO_RING_F_INDIRECT_DESC);
    }
    if (features & (1 << VIRTIO_RING_F_EVENT_IDX)) {
        net->dev.acked_features |= (1 << VIRTIO_RING_F_EVENT_IDX);
    }
    if (features & (1 << VIRTIO_NET_F_MRG_RXBUF)) {
        net->dev.acked_features |= (1 << VIRTIO_NET_F_MRG_RXBUF);
    }
}

static int vhost_net_get_fd(NetClientState *backend)
{
    switch (backend->info->type) {
    case NET_CLIENT_OPTIONS_KIND_TAP:
        return tap_get_fd(backend);
    default:
        fprintf(stderr, "vhost-net requires tap backend\n");
        return -EBADFD;
    }
}

struct vhost_net *vhost_net_init(VhostNetOptions *options)
{
    int r = -1;
    struct vhost_net *net = g_malloc(sizeof *net);

    if (!options->net_backend) {
        fprintf(stderr, "vhost-net requires net backend to be setup\n");
        goto fail;
    }

    if (options->backend_type == VHOST_BACKEND_TYPE_KERNEL) {
        r = vhost_net_get_fd(options->net_backend);
        if (r < 0) {
            goto fail;
        }

        net->dev.backend_features =
                tap_has_vnet_hdr(options->net_backend) ? 0 :
                                (1 << VHOST_NET_F_VIRTIO_NET_HDR);
    }

    net->nc = options->net_backend;
    net->backend = r;

    net->dev.nvqs = 2;
    net->dev.vqs = net->vqs;

    r = vhost_dev_init(&net->dev, options->devfd, options->devpath,
                       options->backend_type, options->force);
    if (r < 0) {
        goto fail;
    }
    if (options->backend_type == VHOST_BACKEND_TYPE_KERNEL) {
        if (!tap_has_vnet_hdr_len(options->net_backend,
                        sizeof(struct virtio_net_hdr_mrg_rxbuf))) {
            net->dev.features &= ~(1 << VIRTIO_NET_F_MRG_RXBUF);
        }
        if (~net->dev.features & net->dev.backend_features) {
            fprintf(stderr, "vhost lacks feature mask %" PRIu64
                   " for backend\n",
                   (uint64_t)(~net->dev.features & net->dev.backend_features));
            vhost_dev_cleanup(&net->dev);
            goto fail;
        }
    }
    /* Set sane init value. Override when guest acks. */
    vhost_net_ack_features(net, 0);
    return net;
fail:
    g_free(net);
    return NULL;
}

bool vhost_net_query(VHostNetState *net, VirtIODevice *dev)
{
    return vhost_dev_query(&net->dev, dev);
}

static int vhost_net_start_one(struct vhost_net *net,
                               VirtIODevice *dev,
                               int vq_index)
{
    struct vhost_vring_file file = { };
    int r;

    if (net->dev.started) {
        return 0;
    }

    net->dev.nvqs = 2;
    net->dev.vqs = net->vqs;
    net->dev.vq_index = vq_index;

    r = vhost_dev_enable_notifiers(&net->dev, dev);
    if (r < 0) {
        goto fail_notifiers;
    }

    r = vhost_dev_start(&net->dev, dev);
    if (r < 0) {
        goto fail_start;
    }

    if (net->nc->info->poll) {
        net->nc->info->poll(net->nc, false);
    }

    if (net->nc->info->type == NET_CLIENT_OPTIONS_KIND_TAP) {
        qemu_set_fd_handler(net->backend, NULL, NULL, NULL);
        file.fd = net->backend;
        for (file.index = 0; file.index < net->dev.nvqs; ++file.index) {
            const VhostOps *vhost_ops = net->dev.vhost_ops;
            r = vhost_ops->vhost_call(&net->dev, VHOST_NET_SET_BACKEND, &file);
            if (r < 0) {
                r = -errno;
                goto fail;
            }
        }
    }
    return 0;
fail:
    file.fd = -1;
    while (file.index-- > 0) {
        const VhostOps *vhost_ops = net->dev.vhost_ops;
        int r = vhost_ops->vhost_call(&net->dev, VHOST_NET_SET_BACKEND, &file);
        assert(r >= 0);
    }
    if (net->nc->info->poll) {
        net->nc->info->poll(net->nc, true);
    }
    vhost_dev_stop(&net->dev, dev);
fail_start:
    vhost_dev_disable_notifiers(&net->dev, dev);
fail_notifiers:
    return r;
}

static void vhost_net_stop_one(struct vhost_net *net,
                               VirtIODevice *dev)
{
    struct vhost_vring_file file = { .fd = -1 };

    if (!net->dev.started) {
        return;
    }

    for (file.index = 0; file.index < net->dev.nvqs; ++file.index) {
        const VhostOps *vhost_ops = net->dev.vhost_ops;
        int r = vhost_ops->vhost_call(&net->dev, VHOST_NET_SET_BACKEND, &file);
        assert(r >= 0);
    }
    if (net->nc->info->poll) {
        net->nc->info->poll(net->nc, true);
    }
    vhost_dev_stop(&net->dev, dev);
    vhost_dev_disable_notifiers(&net->dev, dev);
}

int vhost_net_start(VirtIODevice *dev, NetClientState *ncs,
                    int total_queues)
{
    BusState *qbus = BUS(qdev_get_parent_bus(DEVICE(dev)));
    VirtioBusState *vbus = VIRTIO_BUS(qbus);
    VirtioBusClass *k = VIRTIO_BUS_GET_CLASS(vbus);
    int r, i = 0;

    if (!k->set_guest_notifiers) {
        error_report("binding does not support guest notifiers");
        r = -ENOSYS;
        goto err;
    }

    for (i = 0; i < total_queues; i++) {
        r = vhost_net_start_one(get_vhost_net(ncs[i].peer), dev, i * 2);

        if (r < 0) {
            goto err;
        }
    }

    r = k->set_guest_notifiers(qbus->parent, total_queues * 2, true);
    if (r < 0) {
        error_report("Error binding guest notifier: %d", -r);
        goto err;
    }

    return 0;

err:
    while (--i >= 0) {
        vhost_net_stop_one(get_vhost_net(ncs[i].peer), dev);
    }
    return r;
}

void vhost_net_stop(VirtIODevice *dev, NetClientState *ncs,
                    int total_queues)
{
    BusState *qbus = BUS(qdev_get_parent_bus(DEVICE(dev)));
    VirtioBusState *vbus = VIRTIO_BUS(qbus);
    VirtioBusClass *k = VIRTIO_BUS_GET_CLASS(vbus);
    int i, r;

    r = k->set_guest_notifiers(qbus->parent, total_queues * 2, false);
    if (r < 0) {
        fprintf(stderr, "vhost guest notifier cleanup failed: %d\n", r);
        fflush(stderr);
    }
    assert(r >= 0);

    for (i = 0; i < total_queues; i++) {
        vhost_net_stop_one(get_vhost_net(ncs[i].peer), dev);
    }
}

void vhost_net_cleanup(struct vhost_net *net)
{
    vhost_dev_cleanup(&net->dev);
    g_free(net);
}

bool vhost_net_virtqueue_pending(VHostNetState *net, int idx)
{
    return vhost_virtqueue_pending(&net->dev, idx);
}

void vhost_net_virtqueue_mask(VHostNetState *net, VirtIODevice *dev,
                              int idx, bool mask)
{
    vhost_virtqueue_mask(&net->dev, dev, idx, mask);
}

int vhost_net_link_status(VHostNetState *net)
{
    int r = 0;

    if (net->dev.vhost_ops->vhost_status) {
        r = net->dev.vhost_ops->vhost_status(&net->dev);
    }

    return r;
}

VHostNetState *get_vhost_net(NetClientState *nc)
{
    VHostNetState *vhost_net = 0;

    if (!nc) {
        return 0;
    }

    switch (nc->info->type) {
    case NET_CLIENT_OPTIONS_KIND_TAP:
        vhost_net = tap_get_vhost_net(nc);
        break;
    case NET_CLIENT_OPTIONS_KIND_VHOST_USER:
        vhost_net = vhost_user_get_vhost_net(nc);
        break;
    default:
        break;
    }

    return vhost_net;
}

#else
struct vhost_net *vhost_net_init(VhostNetOptions *options)
{
    error_report("vhost-net support is not compiled in");
    return NULL;
}

bool vhost_net_query(VHostNetState *net, VirtIODevice *dev)
{
    return false;
}

int vhost_net_start(VirtIODevice *dev,
                    NetClientState *ncs,
                    int total_queues)
{
    return -ENOSYS;
}
void vhost_net_stop(VirtIODevice *dev,
                    NetClientState *ncs,
                    int total_queues)
{
}

void vhost_net_cleanup(struct vhost_net *net)
{
}

unsigned vhost_net_get_features(struct vhost_net *net, unsigned features)
{
    return features;
}
void vhost_net_ack_features(struct vhost_net *net, unsigned features)
{
}

bool vhost_net_virtqueue_pending(VHostNetState *net, int idx)
{
    return -ENOSYS;
}

void vhost_net_virtqueue_mask(VHostNetState *net, VirtIODevice *dev,
                              int idx, bool mask)
{
}

int vhost_net_link_status(VHostNetState *net)
{
    return 0;
}

VHostNetState *get_vhost_net(NetClientState *nc)
{
    return 0;
}
#endif
