/*
** $Id: liolib.c,v 1.48 1999/10/19 13:33:22 roberto Exp roberto $
** Standard I/O (and system) library
** See Copyright Notice in lua.h
*/


#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "lauxlib.h"
#include "lua.h"
#include "luadebug.h"
#include "lualib.h"


#ifndef OLD_ANSI
#include <locale.h>
#else
/* no support for locale and for strerror: fake them */
#define setlocale(a,b)	0
#define LC_ALL		0
#define LC_COLLATE	0
#define LC_CTYPE	0
#define LC_MONETARY	0
#define LC_NUMERIC	0
#define LC_TIME		0
#define strerror(e)	"(no error message provided by operating system)"
#endif


#define IOTAG	1

#define FIRSTARG	2  /* 1st is upvalue */

#define CLOSEDTAG(tag)	((tag)-1)  /* assume that CLOSEDTAG = iotag-1 */


#define FINPUT		"_INPUT"
#define FOUTPUT		"_OUTPUT"


#ifdef POPEN
/* FILE *popen();
int pclose(); */
#define CLOSEFILE(f)    ((pclose(f) == -1) ? fclose(f) : 0)
#else
/* no support for popen */
#define popen(x,y) NULL  /* that is, popen always fails */
#define CLOSEFILE(f)    (fclose(f))
#endif



static void pushresult (int i) {
  if (i)
    lua_pushuserdata(NULL);
  else {
    lua_pushnil();
    lua_pushstring(strerror(errno));
    lua_pushnumber(errno);
  }
}


/*
** {======================================================
** FILE Operations
** =======================================================
*/

static int gettag (void) {
  return (int)lua_getnumber(lua_getparam(IOTAG));
}


static int ishandle (lua_Object f) {
  if (lua_isuserdata(f)) {
    int tag = gettag();
    if (lua_tag(f) == CLOSEDTAG(tag))
      lua_error("cannot access a closed file");
    return lua_tag(f) == tag;
  }
  else return 0;
}


static FILE *getfilebyname (const char *name) {
  lua_Object f = lua_rawgetglobal(name);
  if (!ishandle(f))
      luaL_verror("global variable `%.50s' is not a file handle", name);
  return lua_getuserdata(f);
}


static FILE *getfile (int arg) {
  lua_Object f = lua_getparam(arg);
  return (ishandle(f)) ? lua_getuserdata(f) : NULL;
}


static FILE *getnonullfile (int arg) {
  FILE *f = getfile(arg);
  luaL_arg_check(f, arg, "invalid file handle");
  return f;
}


static FILE *getfileparam (const char *name, int *arg) {
  FILE *f = getfile(*arg);
  if (f) {
    (*arg)++;
    return f;
  }
  else
    return getfilebyname(name);
}


static int closefile (FILE *f) {
  if (f == stdin || f == stdout)
    return 1;
  else {
    int tag = gettag();
    lua_pushusertag(f, tag);
    lua_settag(CLOSEDTAG(tag));
    return (CLOSEFILE(f) == 0);
  }
}


static void io_close (void) {
  pushresult(closefile(getnonullfile(FIRSTARG)));
}


static void gc_close (void) {
  FILE *f = getnonullfile(FIRSTARG);
  if (f != stdin && f != stdout && f != stderr) {
    CLOSEFILE(f);
  }
}


static void io_open (void) {
  FILE *f = fopen(luaL_check_string(FIRSTARG), luaL_check_string(FIRSTARG+1));
  if (f) lua_pushusertag(f, gettag());
  else pushresult(0);
}


static void setfile (FILE *f, const char *name, int tag) {
  lua_pushusertag(f, tag);
  lua_setglobal(name);
}


static void setreturn (FILE *f, const char *name) {
  if (f == NULL)
    pushresult(0);
  else {
    int tag = gettag();
    setfile(f, name, tag);
    lua_pushusertag(f, tag);
  }
}


static void io_readfrom (void) {
  FILE *current;
  lua_Object f = lua_getparam(FIRSTARG);
  if (f == LUA_NOOBJECT) {
    if (closefile(getfilebyname(FINPUT)))
      current = stdin;
    else
      current = NULL;  /* to signal error */
  }
  else if (lua_tag(f) == gettag())  /* deprecated option */
    current = lua_getuserdata(f);
  else {
    const char *s = luaL_check_string(FIRSTARG);
    current = (*s == '|') ? popen(s+1, "r") : fopen(s, "r");
  }
  setreturn(current, FINPUT);
}


