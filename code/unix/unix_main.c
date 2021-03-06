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
#include "../renderer/tr_public.h"

#include "linux_local.h" // bk001204

// Structure containing functions exported from refresh DLL
refexport_t re;

unsigned  sys_frame_time;

qboolean stdin_active = qtrue;

// =============================================================
// tty console variables
// =============================================================

// enable/disabled tty input mode
// NOTE TTimo this is used during startup, cannot be changed during run
static cvar_t *ttycon = NULL;
// general flag to tell about tty console mode
static qboolean ttycon_on = qfalse;
// when printing general stuff to stdout stderr (Sys_Printf)
//   we need to disable the tty console stuff
// this increments so we can recursively disable
static int ttycon_hide = 0;
// some key codes that the terminal may be using
// TTimo NOTE: I'm not sure how relevant this is
static int tty_erase;
static int tty_eof;

static struct termios tty_tc;

static field_t tty_con;

// history
// NOTE TTimo this is a bit duplicate of the graphical console history
//   but it's safer and faster to write our own here
#define TTY_HISTORY 32
static field_t ttyEditLines[TTY_HISTORY];
static int hist_current = -1, hist_count = 0;

// =======================================================================
// General routines
// =======================================================================

// bk001207 
#define MEM_THRESHOLD 96*1024*1024

/*
==================
Sys_LowPhysicalMemory()
==================
*/
qboolean Sys_LowPhysicalMemory() {
  //MEMORYSTATUS stat;
  //GlobalMemoryStatus (&stat);
  //return (stat.dwTotalPhys <= MEM_THRESHOLD) ? qtrue : qfalse;
  return qfalse; // bk001207 - FIXME
}

/*
==================
Sys_FunctionCmp
==================
*/
int Sys_FunctionCmp(void *f1, void *f2) {
  return qtrue;
}

/*
==================
Sys_FunctionCheckSum
==================
*/
int Sys_FunctionCheckSum(void *f1) {
  return 0;
}

/*
==================
Sys_MonkeyShouldBeSpanked
==================
*/
int Sys_MonkeyShouldBeSpanked( void ) {
  return 0;
}

void Sys_BeginProfiling( void ) {
}

// =============================================================
// tty console routines
// NOTE: if the user is editing a line when something gets printed to the early console then it won't look good
//   so we provide tty_Clear and tty_Show to be called before and after a stdout or stderr output
// =============================================================

// flush stdin, I suspect some terminals are sending a LOT of shit
// FIXME TTimo relevant?
void tty_FlushIn()
{
  char key;
  while (read(0, &key, 1)!=-1);
}

// do a backspace
// TTimo NOTE: it seems on some terminals just sending '\b' is not enough
//   so for now, in any case we send "\b \b" .. yeah well ..
//   (there may be a way to find out if '\b' alone would work though)
void tty_Back()
{
  char key;
  key = '\b';
  write(1, &key, 1);
  key = ' ';
  write(1, &key, 1);
  key = '\b';
  write(1, &key, 1);
}

// clear the display of the line currently edited
// bring cursor back to beginning of line
void tty_Hide()
{
  int i;
  assert(ttycon_on);
  if (ttycon_hide)
  {
    ttycon_hide++;
    return;
  }
  if (tty_con.cursor>0)
  {
    for (i=0; i<tty_con.cursor; i++)
    {
      tty_Back();
    }
  }
  ttycon_hide++;
}

// show the current line
// FIXME TTimo need to position the cursor if needed??
void tty_Show()
{
  int i;
  assert(ttycon_on);
  assert(ttycon_hide>0);
  ttycon_hide--;
  if (ttycon_hide == 0)
  {
    if (tty_con.cursor)
    {
      for (i=0; i<tty_con.cursor; i++)
      {
        write(1, tty_con.buffer+i, 1);
      }
    }
  }
}

// never exit without calling this, or your terminal will be left in a pretty bad state
void Sys_ConsoleInputShutdown()
{
  if (ttycon_on)
  {
    Com_Printf("Shutdown tty console\n");
    tcsetattr (0, TCSADRAIN, &tty_tc);
  }
}

