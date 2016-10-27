/*
Copyright (C) 1996-2001 Id Software, Inc.
Copyright (C) 2010-2014 QuakeSpasm developers

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
*/

#include "q_stdinc.h"
#include "arch_def.h"
#include "net_sys.h"
#include "quakedef.h"
#include "net_defs.h"

//ipv4 defs
static sys_socket_t netv4_acceptsocket = INVALID_SOCKET;	// socket for fielding new connections
static sys_socket_t netv4_controlsocket;
static sys_socket_t netv4_broadcastsocket = 0;
static struct sockaddr_in broadcastaddrv4;
static in_addr_t	myAddrv4, bindAddrv4;	//spike --keeping separate bind and detected values.

//ipv6 defs
#ifdef IPPROTO_IPV6
typedef struct in_addr6 in_addr6_t;
static sys_socket_t netv6_acceptsocket = INVALID_SOCKET;	// socket for fielding new connections
static sys_socket_t netv6_controlsocket;
static struct sockaddr_in6 broadcastaddrv6;
static in_addr6_t	myAddrv6, bindAddrv6;
#ifndef IPV6_V6ONLY
	#define IPV6_V6ONLY 27
#endif
#endif


#include "net_wins.h"

int winsock_initialized = 0;
WSADATA		winsockdata;
#define __wsaerr_static			/* not static: used by net_wipx.c too */
#include "wsaerror.h"

//=============================================================================

static double	blocktime;

static INT_PTR PASCAL FAR BlockingHook (void)
{
	MSG	msg;
	BOOL	ret;

	if ((Sys_DoubleTime() - blocktime) > 2.0)
	{
		WSACancelBlockingCall();
		return FALSE;
	}

	/* get the next message, if any */
	ret = (BOOL) PeekMessage(&msg, NULL, 0, 0, PM_REMOVE);

	/* if we got one, process it */
	if (ret)
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	/* TRUE if we got a message */
	return ret;
}


static void WINIPv4_GetLocalAddress (void)
{
	struct hostent	*local = NULL;
	char		buff[MAXHOSTNAMELEN];
	in_addr_t	addr;
	int		err;

	if (myAddrv4 != INADDR_ANY)
		return;

	if (gethostname(buff, MAXHOSTNAMELEN) == SOCKET_ERROR)
	{
		err = SOCKETERRNO;
		Con_SafePrintf("WINIPv4_GetLocalAddress: gethostname failed (%s)\n",
							socketerror(err));
		return;
	}

	buff[MAXHOSTNAMELEN - 1] = 0;
	blocktime = Sys_DoubleTime();
	WSASetBlockingHook(BlockingHook);
	local = gethostbyname(buff);
	err = WSAGetLastError();
	WSAUnhookBlockingHook();
	if (local == NULL)
	{
		Con_SafePrintf("WINIPv4_GetLocalAddress: gethostbyname failed (%s)\n",
							__WSAE_StrError(err));
		return;
	}

	myAddrv4 = *(in_addr_t *)local->h_addr_list[0];

	addr = ntohl(myAddrv4);
	sprintf(my_ipv4_address, "%ld.%ld.%ld.%ld", (addr >> 24) & 0xff, (addr >> 16) & 0xff, (addr >> 8) & 0xff, addr & 0xff);
}


