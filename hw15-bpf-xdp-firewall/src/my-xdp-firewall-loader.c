#include <errno.h>
#include <net/if.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <bpf/bpf.h>
#include <bpf/libbpf.h>

#define OBJ_FILE "my-xdp-firewall.bpf.o"
#define MAPS_DIR "/sys/fs/bpf/my-xdp-firewall"
#define PROG_NAME "my_xdp_firewall"
#define MAP_NAME "blocked_ports"

void print_help() {
	printf("usage: my-xdp-firewall-loader [command], where command is one of\n\
		load [interface] -- load on an interface\n\
		unload [interface] -- unload from an interface\n\
		addport [port number] -- add [port number] to the list of blocked ports\n\
		rmport [port number] -- remove [port number] from the list of blocked ports\n\n\
	run 'sudo cat /sys/kernel/debug/tracing/trace_pipe' to see logs\n");
}

int load(const char *interface)
{	
	int ret;

	int interface_index = if_nametoindex(interface);
	if (!interface_index)
	{
		fprintf(stderr, "error: unknown interface: %s\n", interface);
		return -ENOENT;
	}
	
	struct bpf_object *obj = bpf_object__open_file(OBJ_FILE, NULL);
	if (libbpf_get_error(obj))
	{
		fprintf(stderr, "error: bpf_object__open_file\n");
		return -ENOENT;
	}

	ret = bpf_object__load(obj);
	if (ret)
	{
		fprintf(stderr, "error: bpf_object__load\n");
		goto close_bpf_object;
	}

	ret = mkdir(MAPS_DIR, 0755);
	if (ret && errno != EEXIST)
	{
		ret = -errno;
		fprintf(stderr, "error: failed to create the directory for maps\n");
		goto close_bpf_object;
	}

	ret = bpf_object__pin_maps(obj, MAPS_DIR);
	if (ret && ret != -EEXIST)
	{
		fprintf(stderr, "error: bpf_object__pin_maps\n");
		goto remove_maps_directory;
	}

	struct bpf_program *prog = bpf_object__find_program_by_name(obj, PROG_NAME);
	if (!prog) {
		ret = -ENOENT;
		fprintf(stderr, "error: program not found: %s\n", PROG_NAME);
		goto unlink_maps;
	}

	int prog_fd = bpf_program__fd(prog);
	if (prog_fd < 0) {
		ret = prog_fd;
		fprintf(stderr, "error: bpf_program__fd\n");
		goto unlink_maps;
	}

	ret = bpf_xdp_attach(interface_index, prog_fd, 0, NULL);
	if (ret) {
		fprintf(stderr, "error: bpf_xdp_attach\n");
		goto unlink_maps;
	}

	bpf_object__close(obj);
	printf("successfully loaded on interface %s\n", interface);
	return 0;

unlink_maps:
	unlink(MAPS_DIR "/" MAP_NAME);
	unlink(MAPS_DIR "/my_xdp_f_rodata");
remove_maps_directory:
	rmdir(MAPS_DIR);
close_bpf_object:
	bpf_object__close(obj);
	return ret;
}

int unload(const char *interface)
{
	int ret;

	int interface_index = if_nametoindex(interface);
	if (!interface_index)
	{
		fprintf(stderr, "error: unknown interface: %s\n", interface);
		return -ENOENT;
	}

	ret = bpf_xdp_detach(interface_index, 0, NULL);
	if (ret) {
		fprintf(stderr, "error: bpf_xdp_detach\n");
		return ret;
	}

	unlink(MAPS_DIR "/" MAP_NAME);
	unlink(MAPS_DIR "/my_xdp_f_rodata");
	rmdir(MAPS_DIR);
	printf("successfully unloaded from interface %s\n", interface);
	return 0;
}

static int update_port(const char *port_str, int new_val)
{
	__u32 port = atoi(port_str);
	if (!port) {
		fprintf(stderr, "error: invalid port number: %s\n", port_str);
		return -EINVAL;
	}

	int map_fd = bpf_obj_get(MAPS_DIR "/" MAP_NAME);
	if (map_fd < 0) {
		fprintf(stderr, "error: bpf_obj_get\n");
		return -errno;
	}

	if (bpf_map_update_elem(map_fd, &port, &new_val, BPF_ANY)) {
		fprintf(stderr, "error: bpf_map_update_elem\n");
		close(map_fd);
		return -errno;
	}

	close(map_fd);
	return 0;
}

static int add_port(const char *port_str) {
	int ret = update_port(port_str, 1);
	if (ret)
		return ret;
	printf("succesfully added port %s to the list of blocked ports\n", port_str);
	return ret;
}

static int remove_port(const char *port_str) {
int ret = update_port(port_str, 0);
	if (ret)
		return ret;
	printf("succesfully removed port %s from the list of blocked ports\n", port_str);
	return ret;
}

int main(int argc, char **argv)
{
	if (argc != 3) {
		print_help();
		return 0;
	}
	if (strcmp(argv[1], "load") == 0) {
		return load(argv[2]);
	}
	if (strcmp(argv[1], "unload") == 0) {
		return unload(argv[2]);
	}
	if (strcmp(argv[1], "addport") == 0) {
		return add_port(argv[2]);
	}
	if (strcmp(argv[1], "rmport") == 0) {
		return remove_port(argv[2]);
	}
	print_help();
	return 0;
}