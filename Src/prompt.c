/*
 * prompt.c - construct zsh prompts
 *
 * This file is part of zsh, the Z shell.
 *
 * Copyright (c) 1992-1997 Paul Falstad
 * All rights reserved.
 *
 * Permission is hereby granted, without written agreement and without
 * license or royalty fees, to use, copy, modify, and distribute this
 * software and to distribute modified versions of this software for any
 * purpose, provided that the above copyright notice and the following
 * two paragraphs appear in all copies of this software.
 *
 * In no event shall Paul Falstad or the Zsh Development Group be liable
 * to any party for direct, indirect, special, incidental, or consequential
 * damages arising out of the use of this software and its documentation,
 * even if Paul Falstad and the Zsh Development Group have been advised of
 * the possibility of such damage.
 *
 * Paul Falstad and the Zsh Development Group specifically disclaim any
 * warranties, including, but not limited to, the implied warranties of
 * merchantability and fitness for a particular purpose.  The software
 * provided hereunder is on an "as is" basis, and Paul Falstad and the
 * Zsh Development Group have no obligation to provide maintenance,
 * support, updates, enhancements, or modifications.
 *
 */

#include "zsh.mdh"
#include "prompt.pro"

/* text attribute mask */
 
/**/
unsigned txtattrmask;

/* text change - attribute change made by prompts */

/**/
unsigned txtchange;

/* the command stack for use with %_ in prompts */
 
/**/
unsigned char *cmdstack;
/**/
int cmdsp;

/* parser states, for %_ */

static char *cmdnames[] = {
    "for",      "while",     "repeat",    "select",
    "until",    "if",        "then",      "else",
    "elif",     "math",      "cond",      "cmdor",
    "cmdand",   "pipe",      "errpipe",   "foreach",
    "case",     "function",  "subsh",     "cursh",
    "array",    "quote",     "dquote",    "bquote",
    "cmdsubst", "mathsubst", "elif-then", "heredoc",
    "heredocd", "brace",     "braceparam",
};
 
/* The buffer into which an expanded and metafied prompt is being written, *
 * and its size.                                                           */

static char *buf;
static int bufspc;

/* bp is the pointer to the current position in the buffer, where the next *
 * character will be added.                                                */

static char *bp;

/* Position of the start of the current line in the buffer */

static char *bufline;

/* bp1 is an auxilliary pointer into the buffer, which when non-NULL is *
 * moved whenever the buffer is reallocated.  It is used when data is   *
 * being temporarily held in the buffer.                                */

static char *bp1;

/* The format string, for %-expansion. */

static char *fm;

/* Non-zero if truncating the current segment of the buffer. */

static int trunclen;

/* Current level of nesting of %{ / %} sequences. */

static int dontcount;

/* Strings to use for %r and %R (for the spelling prompt). */

static char *rstring, *Rstring;

/* Perform prompt expansion on a string, putting the result in a *
 * permanently-allocated string.  If ns is non-zero, this string *
 * may have embedded Inpar and Outpar, which indicate a toggling *
 * between spacing and non-spacing parts of the prompt, and      *
 * Nularg, which (in a non-spacing sequence) indicates a         *
 * `glitch' space.                                               */

/**/
char *
promptexpand(char *s, int ns, char *rs, char *Rs)
{
    if(!s)
	return ztrdup("");

    if ((termflags & TERM_UNKNOWN) && (unset(INTERACTIVE)))
        init_term();

    if (isset(PROMPTSUBST)) {
	int olderr = errflag;

	HEAPALLOC {
	    s = dupstring(s);
	    if (!parsestr(s))
		singsub(&s);
	} LASTALLOC;
	/* Ignore errors in prompt substitution */
	errflag = olderr;
    }

    rstring = rs;
    Rstring = Rs;
    fm = s;
    bp = bufline = buf = zalloc(bufspc = 256);
    bp1 = NULL;
    trunclen = 0;
    putpromptchar(1, '\0');
    addbufspc(1);
    if(dontcount)
	*bp++ = Outpar;
    *bp = 0;
    if (!ns) {
	/* If zero, Inpar, Outpar and Nularg should be removed. */
	for (bp = buf; *bp; bp++) {
	    if (*bp == Meta)
		bp++;
	    else if (*bp == Inpar || *bp == Outpar || *bp == Nularg)
		chuck(bp);
	}
    }
    return buf;
}

/* Perform %- and !-expansion as required on a section of the prompt.  The *
 * section is ended by an instance of endchar.  If doprint is 0, the valid *
 * % sequences are merely skipped over, and nothing is stored.             */