sys_socket_t WINIPv4_Init (void)
{
	int	i, err;
	char	buff[MAXHOSTNAMELEN];

	if (COM_CheckParm ("-noudp") || COM_CheckParm ("-noudp4"))
		return -1;

	if (winsock_initialized == 0)
	{
		err = WSAStartup(MAKEWORD(1,1), &winsockdata);
		if (err != 0)
		{
			Con_SafePrintf("Winsock initialization failed (%s)\n",
					socketerror(err));
			return INVALID_SOCKET;
		}
	}
	winsock_initialized++;

	// determine my name & address
	if (gethostname(buff, MAXHOSTNAMELEN) != 0)
	{
		err = SOCKETERRNO;
		Con_SafePrintf("WINS_Init: gethostname failed (%s)\n",
							socketerror(err));
	}
	else
	{
		buff[MAXHOSTNAMELEN - 1] = 0;
	}
	i = COM_CheckParm ("-ip");
	if (i)
	{
		if (i < com_argc-1)
		{
			bindAddrv4 = inet_addr(com_argv[i+1]);
			if (bindAddrv4 == INADDR_NONE)
				Sys_Error ("%s is not a valid IP address", com_argv[i+1]);
			strcpy(my_ipv4_address, com_argv[i+1]);
		}
		else
		{
			Sys_Error ("NET_Init: you must specify an IP address after -ip");
		}
	}
	else
	{
		bindAddrv4 = INADDR_ANY;
		strcpy(my_ipv4_address, "INADDR_ANY");
	}

	myAddrv4 = bindAddrv4;

	if ((netv4_controlsocket = WINIPv4_OpenSocket(0)) == INVALID_SOCKET)
	{
		Con_SafePrintf("WINS_Init: Unable to open control socket, UDP disabled\n");
		if (--winsock_initialized == 0)
			WSACleanup ();
		return INVALID_SOCKET;
	}

	broadcastaddrv4.sin_family = AF_INET;
	broadcastaddrv4.sin_addr.s_addr = INADDR_BROADCAST;
	broadcastaddrv4.sin_port = htons((unsigned short)net_hostport);

	Con_SafePrintf("UDP Initialized\n");
	ipv4Available = true;

	return netv4_controlsocket;
}

//=============================================================================

void WINIPv4_Shutdown (void)
{
	WINIPv4_Listen (false);
	WINS_CloseSocket (netv4_controlsocket);
	if (--winsock_initialized == 0)
		WSACleanup ();
}

//=============================================================================

sys_socket_t WINIPv4_Listen (qboolean state)
{
	// enable listening
	if (state)
	{
		if (netv4_acceptsocket != INVALID_SOCKET)
			return netv4_acceptsocket;
		WINIPv4_GetLocalAddress();
		if ((netv4_acceptsocket = WINIPv4_OpenSocket (net_hostport)) == INVALID_SOCKET)
			Sys_Error ("WINS_Listen: Unable to open accept socket");
		return netv4_acceptsocket;
	}

	// disable listening
	if (netv4_acceptsocket == INVALID_SOCKET)
		return INVALID_SOCKET;
	WINS_CloseSocket (netv4_acceptsocket);
	netv4_acceptsocket = INVALID_SOCKET;
	return INVALID_SOCKET;
}

//=============================================================================

sys_socket_t WINIPv4_OpenSocket (int port)
{
	sys_socket_t newsocket;
	struct sockaddr_in address;
	u_long _true = 1;
	int err;

	if ((newsocket = socket (PF_INET, SOCK_DGRAM, IPPROTO_UDP)) == INVALID_SOCKET)
	{
		err = SOCKETERRNO;
		Con_SafePrintf("WINS_OpenSocket: %s\n", socketerror(err));
		return INVALID_SOCKET;
	}

	if (ioctlsocket (newsocket, FIONBIO, &_true) == SOCKET_ERROR)
		goto ErrorReturn;

	memset(&address, 0, sizeof(struct sockaddr_in));
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = bindAddrv4;
	address.sin_port = htons((unsigned short)port);
	if (bind (newsocket, (struct sockaddr *)&address, sizeof(address)) == 0)
		return newsocket;

	if (ipv4Available)
	{
		err = SOCKETERRNO;
		Sys_Error ("Unable to bind to %s (%s)",
				WINS_AddrToString ((struct qsockaddr *) &address, false),
				socketerror(err));
		return INVALID_SOCKET;	/* not reached */
	}
	/* else: we are still in init phase, no need to error */

ErrorReturn:
	err = SOCKETERRNO;
	Con_SafePrintf("WINS_OpenSocket: %s\n", socketerror(err));
	closesocket (newsocket);
	return INVALID_SOCKET;
}

//=============================================================================

int WINS_CloseSocket (sys_socket_t socketid)
{
	if (socketid == netv4_broadcastsocket)
		netv4_broadcastsocket = 0;
	return closesocket (socketid);
}

//=============================================================================

