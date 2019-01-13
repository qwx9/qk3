#include "../game/q_shared.h"
#include "../qcommon/qcommon.h"
#include "../renderer/tr_public.h"
#include "../client/client.h"
#include "linux_local.h"

#include <thread.h>
#include <bio.h>

mainstacksize = 256*1024;

extern Channel *echan;

int svonly = 1;

enum{
	MAX_QUED_EVENTS = 256,
	MASK_QUED_EVENTS = MAX_QUED_EVENTS - 1
};

static sysEvent_t  eventQue[MAX_QUED_EVENTS];
static int eventHead, eventTail;
static byte sys_packetReceived[MAX_MSGLEN];

static Channel *inchan;

void
NET_Sleep(int n)
{
	netadr_t *a;

	if(nbrecv(echan, nil) == 0){
		for(a=cons; a<cons+nelem(cons); a++)
			if(a->fd >= 0 && flen(a->fd) > 0)
				break;
		if(a == cons + nelem(cons))	
			sleep(n);
	}
}

void
Sys_QueEvent(int time, sysEventType_t type, int value, int value2, int ptrLength, void *ptr)
{
	sysEvent_t	*ev;

	ev = &eventQue[ eventHead & MASK_QUED_EVENTS ];
	if(eventHead - eventTail >= MAX_QUED_EVENTS){
		Com_Printf("Sys_QueEvent: overflow\n");
		if(ev->evPtr)
			Z_Free(ev->evPtr);
		eventTail++;
	}
	eventHead++;
	if(time == 0)
		time = Sys_Milliseconds();
	ev->evTime = time;
	ev->evType = type;
	ev->evValue = value;
	ev->evValue2 = value2;
	ev->evPtrLength = ptrLength;
	ev->evPtr = ptr;
}

sysEvent_t
Sys_GetEvent(void)
{
	sysEvent_t	ev;
	char		*s;
	msg_t netmsg;

	// return if we have data
	if ( eventHead > eventTail )
		goto ret;

	// pump the message loop
	// in vga this calls KBD_Update, under X, it calls GetEvent
	Sys_SendKeyEvents ();

	// check for console commands
	if((s = nbrecvp(inchan)) != nil){
		char	*b;
		int	 len;

		len = strlen( s ) + 1;
		b = Z_Malloc( len );
		strcpy( b, s );
		Sys_QueEvent( 0, SE_CONSOLE, 0, 0, len, b );
		free(s);
	}

	// check for other input devices
	IN_Frame();

	// check for network packets
	MSG_Init( &netmsg, sys_packetReceived, sizeof( sys_packetReceived ) );
	if ( Sys_GetPacket ( &netmsg ) )
	{
		netadr_t		*buf;
		int			 len;

		// copy out to a seperate buffer for qeueing
		len = sizeof( netadr_t ) + netmsg.cursize;
		buf = Z_Malloc( len );
		*buf = *net_from;
		memcpy( buf+1, netmsg.data, netmsg.cursize );
		Sys_QueEvent( 0, SE_PACKET, 0, 0, len, buf );
	}

	// return if we have data
	if ( eventHead > eventTail )
	{
ret:
		eventTail++;
		return eventQue[ ( eventTail - 1 ) & MASK_QUED_EVENTS ];
	}

	sleep(1);

	// create an empty event to return

	memset( &ev, 0, sizeof( ev ) );
	ev.evTime = Sys_Milliseconds();

	return ev;
}

static void
cproc(void *)
{
	char *s;
	Biobuf *bf;

	if((bf = Bfdopen(0, OREAD)) == nil)
		sysfatal("Bfdopen: %r");
	for(;;){
		if((s = Brdstr(bf, '\n', 1)) == nil)
			break;
		if(sendp(inchan, s) < 0){
			free(s);
			break;
		}
		send(echan, nil);
	}
	Bterm(bf);
}


void
threadmain(int argc, char **argv)
{
	int i, len;
	char *args;

	if((echan = chancreate(sizeof(int), 1)) == nil
	|| (inchan = chancreate(sizeof(void *), 2)) == nil)
		sysfatal("chancreate: %r");
	Sys_SetDefaultCDPath(argv[0]);
	/* FIXME: no. */
	for(len=1, i=1; i<argc; i++)
		len += strlen(argv[i]) + 1;
	args = emalloc(len);
	for(i=1; i<argc; i++){
		if(i > 1)
			strcat(args, " ");
		strcat(args, argv[i]);
	}
	Com_Init(args);
	NET_Init();
	if(proccreate(cproc, nil, 8192) < 0)
		sysfatal("proccreate iproc: %r");
	setfcr(getfcr() & ~(FPINVAL|FPZDIV));
	for(;;)
		Com_Frame();
}
