#include <linux/init.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/gfp_types.h>
#include <linux/skbuff.h>
#include <linux/if_arp.h>
#include <linux/netdevice.h>
#include <linux/mutex.h>
#include <linux/in.h>
#include <linux/udp.h>
#include <linux/compiler_types.h>
#include <linux/rcupdate.h>
#include <net/ip_tunnels.h>
#include <net/rtnetlink.h>
#include <net/ip.h>
#include <net/udp_tunnel.h>
#include <net/sock.h>

MODULE_LICENSE("GPL v2");

#define UDPTUN_PORT 12345

#define UDPTUN_DBG(fmt, ...)                                             \
	do {                                                           \
		printk(KERN_DEBUG "[%s:%d] %s(): " fmt "\n", __FILE__, \
		       __LINE__, __func__, ##__VA_ARGS__);             \
	} while (0)

struct udptun_priv {
	struct mutex lock;
	struct net __rcu *net;
	struct socket __rcu *sock;
};

static int udptun_rcv(struct sock *sk, struct sk_buff *skb)
{
	UDPTUN_DBG("hello world");
	return 0;
}

static void udptun_socket_free(struct socket *sock)
{
	if (!sock) {
		return;
	}
	if (sock->sk) {
		sk_clear_memalloc(sock->sk);
		udp_tunnel_sock_release(sock->sk->sk_socket);
	}
}

static int udptun_socket_init(struct udptun_priv *priv)
{
	struct net *net;
	struct socket *sock = NULL;
	struct socket *old_sock;
	int ret;

	struct udp_tunnel_sock_cfg cfg = {
		.sk_user_data = priv,
		.encap_type = 1,
		.encap_rcv = udptun_rcv,
	};
	struct udp_port_cfg port = {
		.family = AF_INET,
		.local_ip.s_addr = htonl(INADDR_ANY),
		.local_udp_port = htons(UDPTUN_PORT),
		.use_udp_checksums = true,
	};

	mutex_lock(&priv->lock);

	rcu_read_lock();
	net = rcu_dereference(priv->net);
	if (!net) {
		rcu_read_unlock();
		UDPTUN_DBG("NULL net");
		ret = -ENOENT;
		goto out;
	}

	ret = udp_sock_create(net, &port, &sock);
	rcu_read_unlock();
	if (ret < 0) {
		UDPTUN_DBG("Could not create socket");
		goto out;
	}

	sock->sk->sk_allocation = GFP_ATOMIC;
	sock->sk->sk_sndbuf = INT_MAX;
	sk_set_memalloc(sock->sk);
	setup_udp_tunnel_sock(net, sock, &cfg);

	old_sock = rcu_dereference_protected(priv->sock,
					     lockdep_is_held(&priv->lock));
	rcu_assign_pointer(priv->sock, sock);

	if (old_sock) {
		synchronize_rcu();
		udptun_socket_free(old_sock);
	}

	ret = 0;
out:
	mutex_unlock(&priv->lock);
	return ret;
}

static void udptun_socket_uninit(struct udptun_priv *priv)
{
	struct socket *sock;

	mutex_lock(&priv->lock);

	sock = rcu_dereference_protected(priv->sock,
					 lockdep_is_held(&priv->lock));
	rcu_assign_pointer(priv->sock, NULL);

	mutex_unlock(&priv->lock);

	if (sock) {
		synchronize_rcu();
		udptun_socket_free(sock);
	}
}

static int udptun_open(struct net_device *dev)
{
	struct udptun_priv *priv = netdev_priv(dev);
	int ret;

	ret = udptun_socket_init(priv);
	if (ret < 0) {
		return ret;
	}

	netif_carrier_on(dev);
	netif_start_queue(dev);

	UDPTUN_DBG("Device opened: %s", dev->name);

	return 0;
}

static int udptun_stop(struct net_device *dev)
{
	struct udptun_priv *priv = netdev_priv(dev);

	UDPTUN_DBG("Device stopped: %s", dev->name);

	netif_carrier_off(dev);
	netif_stop_queue(dev);

	udptun_socket_uninit(priv);

	return 0;
}

static netdev_tx_t udptun_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct socket *sock;

	struct iphdr *iph = ip_hdr(skb);
	struct udptun_priv *priv = netdev_priv(dev);

	UDPTUN_DBG("TX: proto=%u, src=%pI4, dst=%pI4, len=%u", iph->protocol,
		 &iph->saddr, &iph->daddr, skb->len);

	rcu_read_lock_bh();
	sock = rcu_dereference_bh(priv->sock);
	if (!sock) {
		rcu_read_unlock_bh();
		UDPTUN_DBG("Socket not initialized");
		goto drop;
	}

	rcu_read_unlock_bh();
	return NETDEV_TX_OK;
drop:
	dev_kfree_skb(skb);
	return NETDEV_TX_OK;
}

static const struct net_device_ops udptun_netdev_ops = {
	.ndo_open = udptun_open,
	.ndo_stop = udptun_stop,
	.ndo_start_xmit = udptun_start_xmit,
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
	mutex_init(&priv->lock);
}

static int udptun_newlink(struct net *src_net, struct net_device *dev,
			struct nlattr *tb[], struct nlattr *data[],
			struct netlink_ext_ack *extack)
{
	struct udptun_priv *priv = netdev_priv(dev);

	rcu_assign_pointer(priv->net, src_net);

	UDPTUN_DBG("Creating new device: %s", dev->name);

	return register_netdevice(dev);
}

static void udptun_dellink(struct net_device *dev, struct list_head *head)
{
	struct udptun_priv *priv = netdev_priv(dev);

	UDPTUN_DBG("Deleting device: %s", dev->name);

	udptun_socket_uninit(priv);

	rcu_assign_pointer(priv->net, NULL);

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
	int ret;

	ret = rtnl_link_register(&udptun_link_ops);
	if (ret < 0) {
		printk(KERN_ERR "udptun: Failed to register rtnl link ops\n");
		return ret;
	}

	UDPTUN_DBG("Module loaded");

	return 0;
}

static void __exit udptun_exit(void)
{
	rtnl_link_unregister(&udptun_link_ops);
	UDPTUN_DBG("Module unloaded");
}

module_init(udptun_init);
module_exit(udptun_exit);