/*
============
PartialIPAddress

this lets you type only as much of the net address as required, using
the local network components to fill in the rest
============
*/
static int PartialIPAddress (const char *in, struct qsockaddr *hostaddr)
{
	char	buff[256];
	char	*b;
	int	addr, mask, num, port, run;

	buff[0] = '.';
	b = buff;
	strcpy(buff+1, in);
	if (buff[1] == '.')
		b++;

	addr = 0;
	mask = -1;
	while (*b == '.')
	{
		b++;
		num = 0;
		run = 0;
		while (!( *b < '0' || *b > '9'))
		{
			num = num*10 + *b++ - '0';
			if (++run > 3)
				return -1;
		}
		if ((*b < '0' || *b > '9') && *b != '.' && *b != ':' && *b != 0)
			return -1;
		if (num < 0 || num > 255)
			return -1;
		mask <<= 8;
		addr = (addr<<8) + num;
	}

	if (*b++ == ':')
		port = Q_atoi(b);
	else
		port = net_hostport;

	hostaddr->qsa_family = AF_INET;
	((struct sockaddr_in *)hostaddr)->sin_port = htons((unsigned short)port);
	((struct sockaddr_in *)hostaddr)->sin_addr.s_addr =
					(myAddrv4 & htonl(mask)) | htonl(addr);

	return 0;
}

//=============================================================================

int WINS_Connect (sys_socket_t socketid, struct qsockaddr *addr)
{
	return 0;
}

//=============================================================================

sys_socket_t WINIPv4_CheckNewConnections (void)
{
	char		buf[4096];

	if (netv4_acceptsocket == INVALID_SOCKET)
		return INVALID_SOCKET;

	if (recvfrom (netv4_acceptsocket, buf, sizeof(buf), MSG_PEEK, NULL, NULL)
								!= SOCKET_ERROR)
	{
		return netv4_acceptsocket;
	}
	return INVALID_SOCKET;
}

//=============================================================================

int WINS_Read (sys_socket_t socketid, byte *buf, int len, struct qsockaddr *addr)
{
	socklen_t addrlen = sizeof(struct qsockaddr);
	int ret;

	ret = recvfrom (socketid, (char *)buf, len, 0, (struct sockaddr *)addr, &addrlen);
	if (ret == SOCKET_ERROR)
	{
		int err = SOCKETERRNO;
		if (err == NET_EWOULDBLOCK || err == NET_ECONNREFUSED)
			return 0;
		if (err == WSAECONNRESET)
			Con_DPrintf ("WINS_Read, recvfrom: %s (%s)\n", socketerror(err), WINS_AddrToString(addr, false));
		else
			Con_SafePrintf ("WINS_Read, recvfrom: %s\n", socketerror(err));
	}
	return ret;
}

//=============================================================================

static int WINS_MakeSocketBroadcastCapable (sys_socket_t socketid)
{
	int	i = 1;

	// make this socket broadcast capable
	if (setsockopt(socketid, SOL_SOCKET, SO_BROADCAST, (char *)&i, sizeof(i))
								 == SOCKET_ERROR)
	{
		int err = SOCKETERRNO;
		Con_SafePrintf ("UDP, setsockopt: %s\n", socketerror(err));
		return -1;
	}
	netv4_broadcastsocket = socketid;

	return 0;
}

//=============================================================================

int WINIPv4_Broadcast (sys_socket_t socketid, byte *buf, int len)
{
	int	ret;

	if (socketid != netv4_broadcastsocket)
	{
		if (netv4_broadcastsocket != 0)
			Sys_Error("Attempted to use multiple broadcasts sockets");
		WINIPv4_GetLocalAddress();
		ret = WINS_MakeSocketBroadcastCapable (socketid);
		if (ret == -1)
		{
			Con_Printf("Unable to make socket broadcast capable\n");
			return ret;
		}
	}

	return WINS_Write (socketid, buf, len, (struct qsockaddr *)&broadcastaddrv4);
}

//=============================================================================

int WINS_Write (sys_socket_t socketid, byte *buf, int len, struct qsockaddr *addr)
{
	int	ret;

	ret = sendto (socketid, (char *)buf, len, 0, (struct sockaddr *)addr,
							sizeof(struct qsockaddr));
	if (ret == SOCKET_ERROR)
	{
		int err = SOCKETERRNO;
		if (err == NET_EWOULDBLOCK)
			return 0;
		Con_SafePrintf ("WINS_Write, sendto: %s\n", socketerror(err));
	}
	return ret;
}

