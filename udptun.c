#include <linux/init.h>
#include <linux/module.h>

MODULE_LICENSE("GPL-2.0");

static int udptun_init(void)
{
	printk(KERN_ALERT "hello world\n");
	return 0;
}

static void udptun_exit(void)
{
	printk(KERN_ALERT "goodbye\n");
}

module_init(udptun_init);
module_exit(udptun_exit);
