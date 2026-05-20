#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <xdp/xdp_helpers.h>
#include "xdp/parsing_helpers.h"

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 65536);
	__type(key, __u32);
	__type(value, __u8);
} blocked_ports SEC(".maps");

SEC("xdp")
int my_xdp_firewall(struct xdp_md *ctx)
{
	(void) ctx;
	void *data_end = (void *)(long)ctx->data_end;
	void *data = (void *)(long)ctx->data;

	struct hdr_cursor nh;

	struct ethhdr *eth;
	int eth_type;

	nh.pos = data;
	eth_type = parse_ethhdr(&nh, data_end, &eth);

	struct iphdr *iphdr;
	int ip_type = 0;
	if (eth_type == bpf_htons(ETH_P_IP)) {
		ip_type = parse_iphdr(&nh, data_end, &iphdr);
	}

	struct udphdr *udphdr;
	if (ip_type == IPPROTO_UDP) {
		parse_udphdr(&nh, data_end, &udphdr);
		if ((void *)(udphdr + 1) > data_end)
            return XDP_PASS;
		__u32 dest_port = bpf_ntohs(udphdr->dest);
		__u8 *elem = bpf_map_lookup_elem(&blocked_ports, &dest_port);
		if (!elem)
			return XDP_PASS;
		if (*elem == 0)
			return XDP_PASS;
		bpf_printk("UDP: dest port = %d DROPPED\n", dest_port);
		return XDP_DROP;
	}

	struct tcphdr *tcphdr;
	if (ip_type == IPPROTO_TCP) {
		parse_tcphdr(&nh, data_end, &tcphdr);
		if ((void *)(tcphdr + 1) > data_end)
            return XDP_PASS;
		__u32 dest_port = bpf_ntohs(tcphdr->dest);
		__u8 *elem = bpf_map_lookup_elem(&blocked_ports, &dest_port);
		if (!elem)
			return XDP_PASS;
		if (*elem == 0)
			return XDP_PASS;
		bpf_printk("TCP: dest port = %d DROPPED\n", dest_port);
		return XDP_DROP;
	}

	return XDP_PASS;
}

char LICENSE[] SEC("license") = "GPL";
