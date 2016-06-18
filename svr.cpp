#include <iostream>
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/fcntl.h>
#include <unistd.h>
#ifdef LINUX
#include <sys/epoll.h>
#elif BSD
#include <sys/event.h>
#include <sys/time.h>
#endif
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <assert.h>
#include <inttypes.h>

#define DBG 1

#include "svr.h"



svr::svr()
{

}


svr::~svr()
{
	
}


#ifdef LINUX
bool svr::AddFdOnRead(int fd, void *data)
{
	dbg_msg("AddFdOnRead():: %d \n", fd);
	struct epoll_event ev;
	//ev.data.fd = fd;
	ev.data.ptr = data;
	ev.events = EPOLLIN | EPOLLET | EPOLLERR;

	int ret = epoll_ctl(m_epoll, EPOLL_CTL_ADD, fd, &ev);
	if (ret == -1)
	{
		throw "error add read event";
		return false;
	}
	return true;
}

bool svr::AddFdOnWrite(int fd, void *data)
{
	struct epoll_event ev;
	//ev.data.fd = fd;
	ev.data.ptr = data;//
	ev.events = EPOLLOUT | EPOLLET | EPOLLERR;

	int ret = epoll_ctl(m_epoll, EPOLL_CTL_ADD, fd, &ev);
	if (ret == -1)
	{
		return false;
	}
	return true;
}

#elif BSD
bool svr::AddFdOnRead(int fd, void *data)
{
	struct kevent kv;
	dbg_msg("add %d for read \n", fd);
	EV_SET(&kv, fd, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, data);
	return (0 == kevent(m_kq, &kv, 1, NULL, 0, NULL));
}

bool svr::AddFdOnWrite(int fd, void *data)
{
	struct kevent kv;
	dbg_msg("add %d for write \n", fd);
	EV_SET(&kv, fd, EVFILT_WRITE, EV_ADD | EV_ENABLE, 0, 0, data);
	return (0 == kevent(m_kq, &kv, 1, NULL, 0, NULL));
}
#endif

