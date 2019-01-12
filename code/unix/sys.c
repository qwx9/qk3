#include "../game/q_shared.h"
#include "../qcommon/qcommon.h"

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
