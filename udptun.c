#include <linux/init.h>
#include <linux/module.h>

MODULE_LICENSE("GPL-2.0");

#define UDPTUN_DBG(fmt, ...)                                             \
	do {                                                           \
		printk(KERN_DEBUG "[%s:%d] %s(): " fmt "\n", __FILE__, \
		       __LINE__, __func__, ##__VA_ARGS__);             \
	} while (0)

static int udptun_init(void)
{
	UDPTUN_DBG("hello world");
	return 0;
}

static void udptun_exit(void)
{
	UDPTUN_DBG("goodbye");
}

module_init(udptun_init);
module_exit(udptun_exit);
