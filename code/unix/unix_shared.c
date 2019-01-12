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

#include "../game/q_shared.h"
#include "../qcommon/qcommon.h"

//=============================================================================

// Used to determine CD Path
static char cdPath[MAX_OSPATH];

// Used to determine local installation path
static char installPath[MAX_OSPATH];

// Used to determine where to store user-specific files
static char homePath[MAX_OSPATH];

//============================================

#define	MAX_FOUND_FILES	0x1000

static void
Sys_ListFilteredFiles(char *basedir, char *subdirs, char *filter, char **list, int *numfiles)
{
	char search[MAX_OSPATH], newsubdirs[MAX_OSPATH], filename[MAX_OSPATH];
	int fd, i, n;
	Dir *d;

	if(*numfiles >= MAX_FOUND_FILES - 1)
		return;
	if(strlen(subdirs))
		Com_sprintf(search, sizeof search, "%s/%s", basedir, subdirs);
	else
		Com_sprintf(search, sizeof search, "%s", basedir);
	if((fd = open(search, OREAD)) < 0)
		return;
	n = dirreadall(fd, &d);
	close(fd);
	if(n < 0)
		return;
	for(i=0; i<n; i++){
		if(d[i].mode & DMDIR){
			if(strlen(subdirs) != 0)
				Com_sprintf(newsubdirs, sizeof newsubdirs, "%s/%s", subdirs, d[i].name);
			else
				Com_sprintf(newsubdirs, sizeof newsubdirs, "%s", d[i].name);
			Sys_ListFilteredFiles(basedir, newsubdirs, filter, list, numfiles);
		}
		if(*numfiles >= MAX_FOUND_FILES-1)
			break;
		Com_sprintf(filename, sizeof filename, "%s/%s", subdirs, d[i].name);
		if(!Com_FilterPath(filter, filename, qfalse))
			continue;
		list[*numfiles] = CopyString(filename);
		(*numfiles)++;
	}
}

char **
Sys_ListFiles(char *directory, char *extension, char *filter, int *numfiles, qboolean wantsubs)
{
	qboolean dironly = wantsubs;
	int			nfiles;
	char		**listCopy;
	char		*list[MAX_FOUND_FILES];
	int fd, i, n, extLen;
	Dir *d;

	if (filter) {

		nfiles = 0;
		Sys_ListFilteredFiles( directory, "", filter, list, &nfiles );

		list[ nfiles ] = 0;
		*numfiles = nfiles;

		if (!nfiles)
			return NULL;

		listCopy = Z_Malloc( ( nfiles + 1 ) * sizeof( *listCopy ) );
		for ( i = 0 ; i < nfiles ; i++ ) {
			listCopy[i] = list[i];
		}
		listCopy[i] = NULL;

		return listCopy;
	}

	if ( !extension)
		extension = "";

	if ( extension[0] == '/' && extension[1] == 0 ) {
		extension = "";
		dironly = qtrue;
	}

	extLen = strlen( extension );
	
	// search
	nfiles = 0;

	if((fd = open(directory, OREAD)) < 0)
		goto err;
	n = dirreadall(fd, &d);
	close(fd);
	if(n < 0)
		goto err;
	for(i=0; i<n; i++){
		if(dironly ^ d[i].mode & DMDIR)
			continue;

		if (*extension) {
			if(strlen(d[i].name) < strlen(extension)
			|| Q_stricmp(d[i].name + strlen(d[i].name) - strlen(extension), extension))
				continue; // didn't match
		}

		if ( nfiles == MAX_FOUND_FILES - 1 )
			break;
		list[ nfiles ] = CopyString( d[i].name );
		nfiles++;
	}

	list[ nfiles ] = 0;

	// return a copy of the list
	*numfiles = nfiles;

	if ( !nfiles ) {
		return NULL;
	}

	listCopy = Z_Malloc( ( nfiles + 1 ) * sizeof( *listCopy ) );
	for ( i = 0 ; i < nfiles ; i++ ) {
		listCopy[i] = list[i];
	}
	listCopy[i] = NULL;

	return listCopy;
err:
	*numfiles = 0;
	return NULL;
}

void	Sys_FreeFileList( char **list ) {
	int		i;

	if ( !list ) {
		return;
	}

	for ( i = 0 ; list[i] ; i++ ) {
		Z_Free( list[i] );
	}

	Z_Free( list );
}

char *Sys_Cwd( void ) 
{
	static char cwd[MAX_OSPATH];

	getwd(cwd, sizeof(cwd) - 1);
	cwd[MAX_OSPATH-1] = 0;

	return cwd;
}

void Sys_SetDefaultCDPath(const char *path)
{
	Q_strncpyz(cdPath, path, sizeof(cdPath));
}

char *Sys_DefaultCDPath(void)
{
        return cdPath;
}

void Sys_SetDefaultInstallPath(const char *path)
{
	Q_strncpyz(installPath, path, sizeof(installPath));
}

char *Sys_DefaultInstallPath(void)
{
	if (*installPath)
		return installPath;
	else
		return Sys_Cwd();
}

void Sys_SetDefaultHomePath(const char *path)
{
	Q_strncpyz(homePath, path, sizeof(homePath));
}

char *Sys_DefaultHomePath(void)
{
	char *p;

        if (*homePath)
            return homePath;
            
	if ((p = getenv("home")) != NULL) {
		Q_strncpyz(homePath, p, sizeof(homePath));
		free(p);
		Q_strcat(homePath, sizeof(homePath), "/.q3a");
		if(Sys_Mkdir(homePath) < 0)
			Sys_Error("Unable to create directory \"%s\"\n", homePath);
		return homePath;
	}
	return ""; // assume current dir
}

//============================================

int Sys_GetProcessorId( void )
{
	return CPUID_GENERIC;
}

void Sys_ShowConsole( int visLevel, qboolean quitOnClose )
{
}

char *Sys_GetCurrentUser( void )
{
	return getuser();
}

unsigned int Sys_ProcessorCount()
{
	return 1;
}
