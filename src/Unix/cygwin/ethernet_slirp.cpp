#include "cpu_emulation.h"
#include "main.h"
#include "ethernet_slirp.h"
#include "debug.h"

#if defined(OS_mingw)
#include <winsock2.h>
#else
#include <arpa/inet.h>
#include <sys/select.h>
#include <sys/time.h>
#endif

WinSlirpEthernetHandler::WinSlirpEthernetHandler(int eth_idx)
	: ETHERNETDriver::Handler(eth_idx), slirp(NULL), winsock_started(false)
{
	pthread_mutex_init(&slirp_lock, NULL);
}

WinSlirpEthernetHandler::~WinSlirpEthernetHandler()
{
	close();
	pthread_mutex_destroy(&slirp_lock);
}

void WinSlirpEthernetHandler::enqueue(const uint8 *packet, size_t length)
{
	if (length <= MAX_PACKET_SIZE)
		packets.push_back(std::vector<uint8>(packet, packet + length));
}

slirp_ssize_t WinSlirpEthernetHandler::send_packet(const void *packet, size_t length, void *opaque)
{
	WinSlirpEthernetHandler *handler = static_cast<WinSlirpEthernetHandler *>(opaque);
	handler->enqueue(static_cast<const uint8 *>(packet), length);
	return length;
}

void WinSlirpEthernetHandler::guest_error(const char *message, void *opaque)
{
	(void)message;
	(void)opaque;
}

/*
 * Parse a dotted-quad from the configuration, falling back to the
 * supplied default when the setting is empty or malformed.  Returns the
 * address in network byte order.
 */
uint32 WinSlirpEthernetHandler::parse_ipv4(const char *text, uint32 fallback)
{
	if (text == NULL || *text == '\0')
		return fallback;
	uint32 addr = (uint32)inet_addr(text);
	if (addr == INADDR_NONE)
		return fallback;
	return addr;
}

int64_t WinSlirpEthernetHandler::clock_get_ns(void *opaque)
{
	struct timeval tv;
	(void)opaque;
	gettimeofday(&tv, NULL);
	return (int64_t)tv.tv_sec * 1000000000LL + (int64_t)tv.tv_usec * 1000LL;
}

int WinSlirpEthernetHandler::add_poll(slirp_os_socket fd, int events, void *opaque)
{
	WinSlirpEthernetHandler *handler = static_cast<WinSlirpEthernetHandler *>(opaque);
	if (fd == SLIRP_INVALID_SOCKET)
		return -1;
	PollEntry entry = { fd, events, 0 };
	handler->poll_entries.push_back(entry);
	return handler->poll_entries.size() - 1;
}

int WinSlirpEthernetHandler::get_revents(int index, void *opaque)
{
	WinSlirpEthernetHandler *handler = static_cast<WinSlirpEthernetHandler *>(opaque);
	if (index < 0 || (size_t)index >= handler->poll_entries.size())
		return 0;
	return handler->poll_entries[index].revents;
}

bool WinSlirpEthernetHandler::open()
{
	SlirpConfig config;
	SlirpCb callbacks;

#if defined(OS_mingw)
	WSADATA winsock_data;
	if (WSAStartup(MAKEWORD(2, 2), &winsock_data) != 0)
		return false;
	winsock_started = true;
#endif

	if (strncmp(bx_options.ethernet[ethX].type, "slirp", 5) != 0)
		return false;

	/*
	 * Honour the configured addresses.  These are what the installer
	 * writes and what nfeth-config reports to the Atari side, so the
	 * emulated network has to match them -- hardcoding the defaults
	 * here silently breaks anyone who changed them.
	 */
	uint32 vhost = parse_ipv4(bx_options.ethernet[ethX].ip_host, inet_addr("10.0.2.2"));
	uint32 vmask = parse_ipv4(bx_options.ethernet[ethX].netmask, inet_addr("255.255.255.0"));
	uint32 vguest = parse_ipv4(bx_options.ethernet[ethX].ip_atari, inet_addr("10.0.2.15"));

	/*
	 * The guest and the virtual router must share a subnet or nothing
	 * routes at all.  Refuse the broken combination and fall back to
	 * the slirp defaults rather than bringing up a network that cannot
	 * work.
	 */
	if ((vhost & vmask) != (vguest & vmask)) {
		panicbug("eth%d: slirp: host %s and Atari %s are not on the same subnet, using defaults",
			ethX, bx_options.ethernet[ethX].ip_host, bx_options.ethernet[ethX].ip_atari);
		vhost = inet_addr("10.0.2.2");
		vmask = inet_addr("255.255.255.0");
		vguest = inet_addr("10.0.2.15");
	}

	memset(&config, 0, sizeof(config));
	config.version = 1;
	config.in_enabled = true;
	config.vnetwork.s_addr = vhost & vmask;
	config.vnetmask.s_addr = vmask;
	config.vhost.s_addr = vhost;
	config.vdhcp_start.s_addr = vguest;
	/* slirp's convention puts DNS one address above the router. */
	config.vnameserver.s_addr = htonl(ntohl(vhost) + 1);
	config.vhostname = "aranym";
	config.if_mtu = 1500;
	config.if_mru = 1500;

	memset(&callbacks, 0, sizeof(callbacks));
	callbacks.send_packet = send_packet;
	callbacks.guest_error = guest_error;
	callbacks.clock_get_ns = clock_get_ns;

	debug = strstr(bx_options.ethernet[ethX].type, "debug") != NULL;
	packets.clear();
	poll_entries.clear();
	slirp = slirp_new(&config, &callbacks, this);
	if (slirp == NULL)
	{
		close();
		return false;
	}

	return true;
}

