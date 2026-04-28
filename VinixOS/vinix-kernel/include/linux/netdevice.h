/* ============================================================
 * linux/netdevice.h
 * ------------------------------------------------------------
 * Network device contract — driver vtable + subsystem API.
 * ============================================================ */

#ifndef LINUX_NETDEVICE_H
#define LINUX_NETDEVICE_H

#include "types.h"
#include "linux/skbuff.h"

#define __iomem         /* Linux sparse annotation — not used here */
#define IFNAMSIZ        16
#define IFF_UP          (1 << 0)
#define IFF_RUNNING     (1 << 1)

struct net_device;

struct net_device_ops {
    int  (*ndo_open)(struct net_device *dev);
    int  (*ndo_stop)(struct net_device *dev);
    int  (*ndo_start_xmit)(struct sk_buff *skb, struct net_device *dev);
    void (*ndo_get_stats)(struct net_device *dev);
    int  (*ndo_set_mac_address)(struct net_device *dev, void *addr);
};

struct net_device {
    char                          name[IFNAMSIZ];
    uint8_t                       dev_addr[6];
    uint32_t                      mtu;
    uint32_t                      flags;
    void                         *priv;
    const struct net_device_ops  *netdev_ops;
};

/* Implemented in kernel/net/core/net_core.c */
struct net_device *alloc_etherdev(uint32_t sizeof_priv);
void               free_netdev(struct net_device *dev);
void              *netdev_priv(struct net_device *dev);
int                register_netdev(struct net_device *dev);
void               unregister_netdev(struct net_device *dev);
void               netif_rx(struct sk_buff *skb);
void               netif_stop_queue(struct net_device *dev);
void               netif_wake_queue(struct net_device *dev);
void               netif_carrier_on(struct net_device *dev);
void               netif_carrier_off(struct net_device *dev);

#endif /* LINUX_NETDEVICE_H */
