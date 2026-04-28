/* ============================================================
 * net/core/net_core.c
 * ------------------------------------------------------------
 * Net subsystem: register_netdev, netif_rx, alloc_skb, rx task.
 * ============================================================ */

#include "linux/netdevice.h"
#include "linux/skbuff.h"
#include "net/core.h"
#include "net/ether.h"
#include "net/arp.h"
#include "spinlock.h"
#include "wait_queue.h"
#include "slab.h"
#include "uart.h"

#define RX_RING_SIZE  16

static struct net_device *s_netdev;

static struct sk_buff    *s_rx_ring[RX_RING_SIZE];
static int                s_rx_head;
static int                s_rx_tail;
static spinlock_t         s_rx_lock;
static wait_queue_head_t  s_rx_wq;

/* ── sk_buff helpers (declared in linux/skbuff.h) ─────────────────────── */

struct sk_buff *alloc_skb(unsigned int size)
{
    struct sk_buff *skb = kmalloc(sizeof(struct sk_buff), GFP_KERNEL);
    if (!skb)
        return 0;

    skb->data = kmalloc(size, GFP_KERNEL);
    if (!skb->data) {
        kfree(skb);
        return 0;
    }

    skb->len = 0;
    skb->dev = 0;
    return skb;
}

void kfree_skb(struct sk_buff *skb)
{
    if (!skb)
        return;
    if (skb->data)
        kfree(skb->data);
    kfree(skb);
}

unsigned char *skb_put(struct sk_buff *skb, unsigned int len)
{
    unsigned char *tail = skb->data + skb->len;
    skb->len += len;
    return tail;
}

void skb_reserve(struct sk_buff *skb, unsigned int len)
{
    skb->data += len;
}

/* ── net_device helpers ────────────────────────────────────────────────── */

struct net_device *alloc_etherdev(uint32_t sizeof_priv)
{
    uint32_t total = sizeof(struct net_device) + sizeof_priv;
    struct net_device *dev = kmalloc(total, GFP_KERNEL);
    if (!dev)
        return 0;

    dev->mtu      = 1500;
    dev->flags    = 0;
    dev->priv     = (void *)((unsigned char *)dev + sizeof(struct net_device));
    dev->netdev_ops = 0;
    return dev;
}

void free_netdev(struct net_device *dev)
{
    kfree(dev);
}

void *netdev_priv(struct net_device *dev)
{
    return (void *)((unsigned char *)dev + sizeof(struct net_device));
}

int register_netdev(struct net_device *dev)
{
    s_netdev = dev;
    dev->flags |= IFF_UP;

    if (dev->netdev_ops && dev->netdev_ops->ndo_open)
        dev->netdev_ops->ndo_open(dev);

    pr_info("[NET] %s registered, MTU=%u\n", dev->name, dev->mtu);
    return 0;
}

void unregister_netdev(struct net_device *dev)
{
    if (dev->netdev_ops && dev->netdev_ops->ndo_stop)
        dev->netdev_ops->ndo_stop(dev);

    dev->flags &= ~IFF_UP;
    s_netdev = 0;
}

void netif_carrier_on(struct net_device *dev)
{
    dev->flags |= IFF_RUNNING;
}

void netif_carrier_off(struct net_device *dev)
{
    dev->flags &= ~IFF_RUNNING;
}

void netif_stop_queue(struct net_device *dev)
{
    (void)dev;
}

void netif_wake_queue(struct net_device *dev)
{
    (void)dev;
}

/* ── RX path ───────────────────────────────────────────────────────────── */

/* Called from ISR — must be IRQ-safe. */
void netif_rx(struct sk_buff *skb)
{
    uint32_t flags = spin_lock_irqsave(&s_rx_lock);

    int next = (s_rx_head + 1) & (RX_RING_SIZE - 1);
    if (next == s_rx_tail) {
        /* Ring full — drop. */
        spin_unlock_irqrestore(&s_rx_lock, flags);
        kfree_skb(skb);
        return;
    }

    s_rx_ring[s_rx_head] = skb;
    s_rx_head = next;

    spin_unlock_irqrestore(&s_rx_lock, flags);
    wake_up(&s_rx_wq);
}

/* Kernel task — drains ring and dispatches to Link layer. */
void net_rx_task_entry(void)
{
    while (1) {
        wait_event(s_rx_wq, s_rx_head != s_rx_tail);

        while (s_rx_head != s_rx_tail) {
            uint32_t flags = spin_lock_irqsave(&s_rx_lock);
            struct sk_buff *skb = s_rx_ring[s_rx_tail];
            s_rx_tail = (s_rx_tail + 1) & (RX_RING_SIZE - 1);
            spin_unlock_irqrestore(&s_rx_lock, flags);

            eth_input(skb);
        }
    }
}

struct net_device *net_get_device(void)
{
    return s_netdev;
}

void net_init(void)
{
    spin_lock_init(&s_rx_lock);
    init_waitqueue_head(&s_rx_wq);
    s_rx_head = 0;
    s_rx_tail = 0;
    s_netdev  = 0;
    pr_info("[NET] subsystem ready\n");
}

void net_configure(void)
{
    if (!s_netdev) {
        pr_err("[NET] no device — check CPSW probe\n");
        return;
    }
    /* BBB static IP: 192.168.10.100 */
    uint32_t ip = (192u << 24) | (168u << 16) | (10u << 8) | 100u;
    arp_set_local(ip, s_netdev->dev_addr);
    pr_info("[NET] %s up, IP 192.168.10.100\n", s_netdev->name);
}