void WinSlirpEthernetHandler::close()
{
	pthread_mutex_lock(&slirp_lock);
	if (slirp != NULL) {
		slirp_cleanup(slirp);
		slirp = NULL;
	}
#if defined(OS_mingw)
	if (winsock_started) {
		WSACleanup();
		winsock_started = false;
	}
#endif
	packets.clear();
	poll_entries.clear();
	pthread_mutex_unlock(&slirp_lock);
}

int WinSlirpEthernetHandler::recv(uint8 *buffer, int length)
{
	for (;;) {
		pthread_mutex_lock(&slirp_lock);
		if (!packets.empty()) {
			std::vector<uint8> packet = packets.front();
			packets.pop_front();
			pthread_mutex_unlock(&slirp_lock);
			int packet_length = MIN((int)packet.size(), length);
			memcpy(buffer, &packet[0], packet_length);
			return packet_length;
		}

		poll_entries.clear();
		uint32 timeout = 100;
		slirp_pollfds_fill_socket(slirp, &timeout, add_poll, this);
		fd_set readfds;
		fd_set writefds;
		FD_ZERO(&readfds);
		FD_ZERO(&writefds);
	int maxfd = -1;
		for (size_t i = 0; i < poll_entries.size(); ++i) {
			if (poll_entries[i].fd == SLIRP_INVALID_SOCKET)
				continue;
			if (poll_entries[i].events & (SLIRP_POLL_IN | SLIRP_POLL_PRI | SLIRP_POLL_ERR | SLIRP_POLL_HUP))
				FD_SET(poll_entries[i].fd, &readfds);
			if (poll_entries[i].events & SLIRP_POLL_OUT)
				FD_SET(poll_entries[i].fd, &writefds);
			maxfd = MAX(maxfd, (int)poll_entries[i].fd);
		}
		pthread_mutex_unlock(&slirp_lock);

		struct timeval wait_time;
		wait_time.tv_sec = timeout / 1000;
		wait_time.tv_usec = (timeout % 1000) * 1000;
#if defined(OS_mingw)
		int result = select(0, &readfds, &writefds, NULL, &wait_time);
#else
		int result = select(maxfd + 1, &readfds, &writefds, NULL, &wait_time);
#endif

		pthread_mutex_lock(&slirp_lock);
		for (size_t i = 0; i < poll_entries.size(); ++i) {
			poll_entries[i].revents = 0;
			if (FD_ISSET(poll_entries[i].fd, &readfds))
				poll_entries[i].revents |= SLIRP_POLL_IN;
			if (FD_ISSET(poll_entries[i].fd, &writefds))
				poll_entries[i].revents |= SLIRP_POLL_OUT;
		}
		if (slirp != NULL)
			slirp_pollfds_poll(slirp, result < 0, get_revents, this);
		pthread_mutex_unlock(&slirp_lock);
		if (result < 0)
			return -1;
	}
}

int WinSlirpEthernetHandler::send(const uint8 *buffer, int length)
{
	pthread_mutex_lock(&slirp_lock);
	if (slirp != NULL)
		slirp_input(slirp, buffer, length);
	pthread_mutex_unlock(&slirp_lock);
	return length;
}