void Hist_Add(field_t *field)
{
  int i;
  assert(hist_count <= TTY_HISTORY);
  assert(hist_count >= 0);
  assert(hist_current >= -1);
  assert(hist_current <= hist_count);
  // make some room
  for (i=TTY_HISTORY-1; i>0; i--)
  {
    ttyEditLines[i] = ttyEditLines[i-1];
  }
  ttyEditLines[0] = *field;
  if (hist_count<TTY_HISTORY)
  {
    hist_count++;
  }
  hist_current = -1; // re-init
}

field_t *Hist_Prev()
{
  int hist_prev;
  assert(hist_count <= TTY_HISTORY);
  assert(hist_count >= 0);
  assert(hist_current >= -1);
  assert(hist_current <= hist_count);
  hist_prev = hist_current + 1;
  if (hist_prev >= hist_count)
  {
    return NULL;
  }
  hist_current++;
  return &(ttyEditLines[hist_current]);
}

field_t *Hist_Next()
{
  assert(hist_count <= TTY_HISTORY);
  assert(hist_count >= 0);
  assert(hist_current >= -1);
  assert(hist_current <= hist_count);
  if (hist_current >= 0)
  {
    hist_current--;
  }
  if (hist_current == -1)
  {
    return NULL;
  }
  return &(ttyEditLines[hist_current]);
}

// =============================================================
// general sys routines
// =============================================================


void Sys_Warn (char *warning, ...)
{ 
  va_list     argptr;
  char        string[1024];

  va_start (argptr,warning);
  vsprintf (string,warning,argptr);
  va_end (argptr);

  if (ttycon_on)
  {
    tty_Hide();
  }

  fprintf(stderr, "Warning: %s", string);

  if (ttycon_on)
  {
    tty_Show();
  }
} 

/*
============
Sys_FileTime

returns -1 if not present
============
*/
int Sys_FileTime (char *path)
{
  struct  stat  buf;

  if (stat (path,&buf) == -1)
    return -1;

  return buf.st_mtime;
}

void floating_point_exception_handler(int whatever)
{
  signal(SIGFPE, floating_point_exception_handler);
}

// initialize the console input (tty mode if wanted and possible)
void Sys_ConsoleInputInit()
{
  struct termios tc;

  // TTimo 
  // https://zerowing.idsoftware.com/bugzilla/show_bug.cgi?id=390
  // ttycon 0 or 1, if the process is backgrounded (running non interactively)
  // then SIGTTIN or SIGTOU is emitted, if not catched, turns into a SIGSTP
  signal(SIGTTIN, SIG_IGN);
  signal(SIGTTOU, SIG_IGN);

  // FIXME TTimo initialize this in Sys_Init or something?
  ttycon = Cvar_Get("ttycon", "1", 0);
  if (ttycon && ttycon->value)
  {
    if (isatty(STDIN_FILENO)!=1)
    {
      Com_Printf("stdin is not a tty, tty console mode failed\n");
      Cvar_Set("ttycon", "0");
      ttycon_on = qfalse;
      return;
    }
    Com_Printf("Started tty console (use +set ttycon 0 to disable)\n");
    Field_Clear(&tty_con);
    tcgetattr (0, &tty_tc);
    tty_erase = tty_tc.c_cc[VERASE];
    tty_eof = tty_tc.c_cc[VEOF];
    tc = tty_tc;
    /*
     ECHO: don't echo input characters
     ICANON: enable canonical mode.  This  enables  the  special
              characters  EOF,  EOL,  EOL2, ERASE, KILL, REPRINT,
              STATUS, and WERASE, and buffers by lines.
     ISIG: when any of the characters  INTR,  QUIT,  SUSP,  or
              DSUSP are received, generate the corresponding sig�
              nal
    */              
    tc.c_lflag &= ~(ECHO | ICANON);
    /*
     ISTRIP strip off bit 8
     INPCK enable input parity checking
     */
    tc.c_iflag &= ~(ISTRIP | INPCK);
    tc.c_cc[VMIN] = 1;
    tc.c_cc[VTIME] = 0;
    tcsetattr (0, TCSADRAIN, &tc);    
    ttycon_on = qtrue;
  } else
    ttycon_on = qfalse;
}