bool svr::start(int port, on_msg func)
{
	struct sockaddr_in serv_addr;
#ifdef LINUX
	m_epoll = epoll_create(100);//
	if (m_epoll == -1)
	{
		throw "error create epoll ~";
	}
#elif BSD
	if ((m_kq = kqueue()) < 0) {
		dbg_msg("Could not open kernel queue.  Error was %s.\n", strerror(errno));
		throw "error create kqueue ~";
	}
#endif
	m_conn_pending = 0;
	m_svr_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	dbg_msg("get socket fd %d \n", m_svr_fd);
	bzero((char *)&serv_addr, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = 0;
	serv_addr.sin_port = htons(port);

	int enable = 1;
	if (setsockopt(m_svr_fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0)
		throw ("setsockopt(SO_REUSEADDR) failed");

	if (bind(m_svr_fd, (sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
	{
		dbg_msg("can not bind on port : %d \n", port);
		close(m_svr_fd);
		return false;
	}

	listen(m_svr_fd, 5);
	unsigned long on = 1L;
	if (ioctl(m_svr_fd, (unsigned long)FIONBIO, (char *)&on))
	{
		dbg_msg("ioctl FIONBIO call failed\n");
	}

	m_stoping = 0;
	m_msg_func = func;

	return AddFdOnRead(m_svr_fd, NULL);
}

bool svr::stop()
{
	dbg_msg("entry stop() \n");
	m_stoping = 1;
	m_lock.lock();
	for (auto p : m_client_list)
	{
		shutdown(p->fd, SHUT_RDWR);
	}
	m_lock.unlock();
	while (1)
	{
		m_lock.lock();
		if (m_client_list.size() == 0)
		{
			m_lock.unlock();
			break;
		}
		m_lock.unlock();
		sleep(1);
	}
	shutdown(m_svr_fd, SHUT_RDWR);
	dbg_msg("stop ok \n");
}

#ifdef LINUX
bool svr::svrLoop()
{
	int nr_events = 0;
	struct epoll_event events;

	dbg_msg("entry epoll () \n");
	nr_events = epoll_wait(m_epoll, &events, 1, -1);

	if (nr_events <= 0)
	{
		perror("epoll_wait");
		return false;
	}

	dbg_msg("event=%ld on fd=%d \n", events.events, ((ClientContext *)events.data.ptr) ? ((ClientContext *)events.data.ptr)->fd : 0);

	if (events.events & EPOLLIN)
	{
		onRead((ClientContext *)events.data.ptr);
	}
	else if (events.events & EPOLLOUT)
	{
		onWrite((ClientContext *)events.data.ptr);
	}
	else if (events.events & EPOLLERR)
	{
		onError((ClientContext *)events.data.ptr);
	}
	else if (events.events & EPOLLHUP)
	{
		onHup((ClientContext *)events.data.ptr);
	}
	return true;
}
#elif BSD
bool svr::svrLoop()
{
	struct kevent ev;
	for (;;) {
		dbg_msg("entry kevent() \n");
		int nev = kevent(m_kq, NULL, 0, &ev, 1, NULL);   /* set upper time limit to block */
		dbg_msg("out kevent() %d\n", nev);
		if (nev == -1) {
			perror("kevent()");
			break;
		}
		else if (nev == 0) {
			/* handle timeout */
		}
		else if (nev > 0) {
			dbg_msg("fd: %d filter:%d flag: %d data: %p \n", ev.ident, ev.filter, ev.flags, ev.udata);
			if (ev.flags & EV_ERROR)
			{
				dbg_msg(" %d EV_ERROR\n", ev.ident);
			}
			else if (ev.flags & EV_EOF)
			{
				dbg_msg(" %d EV_EOF\n", ev.ident);
				onDisconnect((ClientContext *)ev.udata);
			}
			else if (ev.filter == EVFILT_WRITE)
			{
				dbg_msg(" %d NOTE_WRITE\n", ev.ident);
				onWrite((ClientContext *)ev.udata);
			}
			else if (ev.filter == EVFILT_READ)
			{
				onRead((ClientContext *)ev.udata);
			}
				
		}
	}
	return true;
}
#endif

bool svr::onRead(ClientContext *client)
{
	if (client == NULL)
	{
		return onAccept();
	}
	int totalRead = 0;
	while (1)
	{
		char buff[1024];
		int bytes = recv(client->fd, buff, sizeof(buff), 0);
		if (bytes > 0)
		{
			//client->msg_len = bytes;
			//m_msg_func(client, MSG_RECV);
			totalRead += bytes;
			client->buffer.write(buff, bytes);
			if (totalRead > 10 * 1024)
			{
				m_msg_func(client, MSG_RECV);
				totalRead -= 10 * 1024;
			}
		}
		else if (bytes == 0)
		{
			dbg_msg("client disconnect \n");
			totalRead = 0;
			onDisconnect(client);
			break;
		}
		else if(bytes == -1 && errno == EAGAIN)
		{
			break;
		}
		else
		{
			dbg_msg("unexcept ~!! \n");
			dbg_msg("recv return %d, err: %d \n", bytes, errno);
			//throw "error recv";
			totalRead = 0;
			onDisconnect(client);
			break;
		}
	}
	if (totalRead)
	{
		m_msg_func(client, MSG_RECV);
	}
	return true;
}

void svr::onHup(ClientContext *client)
{
	dbg_msg("entry onHup() \n");
	if (client == NULL)
	{
		close(m_svr_fd);
		m_svr_fd = -1;
#ifdef LINUX
		close(m_epoll);
		m_epoll = -1;
#elif BSD
		close(m_kq);
		m_kq = -1;
#endif
	}
}

bool svr::onDisconnect(ClientContext *client)
{
	dbg_msg("try disconnect %d \n", client->fd);
	if (client->buffer.getBufferLen())
	{
		//还有残留的数据 。。处理下
		m_msg_func(client, MSG_RECV);
		client->buffer.clearBuffer();
	}
	m_msg_func(client, MSG_DISCONNECT);
	if(client->fd >= 0)
	{
		close(client->fd);
		client->fd = -1;
	}
	m_lock.lock();
	m_client_list.remove(client);
	m_lock.unlock();

	delete client;
	return true;
}

void svr::onNewClient(ClientContext *client)
{
	m_lock.lock();
	m_client_list.push_back(client);
	m_lock.unlock();

	client->buffer.clearBuffer();
	if (client->is_connect)
	{
		m_msg_func(client, MSG_CONNECT_OK);
	}
	else
	{
		m_msg_func(client, MSG_NEW_CLIENT);
	}
}


bool svr::send(ClientContext *client, unsigned char *data, int len)
{
	int ret, offset;
	m_lock.lock();
	offset = 0;
	while (offset < len)
	{
		ret = ::send(client->fd, data + offset , len - offset, 0);
		if (ret > 0) offset += ret;
		else if (errno == EAGAIN) continue;
		else
		{
			dbg_msg("err: %d off: %d len:%d ret:%d\n", errno, offset, len, ret);
			break;
		}
		//break;
	}
	m_lock.unlock();
	if (!(offset == len))
	{
		dbg_msg("  --- > send error !!!!! %d %d %d\n", ret, offset , len);
	}
	return (offset == len);
}

ClientContext * svr::connect(const char *host, int port)
{
	struct sockaddr_in serv_addr;
	int client_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (client_socket == -1)
	{
		perror("socket():");
		return NULL;
	}

	dbg_msg("get socket fd %d", client_socket);
	bzero((char *)&serv_addr, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = inet_addr(host);
	serv_addr.sin_port = htons(port);

	//设置 connect 的 socket 为非阻塞

	unsigned long on = 1L;
	if (ioctl(client_socket, (unsigned long)FIONBIO, (char *)&on))
	{
		dbg_msg("ioctl FIONBIO call failed\n");
	}

	ClientContext *client = new ClientContext;
	client->fd = client_socket;
	client->lparam = 0;
	client->is_connect = 1;

	AddFdOnWrite(client_socket, client);
	
	if (::connect(client_socket, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
		if (errno != EINPROGRESS)
		{
			dbg_msg("%s", "ERROR connecting");
			throw "!!!!!!!";
		}
	}
	m_conn_pending++;
	return client;
}

bool svr::onAccept()
{
	sockaddr_in client_addr;// = (sockaddr_in *)malloc(sizeof(sockaddr_in));
	socklen_t len = sizeof(sockaddr_in);
	int fd = accept(m_svr_fd, (sockaddr *)&client_addr, &len);
	if (fd < 0)
	{
		dbg_msg("accept() < 0 \n");
		return false;
	}
	if (m_stoping)
	{
		close(fd);
		dbg_msg("svr on stop \n");
		return false;
	}
	ClientContext *ctx = (ClientContext *)malloc(sizeof(ClientContext));
	unsigned long on = 1L;
	if (ioctl(fd, (unsigned long)FIONBIO, (char *)&on))
	{
		dbg_msg("ioctl FIONBIO call failed\n");
	}

	memset(ctx, 0, sizeof(ClientContext));
	ctx->fd = fd;
	ctx->is_connect = 0;
	ctx->host = client_addr.sin_addr;
	ctx->lparam = NULL;
	dbg_msg("accept : %d from %s \n", fd, inet_ntoa(client_addr.sin_addr));
	onNewClient(ctx);
	return AddFdOnRead(fd, ctx);
}

bool svr::onWrite(ClientContext *client)
{
	dbg_msg("onWrite() \n");
	int result = -1;
	m_conn_pending--;
	socklen_t result_len = sizeof(result);
	if (getsockopt(client->fd, SOL_SOCKET, SO_ERROR, &result, &result_len) < 0) {
		// error, fail somehow, close socket
		
	}

	if (result != 0) {
		// connection failed; error code is in 'result'
		dbg_msg("connect failed ~\n");
		onDisconnect(client);
		return true;
	}

#ifdef LINUX
	epoll_ctl(m_epoll, EPOLL_CTL_DEL, client->fd, NULL);
#elif BSD
	struct kevent ke;
	EV_SET(&ke, client->fd, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);

	/* set the event */
	int i = kevent(m_kq, &ke, 1, NULL, 0, NULL);
	if (i == -1)
	{
		dbg_msg("try delete fd , error kevent %d \n", errno);
	}
#endif
	onNewClient(client);
	AddFdOnRead(client->fd, client);

	return true;
}

bool svr::onError(ClientContext *client)
{
	dbg_msg("onError() \n");

	return true;
}

void svr::disconnect(ClientContext *p)
{
	shutdown(p->fd, SHUT_RDWR);
}