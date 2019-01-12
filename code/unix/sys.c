#include "../game/q_shared.h"
#include "../qcommon/qcommon.h"
#include "linux_local.h"

#include <thread.h>

void
Sys_SnapVector(float *v)
{
	ulong fcr;

	fcr = getfcr();
	setfcr(fcr & ~FPRMASK | FPRNR);
	v[0] = (long)v[0];
	v[1] = (long)v[1];
	v[2] = (long)v[2];
	setfcr(fcr);
}

void
Sys_UnloadDll(void *)
{
}

void *
Sys_LoadDll(char *, char *, int (**)(int, ...), int (*)(int, ...))
{
	sysfatal("Sys_LoadDll: NOPE");
	return nil;
}

qboolean
Sys_CheckCD(void)
{
	return qtrue;
}

void
Sys_InitStreamThread(void)
{
}

void
Sys_ShutdownStreamThread(void)
{
}

void
Sys_BeginStreamedFile(fileHandle_t, int)
{
}

void
Sys_EndStreamedFile(fileHandle_t)
{
}

int
Sys_StreamedRead(void *buffer, int size, int count, fileHandle_t f)
{
	return FS_Read(buffer, size * count, f);
}

void Sys_StreamSeek(fileHandle_t f, int offset, int origin)
{
	FS_Seek(f, offset, origin);
}

void
Sys_Error(char *error, ...)
{
	va_list argptr;
	char string[1024];

	CL_Shutdown();
	va_start(argptr, error);
	vsprintf(string, error, argptr);
	va_end(argptr);
	fprint(2, "Sys_Error: %s\n", string);
	sysfatal(string);
} 

void
Sys_Quit(void)
{
	CL_Shutdown();
	threadexitsall(nil);
}

void
Sys_Init(void)
{
	Cvar_Set("arch", "plan 9");
	Cvar_Set("username", Sys_GetCurrentUser());
	IN_Init();
}

void
Sys_Print(char *s)
{
	write(2, s, strlen(s));
}

int
Sys_Milliseconds(void)
{
	static vlong T;

	if(T == 0)
		T = nsec();
	return (nsec() - T) / 1000000;
}

int
Sys_Mkdir(char *path)
{
	int fd;

	if(access(path, AEXIST) == 0)
		return 0;
	if((fd = create(path, OREAD, DMDIR|0777)) < 0){
		fprint(2, "mkdir: %r\n");
		return -1;
	}
	close(fd);
	return 0;
}

void *
emalloc(ulong n)
{
	void *p;

	if((p = mallocz(n, 1)) == nil)
		sysfatal("emalloc %r");
	setmalloctag(p, getcallerpc(&n));
	return p;
}

vlong
flen(int fd)
{
	vlong l;
	Dir *d;

	if((d = dirfstat(fd)) == nil)	/* file assumed extant and readable */ 
		sysfatal("flen: %r");
	l = d->length;
	free(d);
	return l;
}