//=============================================================================

const char *WINS_AddrToString (struct qsockaddr *addr, qboolean masked)
{
	static char buffer[64];
	int		haddr;

#ifdef IPPROTO_IPV6
	if (addr->qsa_family == AF_INET6)
	{
		if (masked)
		{
			q_snprintf(buffer, sizeof(buffer), "[%x:%x:%x:%x::]/64", 
						ntohs(((struct sockaddr_in6 *)addr)->sin6_addr.u.Word[0]),
						ntohs(((struct sockaddr_in6 *)addr)->sin6_addr.u.Word[1]),
						ntohs(((struct sockaddr_in6 *)addr)->sin6_addr.u.Word[2]),
						ntohs(((struct sockaddr_in6 *)addr)->sin6_addr.u.Word[3]));
		}
		else
		{
			if (((struct sockaddr_in6 *)addr)->sin6_scope_id)
			{
				q_snprintf(buffer, sizeof(buffer), "[%x:%x:%x:%x:%x:%x:%x:%x%%%i]:%d", 
						ntohs(((struct sockaddr_in6 *)addr)->sin6_addr.u.Word[0]),
						ntohs(((struct sockaddr_in6 *)addr)->sin6_addr.u.Word[1]),
						ntohs(((struct sockaddr_in6 *)addr)->sin6_addr.u.Word[2]),
						ntohs(((struct sockaddr_in6 *)addr)->sin6_addr.u.Word[3]),
						ntohs(((struct sockaddr_in6 *)addr)->sin6_addr.u.Word[4]),
						ntohs(((struct sockaddr_in6 *)addr)->sin6_addr.u.Word[5]),
						ntohs(((struct sockaddr_in6 *)addr)->sin6_addr.u.Word[6]),
						ntohs(((struct sockaddr_in6 *)addr)->sin6_addr.u.Word[7]),
						(int)((struct sockaddr_in6 *)addr)->sin6_scope_id,
						ntohs(((struct sockaddr_in6 *)addr)->sin6_port));
			}
			else
			{
				q_snprintf(buffer, sizeof(buffer), "[%x:%x:%x:%x:%x:%x:%x:%x]:%d", 
						ntohs(((struct sockaddr_in6 *)addr)->sin6_addr.u.Word[0]),
						ntohs(((struct sockaddr_in6 *)addr)->sin6_addr.u.Word[1]),
						ntohs(((struct sockaddr_in6 *)addr)->sin6_addr.u.Word[2]),
						ntohs(((struct sockaddr_in6 *)addr)->sin6_addr.u.Word[3]),
						ntohs(((struct sockaddr_in6 *)addr)->sin6_addr.u.Word[4]),
						ntohs(((struct sockaddr_in6 *)addr)->sin6_addr.u.Word[5]),
						ntohs(((struct sockaddr_in6 *)addr)->sin6_addr.u.Word[6]),
						ntohs(((struct sockaddr_in6 *)addr)->sin6_addr.u.Word[7]),
						ntohs(((struct sockaddr_in6 *)addr)->sin6_port));
			}
		}
	}
	else
#endif
	{
		haddr = ntohl(((struct sockaddr_in *)addr)->sin_addr.s_addr);
		if (masked)
		{
			sprintf(buffer, "%d.%d.%d.0/24", (haddr >> 24) & 0xff,
					  (haddr >> 16) & 0xff, (haddr >> 8) & 0xff);
		}
		else
		{
			sprintf(buffer, "%d.%d.%d.%d:%d", (haddr >> 24) & 0xff,
					  (haddr >> 16) & 0xff, (haddr >> 8) & 0xff, haddr & 0xff,
					  ntohs(((struct sockaddr_in *)addr)->sin_port));
		}
	}
	return buffer;
}

//=============================================================================