/**/
static int
putpromptchar(int doprint, int endchar)
{
    char *ss, *tmbuf = NULL;
    int t0, arg, test, sep;
    struct tm *tm;
    time_t timet;
    Nameddir nd;

    for (; *fm && *fm != endchar; fm++) {
	arg = 0;
	if (*fm == '%' && isset(PROMPTPERCENT)) {
	    if (idigit(*++fm)) {
		arg = zstrtol(fm, &fm, 10);
	    }
	    if (*fm == '(') {
		int tc, otrunclen;

		if (idigit(*++fm)) {
		    arg = zstrtol(fm, &fm, 10);
		}
		test = 0;
		ss = pwd;
		switch (tc = *fm) {
		case 'c':
		case '.':
		case '~':
		    if ((nd = finddir(ss))) {
			arg--;
			ss += strlen(nd->dir);
		    }
		case '/':
		case 'C':
		    for (; *ss; ss++)
			if (*ss == '/')
			    arg--;
		    if (arg <= 0)
			test = 1;
		    break;
		case 't':
		case 'T':
		case 'd':
		case 'D':
		case 'w':
		    timet = time(NULL);
		    tm = localtime(&timet);
		    switch (tc) {
		    case 't':
			test = (arg == tm->tm_min);
			break;
		    case 'T':
			test = (arg == tm->tm_hour);
			break;
		    case 'd':
			test = (arg == tm->tm_mday);
			break;
		    case 'D':
			test = (arg == tm->tm_mon);
			break;
		    case 'w':
			test = (arg == tm->tm_wday);
			break;
		    }
		    break;
		case '?':
		    if (lastval == arg)
			test = 1;
		    break;
		case '#':
		    if (geteuid() == arg)
			test = 1;
		    break;
		case 'g':
		    if (getegid() == arg)
			test = 1;
		    break;
		case 'l':
		    *bp = '\0';
		    countprompt(bufline, &t0, 0, 0);
		    if (t0 >= arg)
			test = 1;
		    break;
		case 'L':
		    if (shlvl >= arg)
			test = 1;
		    break;
		case 'S':
		    if (time(NULL) - shtimer.tv_sec >= arg)
			test = 1;
		    break;
		case 'v':
		    if (arrlen(psvar) >= arg)
			test = 1;
		    break;
		case '_':
		    test = (cmdsp >= arg);
		    break;
		case '!':
		    test = privasserted();
		    break;
		default:
		    test = -1;
		    break;
		}
		if (!*fm || !(sep = *++fm))
		    return 0;
		fm++;
		/* Don't do the current truncation until we get back */
		otrunclen = trunclen;
		trunclen = 0;
		if (!putpromptchar(test == 1 && doprint, sep) || !*++fm ||
		    !putpromptchar(test == 0 && doprint, ')')) {
		    trunclen = otrunclen;
		    return 0;
		}
		trunclen = otrunclen;
		continue;
	    }
	    if (!doprint)
		switch(*fm) {
		  case '[':
		    while(idigit(*++fm));
		    while(*++fm != ']');
		    continue;
		  case '<':
		    while(*++fm != '<');
		    continue;
		  case '>':
		    while(*++fm != '>');
		    continue;
		  case 'D':
		    if(fm[1]=='{')
			while(*++fm != '}');
		    continue;
		  default:
		    continue;
		}
	    switch (*fm) {
	    case '~':
		if ((nd = finddir(pwd))) {
		    char *t = tricat("~", nd->nam, pwd + strlen(nd->dir));
		    stradd(t);
		    zsfree(t);
		    break;
		}
	    case 'd':
	    case '/':
		stradd(pwd);
		break;
	    case 'c':
	    case '.':
	        {
		    char *t;

		    if ((nd = finddir(pwd)))
			t = tricat("~", nd->nam, pwd + strlen(nd->dir));
		    else
			t = ztrdup(pwd);
		    if (!arg)
			arg++;
		    for (ss = t + strlen(t); ss > t; ss--)
			if (*ss == '/' && !--arg) {
			    ss++;
			    break;
			}
		    if(*ss == '/' && ss[1] && ss != t)
			ss++;
		    stradd(ss);
		    zsfree(t);
		    break;
		}
	    case 'C':
		if (!arg)
		    arg++;
		for (ss = pwd + strlen(pwd); ss > pwd; ss--)
		    if (*ss == '/' && !--arg) {
			ss++;
			break;
		    }
		if (*ss == '/' && ss[1] && (ss != pwd))
		    ss++;
		stradd(ss);
		break;
	    case 'h':
	    case '!':
		addbufspc(DIGBUFSIZE);
		sprintf(bp, "%d", curhist);
		bp += strlen(bp);
		break;
	    case 'M':
		stradd(hostnam);
		break;
	    case 'm':
		if (!arg)
		    arg++;
		for (ss = hostnam; *ss; ss++)
		    if (*ss == '.' && !--arg)
			break;
		t0 = *ss;
		*ss = '\0';
		stradd(hostnam);
		*ss = t0;
		break;
	    case 'S':
		txtchangeset(TXTSTANDOUT, TXTNOSTANDOUT);
		txtset(TXTSTANDOUT);
		tsetcap(TCSTANDOUTBEG, 1);
		break;
	    case 's':
		txtchangeset(TXTNOSTANDOUT, TXTSTANDOUT);
		txtset(TXTDIRTY);
		txtunset(TXTSTANDOUT);
		tsetcap(TCSTANDOUTEND, 1);
		break;
	    case 'B':
		txtchangeset(TXTBOLDFACE, TXTNOBOLDFACE);
		txtset(TXTDIRTY);
		txtset(TXTBOLDFACE);
		tsetcap(TCBOLDFACEBEG, 1);
		break;
	    case 'b':
		txtchangeset(TXTNOBOLDFACE, TXTBOLDFACE);
		txtchangeset(TXTNOSTANDOUT, TXTSTANDOUT);
		txtchangeset(TXTNOUNDERLINE, TXTUNDERLINE);
		txtset(TXTDIRTY);
		txtunset(TXTBOLDFACE);
		tsetcap(TCALLATTRSOFF, 1);
		break;
	    case 'U':
		txtchangeset(TXTUNDERLINE, TXTNOUNDERLINE);
		txtset(TXTUNDERLINE);
		tsetcap(TCUNDERLINEBEG, 1);
		break;
	    case 'u':
		txtchangeset(TXTNOUNDERLINE, TXTUNDERLINE);
		txtset(TXTDIRTY);
		txtunset(TXTUNDERLINE);
		tsetcap(TCUNDERLINEEND, 1);
		break;
	    case '[':
		if (idigit(*++fm))
		    arg = zstrtol(fm, &fm, 10);
		if (!prompttrunc(arg, ']', doprint, endchar))
		    return *fm;
		break;
	    case '<':
	    case '>':
		if (!prompttrunc(arg, *fm, doprint, endchar))
		    return *fm;
		break;
	    case '{': /*}*/
		if (!dontcount++) {
		    addbufspc(1);
		    *bp++ = Inpar;
		}
		break;
	    case /*{*/ '}':
		if (dontcount && !--dontcount) {
		    addbufspc(1);
		    *bp++ = Outpar;
		}
		break;
	    case 't':
	    case '@':
	    case 'T':
	    case '*':
	    case 'w':
	    case 'W':
	    case 'D':
		{
		    char *tmfmt, *dd;

		    switch (*fm) {
		    case 'T':
			tmfmt = "%K:%M";
			break;
		    case '*':
			tmfmt = "%K:%M:%S";
			break;
		    case 'w':
			tmfmt = "%a %f";
			break;
		    case 'W':
			tmfmt = "%m/%d/%y";
			break;
		    case 'D':
			if (fm[1] == '{') /*}*/ {
			    for (ss = fm + 2; *ss && *ss != /*{*/ '}'; ss++)
				if(*ss == '\\' && ss[1])
				    ss++;
			    dd = tmfmt = tmbuf = zalloc(ss - fm);
			    for (ss = fm + 2; *ss && *ss != /*{*/ '}'; ss++) {
				if(*ss == '\\' && ss[1])
				    ss++;
				*dd++ = *ss;
			    }
			    *dd = 0;
			    fm = ss - !*ss;
			} else
			    tmfmt = "%y-%m-%d";
			break;
		    default:
			tmfmt = "%l:%M%p";
			break;
		    }
		    timet = time(NULL);
		    tm = localtime(&timet);
		    for(t0=80; ; t0*=2) {
			addbufspc(t0);
			if(ztrftime(bp, t0, tmfmt, tm) != t0)
			    break;
		    }
		    bp += strlen(bp);
		    free(tmbuf);
		    tmbuf = NULL;
		    break;
		}
	    case 'n':
		stradd(get_username());
		break;
	    case 'l':
		if (*ttystrname) {
		    ss = (strncmp(ttystrname, "/dev/tty", 8) ?
			    ttystrname + 5 : ttystrname + 8);
		    stradd(ss);
		} else
		    stradd("()");
		break;
	    case 'L':
		addbufspc(DIGBUFSIZE);
		sprintf(bp, "%ld", (long)shlvl);
		bp += strlen(bp);
		break;
	    case '?':
		addbufspc(DIGBUFSIZE);
		sprintf(bp, "%ld", (long)lastval);
		bp += strlen(bp);
		break;
	    case '%':
	    case ')':
		addbufspc(1);
		*bp++ = *fm;
		break;
	    case '#':
		addbufspc(1);
		*bp++ = privasserted() ? '#' : '%';
		break;
	    case 'v':
		if (!arg)
		    arg = 1;
		if (arrlen(psvar) >= arg)
		    stradd(psvar[arg - 1]);
		break;
	    case 'E':
                tsetcap(TCCLEAREOL, 1);
		break;
	    case '_':
		if (cmdsp) {
		    if (arg > cmdsp || arg <= 0)
			arg = cmdsp;
		    for (t0 = cmdsp - arg; arg--; t0++) {
			stradd(cmdnames[cmdstack[t0]]);
			if (arg) {
			    addbufspc(1);
			    *bp++=' ';
			}
		    }
		}
		break;
	    case 'r':
		if(rstring)
		    stradd(rstring);
		break;
	    case 'R':
		if(Rstring)
		    stradd(Rstring);
		break;
	    case '\0':
		return 0;
	    case Meta:
		fm++;
		break;
	    }
	} else if(*fm == '!' && isset(PROMPTBANG)) {
	    if(doprint) {
		if(fm[1] == '!') {
		    fm++;
		    addbufspc(1);
		    pputc('!');
		} else {
		    addbufspc(DIGBUFSIZE);
		    sprintf(bp, "%d", curhist);
		    bp += strlen(bp);
		}
	    }
	} else {
	    char c = *fm == Meta ? *++fm ^ 32 : *fm;

	    if (doprint) {
		addbufspc(1);
		pputc(c);
	    }
	}
    }

    return *fm;
}

