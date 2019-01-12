/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Foobar; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/
// unix_net.c

#include "../game/q_shared.h"
#include "../qcommon/qcommon.h"

#include <thread.h>

Channel *echan, *lchan;
netadr_t *net_from, cons[2*MAX_CLIENTS];

static char *netmtpt = "/net";
static int afd = -1, lpid;

char	*NET_BaseAdrToString (netadr_t *a)
{
	return a->addr;
}

qboolean
Sys_StringToAdr(char *s, netadr_t *a)
{
	int fd, n;
	char buf[128], *f[4], *p;

	snprint(buf, sizeof buf, "%s/cs", netmtpt);
	if((fd = open(buf, ORDWR)) < 0)
		sysfatal("open: %r");
	if((p = strrchr(s, '!')) == nil && (p = strrchr(s, ':')) == nil)
		p = "27960";
	else
		p++;
	snprint(buf, sizeof buf, "udp!%s!%s", s, p);
	n = strlen(buf);
	if(write(fd, buf, n) != n){
		fprint(2, "translating %s: %r\n", s);
		return -1;
	}
	seek(fd, 0, 0);
	if((n = read(fd, buf, sizeof(buf)-1)) <= 0){
		fprint(2, "reading cs tables: %r");
		return -1;
	}
	buf[n] = 0;
	close(fd);
	if(getfields(buf, f, 4, 0, " !") < 2)
		goto err;
	memset(a, 0, sizeof *a);
	strncpy(a->addr, f[1], sizeof(a->addr)-1);
	strncpy(a->srv, f[2], sizeof(a->srv)-1);
	snprint(a->sys, sizeof a->sys, "%s!%s", f[1], f[2]);
	return qtrue;
err:
	fprint(2, "bad cs entry %s", buf);
	return qfalse;
}

static void
getinfo(netadr_t *a)
{
	NetConnInfo *nc;

	if((nc = getnetconninfo(nil, a->fd)) == nil){
		fprint(2, "getnetconninfo: %r\n");
		return;
	}
	strncpy(a->addr, nc->raddr, sizeof(a->addr)-1);
	strncpy(a->srv, nc->rserv, sizeof(a->srv)-1);
	snprint(a->sys, sizeof a->sys, "%s!%s", a->addr, a->srv);
	free(nc);
}

qboolean
Sys_GetPacket(msg_t *net_message)
{
	int n, fd;
	netadr_t *a;

	if(svonly && nbrecv(lchan, &fd) > 0){
		for(a=cons; a<cons+nelem(cons); a++)
			if(a->fd < 0)
				break;
		if(a == cons + nelem(cons)){
			close(fd);
			return qfalse;
		}
		a->fd = fd;
		getinfo(a);
	}else{
		for(a=cons; a<cons+nelem(cons); a++)
			if(a->fd >= 0 && flen(a->fd) > 0)
				break;
		if(a == cons + nelem(cons))
			return qfalse;
	}
	net_message->readcount = 0;
	if((n = read(a->fd, net_message->data, net_message->maxsize)) <= 0){
		fprint(2, "read: %r\n");
		return qfalse;
	}
	if(n == net_message->maxsize){
		Com_Printf("Oversize packet from %s\n", a->sys);
		return qfalse;
	}
	net_message->cursize = n;
	net_from = a;
	return qtrue;
}

void
Sys_SendPacket(int length, void *data, netadr_t *to)
{
	netadr_t *a;

	if(to->type != NA_IP && to->type != NA_BROADCAST){
		Com_Error(ERR_FATAL, "NET_SendPacket: bad address type");
		return;
	}
	for(a=cons; a<cons+nelem(cons); a++)
		if(strcmp(a->sys, to->sys) == 0 || a->fd < 0)
			break;
	if(a == cons + nelem(cons))
		return;
	else if(a->fd < 0){
		memcpy(a, to, sizeof *a);
		if((a->fd = dial(netmkaddr(a->addr, "udp", a->srv), nil, nil, nil)) < 0){
			fprint(2, "dial: %r\n");
			return;
		}
	}
	if(write(a->fd, data, length) != length){
		fprint(2, "write: %r\n");
		Com_Printf("NET_SendPacket %s: ERROR\n", to->sys);
	}
}

qboolean
Sys_IsLANAddress(netadr_t *a)
{
	if(a->type == NA_LOOPBACK)
		return qtrue;
	return qfalse;
}

void
Sys_ShowIP(void)
{
}

void
NET_Shutdown(void)
{
	netadr_t *a;

	for(a=cons; a<cons+nelem(cons); a++)
		if(a->fd >= 0){
			close(a->fd);
			a->fd = -1;
		}
	if(afd < 0)
		return;
	close(afd);
	threadkill(lpid);
	afd = -1;
}

static void
lproc(void *)
{
	int fd, lfd;
	char adir[40], ldir[40], data[100];
	cvar_t *port;

	port = Cvar_Get("net_port", "27960", 0);
	snprint(data, sizeof data, "%s/udp!*!%d", netmtpt, (int)port->value);
	if((afd = announce(data, adir)) < 0)
		sysfatal("announce: %r");
	for(;;){
		if((lfd = listen(adir, ldir)) < 0
		|| (fd = accept(lfd, ldir)) < 0)
			break;
		close(lfd);
		send(echan, nil);
		send(lchan, &fd);
	}
}

void NET_Init (void)
{
	netadr_t *a;

	/* FIXME: configure netmtpt */

	if(svonly && afd < 0){
		if((lchan = chancreate(sizeof(int), 0)) == nil)
			sysfatal("chancreate: %r");
		if((lpid = proccreate(lproc, nil, 8192)) < 0)
			sysfatal("proccreate lproc: %r");
	}
	for(a=cons; a<cons+nelem(cons); a++)
		a->fd = -1;
}