char *Sys_ConsoleInput(void)
{
  // we use this when sending back commands
  static char text[256];
  int i;
  int avail;
  char key;
  field_t *history;

  if (ttycon && ttycon->value)
  {
    avail = read(0, &key, 1);
    if (avail != -1)
    {
      // we have something
      // backspace?
      // NOTE TTimo testing a lot of values .. seems it's the only way to get it to work everywhere
      if ((key == tty_erase) || (key == 127) || (key == 8))
      {
        if (tty_con.cursor > 0)
        {
          tty_con.cursor--;
          tty_con.buffer[tty_con.cursor] = '\0';
          tty_Back();
        }
        return NULL;
      }
      // check if this is a control char
      if ((key) && (key) < ' ')
      {
        if (key == '\n')
        {
          // push it in history
          Hist_Add(&tty_con);
          strcpy(text, tty_con.buffer);
          Field_Clear(&tty_con);
          key = '\n';
          write(1, &key, 1);
          return text;
        }
        if (key == '\t')
        {
          tty_Hide();
          Field_CompleteCommand( &tty_con );
          // Field_CompleteCommand does weird things to the string, do a cleanup
          //   it adds a '\' at the beginning of the string
          //   cursor doesn't reflect actual length of the string that's sent back
          tty_con.cursor = strlen(tty_con.buffer);
          if (tty_con.cursor>0)
          {
            if (tty_con.buffer[0] == '\\')
            {
              for (i=0; i<=tty_con.cursor; i++)
              {
                tty_con.buffer[i] = tty_con.buffer[i+1];
              }
              tty_con.cursor--;
            }
          }
          tty_Show();
          return NULL;
        }
        avail = read(0, &key, 1);
        if (avail != -1)
        {
          // VT 100 keys
          if (key == '[' || key == 'O')
          {
            avail = read(0, &key, 1);
            if (avail != -1)
            {
              switch (key)
              {
              case 'A':
                history = Hist_Prev();
                if (history)
                {
                  tty_Hide();
                  tty_con = *history;
                  tty_Show();
                }
                tty_FlushIn();
                return NULL;
                break;
              case 'B':
                history = Hist_Next();
                tty_Hide();
                if (history)
                {
                  tty_con = *history;
                } else
                {
                  Field_Clear(&tty_con);
                }
                tty_Show();
                tty_FlushIn();
                return NULL;
                break;
              case 'C':
                return NULL;
              case 'D':
                return NULL;
              }
            }
          }
        }
        Com_DPrintf("droping ISCTL sequence: %d, tty_erase: %d\n", key, tty_erase);
        tty_FlushIn();
        return NULL;
      }
      // push regular character
      tty_con.buffer[tty_con.cursor] = key;
      tty_con.cursor++;
      // print the current line (this is differential)
      write(1, &key, 1);
    }
    return NULL;
  } else
  {
    int     len;
    fd_set  fdset;
    struct timeval timeout;

    if (!com_dedicated || !com_dedicated->value)
      return NULL;

    if (!stdin_active)
      return NULL;

    FD_ZERO(&fdset);
    FD_SET(0, &fdset); // stdin
    timeout.tv_sec = 0;
    timeout.tv_usec = 0;
    if (select (1, &fdset, NULL, NULL, &timeout) == -1 || !FD_ISSET(0, &fdset))
    {
      return NULL;
    }

    len = read (0, text, sizeof(text));
    if (len == 0)
    { // eof!
      stdin_active = qfalse;
      return NULL;
    }

    if (len < 1)
      return NULL;
    text[len-1] = 0;    // rip off the /n and terminate

    return text;
  }
}

/*****************************************************************************/

/*
=================
Sys_UnloadDll

=================
*/
void Sys_UnloadDll( void *dllHandle ) {
  // bk001206 - verbose error reporting
  const char* err; // rb010123 - now const
  if ( !dllHandle )
  {
    Com_Printf("Sys_UnloadDll(NULL)\n");
    return;
  }
  dlclose( dllHandle );
  err = dlerror();
  if ( err != NULL )
    Com_Printf ( "Sys_UnloadGame failed on dlclose: \"%s\"!\n", err );
}


