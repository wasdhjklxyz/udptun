#include <linux/init.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/gfp_types.h>
#include <linux/skbuff.h>
#include <linux/if_arp.h>
#include <linux/netdevice.h>
#include <net/ip_tunnels.h>
#include <net/rtnetlink.h>

MODULE_LICENSE("GPL v2");

#define UDPTUN_DBG(fmt, ...)                                             \
	do {                                                           \
		printk(KERN_DEBUG "[%s:%d] %s(): " fmt "\n", __FILE__, \
		       __LINE__, __func__, ##__VA_ARGS__);             \
	} while (0)

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

static const struct device_type udptun_device_type = { .name = KBUILD_MODNAME };

static void udptun_setup(struct net_device *dev)
{
	struct udptun_priv *priv = netdev_priv(dev);
	enum {
		UDPTUN_NETDEV_FEATURES = NETIF_F_HW_CSUM | NETIF_F_RXCSUM |
				       NETIF_F_HIGHDMA | NETIF_F_SG
	};

	dev->netdev_ops = &udptun_netdev_ops;
	dev->header_ops = &ip_tunnel_header_ops;
	dev->hard_header_len = 0;
	dev->addr_len = 0;
	dev->type = ARPHRD_NONE;
	dev->flags = IFF_POINTOPOINT | IFF_NOARP;
	dev->priv_flags = IFF_NO_QUEUE;
	dev->features |= UDPTUN_NETDEV_FEATURES;
	dev->hw_features |= UDPTUN_NETDEV_FEATURES;
	dev->hw_enc_features |= UDPTUN_NETDEV_FEATURES;
	dev->lltx = true;
	dev->mtu = 1420;

	SET_NETDEV_DEVTYPE(dev, &udptun_device_type);

	memset(priv, 0, sizeof(struct udptun_priv));
}

static int udptun_newlink(struct net *src_net, struct net_device *dev,
			struct nlattr *tb[], struct nlattr *data[],
			struct netlink_ext_ack *extack)
{
	struct udptun_priv *priv = netdev_priv(dev);

	spin_lock_init(&priv->lock);
	skb_queue_head_init(&priv->rx_queue);
	netif_napi_add(dev, &priv->napi, udptun_poll);

	return register_netdevice(dev);
}

static void udptun_dellink(struct net_device *dev, struct list_head *head)
{
	struct udptun_priv *priv = netdev_priv(dev);

	netif_napi_del(&priv->napi);
	unregister_netdevice_queue(dev, head);
}

static struct rtnl_link_ops udptun_link_ops = {
	.kind = KBUILD_MODNAME,
	.priv_size = sizeof(struct udptun_priv),
	.setup = udptun_setup,
	.newlink = udptun_newlink,
	.dellink = udptun_dellink,
};

static int __init udptun_init(void)
{
	return rtnl_link_register(&udptun_link_ops);
}

static void __exit udptun_exit(void)
{
	rtnl_link_unregister(&udptun_link_ops);
}

module_init(udptun_init);
module_exit(udptun_exit);
