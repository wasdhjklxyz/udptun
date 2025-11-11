#include <linux/init.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/gfp_types.h>
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
	struct napi_struct napi;
	struct sk_buff_head rx_queue;
};

static int udptun_poll(struct napi_struct *napi, int budget)
{
	int work_done = 0;
	struct udptun_priv *priv = container_of(napi, struct udptun_priv, napi);

	while (work_done < budget) {
		struct sk_buff *skb = skb_dequeue(&priv->rx_queue);
		if (!skb) {
			break;
		}

		UDPTUN_DBG("RX sk_buff len=%d", skb->len);

		skb->protocol = eth_type_trans(skb, napi->dev);
		skb->ip_summed = CHECKSUM_UNNECESSARY;

		priv->stats.rx_packets++;
		priv->stats.rx_bytes += skb->len;

		napi_gro_receive(napi, skb);
		work_done++;
	}

	if (work_done < budget) {
		napi_complete_done(napi, work_done);
	}

	UDPTUN_DBG("Poll done, work_done=%d", work_done);

	return work_done;
}

static int udptun_open(struct net_device *dev)
{
	struct udptun_priv *priv = netdev_priv(dev);
	static const u8 mac[ETH_ALEN] = { 0x00, 0x52, 0x54, 0x52, 0x44, 0x00 };

	eth_hw_addr_set(dev, mac);
	napi_enable(&priv->napi);

	netif_carrier_on(dev);
	netif_start_queue(dev);

	return 0;
}

static int udptun_stop(struct net_device *dev)
{
	struct udptun_priv *priv = netdev_priv(dev);

	napi_disable(&priv->napi);
	netif_carrier_off(dev);
	netif_stop_queue(dev);
	return 0;
}

static netdev_tx_t udptun_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct sk_buff *rx_skb;
	struct udptun_priv *priv = netdev_priv(dev);

	UDPTUN_DBG("TX sk_buff len=%u", skb->len);

	priv->stats.tx_packets++;
	priv->stats.tx_bytes += skb->len;

	rx_skb = skb_clone(skb, GFP_ATOMIC);
	if (rx_skb) {
		skb_orphan(rx_skb);
		skb_queue_tail(&priv->rx_queue, rx_skb);
		napi_schedule(&priv->napi);
	}

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

	/* Don't bother locking device we'll handle that */
	dev->lltx = true;

	/* Disable header caching - we're not a real Ethernet */
	dev->header_ops = NULL;

	/* WireGuard uses 1420 to fit in Ethernet */
	dev->mtu = 1420;

	priv = netdev_priv(dev);
	memset(priv, 0, sizeof(struct udptun_priv));
	spin_lock_init(&priv->lock);
	skb_queue_head_init(&priv->rx_queue);
}

static int __init udptun_init(void)
{
	int ret;
	struct udptun_priv *priv;

	udptun_dev = alloc_netdev(sizeof(struct udptun_priv), "udptun%d",
				NET_NAME_UNKNOWN, udptun_probe);
	if (!udptun_dev) {
		UDPTUN_DBG("alloc_netdev failed");
		return -ENOMEM; /* FIXME: This is just assumption */
	}

	priv = netdev_priv(udptun_dev);
	netif_napi_add(udptun_dev, &priv->napi, udptun_poll);

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
	struct udptun_priv *priv = netdev_priv(udptun_dev);

	if (udptun_dev) {
		netif_napi_del(&priv->napi);
		unregister_netdev(udptun_dev);
		free_netdev(udptun_dev);
	}
}

module_init(udptun_init);
module_exit(udptun_exit);