int WINIPv4_StringToAddr (const char *string, struct qsockaddr *addr)
{
	int	ha1, ha2, ha3, ha4, hp, ipaddr;

	sscanf(string, "%d.%d.%d.%d:%d", &ha1, &ha2, &ha3, &ha4, &hp);
	ipaddr = (ha1 << 24) | (ha2 << 16) | (ha3 << 8) | ha4;

	addr->qsa_family = AF_INET;
	((struct sockaddr_in *)addr)->sin_addr.s_addr = htonl(ipaddr);
	((struct sockaddr_in *)addr)->sin_port = htons((unsigned short)hp);
	return 0;
}

//=============================================================================

int WINS_GetSocketAddr (sys_socket_t socketid, struct qsockaddr *addr)
{
	socklen_t addrlen = sizeof(struct qsockaddr);

	memset(addr, 0, sizeof(struct qsockaddr));
	getsockname(socketid, (struct sockaddr *)addr, &addrlen);

	if (addr->qsa_family == AF_INET)
	{
		in_addr_t a = ((struct sockaddr_in *)addr)->sin_addr.s_addr;
		if (a == 0 || a == htonl(INADDR_LOOPBACK))
			((struct sockaddr_in *)addr)->sin_addr.s_addr = myAddrv4;
	}
#ifdef IPPROTO_IPV6
	if (addr->qsa_family == AF_INET6)
	{
		static const in_addr6_t in6addr_any = IN6ADDR_ANY_INIT;
		if (!memcmp(&((struct sockaddr_in6 *)addr)->sin6_addr, &in6addr_any, sizeof(in_addr6_t)))
			memcpy(&((struct sockaddr_in6 *)addr)->sin6_addr, &myAddrv6, sizeof(struct sockaddr_in6));
	}
#endif

	return 0;
}

//=============================================================================

int WINIPv4_GetNameFromAddr (struct qsockaddr *addr, char *name)
{
	struct hostent *hostentry;

	hostentry = gethostbyaddr ((char *)&((struct sockaddr_in *)addr)->sin_addr,
						sizeof(struct in_addr), AF_INET);
	if (hostentry)
	{
		Q_strncpy (name, (char *)hostentry->h_name, NET_NAMELEN - 1);
		return 0;
	}

	Q_strcpy (name, WINS_AddrToString (addr, false));
	return 0;
}

//=============================================================================

int WINIPv4_GetAddrFromName (const char *name, struct qsockaddr *addr)
{
	struct hostent *hostentry;
	char *colon;
	unsigned short port = net_hostport;

	if (name[0] >= '0' && name[0] <= '9')
		return PartialIPAddress (name, addr);

	colon = strrchr(name, ':');
	if (colon)
	{
		char dupe[MAXHOSTNAMELEN];
		if (colon-name+1 > MAXHOSTNAMELEN)
			return -1;
		memcpy(dupe, name, colon-name);
		dupe[colon-name] = 0;
		if (strchr(dupe, ':'))
			return -1;	//don't resolve a name to an ipv4 address if it has multiple colons in it. its probably an ipx or ipv6 address, and I'd rather not block on any screwed dns resolves
		hostentry = gethostbyname (dupe);
		port = strtoul(colon+1, NULL, 10);
	}
	else
		hostentry = gethostbyname (name);
	if (!hostentry)
		return -1;

	addr->qsa_family = AF_INET;
	((struct sockaddr_in *)addr)->sin_port = htons(port);
	((struct sockaddr_in *)addr)->sin_addr.s_addr =
						*(in_addr_t *)hostentry->h_addr_list[0];

	return 0;
}

//=============================================================================

int WINS_AddrCompare (struct qsockaddr *addr1, struct qsockaddr *addr2)
{
	if (addr1->qsa_family != addr2->qsa_family)
		return -1;

#ifdef IPPROTO_IPV6
	if (addr1->qsa_family == AF_INET6)
	{
		if (memcmp(&((struct sockaddr_in6 *)addr1)->sin6_addr, &((struct sockaddr_in6 *)addr2)->sin6_addr, sizeof(((struct sockaddr_in6 *)addr2)->sin6_addr)))
			return -1;

		if (((struct sockaddr_in6 *)addr1)->sin6_port !=
			((struct sockaddr_in6 *)addr2)->sin6_port)
			return 1;
		if (((struct sockaddr_in6 *)addr1)->sin6_scope_id &&
			((struct sockaddr_in6 *)addr2)->sin6_scope_id &&
			((struct sockaddr_in6 *)addr1)->sin6_scope_id !=
			((struct sockaddr_in6 *)addr2)->sin6_scope_id)	//the ipv6 scope id is for use with link-local addresses, to identify the specific interface.
			return 1;
	}
	else
#endif
	{
		if (((struct sockaddr_in *)addr1)->sin_addr.s_addr !=
			((struct sockaddr_in *)addr2)->sin_addr.s_addr)
			return -1;

		if (((struct sockaddr_in *)addr1)->sin_port !=
			((struct sockaddr_in *)addr2)->sin_port)
			return 1;
	}

	return 0;
}

