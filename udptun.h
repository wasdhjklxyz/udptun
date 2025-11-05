#ifndef UDPTUN_INTERNAL_H
#define UDPTUN_INTERNAL_H

#define UDPTUN_DBG(fmt, ...)                                             \
	do {                                                           \
		printk(KERN_DEBUG "[%s:%d] %s(): " fmt "\n", __FILE__, \
		       __LINE__, __func__, ##__VA_ARGS__);             \
	} while (0)

#endif /* UDPTUN_INTERNAL_H */
