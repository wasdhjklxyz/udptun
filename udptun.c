#include <linux/init.h>
#include <linux/module.h>
#include <linux/netdevice.h>

MODULE_LICENSE("GPL-2.0");

#define UDPTUN_DBG(fmt, ...)                                             \
	do {                                                           \
		printk(KERN_DEBUG "[%s:%d] %s(): " fmt "\n", __FILE__, \
		       __LINE__, __func__, ##__VA_ARGS__);             \
	} while (0)

static struct net_device *udptun_dev;

static int __init udptun_init(void)
{
	int ret;

	udptun_dev = alloc_netdev(0, "udptun%d", NET_NAME_UNKNOWN, NULL);
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