//=============================================================================

int WINS_GetSocketPort (struct qsockaddr *addr)
{
#ifdef IPPROTO_IPV6
	if (addr->qsa_family == AF_INET6)
		return ntohs(((struct sockaddr_in6 *)addr)->sin6_port);
	else
#endif
		return ntohs(((struct sockaddr_in *)addr)->sin_port);
}


int WINS_SetSocketPort (struct qsockaddr *addr, int port)
{
#ifdef IPPROTO_IPV6
	if (addr->qsa_family == AF_INET6)
		((struct sockaddr_in6 *)addr)->sin6_port = htons((unsigned short)port);
	else
#endif
		((struct sockaddr_in *)addr)->sin_port = htons((unsigned short)port);
	return 0;
}

//=============================================================================


#ifdef IPPROTO_IPV6
//winxp (and possibly win2k) is dual stack.
//vista+ has a hybrid stack

static void WINIPv6_GetLocalAddress (void)
{
	char		buff[MAXHOSTNAMELEN];
	int		err;
	struct addrinfo hints, *local = NULL;

//	if (myAddrv6 != IN6ADDR_ANY)
//		return;

	if (gethostname(buff, MAXHOSTNAMELEN) == SOCKET_ERROR)
	{
		err = SOCKETERRNO;
		Con_SafePrintf("WINIPv6_GetLocalAddress: gethostname failed (%s)\n",
							socketerror(err));
		return;
	}
	buff[MAXHOSTNAMELEN - 1] = 0;

	blocktime = Sys_DoubleTime();
	WSASetBlockingHook(BlockingHook);

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET6;
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_protocol = IPPROTO_UDP;
	if (getaddrinfo(buff, NULL, &hints, &local) == 0)
	{
		size_t l;
		q_strlcpy(my_ipv6_address, WINS_AddrToString((struct qsockaddr*)local->ai_addr, false), sizeof(my_ipv6_address));
		l = strlen(my_ipv6_address);
		if (l > 2 && !strcmp(my_ipv6_address+l-2, ":0"))
			my_ipv6_address[l-2] = 0;
		freeaddrinfo(local);
	}
	err = WSAGetLastError();
	WSAUnhookBlockingHook();

	if (local == NULL)
	{
		Con_SafePrintf("WINIPv6_GetLocalAddress: gethostbyname failed (%s)\n",
							__WSAE_StrError(err));
		return;
	}
}

