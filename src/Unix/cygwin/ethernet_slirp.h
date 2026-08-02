#ifndef _ETHERNET_SLIRP_H
#define _ETHERNET_SLIRP_H

#include "ethernet.h"

#include <deque>
#include <vector>
#include <libslirp.h>

class WinSlirpEthernetHandler : public ETHERNETDriver::Handler {
	struct PollEntry {
		slirp_os_socket fd;
		int events;
		short revents;
	};

	Slirp *slirp;
	bool winsock_started;
	std::deque<std::vector<uint8> > packets;
	std::vector<PollEntry> poll_entries;
	pthread_mutex_t slirp_lock;

	void enqueue(const uint8 *packet, size_t length);
	static uint32 parse_ipv4(const char *text, uint32 fallback);
	static slirp_ssize_t send_packet(const void *packet, size_t length, void *opaque);
	static void guest_error(const char *message, void *opaque);
	static int64_t clock_get_ns(void *opaque);
	static int add_poll(slirp_os_socket fd, int events, void *opaque);
	static int get_revents(int index, void *opaque);

public:
	WinSlirpEthernetHandler(int eth_idx);
	virtual ~WinSlirpEthernetHandler();

	virtual bool open();
	virtual void close();
	virtual int recv(uint8 *, int);
	virtual int send(const uint8 *, int);
};

#endif
