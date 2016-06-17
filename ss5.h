#pragma once
#define PROVER4 1
#define PROVER5 2

#define MAXBUFSIZE 8192
#define MAX_HOSTNAME 128

#pragma pack(push, 1) //取消内存大小自动对齐

struct Socks4Req {
	uint8_t Ver;
	uint8_t REP;
	uint16_t wPort;
	uint32_t dwIP;
	uint8_t other;
};

struct Socks5Req {
	uint8_t Ver;
	uint8_t nMethods;
	uint8_t Methods[255];
};

struct AuthReq {
	uint8_t Ver;
	uint8_t Ulen;
	uint8_t UserPass[1024];
};

typedef struct _Socks5Info {
	uint8_t Ver; // Version Number
	uint8_t CMD; // 0x01==TCP CONNECT,0x02==TCP BIND,0x03==UDP ASSOCIATE
	uint8_t RSV;
	uint8_t ATYP;
	uint8_t IP_LEN;
	uint8_t szIP;
} Socks5Info;

typedef struct _IPandPort {
	uint32_t dwIP;
	uint16_t wPort;
} IPandPort;

typedef struct _Socks5AnsConn {
	uint8_t Ver;
	uint8_t REP;
	uint8_t RSV;
	uint8_t ATYP;
	IPandPort ip_port;
} Socks5AnsConn;

typedef struct _Socks5UDPHead {
	uint8_t RSV[2];
	uint8_t FRAG;
	uint8_t ATYP;
	IPandPort ip_port;
	uint8_t DATA;
} Socks5UDPHead;

struct SocketInfo {
	int socks;
	IPandPort ip_port;
};

typedef struct {
	SocketInfo Local;
	SocketInfo Client;
	SocketInfo Server;
} Socks5Para;

#pragma pack(pop)