sys_socket_t WINIPv6_Init (void)
{
	int	i;
	char	buff[MAXHOSTNAMELEN];

	if (COM_CheckParm ("-noudp") || COM_CheckParm ("-noudp6"))
		return -1;

	if (winsock_initialized == 0)
	{
		int err = WSAStartup(MAKEWORD(2,2), &winsockdata);
		if (err != 0)
		{
			Con_SafePrintf("Winsock initialization failed (%s)\n",
					socketerror(err));
			return INVALID_SOCKET;
		}
	}
	winsock_initialized++;

	// determine my name & address
	if (gethostname(buff, MAXHOSTNAMELEN) != 0)
	{
		int err = SOCKETERRNO;
		Con_SafePrintf("WINIPv6_Init: gethostname failed (%s)\n",
							socketerror(err));
	}
	else
	{
		buff[MAXHOSTNAMELEN - 1] = 0;
	}
	i = COM_CheckParm ("-ip6");
	if (i)
	{
		if (i < com_argc-1)
		{
			if (WINIPv6_GetAddrFromName(com_argv[i+1], (struct qsockaddr*)&bindAddrv6))
				Sys_Error ("%s is not a valid IPv6 address", com_argv[i+1]);
			if (!*my_ipv6_address)
				strcpy(my_ipv6_address, com_argv[i+1]);
		}
		else
		{
			Sys_Error ("WINIPv6_Init: you must specify an IP address after -ip");
		}
	}
	else
	{
		memset(&bindAddrv6, 0, sizeof(bindAddrv6));
		if (!*my_ipv6_address)
		{
			strcpy(my_ipv6_address, "[::]");
			WINIPv6_GetLocalAddress();
		}
	}

	myAddrv6 = bindAddrv6;

	if ((netv6_controlsocket = WINIPv6_OpenSocket(0)) == INVALID_SOCKET)
	{
		Con_SafePrintf("WINIPv6_Init: Unable to open control socket, UDP disabled\n");
		if (--winsock_initialized == 0)
			WSACleanup ();
		return INVALID_SOCKET;
	}

	broadcastaddrv6.sin6_family = AF_INET6;
	memset(&broadcastaddrv6.sin6_addr, 0, sizeof(broadcastaddrv6.sin6_addr));
	broadcastaddrv6.sin6_addr.u.Byte[0] = 0xff;
	broadcastaddrv6.sin6_addr.u.Byte[1] = 0x03;
	broadcastaddrv6.sin6_addr.u.Byte[15] = 0x01;
	broadcastaddrv6.sin6_port = htons((unsigned short)net_hostport);

	Con_SafePrintf("IPv6 UDP Initialized\n");
	ipv6Available = true;

	return netv6_controlsocket;
}

