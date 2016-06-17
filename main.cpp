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
#include <pthread.h>
#include <signal.h>
#include "ss5.h"
#include "svr.h"

svr g_svr;
char g_ss5_user[64] = { 0 };
char g_ss5_pass[64] = { 0 };

enum ss5STATE
{
	STATE_INIT_CONN,
	STATE_CLOSE,
	STATE_RECV_AUTH,
	STATE_RECV_CONNINFO,
	STATE_CONN_START,
	STATE_START_TRANS,
};

#pragma pack(push, 1)
typedef struct _ss5ConnInfo {
	uint8_t Ver; // Version Number
	uint8_t CMD; // 0x01==TCP CONNECT,0x02==TCP BIND,0x03==UDP ASSOCIATE
	uint8_t RSV;
	uint8_t ATYP;
} ss5ConnInfo;
#pragma pack(pop)

struct ss5Context
{
	ClientContext *	client_local;
	ClientContext *	client_remote;
	ss5ConnInfo		info;
	uint32_t		ip;
	uint16_t		port;
	char			domain[256];
	int state;
};

bool ss5_ProcessReq(ClientContext *client)
{
	unsigned char ver, cnt, med[16];
	ss5Context * ss5 = (ss5Context *)client->lparam;
	client->buffer.read(&ver, 1);
	if (ver == 5)
	{
		client->buffer.read(&cnt, 1);
		if (cnt && cnt < sizeof(med))
		{
			client->buffer.read(&med, cnt);
			dbg_msg("ss5_ProcessReq()  get %d method \n", cnt);

			return true;
		}
	}
	ss5->state = STATE_CLOSE;
	return false;
}

/*
 
*/
bool ss5_ConnectHost(ClientContext *client)
{
	ss5Context * ss5 = (ss5Context *)client->lparam;
	if (ss5->domain[0] == 0)
	{
		// IP
		ss5->client_remote = g_svr.connect(inet_ntoa(*(in_addr *)&ss5->ip), ss5->port);
		ss5->client_remote->lparam = ss5;
	}
	else
	{
		in_addr addr;
		struct hostent *pHost = gethostbyname(ss5->domain);
		if (pHost)
		{
			memcpy(&addr, pHost->h_addr_list[0], pHost->h_length);
			ss5->client_remote = g_svr.connect(inet_ntoa(addr), ss5->port);
			ss5->client_remote->lparam = ss5;
		}
		else
		{
			return false;
		}
	}
	ss5->state = STATE_CONN_START;
	return true;
}

bool ss5_ParseConnInfo(ClientContext *client)
{
	ss5Context * ss5 = (ss5Context *)client->lparam;
	rbuffer buffer;
	client->buffer.copy(&buffer);
	if (buffer.getBufferLen() > 6)
	{
		buffer.read(&ss5->info, sizeof(ss5->info));
		if (ss5->info.Ver == 0x05 && ss5->info.RSV == 0x00 && ss5->info.CMD == 0x01)
		{
			goto READ_ADDR;
		}
		else
		{
			return false;
		}
	}
	else
	{
		//信息不完整 继续读
		return true;
	}
READ_ADDR:
	bool readall = false;
	int packet_len = sizeof(ss5->info);
	switch (ss5->info.ATYP)
	{
	case 1:
	{
		// ip v4 地址
		if (buffer.getBufferLen() >= 6) // ip + port
		{
			buffer.read(&ss5->ip, 4);
			buffer.read(&ss5->port, 2);
			packet_len += 6;
			ss5->port = ntohs(ss5->port);
			ss5->domain[0] = 0;
			dbg_msg("get ss5 conn info: %s %d \n", inet_ntoa(*(in_addr *)&ss5->ip), ss5->port);

			readall = true;
		}
	}
	break;
	case 0x03:
	{
		//域名信息
		if (client->buffer.getBufferLen() > 3) // len + port
		{
			uint8_t len;
			buffer.read(&len, 1);
			if (buffer.getBufferLen() >= len + 2)
			{
				buffer.read(ss5->domain, len);
				ss5->domain[len] = 0;
				buffer.read(&ss5->port, 2);
				packet_len += (2 + len + 1);
				ss5->port = ntohs(ss5->port);
				ss5->ip = 0;
				dbg_msg("get ss5 conn info: %s %d \n", ss5->domain, ss5->port);
				readall = true;
			}
		}
	}
	break;
	case 0x04:
	{
		// ip v6 
		return false;
	}
	break;
	default:
		break;
	}
	if (readall)
	{
		client->buffer.read(NULL, packet_len);
		assert(client->buffer.getBufferLen() == 0);
		dbg_msg("conn info read ok ~! \n");
		ss5_ConnectHost(client);
	}
	return true;
}

