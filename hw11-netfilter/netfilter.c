#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
#include <linux/tcp.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Anna Piatkova");
MODULE_DESCRIPTION("Netfilter");
MODULE_VERSION("0.1");

#define NO_FILTER -1

static int port = 443;

static int port_set(const char *val, const struct kernel_param *kp)
{
	if (val[0] == '\0' || val[0] == '\n' && val[1] == '\0') {
		port = NO_FILTER;
		return 0;
	}
	int new_port;
	if (kstrtoint(val, 10, &new_port)) {
		pr_info("mynetfilter: invalid port number: %s\n", val);
		return -EINVAL;
	}
	port = new_port;
	return 0;
}

static int port_get(char *buffer, const struct kernel_param *kp)
{
	if (port == NO_FILTER) {
		int ret = sprintf(buffer, "\n");
		return ret;
	} else {
		int ret = sprintf(buffer, "%d\n", port);
		return ret;
	}
}

static const struct kernel_param_ops ports_param_ops = {
	.set = port_set,
	.get = port_get,
};

module_param_cb(port, &ports_param_ops, &port, 0644);

static unsigned int my_nf_hoonfn(void *priv, struct sk_buff *skb, const struct nf_hook_state *state) {
	unsigned int saddr = state->sk->sk_rcv_saddr;
	unsigned int daddr = state->sk->sk_daddr;
	unsigned int portpair = state->sk->sk_portpair;
	unsigned int dest_port = (((unsigned int)skb->data[22]) << 8) + (unsigned int)skb->data[23];
	if (dest_port == port)
	{
		pr_info(
			"mynetfilter: dropped packet, src addr = %d.%d.%d.%d:%d, dest addr = %d.%d.%d.%d:%d, remote port = %d\n",
			saddr & 255, (saddr >> 8) & 255, (saddr >> 16) & 255, saddr >> 24, portpair & 65535,
			daddr & 255, (daddr >> 8) & 255, (daddr >> 16) & 255, daddr >> 24, portpair >> 16,
			dest_port
		);
		return NF_DROP;
	}
	return NF_ACCEPT;
}

const struct nf_hook_ops ops = {
	.hook = my_nf_hoonfn,
	.pf = PF_INET,
	.hooknum = NF_INET_LOCAL_OUT,
	.priority = NF_IP_PRI_FILTER,
};

static int __init mynetfilter_init(void)
{
	nf_register_net_hook(&init_net, &ops);
	if (port == NO_FILTER) {
		pr_info("mynetfilter: module loaded, not filtering packets from any ports\n");
	} else {
		pr_info("mynetfilter: module loaded, filtering packets from port %d\n", port);
	}
	return 0;
}

static void __exit mynetfilter_exit(void)
{
	nf_unregister_net_hook(&init_net, &ops);
	pr_info("mynetfilter: module unloaded\n");
}

module_init(mynetfilter_init);
module_exit(mynetfilter_exit);