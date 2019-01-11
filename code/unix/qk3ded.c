#include "../game/q_shared.h"
#include "../qcommon/qcommon.h"
#include "../renderer/tr_public.h"
#include "../client/client.h"

#include <thread.h>

void *
emalloc(ulong n)
{
	void *p;

	if((p = mallocz(n, 1)) == nil)
		sysfatal("emalloc %r");
	setmalloctag(p, getcallerpc(&n));
	return p;
}

static void
Sys_ConsoleInputInit(void)
{
	/* FIXME: on exit: Sys_Exit(0) */
}

void
threadmain(int argc, char **argv)
{
	int i, len;
	char *args;

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
	Sys_ConsoleInputInit();
	setfcr(getfcr() & ~(FPINVAL|FPZDIV));
	for(;;)
		Com_Frame();
}
