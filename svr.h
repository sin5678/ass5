#pragma once

#include <list>
#include <atomic>

#include "rbuffer.h"

class SpinLock
{
public:
	void lock()
	{
		while (lck.test_and_set(std::memory_order_acquire))
		{
		}
	}

	void unlock()
	{
		lck.clear(std::memory_order_release);
	}

private:
	std::atomic_flag lck = ATOMIC_FLAG_INIT;
};


#define dbg_msg printf

struct ClientContext
{
	int				fd;
	in_addr			host;
	int				port;
	rbuffer			buffer;
	int				is_connect;
	void *			lparam;
};

enum MSG_TYPE
{
	MSG_NEW_CLIENT,
	MSG_CONNECT_OK,
	MSG_RECV,
	MSG_CONNECT_ERROR,
	MSG_ERROR,
	MSG_DISCONNECT,
};

typedef void(*on_msg)(ClientContext *, int msg);


class svr
{
public:
	svr();
	virtual ~svr();
public:
	bool start(int port, on_msg func);
	bool stop();
	bool send(ClientContext *client, unsigned char *data, int len);
	ClientContext * connect(const char *host, int port);
	bool svrLoop();
	void disconnect(ClientContext *);

private:
	void onNewClient(ClientContext *);
	bool onAccept();
	bool onRead(ClientContext *);
	bool onDisconnect(ClientContext *);
	bool onWrite(ClientContext *);
	bool onError(ClientContext *);
	void onHup(ClientContext *);
	bool AddFdOnWrite(int fd, void *data);
	bool AddFdOnRead(int fd, void *data);
private:
	int							m_svr_fd;
#ifdef LINUX
	int							m_epoll;
#elif BSD
	int							m_kq;
#endif
	int							m_stoping;
	int							m_conn_pending;
	on_msg						m_msg_func;
	std::list<ClientContext *>	m_client_list;
	SpinLock					m_lock;
};

