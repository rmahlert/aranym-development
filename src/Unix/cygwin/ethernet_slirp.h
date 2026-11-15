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
	/* libslirp retains this pointer for the lifetime of the Slirp instance. */
	SlirpCb callbacks;
	bool winsock_started;
	/*
	 * libslirp is not thread-safe.  The MiNT CPU thread queues frames here;
	 * the receive thread is the sole caller of slirp_input() and of the
	 * poll API.
	 */
	std::deque<std::vector<uint8> > guest_packets;
	std::deque<std::vector<uint8> > packets;
	std::vector<PollEntry> poll_entries;
	pthread_mutex_t slirp_lock;
	/*
	 * Loopback UDP socket that is always in the select() set.  send()
	 * writes a byte to it so queued guest frames reach libslirp at once
	 * instead of after the select() timeout.
	 */
	slirp_os_socket wake_socket;
	bool fd_limit_reported;

	void enqueue(const uint8 *packet, size_t length);
	static uint32 parse_ipv4(const char *text, uint32 fallback);
	bool open_wake_socket();
	void close_wake_socket();
	void wake();
	void drain_wake_socket();
	static slirp_ssize_t send_packet(const void *packet, size_t length, void *opaque);
	static void guest_error(const char *message, void *opaque);
	static int64_t clock_get_ns(void *opaque);
	static void register_poll_fd(int fd, void *opaque);
	static void unregister_poll_fd(int fd, void *opaque);
	static void notify(void *opaque);
	static int add_poll(slirp_os_socket fd, int events, void *opaque);
	static int get_revents(int index, void *opaque);

public:
	WinSlirpEthernetHandler(int eth_idx);
	virtual ~WinSlirpEthernetHandler();

	virtual bool open();
	virtual void close();
	virtual int recv(uint8 *, int);
	virtual int send(const uint8 *, int);
	virtual bool joinReceiverOnStop() const { return true; }
};

#endif