/*
=================
Sys_LoadDll

Used to load a development dll instead of a virtual machine
TTimo:
changed the load procedure to match VFS logic, and allow developer use
#1 look down current path
#2 look in fs_homepath
#3 look in fs_basepath
=================
*/
extern char   *FS_BuildOSPath( const char *base, const char *game, const char *qpath );

void *Sys_LoadDll( const char *name, char *fqpath ,
                   int (**entryPoint)(int, ...),
                   int (*systemcalls)(int, ...) ) 
{
  void *libHandle;
  void  (*dllEntry)( int (*syscallptr)(int, ...) );
  char  curpath[MAX_OSPATH];
  char  fname[MAX_OSPATH];
  char  *basepath;
  char  *homepath;
  char  *pwdpath;
  char  *gamedir;
  char  *fn;
  const char*  err = NULL;
	
	*fqpath = 0;

  // bk001206 - let's have some paranoia
  assert( name );

  getcwd(curpath, sizeof(curpath));
#if defined __i386__
  snprintf (fname, sizeof(fname), "%si386.so", name);
#elif defined __powerpc__   //rcg010207 - PPC support.
  snprintf (fname, sizeof(fname), "%sppc.so", name);
#elif defined __axp__
  snprintf (fname, sizeof(fname), "%saxp.so", name);
#elif defined __mips__
  snprintf (fname, sizeof(fname), "%smips.so", name);
#else
#error Unknown arch
#endif

// bk001129 - was RTLD_LAZY 
#define Q_RTLD    RTLD_NOW

  pwdpath = Sys_Cwd();
  basepath = Cvar_VariableString( "fs_basepath" );
  homepath = Cvar_VariableString( "fs_homepath" );
  gamedir = Cvar_VariableString( "fs_game" );

  // pwdpath
  fn = FS_BuildOSPath( pwdpath, gamedir, fname );
  Com_Printf( "Sys_LoadDll(%s)... \n", fn );
  libHandle = dlopen( fn, Q_RTLD );

  if ( !libHandle )
  {
    Com_Printf( "Sys_LoadDll(%s) failed:\n\"%s\"\n", fn, dlerror() );
    // fs_homepath
    fn = FS_BuildOSPath( homepath, gamedir, fname );
    Com_Printf( "Sys_LoadDll(%s)... \n", fn );
    libHandle = dlopen( fn, Q_RTLD );

    if ( !libHandle )
    {
      Com_Printf( "Sys_LoadDll(%s) failed:\n\"%s\"\n", fn, dlerror() );
      // fs_basepath
      fn = FS_BuildOSPath( basepath, gamedir, fname );
      Com_Printf( "Sys_LoadDll(%s)... \n", fn );
      libHandle = dlopen( fn, Q_RTLD );

      if ( !libHandle )
      {
#ifndef NDEBUG // bk001206 - in debug abort on failure
        Com_Error ( ERR_FATAL, "Sys_LoadDll(%s) failed dlopen() completely!\n", name  );
#else
        Com_Printf ( "Sys_LoadDll(%s) failed dlopen() completely!\n", name );
#endif
        return NULL;
      } else
        Com_Printf ( "Sys_LoadDll(%s): succeeded ...\n", fn );
    } else
      Com_Printf ( "Sys_LoadDll(%s): succeeded ...\n", fn );
  } else
    Com_Printf ( "Sys_LoadDll(%s): succeeded ...\n", fn ); 

  dllEntry = dlsym( libHandle, "dllEntry" ); 
  *entryPoint = dlsym( libHandle, "vmMain" );
  if ( !*entryPoint || !dllEntry )
  {
    err = dlerror();
#ifndef NDEBUG // bk001206 - in debug abort on failure
    Com_Error ( ERR_FATAL, "Sys_LoadDll(%s) failed dlsym(vmMain):\n\"%s\" !\n", name, err );
#else
    Com_Printf ( "Sys_LoadDll(%s) failed dlsym(vmMain):\n\"%s\" !\n", name, err );
#endif
    dlclose( libHandle );
    err = dlerror();
    if ( err != NULL )
      Com_Printf ( "Sys_LoadDll(%s) failed dlcose:\n\"%s\"\n", name, err );
    return NULL;
  }
  Com_Printf ( "Sys_LoadDll(%s) found **vmMain** at  %p  \n", name, *entryPoint ); // bk001212
  dllEntry( systemcalls );
  Com_Printf ( "Sys_LoadDll(%s) succeeded!\n", name );
  if ( libHandle ) Q_strncpyz ( fqpath , fn , MAX_QPATH ) ;		// added 7/20/02 by T.Ray
  return libHandle;
}