/* pputc adds a character to the buffer, metafying.  There must *
 * already be space.                                            */

/**/
static void
pputc(char c)
{
    if(imeta(STOUC(c))) {
	*bp++ = Meta;
	c ^= 32;
    }
    *bp++ = c;
    if (c == '\n' && !dontcount)
	bufline = bp;
}

/* Make sure there is room for `need' more characters in the buffer. */

/**/
static void
addbufspc(int need)
{
    need *= 2;   /* for metafication */
    if((bp - buf) + need > bufspc) {
	int bo = bp - buf;
	int bo1 = bp1 ? bp1 - buf : -1;

	if(need & 255)
	    need = (need | 255) + 1;
	buf = realloc(buf, bufspc += need);
	bp = buf + bo;
	if(bo1 != -1)
	    bp1 = buf + bo1;
    }
}

/* stradd() adds a metafied string to the prompt, *
 * in a visible representation.                   */

/**/
void
stradd(char *d)
{
    char *ps, *pc;
    addbufspc(niceztrlen(d));
    /* This loop puts the nice representation of the string into the prompt *
     * buffer.                                                              */
    for(ps=d; *ps; ps++)
	for(pc=nicechar(*ps == Meta ? STOUC(*++ps)^32 : STOUC(*ps)); *pc; pc++)
	    *bp++ = *pc;
}