static void io_writeto (void) {
  FILE *current;
  lua_Object f = lua_getparam(FIRSTARG);
  if (f == LUA_NOOBJECT) {
    if (closefile(getfilebyname(FOUTPUT)))
      current = stdout;
    else
      current = NULL;  /* to signal error */
  }
  else if (lua_tag(f) == gettag())  /* deprecated option */
    current = lua_getuserdata(f);
  else {
    const char *s = luaL_check_string(FIRSTARG);
    current = (*s == '|') ? popen(s+1,"w") : fopen(s, "w");
  }
  setreturn(current, FOUTPUT);
}


static void io_appendto (void) {
  FILE *current = fopen(luaL_check_string(FIRSTARG), "a");
  setreturn(current, FOUTPUT);
}



/*
** {======================================================
** READ
** =======================================================
*/



#ifdef COMPAT_READPATTERN

/*
** We cannot lookahead without need, because this can lock stdin.
** This flag signals when we need to read a next char.
*/
#define NEED_OTHER (EOF-1)  /* just some flag different from EOF */


static int read_pattern (FILE *f, const char *p) {
  int inskip = 0;  /* {skip} level */
  int c = NEED_OTHER;
  while (*p != '\0') {
    switch (*p) {
      case '{':
        inskip++;
        p++;
        continue;
      case '}':
        if (!inskip) lua_error("unbalanced braces in read pattern");
        inskip--;
        p++;
        continue;
      default: {
        const char *ep = luaI_classend(p);  /* get what is next */
        int m;  /* match result */
        if (c == NEED_OTHER) c = getc(f);
        m = (c==EOF) ? 0 : luaI_singlematch(c, p, ep);
        if (m) {
          if (!inskip) luaL_addchar(c);
          c = NEED_OTHER;
        }
        switch (*ep) {
          case '+':  /* repetition (1 or more) */
            if (!m) goto break_while;  /* pattern fails? */
            /* else go through */
          case '*':  /* repetition (0 or more) */
            while (m) {  /* reads the same item until it fails */
              c = getc(f);
              m = (c==EOF) ? 0 : luaI_singlematch(c, p, ep);
              if (m && !inskip) luaL_addchar(c);
            }
            /* go through to continue reading the pattern */
          case '?':  /* optional */
            p = ep+1;  /* continues reading the pattern */
            continue;
          default:
            if (!m) goto break_while;  /* pattern fails? */
            p = ep;  /* else continues reading the pattern */
        }
      }
    }
  } break_while:
  if (c != NEED_OTHER) ungetc(c, f);
  return (*p == '\0');
}

#else

#define read_pattern(f,p)   (lua_error("read patterns are deprecated"), 0)

#endif


static int read_number (FILE *f) {
  double d;
  if (fscanf(f, "%lf", &d) == 1) {
    lua_pushnumber(d);
    return 1;
  }
  else return 0;  /* read fails */
}


static void read_word (FILE *f) {
  int c;
  do { c = fgetc(f); } while isspace(c);  /* skip spaces */
  while (c != EOF && !isspace(c)) {
    luaL_addchar(c);
    c = fgetc(f);
  }
  ungetc(c, f);
}


#define HUNK_LINE	256
#define HUNK_FILE	BUFSIZ

static int read_line (FILE *f) {
  int n;
  char *b;
  do {
    b = luaL_openspace(HUNK_LINE);
    if (!fgets(b, HUNK_LINE, f)) return 0;  /* read fails */
    n = strlen(b);
    luaL_addsize(n); 
  } while (b[n-1] != '\n');
  luaL_addsize(-1);  /* remove '\n' */
  return 1;
}


static void read_file (FILE *f) {
  int n;
  do {
    char *b = luaL_openspace(HUNK_FILE);
    n = fread(b, sizeof(char), HUNK_FILE, f);
    luaL_addsize(n);
  } while (n==HUNK_FILE);
}


static int read_chars (FILE *f, int n) {
  char *b = luaL_openspace(n);
  int n1 = fread(b, sizeof(char), n, f);
  luaL_addsize(n1);
  return (n == n1);
}