/*****************************************************************************/

void Sys_AppActivate (void)
{
}

char *Sys_GetClipboardData(void)
{
  return NULL;
}

void  Sys_Print( const char *msg )
{
  if (ttycon_on)
  {
    tty_Hide();
  }
  fputs(msg, stderr);
  if (ttycon_on)
  {
    tty_Show();
  }
}


void    Sys_ConfigureFPU() { // bk001213 - divide by zero
#ifdef __linux__
#ifdef __i386
#ifndef NDEBUG

  // bk0101022 - enable FPE's in debug mode
  static int fpu_word = _FPU_DEFAULT & ~(_FPU_MASK_ZM | _FPU_MASK_IM);
  int current = 0;
  _FPU_GETCW(current);
  if ( current!=fpu_word)
  {
#if 0
    Com_Printf("FPU Control 0x%x (was 0x%x)\n", fpu_word, current );
    _FPU_SETCW( fpu_word );
    _FPU_GETCW( current );
    assert(fpu_word==current);
#endif
  }
#else // NDEBUG
  static int fpu_word = _FPU_DEFAULT;
  _FPU_SETCW( fpu_word );
#endif // NDEBUG
#endif // __i386 
#endif // __linux
}

void Sys_PrintBinVersion( const char* name ) {
  char* date = __DATE__;
  char* time = __TIME__;
  char* sep = "==============================================================";
  fprintf( stdout, "\n\n%s\n", sep );
#ifdef DEDICATED
  fprintf( stdout, "Linux Quake3 Dedicated Server [%s %s]\n", date, time );  
#else
  fprintf( stdout, "Linux Quake3 Full Executable  [%s %s]\n", date, time );  
#endif
  fprintf( stdout, " local install: %s\n", name );
  fprintf( stdout, "%s\n\n", sep );
}

void Sys_ParseArgs( int argc, char* argv[] ) {

  if ( argc==2 )
  {
    if ( (!strcmp( argv[1], "--version" ))
         || ( !strcmp( argv[1], "-v" )) )
    {
      Sys_PrintBinVersion( argv[0] );
      Sys_Exit(0);
    }
  }
}

#include "../client/client.h"
extern clientStatic_t cls;

int main ( int argc, char* argv[] )
{
  // int 	oldtime, newtime; // bk001204 - unused
  int   len, i;
  char  *cmdline;
  void Sys_SetDefaultCDPath(const char *path);

  Sys_ParseArgs( argc, argv );  // bk010104 - added this for support

  Sys_SetDefaultCDPath(argv[0]);

  // merge the command line, this is kinda silly
  for (len = 1, i = 1; i < argc; i++)
    len += strlen(argv[i]) + 1;
  cmdline = malloc(len);
  *cmdline = 0;
  for (i = 1; i < argc; i++)
  {
    if (i > 1)
      strcat(cmdline, " ");
    strcat(cmdline, argv[i]);
  }

  // bk000306 - clear queues
  memset( &eventQue[0], 0, MAX_QUED_EVENTS*sizeof(sysEvent_t) ); 
  memset( &sys_packetReceived[0], 0, MAX_MSGLEN*sizeof(byte) );

  Com_Init(cmdline);
  NET_Init();

  Sys_ConsoleInputInit();

  fcntl(0, F_SETFL, fcntl (0, F_GETFL, 0) | FNDELAY);
	
#ifdef DEDICATED
	// init here for dedicated, as we don't have GLimp_Init
	InitSig();
#endif

  while (1)
  {
#ifdef __linux__
    Sys_ConfigureFPU();
#endif
    Com_Frame ();
  }
}
