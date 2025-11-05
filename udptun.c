#include <linux/init.h>
#include <linux/module.h>

#include "udptun.h"

MODULE_LICENSE("GPL-2.0");

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