static void io_read (void) {
  int arg = FIRSTARG;
  FILE *f = getfileparam(FINPUT, &arg);
  lua_Object op = lua_getparam(arg);
  do {  /* repeat for each part */
    long l;
    int success;
    luaL_resetbuffer();
    if (lua_isnumber(op))
      success = read_chars(f, (int)lua_getnumber(op));
    else {
      const char *p = luaL_opt_string(arg, "*l");
      if (p[0] != '*')
        success = read_pattern(f, p);  /* deprecated! */
      else {
        switch (p[1]) {
          case 'n':  /* number */
            if (!read_number(f)) return;  /* read fails */
            continue;  /* number is already pushed; avoid the "pushstring" */
          case 'l':  /* line */
            success = read_line(f);
            break;
          case 'a':  /* file */
            read_file(f);
            success = 1; /* always success */
            break;
          case 'w':  /* word */
            read_word(f);
            success = 0;  /* must read something to succeed */
            break;
          default:
            luaL_argerror(arg, "invalid format");
            success = 0;  /* to avoid warnings */
        }
      }
    }
    l = luaL_getsize();
    if (!success && l==0) return;  /* read fails */
    lua_pushlstring(luaL_buffer(), l);
  } while ((op = lua_getparam(++arg)) != LUA_NOOBJECT);
}

/* }====================================================== */


static void io_write (void) {
  int arg = FIRSTARG;
  FILE *f = getfileparam(FOUTPUT, &arg);
  int status = 1;
  lua_Object o;
  while ((o = lua_getparam(arg++)) != LUA_NOOBJECT) {
    switch (lua_type(o)[2]) {
      case 'r': {  /* stRing? */
        long l = lua_strlen(o);
        status = status &&
                 ((long)fwrite(lua_getstring(o), sizeof(char), l, f) == l);
        break;
      }
      case 'm': /* nuMber? */  /* LUA_NUMBER */
        /* optimization: could be done exactly as for strings */
        status = status && fprintf(f, "%.16g", lua_getnumber(o)) > 0;
        break;
      default: luaL_argerror(arg-1, "string expected");
    }
  }
  pushresult(status);
}


static void io_seek (void) {
  static const int mode[] = {SEEK_SET, SEEK_CUR, SEEK_END};
  static const char *const modenames[] = {"set", "cur", "end", NULL};
  FILE *f = getnonullfile(FIRSTARG);
  int op = luaL_findstring(luaL_opt_string(FIRSTARG+1, "cur"), modenames);
  long offset = luaL_opt_long(FIRSTARG+2, 0);
  luaL_arg_check(op != -1, FIRSTARG+1, "invalid mode");
  op = fseek(f, offset, mode[op]);
  if (op)
    pushresult(0);  /* error */
  else
    lua_pushnumber(ftell(f));
}


static void io_flush (void) {
  FILE *f = getfile(FIRSTARG);
  luaL_arg_check(f || lua_getparam(FIRSTARG) == LUA_NOOBJECT, FIRSTARG,
                 "invalid file handle");
  pushresult(fflush(f) == 0);
}

/* }====================================================== */


/*
** {======================================================
** Other O.S. Operations
** =======================================================
*/

static void io_execute (void) {
  lua_pushnumber(system(luaL_check_string(1)));
}


static void io_remove  (void) {
  pushresult(remove(luaL_check_string(1)) == 0);
}


static void io_rename (void) {
  pushresult(rename(luaL_check_string(1),
                    luaL_check_string(2)) == 0);
}


static void io_tmpname (void) {
  lua_pushstring(tmpnam(NULL));
}



static void io_getenv (void) {
  lua_pushstring(getenv(luaL_check_string(1)));  /* if NULL push nil */
}


static void io_clock (void) {
  lua_pushnumber(((double)clock())/CLOCKS_PER_SEC);
}


static void io_date (void) {
  char b[256];
  const char *s = luaL_opt_string(1, "%c");
  struct tm *tm;
  time_t t;
  time(&t); tm = localtime(&t);
  if (strftime(b,sizeof(b),s,tm))
    lua_pushstring(b);
  else
    lua_error("invalid `date' format");
}


static void setloc (void) {
  static const int cat[] = {LC_ALL, LC_COLLATE, LC_CTYPE, LC_MONETARY,
                      LC_NUMERIC, LC_TIME};
  static const char *const catnames[] = {"all", "collate", "ctype", "monetary",
     "numeric", "time", NULL};
  int op = luaL_findstring(luaL_opt_string(2, "all"), catnames);
  luaL_arg_check(op != -1, 2, "invalid option");
  lua_pushstring(setlocale(cat[op], luaL_check_string(1)));
}