/* tsetcap(), among other things, can write a termcap string into the buffer. */

/**/
void
tsetcap(int cap, int flag)
{
    if (!(termflags & TERM_SHORT) && tcstr[cap]) {
	switch(flag) {
	case -1:
	    tputs(tcstr[cap], 1, putraw);
	    break;
	case 0:
	    tputs(tcstr[cap], 1, putshout);
	    break;
	case 1:
	    if (!dontcount) {
		addbufspc(1);
		*bp++ = Inpar;
	    }
	    tputs(tcstr[cap], 1, putstr);
	    if (!dontcount) {
		int glitch = 0;

		if (cap == TCSTANDOUTBEG || cap == TCSTANDOUTEND)
		    glitch = tgetnum("sg");
		else if (cap == TCUNDERLINEBEG || cap == TCUNDERLINEEND)
		    glitch = tgetnum("ug");
		if(glitch < 0)
		    glitch = 0;
		addbufspc(glitch + 1);
		while(glitch--)
		    *bp++ = Nularg;
		*bp++ = Outpar;
	    }
	    break;
	}

	if (txtisset(TXTDIRTY)) {
	    txtunset(TXTDIRTY);
	    if (txtisset(TXTBOLDFACE) && cap != TCBOLDFACEBEG)
		tsetcap(TCBOLDFACEBEG, flag);
	    if (txtisset(TXTSTANDOUT))
		tsetcap(TCSTANDOUTBEG, flag);
	    if (txtisset(TXTUNDERLINE))
		tsetcap(TCUNDERLINEBEG, flag);
	}
    }
}

