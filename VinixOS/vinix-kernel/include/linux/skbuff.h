/* ============================================================
 * linux/skbuff.h
 * ------------------------------------------------------------
 * Socket buffer — universal packet carrier across all net layers.
 * ============================================================ */

#ifndef LINUX_SKBUFF_H
#define LINUX_SKBUFF_H

#include "types.h"

struct net_device;

struct sk_buff {
    unsigned char     *data;
    unsigned int       len;
    struct net_device *dev;
};

struct sk_buff *alloc_skb(unsigned int size);
void            kfree_skb(struct sk_buff *skb);
unsigned char  *skb_put(struct sk_buff *skb, unsigned int len);
void            skb_reserve(struct sk_buff *skb, unsigned int len);

#endif /* LINUX_SKBUFF_H */
