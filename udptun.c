#include <linux/init.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/gfp_types.h>
#include <linux/skbuff.h>
#include <linux/skbuff.h>

MODULE_LICENSE("GPL-2.0");

#define UDPTUN_DBG(fmt, ...)                                             \
	do {                                                           \
		printk(KERN_DEBUG "[%s:%d] %s(): " fmt "\n", __FILE__, \
		       __LINE__, __func__, ##__VA_ARGS__);             \
	} while (0)

static struct net_device *udptun_dev;

struct udptun_priv {
	struct net_device_stats stats;
	spinlock_t lock;
};

static int udptun_open(struct net_device *dev)
{
	eth_hw_addr_set(dev, "\0UDPTUN\0"); /* Set fake MAC address */
	netif_start_queue(dev);
	return 0;
}

static int udptun_stop(struct net_device *dev)
{
	netif_stop_queue(dev);
	return 0;
}

static netdev_tx_t udptun_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct udptun_priv *priv = netdev_priv(dev);

	UDPTUN_DBG("TX packet len=%u", skb->len);

	priv->stats.tx_packets++;
	priv->stats.tx_bytes += skb->len;
	dev->stats.tx_packets++;
	dev->stats.tx_bytes += skb->len;

	dev_kfree_skb(skb);

	return NETDEV_TX_OK;
}

static void udptun_get_stats64(struct net_device *dev,
			     struct rtnl_link_stats64 *stats)
{
	struct udptun_priv *priv = netdev_priv(dev);

	stats->rx_packets = priv->stats.rx_packets;
	stats->tx_packets = priv->stats.tx_packets;
	stats->rx_bytes = priv->stats.rx_bytes;
	stats->tx_bytes = priv->stats.tx_bytes;
	stats->rx_errors = priv->stats.rx_errors;
	stats->tx_errors = priv->stats.tx_errors;
	stats->rx_dropped = priv->stats.rx_dropped;
	stats->tx_dropped = priv->stats.tx_dropped;
}

static const struct net_device_ops udptun_netdev_ops = {
	.ndo_open = udptun_open,
	.ndo_stop = udptun_stop,
	.ndo_start_xmit = udptun_start_xmit,
	.ndo_get_stats64 = udptun_get_stats64,
};

static void udptun_probe(struct net_device *dev)
{
	struct udptun_priv *priv;

	ether_setup(dev);

	dev->netdev_ops = &udptun_netdev_ops;

	dev->flags |= IFF_NOARP; /* We're not using ARP */
	dev->flags &= ~IFF_BROADCAST; /* Not a broadcast device */

	/* We handle our own checksums */
	dev->features |= NETIF_F_HW_CSUM | NETIF_F_HIGHDMA;

	/* Disable header caching - we're not a real Ethernet */
	dev->header_ops = NULL;

	/* WireGuard uses 1420 to fit in Ethernet */
	dev->mtu = 1420;

	priv = netdev_priv(dev);
	memset(priv, 0, sizeof(struct udptun_priv));
	spin_lock_init(&priv->lock);
}

static int __init udptun_init(void)
{
	int ret;

	udptun_dev = alloc_netdev(sizeof(struct udptun_priv), "udptun%d",
				NET_NAME_UNKNOWN, udptun_probe);
	if (!udptun_dev) {
		UDPTUN_DBG("alloc_netdev failed");
		return -ENOMEM; /* FIXME: This is just assumption */
	}

	ret = register_netdev(udptun_dev);
	if (ret < 0) {
		UDPTUN_DBG("register_netdev failed: %d", ret);
		free_netdev(udptun_dev);
		return -ret;
	}

	UDPTUN_DBG("registered as %s", udptun_dev->name);
	return 0;
}

static void __exit udptun_exit(void)
{
	if (udptun_dev) {
		unregister_netdev(udptun_dev);
		free_netdev(udptun_dev);
	}
}

module_init(udptun_init);
module_exit(udptun_exit);