sys_socket_t WINIPv6_Listen (qboolean state)
{
	if (state)
	{
		// enable listening
		if (netv6_acceptsocket == INVALID_SOCKET)
		{
			if ((netv6_acceptsocket = WINIPv6_OpenSocket (net_hostport)) == INVALID_SOCKET)
				Sys_Error ("WINIPv6_Listen: Unable to open accept socket");
		}
	}
	else
	{
		// disable listening
		if (netv6_acceptsocket != INVALID_SOCKET)
		{
			WINS_CloseSocket (netv6_acceptsocket);
			netv6_acceptsocket = INVALID_SOCKET;
		}
	}
	return netv6_acceptsocket;
}
void WINIPv6_Shutdown (void)
{
	WINIPv6_Listen(false);
	WINS_CloseSocket (netv6_controlsocket);
	if (--winsock_initialized == 0)
		WSACleanup ();
}
sys_socket_t  WINIPv6_OpenSocket (int port)
{
	sys_socket_t newsocket;
	struct sockaddr_in6 address;
	u_long _true = 1;
	int err;

	if ((newsocket = socket (PF_INET6, SOCK_DGRAM, IPPROTO_UDP)) == INVALID_SOCKET)
	{
		err = SOCKETERRNO;
		Con_SafePrintf("WINS_OpenSocket: %s\n", socketerror(err));
		return INVALID_SOCKET;
	}

	setsockopt(newsocket, IPPROTO_IPV6, IPV6_V6ONLY, (char *)&_true, sizeof(_true));

	if (ioctlsocket (newsocket, FIONBIO, &_true) == SOCKET_ERROR)
		goto ErrorReturn;

	memset(&address, 0, sizeof(address));
	address.sin6_family = AF_INET6;
	address.sin6_addr = bindAddrv6;
	address.sin6_port = htons((unsigned short)port);
	if (bind (newsocket, (struct sockaddr *)&address, sizeof(address)) == 0)
	{
		//we don't know if we're the server or not. oh well.
		struct ipv6_mreq req;
		req.ipv6mr_multiaddr = broadcastaddrv6.sin6_addr;
		req.ipv6mr_interface = 0;
		setsockopt(newsocket, IPPROTO_IPV6, IPV6_JOIN_GROUP, (char *)&req, sizeof(req));


		return newsocket;
	}

	if (ipv6Available)
	{
		err = SOCKETERRNO;
		Sys_Error ("Unable to bind to %s (%s)",
				WINS_AddrToString ((struct qsockaddr *) &address, false),
				socketerror(err));
		return INVALID_SOCKET;	/* not reached */
	}
	/* else: we are still in init phase, no need to error */

ErrorReturn:
	err = SOCKETERRNO;
	Con_SafePrintf("WINS_OpenSocket: %s\n", socketerror(err));
	closesocket (newsocket);
	return INVALID_SOCKET;
}
sys_socket_t  WINIPv6_CheckNewConnections (void)
{
	char		buf[4096];

	if (netv6_acceptsocket == INVALID_SOCKET)
		return INVALID_SOCKET;

	if (recvfrom (netv6_acceptsocket, buf, sizeof(buf), MSG_PEEK, NULL, NULL)
								!= SOCKET_ERROR)
	{
		return netv6_acceptsocket;
	}
	return INVALID_SOCKET;
}
int  WINIPv6_Broadcast (sys_socket_t socketid, byte *buf, int len)
{
	broadcastaddrv6.sin6_port = htons((unsigned short)net_hostport);
	return WINS_Write(socketid, buf, len, (struct qsockaddr*)&broadcastaddrv6);
}
int  WINIPv6_StringToAddr (const char *string, struct qsockaddr *addr)
{	//This is never actually called...
	return -1;
}
int  WINIPv6_GetNameFromAddr (struct qsockaddr *addr, char *name)
{
	//FIXME: should really do a reverse dns lookup.
	q_strlcpy(name, WINS_AddrToString(addr, false), NET_NAMELEN);
	return 0;
}
int  WINIPv6_GetAddrFromName (const char *name, struct qsockaddr *addr)
{
	struct addrinfo *addrinfo = NULL;
	struct addrinfo *pos;
	struct addrinfo udp6hint;
	int error;
	char *port;
	char dupbase[256];
	size_t len;
	qboolean success = false;

	memset(&udp6hint, 0, sizeof(udp6hint));
	udp6hint.ai_family = 0;//Any... we check for AF_INET6 or 4
	udp6hint.ai_socktype = SOCK_DGRAM;
	udp6hint.ai_protocol = IPPROTO_UDP;

	if (*name == '[')
	{
		port = strstr(name, "]");
		if (!port)
			error = EAI_NONAME;
		else
		{
			len = port - (name+1);
			if (len >= sizeof(dupbase))
				len = sizeof(dupbase)-1;
			strncpy(dupbase, name+1, len);
			dupbase[len] = '\0';
			error = getaddrinfo(dupbase, (port[1] == ':')?port+2:NULL, &udp6hint, &addrinfo);
		}
	}
	else
	{
		port = strrchr(name, ':');

		if (port)
		{
			len = port - name;
			if (len >= sizeof(dupbase))
				len = sizeof(dupbase)-1;
			strncpy(dupbase, name, len);
			dupbase[len] = '\0';
			error = getaddrinfo(dupbase, port+1, &udp6hint, &addrinfo);
		}
		else
			error = EAI_NONAME;
		if (error)	//failed, try string with no port.
			error = getaddrinfo(name, NULL, &udp6hint, &addrinfo);	//remember, this func will return any address family that could be using the udp protocol... (ip4 or ip6)
	}

	if (!error)
	{
		((struct sockaddr*)addr)->sa_family = 0;
		for (pos = addrinfo; pos; pos = pos->ai_next)
		{
			if (0)//pos->ai_family == AF_INET)
			{
				memcpy(addr, pos->ai_addr, pos->ai_addrlen);
				success = true;
				break;
			}
			if (pos->ai_family == AF_INET6 && !success)
			{
				memcpy(addr, pos->ai_addr, pos->ai_addrlen);
				success = true;
			}
		}
		freeaddrinfo (addrinfo);
	}

	if (success)
	{
		if (((struct sockaddr*)addr)->sa_family == AF_INET)
		{
			if (!((struct sockaddr_in *)addr)->sin_port)
				((struct sockaddr_in *)addr)->sin_port = htons(net_hostport);
		}
		else if (((struct sockaddr*)addr)->sa_family == AF_INET6)
		{
			if (!((struct sockaddr_in6 *)addr)->sin6_port)
				((struct sockaddr_in6 *)addr)->sin6_port = htons(net_hostport);
		}
		return 0;
	}
	return -1;
}
#endif
