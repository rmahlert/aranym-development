/*
 * netsetup - configure the Atari side of ARAnyM networking
 *
 * Asks ARAnyM what it was configured with (the same SIOCGLNKSTATS call
 * nfeth-config uses), then brings eth0 up with those settings, adds the
 * default route and writes /etc/resolv.conf.
 *
 * Nothing is hardcoded: the addresses come from the emulator, so whatever
 * was chosen on the installer's network page is what gets used.
 *
 * Unlike the shell script equivalent this needs no bash, ifconfig or route
 * in the guest -- it talks to MiNT-Net directly.
 *
 * GPL, following nfeth-config by Petr Stehlik.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <net/if.h>
#include <net/route.h>
#include <sockios.h>
#include <gem.h>

/* indices into the array returned by SIOCGLNKSTATS */
enum { LNK_ADDR = 0, LNK_DSTADDR = 1, LNK_NETMASK = 2 };

static int have_aes = 0;

static void report(const char *title, const char *body)
{
	char alert[512];

	if (have_aes) {
		snprintf(alert, sizeof(alert), "[1][%s|%s][ OK ]", title, body);
		form_alert(1, alert);
	} else {
		printf("%s\n%s\n", title, body);
	}
}

static void fill_addr(struct sockaddr *sa, unsigned long addr)
{
	struct sockaddr_in *sin = (struct sockaddr_in *)sa;

	memset(sin, 0, sizeof(*sin));
	sin->sin_family = AF_INET;
	sin->sin_addr.s_addr = htonl(addr);
}

static void format_addr(char *buf, size_t len, unsigned long addr)
{
	snprintf(buf, len, "%ld.%ld.%ld.%ld",
		(addr >> 24) & 0xff, (addr >> 16) & 0xff,
		(addr >> 8) & 0xff, addr & 0xff);
}

/*
 * SLIRP places its DNS forwarder one address above the gateway, matching
 * the emulator's own default.  An existing resolv.conf is left alone.
 */
static const char *write_resolv_conf(unsigned long gateway)
{
	FILE *f;
	char dns[32];

	if (access("/etc/resolv.conf", 0) == 0)
		return "resolv.conf already present, left unchanged";

	f = fopen("/etc/resolv.conf", "w");
	if (f == NULL)
		return "could not write /etc/resolv.conf";

	format_addr(dns, sizeof(dns), gateway + 1);
	fprintf(f, "nameserver %s\n", dns);
	fclose(f);
	return "resolv.conf written";
}

int main(int argc, char **argv)
{
	const char *nif = "eth0";
	int sockfd;
	struct ifreq ifr;
	struct rtentry rt;
	long addr[10];
	char body[512];
	char s_atari[32], s_mask[32], s_gw[32];
	const char *dns_msg;

	if (argc > 1)
		nif = argv[1];

	have_aes = (appl_init() >= 0);

	sockfd = socket(AF_INET, SOCK_DGRAM, 0);
	if (sockfd < 0) {
		report("ARAnyM network setup failed",
			"Could not open a socket.|Is MiNT-Net installed and running?");
		if (have_aes)
			appl_exit();
		return 1;
	}

	/* Ask ARAnyM for the addresses it was configured with. */
	memset(&ifr, 0, sizeof(ifr));
	strcpy(ifr.ifr_name, nif);
	ifr.ifr_data = (caddr_t)addr;
	if (ioctl(sockfd, SIOCGLNKSTATS, &ifr) != 0) {
		snprintf(body, sizeof(body),
			"Interface %s was not found.|"
			"Copy nfeth.xif into the mint|folder and reboot.", nif);
		report("ARAnyM network setup failed", body);
		if (have_aes)
			appl_exit();
		return 1;
	}

	format_addr(s_atari, sizeof(s_atari), addr[LNK_ADDR]);
	format_addr(s_mask, sizeof(s_mask), addr[LNK_NETMASK]);
	format_addr(s_gw, sizeof(s_gw), addr[LNK_DSTADDR]);

	/* Address, then netmask, then bring the interface up. */
	memset(&ifr, 0, sizeof(ifr));
	strcpy(ifr.ifr_name, nif);
	fill_addr(&ifr.ifr_addr, addr[LNK_ADDR]);
	if (ioctl(sockfd, SIOCSIFADDR, &ifr) != 0) {
		report("ARAnyM network setup failed",
			"Could not set the interface address.");
		if (have_aes)
			appl_exit();
		return 1;
	}

	memset(&ifr, 0, sizeof(ifr));
	strcpy(ifr.ifr_name, nif);
	fill_addr(&ifr.ifr_netmask, addr[LNK_NETMASK]);
	if (ioctl(sockfd, SIOCSIFNETMASK, &ifr) != 0) {
		report("ARAnyM network setup failed",
			"Could not set the netmask.");
		if (have_aes)
			appl_exit();
		return 1;
	}

	memset(&ifr, 0, sizeof(ifr));
	strcpy(ifr.ifr_name, nif);
	if (ioctl(sockfd, SIOCGIFFLAGS, &ifr) == 0) {
		ifr.ifr_flags |= IFF_UP | IFF_RUNNING;
		if (ioctl(sockfd, SIOCSIFFLAGS, &ifr) != 0) {
			report("ARAnyM network setup failed",
				"Could not bring the interface up.");
			if (have_aes)
				appl_exit();
			return 1;
		}
	}

	/*
	 * Default route via the emulator's gateway.  MiNT-Net uses the 4.3BSD
	 * rtentry, so the destination is a plain sockaddr and there is no
	 * genmask field: a zero destination is the default route.
	 */
	memset(&rt, 0, sizeof(rt));
	fill_addr(&rt.rt_dst, 0);
	fill_addr(&rt.rt_gateway, addr[LNK_DSTADDR]);
	rt.rt_flags = RTF_UP | RTF_GATEWAY;
	if (ioctl(sockfd, SIOCADDRT, &rt) != 0) {
		snprintf(body, sizeof(body),
			"Address %s is set,|but the default route via|%s could not be added.",
			s_atari, s_gw);
		report("ARAnyM network partly configured", body);
		if (have_aes)
			appl_exit();
		return 1;
	}

	dns_msg = write_resolv_conf((unsigned long)addr[LNK_DSTADDR]);

	snprintf(body, sizeof(body),
		"Address: %s|Netmask: %s|Gateway: %s|%s||"
		"Test with TCP, not ping.",
		s_atari, s_mask, s_gw, dns_msg);
	report("ARAnyM network configured", body);

	close(sockfd);
	if (have_aes)
		appl_exit();
	return 0;
}