void onMsg(ClientContext * client, int msg)
{
	switch (msg)
	{
	case MSG_NEW_CLIENT:
	{
		ss5Context * s = new ss5Context;
		s->client_local = client;
		s->client_remote = NULL;
		s->state = STATE_INIT_CONN;
		client->lparam = s;
	}
		break;
	case MSG_CONNECT_OK:
	{
		dbg_msg("MSG_CONNECT_OK: %d \n", client->fd);
		ss5Context * ss5 = (ss5Context *)client->lparam;
		if (!ss5)
		{
			g_svr.disconnect(client);
			break;
		}
		unsigned char p[10] = {0x05,
		0x00,  // connect ok
		0x00,
		0x01,
		0x00,0x00,0x00,0x00,
		0x00,0x00};
		g_svr.send(ss5->client_local, p, sizeof(p));
		ss5->state = STATE_START_TRANS;
		assert(ss5->client_local->buffer.getBufferLen() == 0);
		assert(ss5->client_remote->buffer.getBufferLen() == 0);
	}
	break;
	case MSG_RECV:
	{
		dbg_msg("%d MSG_RECV:len : %d \n", client->fd, client->buffer.getBufferLen());
		ss5Context * s = (ss5Context *)client->lparam;
		if (!s) break;
		switch (s->state)
		{
		case STATE_INIT_CONN:

			if (client->buffer.getBufferLen() >= 3)
			{
				//请求长度是不固定的 最短是 3
				if (!ss5_ProcessReq(client))
				{
					g_svr.disconnect(client);
				}
				else
				{
					unsigned char p[2] = { 0x05,0x00 };
					if (strlen(g_ss5_user))
					{
						p[1] = 0x02;
						s->state = STATE_RECV_AUTH;
					}
					else
					{
						s->state = STATE_RECV_CONNINFO;
					}
					g_svr.send(client, p, 2);
					
				}
			}
			break;
		case STATE_RECV_AUTH:
		{
			dbg_msg("state: STATE_RECV_AUTH \n");
		}
		break;
		case STATE_RECV_CONNINFO:
		{
			dbg_msg("%d: state: STATE_RECV_CONNINFO \n", client->fd);
			if (!ss5_ParseConnInfo(client))
			{
				dbg_msg("!!!!!!! error get conn info %d\n", client->fd);
				g_svr.disconnect(client);
			}
		}
		break;
		case STATE_START_TRANS:
		{
			dbg_msg("%d state: STATE_START_TRANS \n", client->fd);
			dbg_msg("trans: %d <==> %d \n", s->client_local->fd, s->client_remote->fd);
			if (client == s->client_local)
			{
				unsigned char buff[8192];
				while (client->buffer.getBufferLen())
				{
					int bytes = client->buffer.read(buff, sizeof(buff));
					g_svr.send(s->client_remote, buff, bytes);
				}
			}
			else
			{
				unsigned char buff[8192];
				while (client->buffer.getBufferLen())
				{
					int bytes = client->buffer.read(buff, sizeof(buff));
					g_svr.send(s->client_local, buff, bytes);
				}
			}
		}
		break;
		default:
			break;
		}
	}
		break;
	case MSG_DISCONNECT:
	{
		dbg_msg("%d: MSG_DISCONNECT\n", client->fd);
		ss5Context * s = (ss5Context *)client->lparam;
		if (s)
		{
			if (client == s->client_local && s->client_remote)
			{
				s->client_remote->lparam = NULL;
				g_svr.disconnect(s->client_remote);
			}
			if (client == s->client_remote && s->client_local)
			{
				s->client_local->lparam = NULL;
				g_svr.disconnect(s->client_local);
			}
			delete s;
		}
	}
		break;
	default:
		break;
	}
	//dbg_msg("recv msg %d on fd %d    bytes:%d \n", msg, client->fd, client->msg_len);
}

void create_thread(void *(*func)(void *), void *lparam) {
	// CreateThread(NULL,0,func,lparam,0,NULL);
	pthread_t thread_id;
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	pthread_create(&thread_id, &attr, func, lparam);
	pthread_attr_destroy(&attr);
}

void *stop_thread(void * p)
{
	sleep(10);
	//s.stop();

	//c = s.connect("127.0.0.1", 8080);

	return NULL;
}

void IgnoreAllSignals() {
	struct sigaction sa;
	sa.sa_handler = SIG_IGN;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;

	for (int i = 0; i < 32; i++) {
		sigaction(i, &sa, 0);
	}

	//有几个信号是不能屏蔽的
	/*
	SIGBUS, SIGFPE, SIGILL,  SIGSEGV
	*/
	sa.sa_handler = SIG_DFL;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	sigaction(SIGBUS, &sa, 0);
	sigaction(SIGFPE, &sa, 0);
	sigaction(SIGILL, &sa, 0);
	sigaction(SIGSEGV, &sa, 0);
	// 不忽略这个 因为需要知道子进程的退出
	sigaction(SIGCHLD, &sa, 0);
}

int main()
{
	IgnoreAllSignals();
	//create_thread(stop_thread, NULL);
	if (g_svr.start(8080, onMsg))
	{
		while (g_svr.svrLoop());
	}
}