static void io_exit (void) {
  exit(luaL_opt_int(1, EXIT_SUCCESS));
}

/* }====================================================== */



static void io_debug (void) {
  for (;;) {
    char buffer[250];
    fprintf(stderr, "lua_debug> ");
    if (fgets(buffer, sizeof(buffer), stdin) == 0 ||
        strcmp(buffer, "cont\n") == 0)
      return;
    lua_dostring(buffer);
  }
}



#define MESSAGESIZE	150
#define MAXMESSAGE	(MESSAGESIZE*10)


#define MAXSRC		60


static void errorfb (void) {
  char buff[MAXMESSAGE];
  int level = 1;  /* skip level 0 (it's this function) */
  lua_Object func;
  sprintf(buff, "lua error: %.200s\n", lua_getstring(lua_getparam(1)));
  while ((func = lua_stackedfunction(level++)) != LUA_NOOBJECT) {
    const char *name;
    int currentline;
    const char *chunkname;
    char buffchunk[MAXSRC];
    int linedefined;
    lua_funcinfo(func, &chunkname, &linedefined);
    luaL_chunkid(buffchunk, chunkname, sizeof(buffchunk));
    if (level == 2) strcat(buff, "Active Stack:\n");
    strcat(buff, "  ");
    if (strlen(buff) > MAXMESSAGE-MESSAGESIZE) {
      strcat(buff, "...\n");
      break;  /* buffer is full */
    }
    switch (*lua_getobjname(func, &name)) {
      case 'g':
        sprintf(buff+strlen(buff), "function `%.50s'", name);
        break;
      case 't':
        sprintf(buff+strlen(buff), "`%.50s' tag method", name);
        break;
      default: {
        if (linedefined == 0)
          sprintf(buff+strlen(buff), "main of %.70s", buffchunk);
        else if (linedefined < 0)
          sprintf(buff+strlen(buff), "%.70s", buffchunk);
        else
          sprintf(buff+strlen(buff), "function <%d:%.70s>",
                  linedefined, buffchunk);
        chunkname = NULL;
      }
    }
    if ((currentline = lua_currentline(func)) > 0)
      sprintf(buff+strlen(buff), " at line %d", currentline);
    if (chunkname)
      sprintf(buff+strlen(buff), " [%.70s]", buffchunk);
    strcat(buff, "\n");
  }
  func = lua_rawgetglobal("_ALERT");
  if (lua_isfunction(func)) {  /* avoid error loop if _ALERT is not defined */
    lua_pushstring(buff);
    lua_callfunction(func);
  }
}



static const struct luaL_reg iolib[] = {
  {"_ERRORMESSAGE", errorfb},
  {"clock",     io_clock},
  {"date",     io_date},
  {"debug",    io_debug},
  {"execute",  io_execute},
  {"exit",     io_exit},
  {"getenv",   io_getenv},
  {"remove",   io_remove},
  {"rename",   io_rename},
  {"setlocale", setloc},
  {"tmpname",   io_tmpname}
};


static const struct luaL_reg iolibtag[] = {
  {"appendto", io_appendto},
  {"closefile",   io_close},
  {"flush",     io_flush},
  {"openfile",   io_open},
  {"read",     io_read},
  {"readfrom", io_readfrom},
  {"seek",     io_seek},
  {"write",    io_write},
  {"writeto",  io_writeto}
};


static void openwithtags (void) {
  int i;
  int iotag = lua_newtag();
  lua_newtag();  /* alloc CLOSEDTAG: assume that CLOSEDTAG = iotag-1 */
  for (i=0; i<sizeof(iolibtag)/sizeof(iolibtag[0]); i++) {
    /* put iotag as upvalue for these functions */
    lua_pushnumber(iotag);
    lua_pushcclosure(iolibtag[i].func, 1);
    lua_setglobal(iolibtag[i].name);
  }
  /* predefined file handles */
  setfile(stdin, FINPUT, iotag);
  setfile(stdout, FOUTPUT, iotag);
  setfile(stdin, "_STDIN", iotag);
  setfile(stdout, "_STDOUT", iotag);
  setfile(stderr, "_STDERR", iotag);
  /* close file when collected */
  lua_pushnumber(iotag);
  lua_pushcclosure(gc_close, 1); 
  lua_settagmethod(iotag, "gc");
}

void lua_iolibopen (void) {
  /* register lib functions */
  luaL_openlib(iolib, (sizeof(iolib)/sizeof(iolib[0])));
  openwithtags();
}