/**/
int
putstr(int d)
{
    addbufspc(1);
    pputc(d);
    return 0;
}

/* Count height etc. of a prompt string returned by promptexpand(). *
 * This depends on the current terminal width, and tabs and         *
 * newlines require nontrivial processing.                          */

/**/
void
countprompt(char *str, int *wp, int *hp, int overf)
{
    int w = 0, h = 1;
    int s = 1;
    for(; *str; str++) {
	if(w >= columns) {
	    w = 0;
	    h++;
	}
	if(*str == Meta)
	    str++;
	if(*str == Inpar)
	    s = 0;
	else if(*str == Outpar)
	    s = 1;
	else if(*str == Nularg)
	    w++;
	else if(s) {
	    if(*str == '\t')
		w = (w | 7) + 1;
	    else if(*str == '\n') {
		w = 0;
		h++;
	    } else
		w++;
	}
    }
    if(w >= columns) {
	if (!overf || w > columns) {
	    w = 0;
	    h++;
	}
    }
    if(wp)
	*wp = w;
    if(hp)
	*hp = h;
}

/**/
static int
prompttrunc(int arg, int truncchar, int doprint, int endchar)
{
    if (arg) {
	char ch = *fm, *ptr = bp, *truncstr;
	int truncatleft = ch == '<';

	/*
	 * If there is already a truncation active, return so that
	 * can be finished, backing up so that the new truncation
	 * can be started afterwards.
	 */
	if (trunclen) {
	    while (*--fm != '%')
		;
	    fm--;
	    return 0;
	}

	trunclen = arg;
	if (*fm != ']')
	    fm++;
	while (*fm && *fm != truncchar) {
	    if (*fm == '\\' && fm[1])
		++fm;
	    addbufspc(1);
	    *bp++ = *fm++;
	}
	if (!*fm)
	    return 0;
	if (bp == ptr && truncchar == ']') {
	    addbufspc(1);
	    *bp++ = '<';
	}
	truncstr = ztrduppfx(ptr, bp - ptr);

	bp = ptr;
	fm++;
	putpromptchar(doprint, endchar);
	*bp = '\0';
	if (bp - ptr > trunclen) {
	    /*
	     * We need to truncate.  t points to the truncation string -- *
	     * which is inserted literally, without nice representation.  *
	     * tlen is its length, and maxlen is the amount of the main	  *
	     * string that we want to keep.  Note that if the truncation  *
	     * string is longer than the truncation length (tlen >	  *
	     * trunclen), the truncation string is used in full.	  *
	     */
	    char *t = truncstr;
	    int fullen = bp - ptr;
	    int tlen = ztrlen(t), maxlen;
	    if (tlen > fullen) {
		addbufspc(tlen - fullen);
		bp += tlen - fullen;
	    } else
		bp -= fullen - trunclen;
	    maxlen = tlen < trunclen ? trunclen - tlen : 0;
	    if (truncatleft) {
		if (maxlen)
		    memmove(ptr + strlen(t), ptr + fullen - maxlen,
			    maxlen);
		while (*t)
		    *ptr++ = *t++;
	    } else {
		ptr += maxlen;
		while (*t)
		    *ptr++ = *t++;
	    }
	}
	zsfree(truncstr);
	trunclen = 0;
	/*
	 * We may have returned early from the previous putpromptchar *
	 * because we found another truncation following this one.    *
	 * In that case we need to do the rest now.                   *
	 */
	if (!*fm)
	    return 0;
	if (*fm != endchar) {
	    fm++;
	    /*
	     * With trunclen set to zero, we always reach endchar *
	     * (or the terminating NULL) this time round.         *
	     */
	    if (!putpromptchar(doprint, endchar))
		return 0;
	    /* Now we have to trick it into matching endchar again */
	    fm--;
	}
    } else {
	if (*fm != ']')
	    fm++;
	while(*fm && *fm != truncchar) {
	    if (*fm == '\\' && fm[1])
		fm++;
	    fm++;
	}
	if (trunclen || !*fm)
	    return 0;
    }
    return 1;
}
