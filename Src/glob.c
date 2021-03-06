/*
 * glob.c - filename generation
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
#include "glob.pro"

/* flag for CSHNULLGLOB */

typedef struct gmatch *Gmatch; 

struct gmatch {
    char *name;
    long size;
    long atime;
    long mtime;
    long ctime;
    long links;
    long _size;
    long _atime;
    long _mtime;
    long _ctime;
    long _links;
};

#define GS_NAME   1
#define GS_SIZE   2
#define GS_ATIME  4
#define GS_MTIME  8
#define GS_CTIME 16
#define GS_LINKS 32

#define GS_SHIFT  5
#define GS__SIZE  (GS_SIZE << GS_SHIFT)
#define GS__ATIME (GS_ATIME << GS_SHIFT)
#define GS__MTIME (GS_MTIME << GS_SHIFT)
#define GS__CTIME (GS_CTIME << GS_SHIFT)
#define GS__LINKS (GS_LINKS << GS_SHIFT)

#define GS_DESC  2048

#define GS_NORMAL (GS_SIZE | GS_ATIME | GS_MTIME | GS_CTIME | GS_LINKS)
#define GS_LINKED (GS_NORMAL << GS_SHIFT)

/**/
int badcshglob;
 
static int mode;		/* != 0 if we are parsing glob patterns */
static int scanning;		/* != 0 if we are scanning file paths   */
static int pathpos;		/* position in pathbuf                  */
static int matchsz;		/* size of matchbuf                     */
static int matchct;		/* number of matches found              */
static char *pathbuf;		/* pathname buffer                      */
static int pathbufsz;		/* size of pathbuf			*/
static int pathbufcwd;		/* where did we chdir()'ed		*/
static Gmatch matchbuf;		/* array of matches                     */
static Gmatch matchptr;		/* &matchbuf[matchct]                   */
static char *colonmod;		/* colon modifiers in qualifier list    */

typedef struct stat *Statptr;	 /* This makes the Ultrix compiler happy.  Go figure. */

/* modifier for unit conversions */

#define TT_DAYS 0
#define TT_HOURS 1
#define TT_MINS 2
#define TT_WEEKS 3
#define TT_MONTHS 4

#define TT_BYTES 0
#define TT_POSIX_BLOCKS 1
#define TT_KILOBYTES 2
#define TT_MEGABYTES 3

typedef int (*TestMatchFunc) _((struct stat *, long));

struct qual {
    struct qual *next;		/* Next qualifier, must match                */
    struct qual *or;		/* Alternative set of qualifiers to match    */
    TestMatchFunc func;		/* Function to call to test match            */
    long data;			/* Argument passed to function               */
    int sense;			/* Whether asserting or negating             */
    int amc;			/* Flag for which time to test (a, m, c)     */
    int range;			/* Whether to test <, > or = (as per signum) */
    int units;			/* Multiplier for time or size, respectively */
};

/* Qualifiers pertaining to current pattern */
static struct qual *quals;

/* Other state values for current pattern */
static int qualct, qualorct;
static int range, amc, units;
static int gf_nullglob, gf_markdirs, gf_noglobdots, gf_listtypes, gf_follow;
static int gf_sorts, gf_nsorts, gf_sortlist[11];

/* Prefix, suffix for doing zle trickery */

/**/
char *glob_pre, *glob_suf;

/* pathname component in filename patterns */

struct complist {
    Complist next;
    Comp comp;
    int closure;		/* 1 if this is a (foo/)# */
    int follow; 		/* 1 to go thru symlinks */
};
struct comp {
    Comp left, right, next, exclude;
    char *str;
    int stat, errsmax;
};

/* Type of Comp:  a closure with one or two #'s, the end of a *
 * pattern or path component, a piece of path to be added.    */
#define C_ONEHASH	1
#define C_TWOHASH	2
#define C_OPTIONAL	4
#define C_STAR		8    
#define C_CLOSURE	(C_ONEHASH|C_TWOHASH|C_OPTIONAL|C_STAR)
#define C_LAST		16
#define C_PATHADD	32
#define C_LCMATCHUC	64
#define C_IGNCASE	128

/* Test macros for the above */
#define CLOSUREP(c)	(c->stat & C_CLOSURE)
#define ONEHASHP(c)	(c->stat & (C_ONEHASH|C_STAR))
#define TWOHASHP(c)	(c->stat & C_TWOHASH)
#define OPTIONALP(c)	(c->stat & C_OPTIONAL)
#define STARP(c)	(c->stat & C_STAR)
#define LASTP(c)	(c->stat & C_LAST)
#define PATHADDP(c)	(c->stat & C_PATHADD)

/* Flags passed down to guts when compiling */
#define GF_PATHADD	1	/* file glob, adding path components */
#define GF_TOPLEV	2	/* outside (), so ~ ends main match */

/* Next character after one which may be a Meta (x is any char *) */
#define METANEXT(x)	(*(x) == Meta ? (x)+2 : (x)+1)
/*
 * Increment pointer which may be on a Meta (x is a pointer variable),
 * returning the incremented value (i.e. like pre-increment).
 */
#define METAINC(x)	((x) += (*(x) == Meta) ? 2 : 1)
/*
 * Return unmetafied char from string (x is any char *)
 */
#define UNMETA(x)	(*(x) == Meta ? (x)[1] ^ 32 : *(x))

static char *pptr;		/* current place in string being matched */
static Comp tail;
static int first;		/* are leading dots special? */
static int longest;		/* always match longest piece of path. */
static int inclosure;		/* see comment in doesmatch() */

/* Add a component to pathbuf: This keeps track of how    *
 * far we are into a file name, since each path component *
 * must be matched separately.                            */

/**/
static void
addpath(char *s)
{
    DPUTS(!pathbuf, "BUG: pathbuf not initialised");
    while (pathpos + (int) strlen(s) + 1 >= pathbufsz)
	pathbuf = realloc(pathbuf, pathbufsz *= 2);
    while ((pathbuf[pathpos++] = *s++));
    pathbuf[pathpos - 1] = '/';
    pathbuf[pathpos] = '\0';
}

/* stat the filename s appended to pathbuf.  l should be true for lstat,    *
 * false for stat.  If st is NULL, the file is only chechked for existance. *
 * s == "" is treated as s == ".".  This is necessary since on most systems *
 * foo/ can be used to reference a non-directory foo.  Returns nonzero if   *
 * the file does not exists.                                                */

/**/
static int
statfullpath(const char *s, struct stat *st, int l)
{
    char buf[PATH_MAX];

    DPUTS(strlen(s) + !*s + pathpos - pathbufcwd >= PATH_MAX,
	  "BUG: statfullpath(): pathname too long");
    strcpy(buf, pathbuf + pathbufcwd);
    strcpy(buf + pathpos - pathbufcwd, s);
    if (!*s) {
	buf[pathpos - pathbufcwd] = '.';
	buf[pathpos - pathbufcwd + 1] = '\0';
	l = 0;
    }
    unmetafy(buf, NULL);
    if (!st)
	return access(buf, F_OK) && (!l || readlink(buf, NULL, 0));
    return l ? lstat(buf, st) : stat(buf, st);
}

/* add a match to the list */

/**/
static void
insert(char *s, int checked)
{
    struct stat buf, buf2, *bp;
    char *news = s;
    int statted = 0;

    if (gf_listtypes || gf_markdirs) {
	/* Add the type marker to the end of the filename */
	mode_t mode;
	checked = statted = 1;
	if (statfullpath(s, &buf, 1))
	    return;
	mode = buf.st_mode;
	if (gf_follow) {
	    if (!S_ISLNK(mode) || statfullpath(s, &buf2, 0))
		memcpy(&buf2, &buf, sizeof(buf));
	    statted = 2;
	    mode = buf2.st_mode;
	}
	if (gf_listtypes || S_ISDIR(mode)) {
	    int ll = strlen(s);

	    news = (char *)ncalloc(ll + 2);
	    strcpy(news, s);
	    news[ll] = file_type(mode);
	    news[ll + 1] = '\0';
	}
    }
    if (qualct || qualorct) {
	/* Go through the qualifiers, rejecting the file if appropriate */
	struct qual *qo, *qn;

	if (!statted && statfullpath(s, &buf, 1))
	    return;
	statted = 1;
	qo = quals;
	for (qn = qo; qn && qn->func;) {
	    range = qn->range;
	    amc = qn->amc;
	    units = qn->units;
	    if ((qn->sense & 2) && statted != 2) {
		/* If (sense & 2), we're following links */
		if (!S_ISLNK(buf.st_mode) || statfullpath(s, &buf2, 0))
		    memcpy(&buf2, &buf, sizeof(buf));
		statted = 2;
	    }
	    bp = (qn->sense & 2) ? &buf2 : &buf;
	    /* Reject the file if the function returned zero *
	     * and the sense was positive (sense&1 == 0), or *
	     * vice versa.                                   */
	    if ((!((qn->func) (bp, qn->data)) ^ qn->sense) & 1) {
		/* Try next alternative, or return if there are no more */
		if (!(qo = qo->or))
		    return;
		qn = qo;
		continue;
	    }
	    qn = qn->next;
	}
    } else if (!checked) {
	if (statfullpath(s, NULL, 1))
	    return;
	statted = 1;
    }
    news = dyncat(pathbuf, news);
    if (colonmod) {
	/* Handle the remainder of the qualifer:  e.g. (:r:s/foo/bar/). */
	s = colonmod;
	modify(&news, &s);
    }
    if (!statted && (gf_sorts & GS_NORMAL)) {
	statfullpath(s, &buf, 1);
	statted = 1;
    }
    if (statted != 2 && (gf_sorts & GS_LINKED)) {
	if (statted) {
	    if (!S_ISLNK(buf.st_mode) || statfullpath(s, &buf2, 0))
		memcpy(&buf2, &buf, sizeof(buf));
	} else if (statfullpath(s, &buf2, 0))
	    statfullpath(s, &buf2, 1);
    }
    matchptr->name = news;
    matchptr->size = buf.st_size;
    matchptr->atime = buf.st_atime;
    matchptr->mtime = buf.st_mtime;
    matchptr->ctime = buf.st_ctime;
    matchptr->links = buf.st_nlink;
    matchptr->_size = buf2.st_size;
    matchptr->_atime = buf2.st_atime;
    matchptr->_mtime = buf2.st_mtime;
    matchptr->_ctime = buf2.st_ctime;
    matchptr->_links = buf2.st_nlink;
    matchptr++;

    if (++matchct == matchsz) {
	matchbuf = (Gmatch )realloc((char *)matchbuf,
				    sizeof(struct gmatch) * (matchsz *= 2));

	matchptr = matchbuf + matchct;
    }
}

/* Check to see if str is eligible for filename generation. */

/**/
int
haswilds(char *str)
{
    /* `[' and `]' are legal even if bad patterns are usually not. */
    if ((*str == Inbrack || *str == Outbrack) && !str[1])
	return 0;

    /* If % is immediately followed by ?, then that ? is     *
     * not treated as a wildcard.  This is so you don't have *
     * to escape job references such as %?foo.               */
    if (str[0] == '%' && str[1] == Quest)
	str[1] = '?';

    for (; *str; str++) {
	switch (*str) {
	    case Inpar:
	    case Bar:
	    case Star:
	    case Inbrack:
	    case Inang:
	    case Quest:
		return 1;
	    case Pound:
	    case Hat:
		if (isset(EXTENDEDGLOB))
		    return 1;
		break;
	}
    }
    return 0;
}

/* Flags to apply to current level of grouping */

static int addflags;

/*
 * Approximate matching.
 *
 * errsmax is used while parsing to store the current number
 * of errors allowed.  While executing it is usually set to -1,
 * but can be set to something else to fix a different maximum
 * no. of errors which acts as a limit on the local value:  see
 * scanner() for why this is necessary.
 *
 * errsfound is used while executing a pattern to record the
 * number of errors found so far.
 */

static int errsmax, errsfound;

/* Do the globbing:  scanner is called recursively *
 * with successive bits of the path until we've    *
 * tried all of it.                                */

/**/
static void
scanner(Complist q)
{
    Comp c;
    int closure;
    int pbcwdsav = pathbufcwd;
    int errssofar = errsfound;
    struct dirsav ds;
    char *str;

    ds.ino = ds.dev = 0;
    ds.dirname = NULL;
    ds.dirfd = ds.level = -1;
    if (!q)
	return;

    if ((closure = q->closure)) {
	/* (foo/)# - match zero or more dirs */
	if (q->closure == 2)	/* (foo/)## - match one or more dirs */
	    q->closure = 1;
	else
	    scanner(q->next);
    }
    c = q->comp;
    str = c->str;
    /* Now the actual matching for the current path section. */
    if (!(c->next || c->left) && !haswilds(str)
	&& (!((c->stat & (C_LCMATCHUC|C_IGNCASE)) || c->errsmax)
	    || !*str || !strcmp(".", str) || !strcmp("..", str))) {
	/*
	 * We always need to match . and .. explicitly, even if we're
	 * checking other strings for case-insensitive matches.
	 *
	 * It's a straight string to the end of the path section.
	 */
	int l = strlen(str);

	if (l + !l + pathpos - pathbufcwd >= PATH_MAX) {
	    int err;

	    if (l >= PATH_MAX)
		return;
	    err = lchdir(pathbuf + pathbufcwd, &ds, 0);
	    if (err == -1)
		return;
	    if (err) {
		zerr("current directory lost during glob", NULL, 0);
		return;
	    }
	    pathbufcwd = pathpos;
	}
	if (q->next) {
	    /* Not the last path section. Just add it to the path. */
	    int oppos = pathpos;

	    if (!errflag && !(q->closure && !strcmp(str, "."))) {
		addpath(str);
		if (!closure || statfullpath("", NULL, 1))
		    scanner((q->closure) ? q : q->next);
		pathbuf[pathpos = oppos] = '\0';
	    }
	} else
	    insert(str, 0);
    } else {
	/* Do pattern matching on current path section. */
	char *fn = pathbuf[pathbufcwd] ? unmeta(pathbuf + pathbufcwd) : ".";
	int dirs = !!q->next;
	DIR *lock = opendir(fn);
	char *subdirs = NULL;
	int subdirlen = 0;

	if (lock == NULL)
	    return;
	while ((fn = zreaddir(lock, 1)) && !errflag) {
	    /* prefix and suffix are zle trickery */
	    if (!dirs && !colonmod &&
		((glob_pre && !strpfx(glob_pre, fn))
		 || (glob_suf && !strsfx(glob_suf, fn))))
		continue;
	    errsfound = errssofar;
	    if (domatch(fn, c, gf_noglobdots)) {
		/* if this name matchs the pattern... */
		if (pbcwdsav == pathbufcwd &&
		    strlen(fn) + pathpos - pathbufcwd >= PATH_MAX) {
		    int err;

		    DPUTS(pathpos == pathbufcwd,
			  "BUG: filename longer than PATH_MAX");
		    err = lchdir(pathbuf + pathbufcwd, &ds, 0);
		    if (err == -1)
			break;
		    if (err) {
			zerr("current directory lost during glob", NULL, 0);
			break;
		    }
		    pathbufcwd = pathpos;
		}
		if (dirs) {
		    int l;

		    /*
		     * If not the last component in the path:
		     *
		     * If we made an approximation in the new path segment,
		     * and the pattern includes closures other than a
		     * trailing * (we only test if it is not a string),
		     * then it is possible we made too many errors.  For
		     * example, (ab)#(cb)# will match the directory abcb
		     * with one error if allowed to, even though it can
		     * match with none.  This will stop later parts of the
		     * path matching, so we need to check by reducing the
		     * maximum number of errors and seeing if the directory
		     * still matches.  Luckily, this is not a terribly
		     * common case, since complex patterns typically occur
		     * in the last part of the path which is not affected
		     * by this problem.
		     */
		    if (errsfound > errssofar && (c->next || c->left)) {
			errsmax = errsfound - 1;
			while (errsmax >= errssofar) {
			    errsfound = errssofar;
			    if (!domatch(fn, c, gf_noglobdots))
				break;
			    errsmax = errsfound - 1;
			}
			errsfound = errsmax + 1;
			errsmax = -1;
		    }
		    if (closure) {
			/* if matching multiple directories */
			struct stat buf;

			if (statfullpath(fn, &buf, !q->follow)) {
			    if (errno != ENOENT && errno != EINTR &&
				errno != ENOTDIR && !errflag) {
				zerr("%e: %s", fn, errno);
				errflag = 0;
			    }
			    continue;
			}
			if (!S_ISDIR(buf.st_mode))
			    continue;
		    }
		    l = strlen(fn) + 1;
		    subdirs = hrealloc(subdirs, subdirlen, subdirlen + l
				       + sizeof(int));
		    strcpy(subdirs + subdirlen, fn);
		    subdirlen += l;
		    /* store the count of errors made so far, too */
		    memcpy(subdirs + subdirlen, (char *)&errsfound,
			   sizeof(int));
		    subdirlen += sizeof(int);
		} else
		    /* if the last filename component, just add it */
		    insert(fn, 1);
	    }
	}
	closedir(lock);
	if (subdirs) {
	    int oppos = pathpos;

	    for (fn = subdirs; fn < subdirs+subdirlen; ) {
		addpath(fn);
		fn += strlen(fn) + 1;
		memcpy((char *)&errsfound, fn, sizeof(int));
		fn += sizeof(int);
		scanner((q->closure) ? q : q->next);  /* scan next level */
		pathbuf[pathpos = oppos] = '\0';
	    }
	    hrealloc(subdirs, subdirlen, 0);
	}
    }
    if (pbcwdsav < pathbufcwd) {
	if (restoredir(&ds))
	    zerr("current directory lost during glob", NULL, 0);
	zsfree(ds.dirname);
	if (ds.dirfd >= 0)
	    close(ds.dirfd);
	pathbufcwd = pbcwdsav;
    }
}

/* Parse a series of path components pointed to by pptr */

/**/
static Comp
compalloc(void)
{
    Comp c = (Comp) alloc(sizeof *c);
    c->stat |= addflags;
    c->errsmax = errsmax;
    return c;
}

/**/
static int
getglobflags(void)
{
    char *nptr;
    /* (#X): assumes we are still positioned on the initial '(' */
    pptr++;
    while (*++pptr && *pptr != Outpar) {
	switch (*pptr) {
	case 'a':
	    /* Approximate matching, max no. of errors follows */
	    errsmax = zstrtol(++pptr, &nptr, 10);
	    if (pptr == nptr)
		return 1;
	    pptr = nptr-1;
	    break;

	case 'l':
	    /* Lowercase in pattern matches lower or upper in target */
	    addflags |= C_LCMATCHUC;
	    break;

	case 'i':
	    /* Fully case insensitive */
	    addflags |= C_IGNCASE;
	    break;

	case 'I':
	    /* Restore case sensitivity */
	    addflags &= ~(C_LCMATCHUC|C_IGNCASE);
	    break;

	default:
	    return 1;
	}
    }
    if (*pptr != Outpar)
	return 1;
    pptr++;
    return 0;
}

/**/
static void
parse_charset(void)
{
    /* Character set: brackets had better match */
    if (pptr[1] == Outbrack)
	*++pptr = ']';
    else if ((pptr[1] == Hat || pptr[1] == '^' || pptr[1] == '!') &&
	     pptr[2] == Outbrack)
	*(pptr += 2) = ']';
    while (*++pptr && *pptr != Outbrack) {
	if (itok(*pptr)) {
	    /* POSIX classes: make sure it's a real one,
	     * leave the Inbrack tokenised if so.
	     * We need to untokenize the Outbrack since otherwise
	     * it might look like we got to the end of the range without
	     * matching; we also need to accept ']' instead of
	     * Outbrack in case this has already happened.
	     */
	    char *nptr;
	    if (*pptr == Inbrack && pptr[1] == ':'
		&& (nptr = strchr(pptr+2, ':')) && 
		(*++nptr == Outbrack || *nptr == ']'))
		*(pptr = nptr) = ']';
	    else
		*pptr = ztokens[*pptr - Pound];
	}
    }
}

/* enum used with ksh-like patterns, @(...) etc. */

enum { KF_NONE, KF_AT, KF_QUEST, KF_STAR, KF_PLUS, KF_NOT };

/* parse lowest level pattern */

/**/
static Comp
parsecomp(int gflag)
{
    int kshfunc;
    Comp c = compalloc(), c1, c2;
    char *cstr, *ls = NULL;

    /* In case of alternatives, code coming up is stored in tail. */
    c->next = tail;
    cstr = pptr;

    while (*pptr && (mode || *pptr != '/') && *pptr != Bar &&
	   (unset(EXTENDEDGLOB) || *pptr != Tilde ||
	    !pptr[1] || pptr[1] == Outpar || pptr[1] == Bar) &&
	   *pptr != Outpar) {
	/* Go through code until we find something separating alternatives,
	 * or path components if relevant.
	 */
	if (*pptr == Hat && isset(EXTENDEDGLOB)) {
	    /* negate remaining pattern */
	    Comp stail = tail;
	    tail = NULL;
	    c->str = dupstrpfx(cstr, pptr - cstr);
	    pptr++;

	    c1 = compalloc();
	    c1->stat |= C_STAR;

	    c2 = compalloc();
	    if (!(c2->exclude = parsecomp(gflag)))
		return NULL;
	    if (!*pptr || *pptr == '/')
		c2->stat |= C_LAST;
	    c2->left = c1;
	    c2->next = stail;
	    c->next = c2;
	    tail = stail;
	    return c;
	}

	/* Ksh-type globs */
	kshfunc = KF_NONE;
	if (isset(KSHGLOB) && *pptr && pptr[1] == Inpar) {
	    switch (*pptr) {
	    case '@':		/* just do paren as usual */
		kshfunc = KF_AT;
		break;

	    case Quest:
	    case '?':		/* matched optionally, treat as (...|) */
		kshfunc = KF_QUEST;
		break;

	    case Star:
	    case '*':		/* treat as (...)# */
		kshfunc = KF_STAR;
		break;

	    case '+':		/* treat as (...)## */
		kshfunc = KF_PLUS;
		break;

	    case '!':		/* treat as (*~...) */
		kshfunc = KF_NOT;
		break;
	    }
	    if (kshfunc != KF_NONE)
		pptr++;
	}

	if (*pptr == Inpar && pptr[1] == Pound) {
	    /* Found some globbing flags */
	    char *eptr = pptr;
	    if (kshfunc != KF_NONE)
		eptr--;
	    if (getglobflags())
		return NULL;
	    if (eptr == cstr) {
		/* if no string yet, carry on and get one. */
		c->stat = addflags;
		c->errsmax = errsmax;
		cstr = pptr;
		continue;
	    }
	    c->str = dupstrpfx(cstr, eptr - cstr);
	    /*
	     * The next bit simply handles the case where . or ..
	     * is followed by a set of flags, but we need to force
	     * them to be handled as a string.  Hardly worth it.
	     */
	    if (!*pptr || (!mode && *pptr == '/') || *pptr == Bar ||
		(isset(EXTENDEDGLOB) && *pptr == Tilde &&
		 pptr[1] && pptr[1] != Outpar && pptr[1] != Bar) ||
		*pptr == Outpar) {
		if (*pptr == '/' || !*pptr ||
		    ((*pptr == Bar ||
		      (isset(EXTENDEDGLOB) && *pptr == Tilde)) &&
		     (gflag & GF_TOPLEV)))
		    c->stat |= C_LAST;
		return c;
	    }
	    if (!(c->next = parsecomp(gflag)))
		return NULL;
	    return c;
	}
	if (*pptr == Inpar) {
	    /* Found a group (...) */
	    char *startp = pptr, *endp;
	    Comp stail = tail;
	    int dpnd = 0;

	    /* Need matching close parenthesis */
	    if (skipparens(Inpar, Outpar, &pptr)) {
		errflag = 1;
		return NULL;
	    }
	    if (kshfunc == KF_STAR)
		dpnd = 1;
	    else if (kshfunc == KF_PLUS)
		dpnd = 2;
	    else if (kshfunc == KF_QUEST)
		dpnd = 3;
	    if (*pptr == Pound && isset(EXTENDEDGLOB)) {
		/* Zero (or one) or more repetitions of group */
		pptr++;
		if(*pptr == Pound) {
		    pptr++;
		    if(dpnd == 0)
			dpnd = 2;
		    else if(dpnd == 3)
			dpnd = 1;
		} else
		    dpnd = 1;
	    }
	    /* Parse the remaining pattern following the group... */
	    if (!(c1 = parsecomp(gflag)))
		return NULL;
	    /* ...remembering what comes after it... */
	    tail = (dpnd || kshfunc == KF_NOT) ? NULL : c1;
	    /* ...before going back and parsing inside the group. */
	    endp = pptr;
	    pptr = startp;
	    c->str = dupstrpfx(cstr, (pptr - cstr) - (kshfunc != KF_NONE));
	    pptr++;
	    c2 = compalloc();
	    c->next = c2;
	    c2->next = (dpnd || kshfunc == KF_NOT) ?
		c1 : compalloc();
	    if (!(c2->left = parsecompsw(0)))
		return NULL;
	    if (kshfunc == KF_NOT) {
		/* we'd actually rather it didn't match.  Instead, match *
		 * a star and put the parsed pattern into exclude.       */
		Comp c3 = compalloc();
		c3->stat |= C_STAR;

		c2->exclude = c2->left;
		c2->left = c3;
	    }
	    /* Remember closures for group. */
	    if (dpnd)
		c2->stat |= (dpnd == 3) ? C_OPTIONAL
		    : (dpnd == 2) ? C_TWOHASH : C_ONEHASH;
	    pptr = endp;
	    tail = stail;
	    return c;
	}
	if (*pptr == Star && pptr[1] &&
	    (unset(EXTENDEDGLOB) || !(gflag & GF_TOPLEV) ||
	     pptr[1] != Tilde || !pptr[2] || pptr[2] == Bar ||
	     pptr[2] == Outpar) && (mode || pptr[1] != '/')) {
	    /* Star followed by other patterns is now treated as a special
	     * type of closure in doesmatch().
	     */
	    c->str = dupstrpfx(cstr, pptr - cstr);
	    pptr++;
	    c1 = compalloc();
	    c1->stat |= C_STAR;
	    if (!(c2 = parsecomp(gflag)))
		return NULL;
	    c1->next = c2;
	    c->next = c1;
	    return c;
	}
	if (*pptr == Pound && isset(EXTENDEDGLOB)) {
	    /* repeat whatever we've just had (ls) zero or more times */
	    if (!ls)
		return NULL;
	    c2 = compalloc();
	    c2->str = dupstrpfx(ls, pptr - ls);
	    pptr++;
	    if (*pptr == Pound) {
		/* need one or more matches: cheat by copying previous char */
		pptr++;
		c->next = c1 = compalloc();
		c1->str = c2->str;
	    } else
		c1 = c;
	    c1->next = c2;
	    c2->stat |= C_ONEHASH;
	    /* parse the rest of the pattern and return. */
	    c2->next = parsecomp(gflag);
	    if (!c2->next)
		return NULL;
	    c->str = dupstrpfx(cstr, ls - cstr);
	    return c;
	}
	ls = pptr;		/* whatever we just parsed */
	if (*pptr == Inang) {
	    /* Numeric glob */
	    int dshct;

	    dshct = (pptr[1] == Outang);
	    while (*++pptr && *pptr != Outang)
		if (*pptr == '-' && !dshct)
		    dshct = 1;
		else if (!idigit(*pptr))
		    break;
	    if (*pptr != Outang)
		return NULL;
	} else if (*pptr == Inbrack) {
	    parse_charset();
	    if (*pptr != Outbrack)
		return NULL;
	} else if (itok(*pptr) && *pptr != Star && *pptr != Quest)
	    /* something that can be tokenised which isn't otherwise special */
	    *pptr = ztokens[*pptr - Pound];
	pptr++;
    }
    /* mark if last pattern component in path component or pattern */
    if (*pptr == '/' || !*pptr ||
	((*pptr == Bar ||
	 (isset(EXTENDEDGLOB) && *pptr == Tilde)) && (gflag & GF_TOPLEV)))
	c->stat |= C_LAST;
    c->str = dupstrpfx(cstr, pptr - cstr);
    return c;
}

/* Parse pattern possibly with different alternatives (|) */

/**/
static Comp
parsecompsw(int gflag)
{
    Comp c1, c2, c3, excl = NULL, stail = tail;
    int oaddflags = addflags;
    int oerrsmax = errsmax;
    char *sptr;

    /*
     * Check for a tilde in the expression.  We need to know this in 
     * advance so as to be able to treat the whole a~b expression by
     * backtracking:  see exclusion code in doesmatch().
     */
    if (isset(EXTENDEDGLOB)) {
	int pct = 0;
	for (sptr = pptr; *sptr; sptr++) {
	    if (*sptr == Inpar)
		pct++;
	    else if (*sptr == Outpar && --pct < 0)
		break;
	    else if (*sptr == Bar && !pct)
		break;
	    else if (*sptr == Inbrack) {
		/*
		 * Character classes can have tokenized characters in,
		 * so we have to parse them properly.
		 */
		char *bstart = pptr;

		pptr = sptr;
		parse_charset();
		sptr = pptr;
		pptr = bstart;
		if (*sptr != Outbrack)
		    break;
	    } else if (*sptr == Tilde && !pct) {
		tail = NULL;
		break;
	    }
	}
    }

    c1 = parsecomp(gflag);
    if (!c1)
	return NULL;
    if (isset(EXTENDEDGLOB) && *pptr == Tilde) {
	/* Matching remainder of pattern excludes the pattern from matching */
	int oldmode = mode, olderrsmax = errsmax;

	mode = 1;
	errsmax = 0;
	pptr++;
	excl = parsecomp(gflag);
	mode = oldmode;
	errsmax = olderrsmax;
	if (!excl)
	    return NULL;
    }
    tail = stail;
    if (*pptr == Bar || excl) {
	/* found an alternative or something to exclude */
	c2 = compalloc();
	if (*pptr == Bar) {
	    /* get the next alternative after the | */
	    pptr++;
	    c3 = parsecompsw(gflag);
	    if (!c3)
		return NULL;
	} else
	    c3 = NULL;
	/* mark if end of pattern or path component */
	if (!*pptr || *pptr == '/')
	    c1->stat |= c2->stat = C_LAST;
	c2->str = dupstring("");
	c2->left = c1;
	c2->right = c3;
	if ((c2->exclude = excl))
	    c2->next = stail;
	if (gflag & GF_PATHADD)
	    c2->stat |= C_PATHADD;
	c1 = c2;
    }
    if (!(gflag & GF_TOPLEV)) {
	addflags = oaddflags;
	errsmax = oerrsmax;
    }
    return c1;
}

/* This function tokenizes a zsh glob pattern */

/**/
static Complist
parsecomplist(void)
{
    Comp c1;
    Complist p1;
    char *str;

    if (pptr[0] == Star && pptr[1] == Star &&
        (pptr[2] == '/' || (pptr[2] == Star && pptr[3] == '/'))) {
	/* Match any number of directories. */
	int follow;

	/* with three stars, follow symbolic links */
	follow = (pptr[2] == Star);
	pptr += (3 + follow);

	/* Now get the next path component if there is one. */
	p1 = (Complist) alloc(sizeof *p1);
	if ((p1->next = parsecomplist()) == NULL) {
	    errflag = 1;
	    return NULL;
	}
	p1->comp = compalloc();
	p1->comp->stat |= C_LAST;	/* end of path component  */
	p1->comp->str = dupstring("*");
	*p1->comp->str = Star;		/* match anything...      */
	p1->closure = 1;		/* ...zero or more times. */
	p1->follow = follow;
	return p1;
    }

    /* Parse repeated directories such as (dir/)# and (dir/)## */
    if (*(str = pptr) == Inpar && !skipparens(Inpar, Outpar, &str) &&
        *str == Pound && isset(EXTENDEDGLOB) && str[-2] == '/') {
	pptr++;
	if (!(c1 = parsecompsw(0)))
	    return NULL;
	if (pptr[0] == '/' && pptr[1] == Outpar && pptr[2] == Pound) {
	    int pdflag = 0;

	    pptr += 3;
	    if (*pptr == Pound) {
		pdflag = 1;
		pptr++;
	    }
	    p1 = (Complist) alloc(sizeof *p1);
	    p1->comp = c1;
	    p1->closure = 1 + pdflag;
	    p1->follow = 0;
	    p1->next = parsecomplist();
	    return (p1->comp) ? p1 : NULL;
	}
    } else {
	/* parse single path component */
	if (!(c1 = parsecompsw(GF_PATHADD|GF_TOPLEV)))
	    return NULL;
	/* then do the remaining path compoents */
	if (*pptr == '/' || !*pptr) {
	    int ef = *pptr == '/';

	    p1 = (Complist) alloc(sizeof *p1);
	    p1->comp = c1;
	    p1->closure = 0;
	    p1->next = ef ? (pptr++, parsecomplist()) : NULL;
	    return (ef && !p1->next) ? NULL : p1;
	}
    }
    errflag = 1;
    return NULL;
}

/* turn a string into a Complist struct:  this has path components */

/**/
static Complist
parsepat(char *str)
{
    mode = 0;			/* path components present */
    addflags = 0;
    errsmax = 0;
    pptr = str;
    tail = NULL;
    /*
     * Check for initial globbing flags, so that they don't form
     * a bogus path component.
     */
    if (*pptr == Inpar && pptr[1] == Pound && isset(EXTENDEDGLOB) &&
	getglobflags())
	return NULL;

    /* Now there is no (#X) in front, we can check the path. */
    if (!pathbuf)
	pathbuf = zalloc(pathbufsz = PATH_MAX);
    DPUTS(pathbufcwd, "BUG: glob changed directory");
    if (*pptr == '/') {		/* pattern has absolute path */
	pptr++;
	pathbuf[0] = '/';
	pathbuf[pathpos = 1] = '\0';
    } else			/* pattern is relative to pwd */
	pathbuf[pathpos = 0] = '\0';

    return parsecomplist();
}

/* get number after qualifier */

/**/
static long
qgetnum(char **s)
{
    long v = 0;

    if (!idigit(**s)) {
	zerr("number expected", NULL, 0);
	return 0;
    }
    while (idigit(**s))
	v = v * 10 + *(*s)++ - '0';
    return v;
}

/* get mode spec after qualifier */

/**/
static long
qgetmodespec(char **s)
{
    long yes = 0, no = 0, val, mask, t;
    char *p = *s, c, how, end;

    if ((c = *p) == '=' || c == Equals || c == '+' || c == '-' ||
	c == '?' || c == Quest || (c >= '0' && c <= '7')) {
	end = 0;
	c = 0;
    } else {
	end = (c == '<' ? '>' :
	       (c == '[' ? ']' :
		(c == '{' ? '}' :
		 (c == Inang ? Outang :
		  (c == Inbrack ? Outbrack :
		   (c == Inbrace ? Outbrace : c))))));
	p++;
    }
    do {
	mask = 0;
	while (((c = *p) == 'u' || c == 'g' || c == 'o' || c == 'a') && end) {
	    switch (c) {
	    case 'o': mask |= 01007; break;
	    case 'g': mask |= 02070; break;
	    case 'u': mask |= 04700; break;
	    case 'a': mask |= 07777; break;
	    }
	    p++;
	}
	how = ((c == '+' || c == '-') ? c : '=');
	if (c == '+' || c == '-' || c == '=' || c == Equals)
	    p++;
	val = 0;
	if (mask) {
	    while ((c = *p++) != ',' && c != end) {
		switch (c) {
		case 'x': val |= 00111; break;
		case 'w': val |= 00222; break;
		case 'r': val |= 00444; break;
		case 's': val |= 06000; break;
		case 't': val |= 01000; break;
		case '0': case '1': case '2': case '3':
		case '4': case '5': case '6': case '7':
		    t = ((long) c - '0');
		    val |= t | (t << 3) | (t << 6);
		    break;
		default:
		    zerr("invalid mode specification", NULL, 0);
		    return 0;
		}
	    }
	    if (how == '=' || how == '+') {
		yes |= val & mask;
		val = ~val;
	    }
	    if (how == '=' || how == '-')
		no |= val & mask;
	} else {
	    t = 07777;
	    while ((c = *p) == '?' || c == Quest ||
		   (c >= '0' && c <= '7')) {
		if (c == '?' || c == Quest) {
		    t = (t << 3) | 7;
		    val <<= 3;
		} else {
		    t <<= 3;
		    val = (val << 3) | ((long) c - '0');
		}
		p++;
	    }
	    if (end && c != end && c != ',') {
		zerr("invalid mode specification", NULL, 0);
		return 0;
	    }
	    if (how == '=') {
		yes = (yes & ~t) | val;
		no = (no & ~t) | (~val & ~t);
	    } else if (how == '+')
		yes |= val;
	    else
		no |= val;
	}
    } while (end && c != end);

    *s = p;
    return ((yes & 07777) | ((no & 07777) << 12));
}

static int
gmatchcmp(Gmatch a, Gmatch b)
{
    int i, *s;
    long r = 0L;

    for (i = gf_nsorts, s = gf_sortlist; i; i--, s++) {
	switch (*s & ~GS_DESC) {
	case GS_NAME:
	    r = notstrcmp(&a->name, &b->name);
	    break;
	case GS_SIZE:
	    r = b->size - a->size;
	    break;
	case GS_ATIME:
	    r = a->atime - b->atime;
	    break;
	case GS_MTIME:
	    r = a->mtime - b->mtime;
	    break;
	case GS_CTIME:
	    r = a->ctime - b->ctime;
	    break;
	case GS_LINKS:
	    r = b->links - a->links;
	    break;
	case GS__SIZE:
	    r = b->_size - a->_size;
	    break;
	case GS__ATIME:
	    r = a->_atime - b->_atime;
	    break;
	case GS__MTIME:
	    r = a->_mtime - b->_mtime;
	    break;
	case GS__CTIME:
	    r = a->_ctime - b->_ctime;
	    break;
	case GS__LINKS:
	    r = b->_links - a->_links;
	    break;
	}
	if (r)
	    return (int) ((*s & GS_DESC) ? -r : r);
    }
    return 0;
}

/* Main entry point to the globbing code for filename globbing. *
 * np points to a node in the list list which will be expanded  *
 * into a series of nodes.                                      */

/**/
void
glob(LinkList list, LinkNode np)
{
    struct qual *qo, *qn, *ql;
    LinkNode node = prevnode(np);
    char *str;				/* the pattern                   */
    int sl;				/* length of the pattern         */
    Complist q;				/* pattern after parsing         */
    char *ostr = (char *)getdata(np);	/* the pattern before the parser */
					/* chops it up                   */
    int first = 0, last = -1;		/* index of first/last match to  */
				        /* return */
    MUSTUSEHEAP("glob");
    if (unset(GLOBOPT) || !haswilds(ostr)) {
	untokenize(ostr);
	return;
    }
    str = dupstring(ostr);
    sl = strlen(str);
    uremnode(list, np);

    /* Initialise state variables for current file pattern */
    qo = qn = quals = ql = NULL;
    qualct = qualorct = 0;
    colonmod = NULL;
    gf_nullglob = isset(NULLGLOB);
    gf_markdirs = isset(MARKDIRS);
    gf_listtypes = gf_follow = 0;
    gf_noglobdots = unset(GLOBDOTS);
    gf_sorts = gf_nsorts = 0;

    /* Check for qualifiers */
    if (isset(BAREGLOBQUAL) && str[sl - 1] == Outpar) {
	char *s;

	/* Check these are really qualifiers, not a set of *
	 * alternatives or exclusions                      */
	for (s = str + sl - 2; *s != Inpar; s--)
	    if (*s == Bar || *s == Outpar ||
		(isset(EXTENDEDGLOB) && *s == Tilde))
		break;
	if (*s == Inpar && (!isset(EXTENDEDGLOB) || s[1] != Pound)) {
	    /* Real qualifiers found. */
	    int sense = 0;	/* bit 0 for match (0)/don't match (1)   */
				/* bit 1 for follow links (2), don't (0) */
	    long data = 0;	/* Any numerical argument required       */
	    int (*func) _((Statptr, long));

	    str[sl-1] = 0;
	    *s++ = 0;
	    while (*s && !colonmod) {
		func = (int (*) _((Statptr, long)))0;
		if (idigit(*s)) {
		    /* Store numeric argument for qualifier */
		    func = qualflags;
		    data = 0;
		    while (idigit(*s))
			data = data * 010 + (*s++ - '0');
		} else if (*s == ',') {
		    /* A comma separates alternative sets of qualifiers */
		    s++;
		    sense = 0;
		    if (qualct) {
			qn = (struct qual *)hcalloc(sizeof *qn);
			qo->or = qn;
			qo = qn;
			qualorct++;
			qualct = 0;
			ql = NULL;
		    }
		} else
		    switch (*s++) {
		    case ':':
			/* Remaining arguments are history-type     *
			 * colon substitutions, handled separately. */
			colonmod = s - 1;
			untokenize(colonmod);
			break;
		    case Hat:
		    case '^':
			/* Toggle sense:  go from positive to *
			 * negative match and vice versa.     */
			sense ^= 1;
			break;
		    case '-':
			/* Toggle matching of symbolic links */
			sense ^= 2;
			break;
		    case '@':
			/* Match symbolic links */
			func = qualislnk;
			break;
		    case Equals:
		    case '=':
			/* Match sockets */
			func = qualissock;
			break;
		    case 'p':
			/* Match named pipes */
			func = qualisfifo;
			break;
		    case '/':
			/* Match directories */
			func = qualisdir;
			break;
		    case '.':
			/* Match regular files */
			func = qualisreg;
			break;
		    case '%':
			/* Match special files: block, *
			 * character or any device     */
			if (*s == 'b')
			    s++, func = qualisblk;
			else if (*s == 'c')
			    s++, func = qualischr;
			else
			    func = qualisdev;
			break;
		    case Star:
			/* Match executable plain files */
			func = qualiscom;
			break;
		    case 'R':
			/* Match world-readable files */
			func = qualflags;
			data = 0004;
			break;
		    case 'W':
			/* Match world-writeable files */
			func = qualflags;
			data = 0002;
			break;
		    case 'X':
			/* Match world-executable files */
			func = qualflags;
			data = 0001;
			break;
		    case 'A':
			func = qualflags;
			data = 0040;
			break;
		    case 'I':
			func = qualflags;
			data = 0020;
			break;
		    case 'E':
			func = qualflags;
			data = 0010;
			break;
		    case 'r':
			/* Match files readable by current process */
			func = qualflags;
			data = 0400;
			break;
		    case 'w':
			/* Match files writeable by current process */
			func = qualflags;
			data = 0200;
			break;
		    case 'x':
			/* Match files executable by current process */
			func = qualflags;
			data = 0100;
			break;
		    case 's':
			/* Match setuid files */
			func = qualflags;
			data = 04000;
			break;
		    case 'S':
			/* Match setgid files */
			func = qualflags;
			data = 02000;
			break;
		    case 't':
			func = qualflags;
			data = 01000;
			break;
		    case 'd':
			/* Match device files by device number  *
			 * (as given by stat's st_dev element). */
			func = qualdev;
			data = qgetnum(&s);
			break;
		    case 'l':
			/* Match files with the given no. of hard links */
			func = qualnlink;
			amc = -1;
			goto getrange;
		    case 'U':
			/* Match files owned by effective user ID */
			func = qualuid;
			data = geteuid();
			break;
		    case 'G':
			/* Match files owned by effective group ID */
			func = qualgid;
			data = getegid();
			break;
		    case 'u':
			/* Match files owned by given user id */
			func = qualuid;
			/* either the actual uid... */
			if (idigit(*s))
			    data = qgetnum(&s);
			else {
			    /* ... or a user name */
			    char sav, *tt;

			    /* Find matching delimiters */
			    tt = get_strarg(s);
			    if (!*tt) {
				zerr("missing end of name",
				     NULL, 0);
				data = 0;
			    } else {
#ifdef HAVE_GETPWNAM
				struct passwd *pw;
				sav = *tt;
				*tt = '\0';

				if ((pw = getpwnam(s + 1)))
				    data = pw->pw_uid;
				else {
				    zerr("unknown user", NULL, 0);
				    data = 0;
				}
				*tt = sav;
#else /* !HAVE_GETPWNAM */
				sav = *tt;
				zerr("unknown user", NULL, 0);
				data = 0;
#endif /* !HAVE_GETPWNAM */
				if (sav)
				    s = tt + 1;
				else
				    s = tt;
			    }
			}
			break;
		    case 'g':
			/* Given gid or group id... works like `u' */
			func = qualgid;
			/* either the actual gid... */
			if (idigit(*s))
			    data = qgetnum(&s);
			else {
			    /* ...or a delimited group name. */
			    char sav, *tt;

			    tt = get_strarg(s);
			    if (!*tt) {
				zerr("missing end of name",
				     NULL, 0);
				data = 0;
			    } else {
#ifdef HAVE_GETGRNAM
				struct group *gr;
				sav = *tt;
				*tt = '\0';

				if ((gr = getgrnam(s + 1)))
				    data = gr->gr_gid;
				else {
				    zerr("unknown group", NULL, 0);
				    data = 0;
				}
				*tt = sav;
#else /* !HAVE_GETGRNAM */
				sav = *tt;
				zerr("unknown group", NULL, 0);
				data = 0;
#endif /* !HAVE_GETGRNAM */
				if (sav)
				    s = tt + 1;
				else
				    s = tt;
			    }
			}
			break;
		    case 'f':
			/* Match modes with chmod-spec. */
			func = qualmodeflags;
			data = qgetmodespec(&s);
			break;
		    case 'M':
			/* Mark directories with a / */
			if ((gf_markdirs = !(sense & 1)))
			    gf_follow = sense & 2;
			break;
		    case 'T':
			/* Mark types in a `ls -F' type fashion */
			if ((gf_listtypes = !(sense & 1)))
			    gf_follow = sense & 2;
			break;
		    case 'N':
			/* Nullglob:  remove unmatched patterns. */
			gf_nullglob = !(sense & 1);
			break;
		    case 'D':
			/* Glob dots: match leading dots implicitly */
			gf_noglobdots = sense & 1;
			break;
		    case 'a':
			/* Access time in given range */
			amc = 0;
			func = qualtime;
			goto getrange;
		    case 'm':
			/* Modification time in given range */
			amc = 1;
			func = qualtime;
			goto getrange;
		    case 'c':
			/* Inode creation time in given range */
			amc = 2;
			func = qualtime;
			goto getrange;
		    case 'L':
			/* File size (Length) in given range */
			func = qualsize;
			amc = -1;
			/* Get size multiplier */
			units = TT_BYTES;
			if (*s == 'p' || *s == 'P')
			    units = TT_POSIX_BLOCKS, ++s;
			else if (*s == 'k' || *s == 'K')
			    units = TT_KILOBYTES, ++s;
			else if (*s == 'm' || *s == 'M')
			    units = TT_MEGABYTES, ++s;
		      getrange:
			/* Get time multiplier */
			if (amc >= 0) {
			    units = TT_DAYS;
			    if (*s == 'h')
				units = TT_HOURS, ++s;
			    else if (*s == 'm')
				units = TT_MINS, ++s;
			    else if (*s == 'w')
				units = TT_WEEKS, ++s;
			    else if (*s == 'M')
				units = TT_MONTHS, ++s;
			}
			/* See if it's greater than, equal to, or less than */
			if ((range = *s == '+' ? 1 : *s == '-' ? -1 : 0))
			    ++s;
			data = qgetnum(&s);
			break;

		    case 'o':
		    case 'O':
			{
			    int t;

			    switch (*s) {
			    case 'n': t = GS_NAME; break;
			    case 'L': t = GS_SIZE; break;
			    case 'l': t = GS_LINKS; break;
			    case 'a': t = GS_ATIME; break;
			    case 'm': t = GS_MTIME; break;
			    case 'c': t = GS_CTIME; break;
			    default:
				zerr("unknown sort specifier", NULL, 0);
				return;
			    }
			    if ((sense & 2) && t != GS_NAME)
				t <<= GS_SHIFT;
			    if (gf_sorts & t) {
				zerr("doubled sort specifier", NULL, 0);
				return;
			    }
			    gf_sorts |= t;
			    gf_sortlist[gf_nsorts++] = t |
				(((sense & 1) ^ (s[-1] == 'O')) ? GS_DESC : 0);
			    s++;
			    break;
			}
		    case '[':
		    case Inbrack:
			{
			    char *os = --s;
			    struct value v;

			    v.isarr = SCANPM_WANTVALS;
			    v.pm = NULL;
			    v.b = -1;
			    v.inv = 0;
			    if (getindex(&s, &v) || s == os) {
				zerr("invalid subscript", NULL, 0);
				return;
			    }
			    first = v.a;
			    last = v.b;
			    break;
			}
		    default:
			zerr("unknown file attribute", NULL, 0);
			return;
		    }
		if (func) {
		    /* Requested test is performed by function func */
		    if (!qn)
			qn = (struct qual *)hcalloc(sizeof *qn);
		    if (ql)
			ql->next = qn;
		    ql = qn;
		    if (!quals)
			quals = qo = qn;
		    qn->func = func;
		    qn->sense = sense;
		    qn->data = data;
		    qn->range = range;
		    qn->units = units;
		    qn->amc = amc;
		    qn = NULL;
		    qualct++;
		}
		if (errflag)
		    return;
	    }
	}
    }
    q = parsepat(str);
    if (!q || errflag) {	/* if parsing failed */
	if (unset(BADPATTERN)) {
	    untokenize(ostr);
	    insertlinknode(list, node, ostr);
	    return;
	}
	errflag = 0;
	zerr("bad pattern: %s", ostr, 0);
	return;
    }
    if (!gf_nsorts) {
	gf_sortlist[0] = gf_sorts = GS_NAME;
	gf_nsorts = 1;
    }
    /* Initialise receptacle for matched files, *
     * expanded by insert() where necessary.    */
    matchptr = matchbuf = (Gmatch)zalloc((matchsz = 16) *
					 sizeof(struct gmatch));
    matchct = 0;
    errsfound = 0;
    errsmax = -1;

    /* The actual processing takes place here: matches go into  *
     * matchbuf.  This is the only top-level call to scanner(). */
    scanning = 1;
    scanner(q);
    scanning = 0;

    /* Deal with failures to match depending on options */
    if (matchct)
	badcshglob |= 2;	/* at least one cmd. line expansion O.K. */
    else if (!gf_nullglob) {
	if (isset(CSHNULLGLOB)) {
	    badcshglob |= 1;	/* at least one cmd. line expansion failed */
	} else if (isset(NOMATCH)) {
	    zerr("no matches found: %s", ostr, 0);
	    free(matchbuf);
	    return;
	} else {
	    /* treat as an ordinary string */
	    untokenize(matchptr->name = dupstring(ostr));
	    matchptr++;
	    matchct = 1;
	}
    }
    /* Sort arguments in to lexical (and possibly numeric) order. *
     * This is reversed to facilitate insertion into the list.    */
    qsort((void *) & matchbuf[0], matchct, sizeof(struct gmatch),
	       (int (*) _((const void *, const void *)))gmatchcmp);

    if (first < 0)
	first += matchct;
    if (last < 0)
	last += matchct;
    if (first < 0)
	first = 0;
    if (last >= matchct)
	last = matchct - 1;
    if (first <= last) {
	matchptr = matchbuf + matchct - 1 - last;
	last -= first;
	while (last-- >= 0) {		/* insert matches in the arg list */
	    insertlinknode(list, node, matchptr->name);
	    matchptr++;
	}
    }
    free(matchbuf);
}

/* Return the order of two strings, taking into account *
 * possible numeric order if NUMERICGLOBSORT is set.    *
 * The comparison here is reversed.                     */

/**/
static int
notstrcmp(char **a, char **b)
{
    char *c = *b, *d = *a;
    int cmp;

#ifdef HAVE_STRCOLL
    cmp = strcoll(c, d);
#endif
    for (; *c == *d && *c; c++, d++);
#ifndef HAVE_STRCOLL
    cmp = (int)STOUC(*c) - (int)STOUC(*d);
#endif
    if (isset(NUMERICGLOBSORT) && (idigit(*c) || idigit(*d))) {
	for (; c > *b && idigit(c[-1]); c--, d--);
	if (idigit(*c) && idigit(*d)) {
	    while (*c == '0')
		c++;
	    while (*d == '0')
		d++;
	    for (; idigit(*c) && *c == *d; c++, d++);
	    if (idigit(*c) || idigit(*d)) {
		cmp = (int)STOUC(*c) - (int)STOUC(*d);
		while (idigit(*c) && idigit(*d))
		    c++, d++;
		if (idigit(*c) && !idigit(*d))
		    return 1;
		if (idigit(*d) && !idigit(*c))
		    return -1;
	    }
	}
    }
    return cmp;
}

/* Return the trailing character for marking file types */

/**/
char
file_type(mode_t filemode)
{
    if(S_ISBLK(filemode))
	return '#';
    else if(S_ISCHR(filemode))
	return '%';
    else if(S_ISDIR(filemode))
	return '/';
    else if(S_ISFIFO(filemode))
	return '|';
    else if(S_ISLNK(filemode))
	return '@';
    else if(S_ISREG(filemode))
	return (filemode & S_IXUGO) ? '*' : ' ';
    else if(S_ISSOCK(filemode))
	return '=';
    else
	return '?';
}

/* check to see if str is eligible for brace expansion */

/**/
int
hasbraces(char *str)
{
    char *lbr, *mbr, *comma;

    if (isset(BRACECCL)) {
	/* In this case, any properly formed brace expression  *
	 * will match and expand to the characters in between. */
	int bc;

	for (bc = 0; *str; ++str)
	    if (*str == Inbrace) {
		if (!bc && str[1] == Outbrace)
		    *str++ = '{', *str = '}';
		else
		    bc++;
	    } else if (*str == Outbrace) {
		if (!bc)
		    *str = '}';
		else if (!--bc)
		    return 1;
	    }
	return 0;
    }
    /* Otherwise we need to look for... */
    lbr = mbr = comma = NULL;
    for (;;) {
	switch (*str++) {
	case Inbrace:
	    if (!lbr) {
		lbr = str - 1;
		while (idigit(*str))
		    str++;
		if (*str == '.' && str[1] == '.') {
		    str++;
		    while (idigit(*++str));
		    if (*str == Outbrace &&
			(idigit(lbr[1]) || idigit(str[-1])))
			return 1;
		}
	    } else {
		char *s = --str;

		if (skipparens(Inbrace, Outbrace, &str)) {
		    *lbr = *s = '{';
		    if (comma)
			str = comma;
		    if (mbr && mbr < str)
			str = mbr;
		    lbr = mbr = comma = NULL;
		} else if (!mbr)
		    mbr = s;
	    }
	    break;
	case Outbrace:
	    if (!lbr)
		str[-1] = '}';
	    else if (comma)
		return 1;
	    else {
		*lbr = '{';
		str[-1] = '}';
		if (mbr)
		    str = mbr;
		mbr = lbr = NULL;
	    }
	    break;
	case Comma:
	    if (!lbr)
		str[-1] = ',';
	    else if (!comma)
		comma = str - 1;
	    break;
	case '\0':
	    if (lbr)
		*lbr = '{';
	    if (!mbr && !comma)
		return 0;
	    if (comma)
		str = comma;
	    if (mbr && mbr < str)
		str = mbr;
	    lbr = mbr = comma = NULL;
	    break;
	}
    }
}

/* expand stuff like >>*.c */

/**/
int
xpandredir(struct redir *fn, LinkList tab)
{
    LinkList fake;
    char *nam;
    struct redir *ff;
    int ret = 0;

    /* Stick the name in a list... */
    fake = newlinklist();
    addlinknode(fake, fn->name);
    /* ...which undergoes all the usual shell expansions */
    prefork(fake, isset(MULTIOS) ? 0 : PF_SINGLE);
    /* Globbing is only done for multios. */
    if (!errflag && isset(MULTIOS))
	globlist(fake);
    if (errflag)
	return 0;
    if (nonempty(fake) && !nextnode(firstnode(fake))) {
	/* Just one match, the usual case. */
	char *s = peekfirst(fake);
	fn->name = s;
	untokenize(s);
	if (fn->type == MERGEIN || fn->type == MERGEOUT) {
	    if (s[0] == '-' && !s[1])
		fn->type = CLOSE;
	    else if (s[0] == 'p' && !s[1]) 
		fn->fd2 = (fn->type == MERGEOUT) ? coprocout : coprocin;
	    else {
		while (idigit(*s))
		    s++;
		if (!*s && s > fn->name)
		    fn->fd2 = zstrtol(fn->name, NULL, 10);
		else if (fn->type == MERGEIN)
		    zerr("file number expected", NULL, 0);
		else
		    fn->type = ERRWRITE;
	    }
	}
    } else if (fn->type == MERGEIN)
	zerr("file number expected", NULL, 0);
    else {
	if (fn->type == MERGEOUT)
	    fn->type = ERRWRITE;
	while ((nam = (char *)ugetnode(fake))) {
	    /* Loop over matches, duplicating the *
	     * redirection for each file found.   */
	    ff = (struct redir *)alloc(sizeof *ff);
	    *ff = *fn;
	    ff->name = nam;
	    addlinknode(tab, ff);
	    ret = 1;
	}
    }
    return ret;
}

/* concatenate s1 and s2 in dynamically allocated buffer */

/**/
char *
dyncat(char *s1, char *s2)
{
    /* This version always uses space from the current heap. */
    char *ptr;
    int l1 = strlen(s1);

    ptr = (char *)ncalloc(l1 + strlen(s2) + 1);
    strcpy(ptr, s1);
    strcpy(ptr + l1, s2);
    return ptr;
}

/* concatenate s1, s2, and s3 in dynamically allocated buffer */

/**/
char *
tricat(char const *s1, char const *s2, char const *s3)
{
    /* This version always uses permanently-allocated space. */
    char *ptr;

    ptr = (char *)zalloc(strlen(s1) + strlen(s2) + strlen(s3) + 1);
    strcpy(ptr, s1);
    strcat(ptr, s2);
    strcat(ptr, s3);
    return ptr;
}

/* brace expansion */

/**/
void
xpandbraces(LinkList list, LinkNode *np)
{
    LinkNode node = (*np), last = prevnode(node);
    char *str = (char *)getdata(node), *str3 = str, *str2;
    int prev, bc, comma, dotdot;

    for (; *str != Inbrace; str++);
    /* First, match up braces and see what we have. */
    for (str2 = str, bc = comma = dotdot = 0; *str2; ++str2)
	if (*str2 == Inbrace)
	    ++bc;
	else if (*str2 == Outbrace) {
	    if (--bc == 0)
		break;
	} else if (bc == 1) {
	    if (*str2 == Comma)
		++comma;	/* we have {foo,bar} */
	    else if (*str2 == '.' && str2[1] == '.')
		dotdot++;	/* we have {num1..num2} */
	}
    DPUTS(bc, "BUG: unmatched brace in xpandbraces()");
    if (!comma && dotdot) {
	/* Expand range like 0..10 numerically: comma or recursive
	   brace expansion take precedence. */
	char *dots, *p;
	LinkNode olast = last;
	/* Get the first number of the range */
	int rstart = zstrtol(str+1,&dots,10), rend = 0, err = 0, rev = 0;
	int wid1 = (dots - str) - 1, wid2 = (str2 - dots) - 2;
	int strp = str - str3;
      
	if (dots == str + 1 || *dots != '.' || dots[1] != '.')
	    err++;
	else {
	    /* Get the last number of the range */
	    rend = zstrtol(dots+2,&p,10);
	    if (p == dots+2 || p != str2)
		err++;
	}
	if (!err) {
	    /* If either no. begins with a zero, pad the output with   *
	     * zeroes. Otherwise, choose a min width to suppress them. */
	    int minw = (str[1] == '0') ? wid1 : (dots[2] == '0' ) ? wid2 :
		(wid2 > wid1) ? wid1 : wid2;
	    if (rstart > rend) {
		/* Handle decreasing ranges correctly. */
		int rt = rend;
		rend = rstart;
		rstart = rt;
		rev = 1;
	    }
	    uremnode(list, node);
	    for (; rend >= rstart; rend--) {
		/* Node added in at end, so do highest first */
		p = dupstring(str3);
		sprintf(p + strp, "%0*d", minw, rend);
		strcat(p + strp, str2 + 1);
		insertlinknode(list, last, p);
		if (rev)	/* decreasing:  add in reverse order. */
		    last = nextnode(last);
	    }
	    *np = nextnode(olast);
	    return;
	}
    }
    if (!comma && isset(BRACECCL)) {	/* {a-mnop} */
	/* Here we expand each character to a separate node,      *
	 * but also ranges of characters like a-m.  ccl is a      *
	 * set of flags saying whether each character is present; *
	 * the final list is in lexical order.                    */
	char ccl[256], *p;
	unsigned char c1, c2, lastch;
	unsigned int len, pl;

	uremnode(list, node);
	memset(ccl, 0, sizeof(ccl) / sizeof(ccl[0]));
	for (p = str + 1, lastch = 0; p < str2;) {
	    if (itok(c1 = *p++))
		c1 = ztokens[c1 - STOUC(Pound)];
	    if ((char) c1 == Meta)
		c1 = 32 ^ *p++;
	    if (itok(c2 = *p))
		c2 = ztokens[c2 - STOUC(Pound)];
	    if ((char) c2 == Meta)
		c2 = 32 ^ p[1];
	    if (c1 == '-' && lastch && p < str2 && (int)lastch <= (int)c2) {
		while ((int)lastch < (int)c2)
		    ccl[lastch++] = 1;
		lastch = 0;
	    } else
		ccl[lastch = c1] = 1;
	}
	pl = str - str3;
	len = pl + strlen(++str2) + 2;
	for (p = ccl + 255; p-- > ccl;)
	    if (*p) {
		c1 = p - ccl;
		if (imeta(c1)) {
		    str = ncalloc(len + 1);
		    str[pl] = Meta;
		    str[pl+1] = c1 ^ 32;
		    strcpy(str + pl + 2, str2);
		} else {
		    str = ncalloc(len);
		    str[pl] = c1;
		    strcpy(str + pl + 1, str2);
		}
		memcpy(str, str3, pl);
		insertlinknode(list, last, str);
	    }
	*np = nextnode(last);
	return;
    }
    prev = str++ - str3;
    str2++;
    uremnode(list, node);
    node = last;
    /* Finally, normal comma expansion               *
     * str1{foo,bar}str2 -> str1foostr2 str1barstr2. *
     * Any number of intervening commas is allowed.  */
    for (;;) {
	char *zz, *str4;
	int cnt;

	for (str4 = str, cnt = 0; cnt || (*str != Comma && *str !=
					  Outbrace); str++) {
	    if (*str == Inbrace)
		cnt++;
	    else if (*str == Outbrace)
		cnt--;
	    DPUTS(!*str, "BUG: illegal brace expansion");
	}
	/* Concatenate the string before the braces (str3), the section *
	 * just found (str4) and the text after the braces (str2)       */
	zz = (char *)ncalloc(prev + (str - str4) + strlen(str2) + 1);
	ztrncpy(zz, str3, prev);
	strncat(zz, str4, str - str4);
	strcat(zz, str2);
	/* and add this text to the argument list. */
	insertlinknode(list, node, zz);
	incnode(node);
	if (*str != Outbrace)
	    str++;
	else
	    break;
    }
    *np = nextnode(last);
}

/* check to see if a matches b (b is not a filename pattern) */

/**/
int
matchpat(char *a, char *b)
{
    Comp c = parsereg(b);

    if (!c) {
	zerr("bad pattern: %s", b, 0);
	return 0;
    }
    return domatch(a, c, 0);
}

/* do the ${foo%%bar}, ${foo#bar} stuff */
/* please do not laugh at this code. */

struct repldata {
    int b, e;			/* beginning and end of chunk to replace */
};
typedef struct repldata *Repldata;

/* 
 * List of bits of matches to concatenate with replacement string.
 * The data is a struct repldata.  It is not used in cases like
 * ${...//#foo/bar} even though SUB_GLOBAL is set, since the match
 * is anchored.  It goes on the heap.
 */

static LinkList repllist;

/* Having found a match in getmatch, decide what part of string
 * to return.  The matched part starts b characters into string s
 * and finishes e characters in: 0 <= b <= e <= strlen(s)
 * (yes, empty matches should work).
 * fl is a set of the SUB_* matches defined in zsh.h from SUB_MATCH onwards;
 * the lower parts are ignored.
 * replstr is the replacement string for a substitution
 */

/**/
static char *
get_match_ret(char *s, int b, int e, int fl, char *replstr)
{
    char buf[80], *r, *p, *rr;
    int ll = 0, l = strlen(s), bl = 0, t = 0, i;

    if (replstr) {
	if ((fl & SUB_GLOBAL) && repllist) {
	    /* We are replacing the chunk, just add this to the list */
	    Repldata rd = (Repldata) zhalloc(sizeof(*rd));
	    rd->b = b;
	    rd->e = e;
	    addlinknode(repllist, rd);
	    return s;
	}
	ll += strlen(replstr);
    }
    if (fl & SUB_MATCH)			/* matched portion */
	ll += 1 + (e - b);
    if (fl & SUB_REST)		/* unmatched portion */
	ll += 1 + (l - (e - b));
    if (fl & SUB_BIND) {
	/* position of start of matched portion */
	sprintf(buf, "%d ", b + 1);
	ll += (bl = strlen(buf));
    }
    if (fl & SUB_EIND) {
	/* position of end of matched portion */
	sprintf(buf + bl, "%d ", e + 1);
	ll += (bl = strlen(buf));
    }
    if (fl & SUB_LEN) {
	/* length of matched portion */
	sprintf(buf + bl, "%d ", e - b);
	ll += (bl = strlen(buf));
    }
    if (bl)
	buf[bl - 1] = '\0';

    rr = r = (char *)ncalloc(ll);

    if (fl & SUB_MATCH) {
	/* copy matched portion to new buffer */
	for (i = b, p = s + b; i < e; i++)
	    *rr++ = *p++;
	t = 1;
    }
    if (fl & SUB_REST) {
	/* Copy unmatched portion to buffer.  If both portions *
	 * requested, put a space in between (why?)            */
	if (t)
	    *rr++ = ' ';
	/* there may be unmatched bits at both beginning and end of string */
	for (i = 0, p = s; i < b; i++)
	    *rr++ = *p++;
	if (replstr)
	    for (p = replstr; *p; )
		*rr++ = *p++;
	for (i = e, p = s + e; i < l; i++)
	    *rr++ = *p++;
	t = 1;
    }
    *rr = '\0';
    if (bl) {
	/* if there was a buffer (with a numeric result), add it; *
	 * if there was other stuff too, stick in a space first.  */
	if (t)
	    *rr++ = ' ';
	strcpy(rr, buf);
    }
    return r;
}

/*
 * Run the pattern so that we always get the longest possible match.
 * This eliminates a loop where we gradually shorten the target string
 * to find same.  We also need to check pptr (the point successfully
 * reached along the target string) explicitly.
 *
 * For this to work, we need the full hairy closure code, so
 * set inclosure.
 */

/**/
static int
dolongestmatch(char *str, Comp c, int fist)
{
    int ret;
    longest = 1;
    inclosure++;
    ret = domatch(str, c, fist);
    inclosure--;
    longest = 0;
    return ret;
}

/*
 * This is called from paramsubst to get the match for ${foo#bar} etc.
 * fl is a set of the SUB_* flags defined in zsh.h
 * *sp points to the string we have to modify. The n'th match will be
 * returned in *sp. ncalloc is used to get memory for the result string.
 * replstr is the replacement string from a ${.../orig/repl}, in
 * which case pat is the original.
 *
 * n is now ignored unless we are looking for a substring, in
 * which case the n'th match from the start is counted such that
 * there is no more than one match from each position.
 */

/**/
int
getmatch(char **sp, char *pat, int fl, int n, char *replstr)
{
    Comp c;

    MUSTUSEHEAP("getmatch");	/* presumably covered by prefork() test */
    c = parsereg(pat);
    if (!c) {
 	zerr("bad pattern: %s", pat, 0);
 	return 1;
    }
    return igetmatch(sp, c, fl, n, replstr);
}

/**/
void
getmatcharr(char ***ap, char *pat, int fl, int n, char *replstr)
{
    char **arr = *ap, **pp;
    Comp c;

    MUSTUSEHEAP("getmatch");	/* presumably covered by prefork() test */

    c = parsereg(pat);
    if (!c) {
	zerr("bad pattern: %s", pat, 0);
	return;
    }
    *ap = pp = ncalloc(sizeof(char *) * (arrlen(arr) + 1));
    while ((*pp = *arr++))
	if (igetmatch(pp, c, fl, n, replstr))
	    pp++;
}

/**/
static int
igetmatch(char **sp, Comp c, int fl, int n, char *replstr)
{
    char *s = *sp, *t, *start, sav;
    int i, l = strlen(*sp), matched;

    repllist = NULL;

    if (fl & SUB_ALL) {
	i = domatch(s, c, 0);
	*sp = get_match_ret(*sp, 0, i ? l : 0, fl, i ? replstr : 0);
	if (! **sp && (((fl & SUB_MATCH) && !i) || ((fl & SUB_REST) && i)))
	    return 0;
	return 1;
    }
    switch (fl & (SUB_END|SUB_LONG|SUB_SUBSTR)) {
    case 0:
    case SUB_LONG:
	/*
	 * Largest/smallest possible match at head of string.
	 * First get the longest match...
	 */
	if (dolongestmatch(s, c, 0)) {
	    char *mpos = pptr;
	    if (!(fl & SUB_LONG)) {
	      /*
	       * ... now we know whether it's worth looking for the
	       * shortest, which we do by brute force.
	       */
	      for (t = s; t < mpos; METAINC(t)) {
		sav = *t;
		*t = '\0';
		if (dolongestmatch(s, c, 0)) {
		  mpos = pptr;
		  *t = sav;
		  break;
		}
		*t = sav;
	      }
	    }
	    *sp = get_match_ret(*sp, 0, mpos-s, fl, replstr);
	    return 1;
	}
	break;

    case SUB_END:
	/* Smallest possible match at tail of string:  *
	 * move back down string until we get a match. *
	 * There's no optimization here.               */
	for (t = s + l; t >= s; t--) {
	    if (domatch(t, c, 0)) {
		*sp = get_match_ret(*sp, t - s, l, fl, replstr);
		return 1;
	    }
	    if (t > s+1 && t[-2] == Meta)
		t--;
	}
	break;

    case (SUB_END|SUB_LONG):
	/* Largest possible match at tail of string:       *
	 * move forward along string until we get a match. *
	 * Again there's no optimisation.                  */
	for (i = 0, t = s; i < l; i++, t++) {
	    if (domatch(t, c, 0)) {
		*sp = get_match_ret(*sp, i, l, fl, replstr);
		return 1;
	    }
	    if (*t == Meta)
		i++, t++;
	}
	break;

    case SUB_SUBSTR:
	/* Smallest at start, but matching substrings. */
	if (!(fl & SUB_GLOBAL) && domatch(s + l, c, 0) && !--n) {
	    *sp = get_match_ret(*sp, 0, 0, fl, replstr);
	    return 1;
	} /* fall through */
    case (SUB_SUBSTR|SUB_LONG):
	/* longest or smallest at start with substrings */
	start = s;
	if (fl & SUB_GLOBAL)
	    repllist = newlinklist();
	do {
	    /* loop over all matches for global substitution */
	    matched = 0;
	    for (t = start; t < s + l; t++) {
		/* Find the longest match from this position. */
		if (dolongestmatch(t, c, 0) && pptr > t) {
		    char *mpos = pptr;
		    while (!(fl & SUB_LONG) && pptr > t) {
			/* Now reduce to find the smallest match */
			char *p = (pptr > t + 1 && pptr[-2] == Meta) ?
			    pptr - 2 : pptr - 1;
			sav = *p;
			*p = '\0';
			if (!dolongestmatch(t, c, 0)) {
			    *p = sav;
			    break;
			}
			mpos = pptr;
			*p = sav;
		    }
		    if (!--n || (n <= 0 && (fl & SUB_GLOBAL)))
			*sp = get_match_ret(*sp, t-s, mpos-s, fl, replstr);
		    if (!(fl & SUB_GLOBAL)) {
			if (n) {
			    /*
			     * Looking for a later match: in this case,
			     * we can continue looking for matches from
			     * the next character, even if it overlaps
			     * with what we just found.
			     */
			    continue;
			} else
			    return 1;
		    }
		    /*
		     * For a global match, we need to skip the stuff
		     * which is already marked for replacement.
		     */
		    matched = 1;
		    start = mpos;
		    break;
		}
		if (*t == Meta)
		    t++;
	    }
	} while (matched);
	/*
	 * check if we can match a blank string, if so do it
	 * at the start.  Goodness knows if this is a good idea
	 * with global substitution, so it doesn't happen.
	 */
	if ((fl & (SUB_LONG|SUB_GLOBAL)) == SUB_LONG &&
	    domatch(s + l, c, 0) && !--n) {
	    *sp = get_match_ret(*sp, 0, 0, fl, replstr);
	    return 1;
	}
	break;

    case (SUB_END|SUB_SUBSTR):
	/* Shortest at end with substrings */
	if (domatch(s + l, c, 0) && !--n) {
	    *sp = get_match_ret(*sp, l, l, fl, replstr);
	    return 1;
	} /* fall through */
    case (SUB_END|SUB_LONG|SUB_SUBSTR):
	/* Longest/shortest at end, matching substrings.       */
	for (t = s + l - 1; t >= s; t--) {
	    if (t > s && t[-1] == Meta)
		t--;
	    if (dolongestmatch(t, c, 0) && pptr > t && !--n) {
		/* Found the longest match */
		char *mpos = pptr;
		while (!(fl & SUB_LONG) && pptr > t) {
		    /* Look for the shortest match */
		    char *p = (pptr > t+1 && pptr[-2] == Meta) ?
			pptr-2 : pptr-1;
		    sav = *p;
		    *p = '\0';
		    if (!dolongestmatch(t, c, 0) || pptr == t) {
			*p = sav;
			break;
		    }
		    *p = sav;
		    mpos = pptr;
		}
		*sp = get_match_ret(*sp, t-s, mpos-s, fl, replstr);
		return 1;
	    }
	}
	if ((fl & SUB_LONG) && domatch(s + l, c, 0) && !--n) {
	    *sp = get_match_ret(*sp, l, l, fl, replstr);
	    return 1;
	}
	break;
    }

    if (repllist && nonempty(repllist)) {
	/* Put all the bits of a global search and replace together. */
	LinkNode nd;
	Repldata rd;
	int rlen;
	int lleft = 0;		/* size of returned string */

	i = 0;			/* start of last chunk we got from *sp */
	rlen = strlen(replstr);
	for (nd = firstnode(repllist); nd; incnode(nd)) {
	    rd = (Repldata) getdata(nd);
	    lleft += rd->b - i; /* previous chunk of *sp */
	    lleft += rlen;	/* the replaced bit */
	    i = rd->e;		/* start of next chunk of *sp */
	}
	lleft += l - i;	/* final chunk from *sp */
	start = t = zhalloc(lleft+1);
	i = 0;
	for (nd = firstnode(repllist); nd; incnode(nd)) {
	    rd = (Repldata) getdata(nd);
	    memcpy(t, s + i, rd->b - i);
	    t += rd->b - i;
	    memcpy(t, replstr, rlen);
	    t += rlen;
	    i = rd->e;
	}
	memcpy(t, s + i, l - i);
	start[lleft] = '\0';
	*sp = start;
	return 1;
    }

    /* munge the whole string: no match, so no replstr */
    *sp = get_match_ret(*sp, 0, 0, fl, 0);
    return 1;
}

/* The main entry point for matching a string str against  *
 * a compiled pattern c.  `fist' indicates whether leading *
 * dots are special.                                       */

/**/
int
domatch(char *str, Comp c, int fist)
{
    int ret;
    pptr = str;
    first = fist;
    /*
     * If scanning paths, keep the error count over the whole
     * filename
     */
    if (!scanning) {
	errsfound = 0;
	errsmax = -1;
    }
    if (*pptr == Nularg)
	pptr++;
    PERMALLOC {
	ret = doesmatch(c);
    } LASTALLOC;
    return ret;
}

#define untok(C)  (itok(C) ? ztokens[(C) - Pound] : (C))

/* See if pattern has a matching exclusion (~...) part */

/**/
static int
excluded(Comp c, char *eptr)
{
    char *saves = pptr;
    int savei = first, savel = longest, savee = errsfound, ret;

    first = 0;
    /*
     * Even if we've been told always to match the longest string,
     * i.e. not anchored to the end, we want to match the full string
     * we've already matched when we're trying to exclude it.
     * Likewise, approximate matching here is treated separately.
     */
    longest = 0;
    errsfound = 0;
    if (PATHADDP(c) && pathpos) {
	VARARR(char, buf, pathpos + strlen(eptr) + 1);

	strcpy(buf, pathbuf);
	strcpy(buf + pathpos, eptr);
	pptr = buf;
	ret = doesmatch(c->exclude);
    } else {
	pptr = eptr;
	ret = doesmatch(c->exclude);
    }
    if (*pptr)
	ret = 0;

    pptr = saves;
    first = savei;
    longest = savel;
    errsfound = savee;

    return ret;
}

/*
 * Structure for storing instances of a pattern in a closured.
 */
struct gclose {
    char *start;		/* Start of this instance */
    char *end;			/* End of this instance */
    int errsfound;		/* Errors found up to start of instance */
};
typedef struct gclose *Gclose;

/* Add a list of matches that fit the closure.  trystring is a string of
 * the same length as the target string; a non-zero in that string
 * indicates that we have already tried to match the patterns following
 * the closure (c->next) at that point and failed.  This means that not
 * only should we not bother using the corresponding match, we should
 * also not bother going any further, since the first time we got to
 * that position (when it was marked), we must already have failed on
 * and backtracked over any further closure matches beyond that point.
 * If we are using approximation, the number in the string is incremented
 * by the current error count; if we come back to that point with a
 * lower error count, it is worth trying from that point again, but
 * we must now mark that point in trystring with the new error.
 */

/**/
static void
addclosures(Comp c, LinkList closlist, int *pdone, unsigned char *trystring)
{
    Gclose gcnode;
    char *opptr = pptr;
    unsigned char *tpos;

    while (*pptr) {
	if (STARP(c)) {
	    if (*(tpos = trystring + ((pptr+1) - opptr))) {
		if ((unsigned)(errsfound+1) >= *tpos)
		    break;
		*tpos = (unsigned)(errsfound+1);
	    }
	    gcnode = (Gclose)zalloc(sizeof(struct gclose));
	    gcnode->start = pptr;
	    gcnode->end = METAINC(pptr);
	    gcnode->errsfound = errsfound;
	} else {
	    char *saves = pptr;
	    int savee = errsfound;
	    if (OPTIONALP(c) && *pdone >= 1)
		return;
	    if (!matchonce(c) || saves == pptr ||
		(*(tpos = trystring + (pptr-opptr)) &&
		 (unsigned)(errsfound+1) >= *tpos)) {
		pptr = saves;
		break;
	    }
	    if (*tpos)
		*tpos = (unsigned)(errsfound+1);
	    gcnode = (Gclose)zalloc(sizeof(struct gclose));
	    gcnode->start = saves;
	    gcnode->end = pptr;
	    gcnode->errsfound = savee;
	}
	pushnode(closlist, gcnode);
	(*pdone)++;
    }
}

/*
 * Match characters with case-insensitivity.  Note charmatch(x,y) !=
 * charmatch(y,x): the first argument comes from the target string, the
 * second from the pattern.  This used to be a macro, and in principle
 * could be turned back into one.
 */

/**/
static int
charmatch(Comp c, char *x, char *y)
{
    /*
     * This takes care of matching two characters which may be a
     * two byte sequence, Meta followed by something else.
     * Here we bypass tulower() and tuupper() for speed.
     */
    int xi = (STOUC(UNMETA(x)) & 0xff), yi = (STOUC(UNMETA(y)) & 0xff);
    return xi == yi ||
	(((c->stat & C_IGNCASE) ?
	  ((isupper(xi) ? tolower(xi) : xi) ==
	   (isupper(yi) ? tolower(yi) : yi)) :
	  (c->stat & C_LCMATCHUC) ?
	  (islower(yi) && toupper(yi) == xi) : 0));
}

/* see if current string in pptr matches c */

/**/
static int
doesmatch(Comp c)
{
    if (CLOSUREP(c)) {
	int done, retflag = 0;
	char *saves, *opptr;
	unsigned char *trystring, *tpos;
	int savee;
	LinkList closlist;
	Gclose gcnode;

	if (first && *pptr == '.')
	    return 0;

	if (!inclosure && !c->left && !c->errsmax) {
	    /* We are not inside another closure, and the current
	     * pattern is a simple string.  We handle this very common
	     * case specially: otherwise, matches like *foo* are
	     * extremely slow.  Here, instead of backtracking, we track
	     * forward until we get a match.  At top level, we are bound
	     * to get there eventually, so this is OK.
	     */
	    char looka;

	    if (STARP(c) && c->next &&
		!c->next->left && (looka = *c->next->str) &&
		!itok(looka)) {
		/* Another simple optimisation for a very common case:
		 * we are processing a * and there is
		 * an ordinary character match (which may not be a Metafied
		 * character, just to make it easier) next.  We look ahead
		 * for that character, taking care of Meta bytes.
		 */
		while (*pptr) {
		    for (; *pptr; pptr++) {
			if (*pptr == Meta)
			    pptr++;
			else if (charmatch(c, pptr, &looka))
			    break;
		    }
		    if (!*(saves = pptr))
			break;
		    savee = errsfound;
		    if (doesmatch(c->next))
			return 1;
		    pptr = saves+1;
		    errsfound = savee;
		}
	    } else {
		/* Standard track-forward code */
		for (done = 0; ; done++) {
		    saves = pptr;
		    savee = errsfound;
		    if ((done || ONEHASHP(c) || OPTIONALP(c)) &&
			((!c->next && (!LASTP(c) || !*pptr || longest)) ||
			 (c->next && doesmatch(c->next))))
			return 1;
		    if (done && OPTIONALP(c))
			return 0;
		    pptr = saves;
		    errsfound = savee;
		    first = 0;
		    if (STARP(c)) {
			if (!*pptr)
			    return 0;
			pptr++;
		    } else if (!matchonce(c) || pptr == saves)
			return 0;
		}
	    }
	    return 0;
	}
	/* The full, gory backtracking code is now necessary. */
	inclosure++;
	closlist = newlinklist();
	trystring = (unsigned char *)zcalloc(strlen(pptr)+1);
	opptr = pptr;

	/* Start by making a list where each match is as long
	 * as possible.  We later have to take account of the
	 * fact that earlier matches may be too long.
	 */
	done = 0;
	addclosures(c, closlist, &done, trystring);
	for (;;) {
	    if (TWOHASHP(c) && !done)
		break;
	    saves = pptr;
	    /* do we really want this LASTP here?? */
	    if ((!c->next && (!LASTP(c) || !*pptr || longest)) ||
		(c->next && doesmatch(c->next))) {
		retflag = 1;
		break;
	    }
	    trystring[saves-opptr] = (unsigned)(errsfound + 1);
	    /*
	     * If we failed, the first thing to try is whether we can
	     * shorten the match using the last pattern in the closure.
	     */
	    gcnode = firstnode(closlist) ? peekfirst(closlist) : NULL;
	    if (gcnode && --gcnode->end > gcnode->start
		&& (gcnode->end[-1] != Meta ||
		    --gcnode->end > gcnode->start)) {
		char savec = *gcnode->end;
		*gcnode->end = '\0';
		pptr = gcnode->start;
		errsfound = gcnode->errsfound;
		if (matchonce(c) && pptr != gcnode->start
		    && (!*(tpos = trystring + (pptr-opptr)) ||
			*tpos > (unsigned)(errsfound+1))) {
		    if (*tpos)
			*tpos = (unsigned)(errsfound+1);
		    *gcnode->end = savec;
		    gcnode->end = pptr;
		    /* Try again to construct a list based on
		     * this new position
		     */
		    addclosures(c, closlist, &done, tpos);
		    continue;
		}
		*gcnode->end = savec;
	    }
	    /* We've now exhausted the possibilities with that match,
	     * backtrack to the previous.
	     */
	    if ((gcnode = (Gclose)getlinknode(closlist))) {
		pptr = gcnode->start;
		errsfound = gcnode->errsfound;
		zfree(gcnode, sizeof(struct gclose));
		done--;
	    } else
		break;
	}
	freelinklist(closlist, free);
	zfree(trystring, strlen(opptr)+1);
	inclosure--;

	return retflag;
    } else
	return matchonce(c);
}

/**/
static int
posix_range(char **patptr, int ch)
{
    /* Match POSIX ranges, which correspond to ctype macros,  *
     * e.g. [:alpha:] -> isalpha.  It just doesn't seem worth * 
     * the palaver of creating a hash table for this.         */
    char *start = *patptr;
    int len;

    /* we made sure in parsecomp() there was a ':' to search for */
    *patptr = strchr(start, ':');
    len = (*patptr)++ - start;

    if (!strncmp(start, "alpha", len))
	return isalpha(ch);
    if (!strncmp(start, "alnum", len))
	return isalnum(ch);
    if (!strncmp(start, "blank", len))
	return ch == ' ' || ch == '\t';
    if (!strncmp(start, "cntrl", len))
	return iscntrl(ch);
    if (!strncmp(start, "digit", len))
	return isdigit(ch);
    if (!strncmp(start, "graph", len))
	return isgraph(ch);
    if (!strncmp(start, "lower", len))
	return islower(ch);
    if (!strncmp(start, "print", len))
	return isprint(ch);
    if (!strncmp(start, "punct", len))
	return ispunct(ch);
    if (!strncmp(start, "space", len))
	return isspace(ch);
    if (!strncmp(start, "upper", len))
	return isupper(ch);
    if (!strncmp(start, "xdigit", len))
	return isxdigit(ch);
    return 0;
}

/**/
static void
rangematch(char **patptr, int ch, int rchar)
{
    /* Check for a character in a [...] or [^...].  The [ *
     * and optional ^ have already been skipped.          */

    char *pat = *patptr;
#ifdef HAVE_STRCOLL
    char l_buf[2], r_buf[2], ch_buf[2];

    ch_buf[0] = ch;
    l_buf[1] = r_buf[1] = ch_buf[1] = '\0';
#endif

#define PAT(X) (pat[X] == Meta ? pat[(X)+1] ^ 32 : untok(pat[X]))
#define PPAT(X) (pat[(X)-1] == Meta ? pat[X] ^ 32 : untok(pat[X]))

    for (pat++; *pat != Outbrack && *pat; METAINC(pat)) {
	if (*pat == Inbrack) {
	    /* Inbrack can only occur inside a range if we found [:...:]. */
	    pat += 2;
	    if (posix_range(&pat, ch))
		break;
	} else if (*pat == '-' && pat[-1] != rchar &&
		   pat[1] != Outbrack) {
#ifdef HAVE_STRCOLL
	    l_buf[0] = PPAT(-1);
	    r_buf[0] = PAT(1);
	    if (strcoll(l_buf, ch_buf) <= 0 &&
		strcoll(ch_buf, r_buf) <= 0)
#else
		if (PPAT(-1) <= ch && PAT(1) >= ch)
#endif
		    break;
	} else if (ch == PAT(0))
	    break;
    }

    *patptr = pat;
}

/*
 * matchonce() is the core of the pattern matching, handling individual
 * strings and instances of a pattern in a closure.
 *
 * Note on approximate matching:  The rule is supposed to be
 *   (1) Take the longest possible match without approximation.
 *   (2) At any failure, make the single approximation that results
 *       in the longest match for the remaining part (which may
 *       include further approximations).
 *   (3) If we match the same distance, take the one with fewer
 *       approximations.
 * If this is wrong, I haven't yet discovered a counterexample.  Email
 * lines are now open.
 *                   pws 1999/02/23
 */

/**/
static int
matchonce(Comp c)
{
    char *pat = c->str;
    for (;;) {
	/* loop until success or failure of pattern */
	if (!pat || !*pat) {
	    /* No current pattern (c->str). */
	    char *saves;
	    int savee, savei;

	    if (errflag)
		return 0;
	    /* Remember state in case we need to go back and   *
	     * check for exclusion of pattern or alternatives. */
	    saves = pptr;
	    savei = first;
	    savee = errsfound;
	    /* Loop over alternatives with exclusions: (foo~bar|...). *
	     * Exclusions apply to the pattern in c->left.            */
	    if (c->left || c->right) {
		int ret = 0, ret2 = 0;
		if (c->exclude) {
		    char *exclend = 0;

		    /* We may need to back up on things like `(*~foo)'
		     * if the `*' matched `foo' but needs to match `fo'.
		     * exclend marks the end of the shortened text.  We
		     * need to restore it to match the tail.
		     * We set `inclosure' because we need the more
		     * sophisticated code in doesmatch() for any nested
		     * closures.
		     */
		    inclosure++;

		    while (!exclend || exclend >= pptr) {
			char exclsav = 0;
			if (exclend) {
			     exclsav = *exclend;
			    *exclend = '\0';
			 }
			if ((ret = doesmatch(c->left))) {
			    if (exclend)
				*exclend = exclsav;
			    exclsav = *(exclend = pptr);
			    *exclend = '\0';
			    ret2 = !excluded(c, saves);
			}
			if (exclend)
			    *exclend = exclsav;

			if (!ret)
			    break;
			if ((ret = ret2 &&
			     ((!c->next && (!LASTP(c) || !*pptr || longest))
			      || (c->next && doesmatch(c->next)))) ||
			    (!c->next && LASTP(c)))
			    break;
			/* Back up if necessary: exclend gives the position
			 * of the end of the match we are excluding,
			 * so only try to match to there.
			 */
			exclend--;
			pptr = saves;
			errsfound = savee;
		    }
		    inclosure--;
		    if (ret)
			return 1;
		} else
		    ret = doesmatch(c->left);
		ret2 = 0;
		if (c->right && (!ret || inclosure)) {
		    /* If in a closure, we always want the longest match. */
		    char *newpptr = pptr;
		    int newerrsfound = errsfound;
		    pptr = saves;
		    first = savei;
		    errsfound = savee;
		    ret2 = doesmatch(c->right);
		    if (ret && (!ret2 || pptr < newpptr)) {
			pptr = newpptr;
			errsfound = newerrsfound;
		    }
		}
		if (!ret && !ret2) {
		    pptr = saves;
		    first = savei;
		    errsfound = savee;
		    break;
		}
	    }
	    if (CLOSUREP(c))
		return 1;
	    if (!c->next) {
		/*
		 * No more patterns left, but we may still be in the middle
		 * of a match, in which case alles in Ordnung...
		 */ 
		if (!LASTP(c))
		    return 1;
		/*
		 * ...else we're at the last pattern, so this is our last
		 * ditch attempt at an approximate match: try to omit the
		 * last few characters.
		 */
		for (; *pptr && errsfound < c->errsmax &&
			 (errsmax == -1 || errsfound < errsmax);
		     METAINC(pptr))
		    errsfound++;
		return !*pptr || longest;
	    }
	    /* optimisation when next pattern is not a closure */
	    if (!CLOSUREP(c->next)) {
		c = c->next;
		pat = c->str;
		continue;
	    }
	    return doesmatch(c->next);
	}
	/*
	 * Don't match leading dot if first is set
	 * (don't even try for an approximate match)
	 */
	if (first && *pptr == '.' && *pat != '.')
	    return 0;
	if (*pat == Star) {	/* final * is not expanded to ?#; returns success */
	    while (*pptr)
		pptr++;
	    return 1;
	}
	first = 0;		/* finished checking start of pattern */
	if (*pat == Quest) {
	    /* match exactly one character */
	    if (!*pptr)
		break;
	    METAINC(pptr);
	    pat++;
	    continue;
	}
	if (*pat == Inbrack) {
	    /* Match groups of characters */
	    char ch;
	    char *saves, *savep;

	    if (!*pptr)
		break;
	    saves = pptr;
	    savep = pat;
	    ch = UNMETA(pptr);
	    if (pat[1] == Hat || pat[1] == '^' || pat[1] == '!') {
		/* group is negated */
		*++pat = Hat;
		rangematch(&pat, ch, Hat);
		DPUTS(!*pat, "BUG: something is very wrong in doesmatch()");
		if (*pat != Outbrack) {
		    pptr = saves;
		    pat = savep;
		    break;
		}
		pat++;
		METAINC(pptr);
		continue;
	    } else {
		/* pattern is not negated (affirmed? asserted?) */
		rangematch(&pat, ch, Inbrack);
		DPUTS(!pat || !*pat, "BUG: something is very wrong in doesmatch()");
		if (*pat == Outbrack) {
		    pptr = saves;
		    pat = savep;
		    break;
		}
		for (METAINC(pptr); *pat != Outbrack; pat++);
		pat++;
		continue;
	    }
	}
	if (*pat == Inang) {
	    /* Numeric globbing. */
	    unsigned long t1, t2, t3;
	    char *ptr, *saves = pptr, *savep = pat;

	    if (!idigit(*pptr))
		break;
	    if (*++pat == Outang || 
		(*pat == '-' && pat[1] == Outang && ++pat)) {
		/* <> or <->:  any number matches */
		while (idigit(*++pptr));
		pat++;
	    } else {
		/* Flag that there is no upper limit */
		int not3 = 0;
		char *opptr = pptr;
		/*
		 * Form is <a-b>, where a or b are numbers or blank.
		 * t1 = number supplied:  must be positive, so use
		 * unsigned arithmetic.
		 */
		t1 = (unsigned long)zstrtol(pptr, &ptr, 10);
		pptr = ptr;
		/* t2 = lower limit */
		if (idigit(*pat))
		    t2 = (unsigned long)zstrtol(pat, &ptr, 10);
		else
		    t2 = 0, ptr = pat;
		if (*ptr != '-' || (not3 = (ptr[1] == Outang)))
				/* exact match or no upper limit */
		    t3 = t2, pat = ptr + not3;
		else		/* t3 = upper limit */
		    t3 = (unsigned long)zstrtol(ptr + 1, &pat, 10);
		DPUTS(*pat != Outang, "BUG: wrong internal range pattern");
		pat++;
		/*
		 * If the number found is too large for the pattern,
		 * try matching just the first part.  This way
		 * we always get the longest possible match.
		 */
		while (!not3 && t1 > t3 && pptr > opptr+1) {
		  pptr--;
		  t1 /= 10;
		}
		if (t1 < t2 || (!not3 && t1 > t3)) {
		    pptr = saves;
		    pat = savep;
		    break;
		}
	    }
	    continue;
	}
	/* itok(Meta) is zero */
	DPUTS(itok(*pat), "BUG: matching tokenized character");
	if (charmatch(c, pptr, pat)) {
	    /* just plain old characters (or maybe unplain new characters) */
	    METAINC(pptr);
	    METAINC(pat);
	    continue;
	}
	if (errsfound < c->errsmax &&
	    (errsmax == -1 || errsfound < errsmax)) {
	    /*
	     * We tried to match literal characters and failed.  Now it's
	     * time to match approximately.  Note this doesn't handle the
	     * case where we *didn't* try to match literal characters,
	     * including the case where we were already at the end of the
	     * pattern, because then we never get here.  In that case the
	     * pattern has to be matched exactly, but we could maybe
	     * advance up the target string before trying it, so we have to
	     * handle that case elsewhere.
	     */
	    char *saves = pptr, *savep = c->str;
	    char *maxpptr = pptr, *patnext = METANEXT(pat);
	    int savee, maxerrs = -1;

	    /* First try to advance up the pattern. */
	    c->str = patnext;
	    savee = ++errsfound;
	    if (matchonce(c)) {
		maxpptr = pptr;
		maxerrs = errsfound;
	    }
	    errsfound = savee;

	    if (*saves) {
		/*
		 * If we have characters on both strings, we have more
		 * choice.
		 *
		 * Try to edge up the target string.
		 */
		char *strnext = METANEXT(saves);
		pptr = strnext;
		c->str = pat;
		if (matchonce(c) &&
		    (maxerrs == -1 || pptr > maxpptr ||
		     (pptr == maxpptr && errsfound <= maxerrs))) {
		    maxpptr = pptr;
		    maxerrs = errsfound;
		}
		errsfound = savee;

		/*
		 * Try edging up both of them at once.
		 * Note this takes precedence in the case of equal
		 * length as we get further up the pattern.
		 */
		c->str = patnext;
		pptr = strnext;
		if (matchonce(c) &&
		    (maxerrs == -1 || pptr > maxpptr ||
		     (pptr == maxpptr && errsfound <= maxerrs))) {
		    maxpptr = pptr;
		    maxerrs = errsfound;
		}
		errsfound = savee;

		/*
		 * See if we can transpose: that counts as just one error.
		 *
		 * Note, however, `abanana' and `banana': transposing
		 * is wrong here, as it gets us two errors,
		 * [ab][a]nana vs [ba][]nana, instead of the correct
		 * [a]banana vs []banana, so we still need to try
		 * the other possibilities.
		 */
		if (strnext && patnext && !itok(*patnext) &&
		    charmatch(c, strnext, pat) &&
		    charmatch(c, saves, patnext)) {
		    c->str = patnext;
		    METAINC(c->str);

		    pptr = strnext;
		    METAINC(pptr);

		    if (matchonce(c) &&
			(maxerrs == -1 || pptr > maxpptr ||
			 (pptr == maxpptr && errsfound <= maxerrs))) {
			maxpptr = pptr;
			maxerrs = errsfound;
		    }
		}
	    }
	    /*
	     * We don't usually restore state on failure, but we need
	     * to fix up the Comp structure which we altered to
	     * look at the tail of the pattern.
	     */
	    c->str = savep;
	    /*
	     * If that failed, game over:  we don't want to break
	     * and try the other approximate test, because we just did
	     * that.
	     */
	    if (maxerrs == -1)
		return 0;
	    pptr = maxpptr;
	    errsfound = maxerrs;
	    return 1;
	}
	break;
    }
    if (*pptr && errsfound < c->errsmax &&
	(errsmax == -1 || errsfound < errsmax)) {
	/*
	 * The pattern failed, but we can try edging up the target string
	 * and rematching with an error.  Note we do this from wherever we
	 * got to in the pattern string c->str, not the start. hence the
	 * need to modify c->str.
	 *
	 * At this point, we don't have a literal character in the pattern
	 * (handled above), so we don't try any funny business on the
	 * pattern itself.
	 */
	int ret;
	char *savep = c->str;
	errsfound++;
	METAINC(pptr);
	c->str = pat;
	ret = matchonce(c);
	c->str = savep;
	return ret;
    }
    return 0;
}

/* turn a string into a Comp struct:  this doesn't treat / specially */

/**/
Comp
parsereg(char *str)
{
    remnulargs(str);
    mode = 1;			/* no path components */
    addflags = 0;
    errsmax = 0;
    pptr = str;
    tail = NULL;
    return parsecompsw(GF_TOPLEV);
}

/* blindly turn a string into a tokenised expression without lexing */

/**/
void
tokenize(char *s)
{
    char *t;
    int bslash = 0;

    for (; *s; s++) {
      cont:
	switch (*s) {
	case Bnull:
	case '\\':
	    if (bslash) {
		s[-1] = Bnull;
		break;
	    }
	    bslash = 1;
	    continue;
	case '<':
	    if (isset(SHGLOB))
		break;
	    if (bslash) {
		s[-1] = Bnull;
		break;
	    }
	    t = s;
	    while (idigit(*++s));
	    if (*s != '-')
		goto cont;
	    while (idigit(*++s));
	    if (*s != '>')
		goto cont;
	    *t = Inang;
	    *s = Outang;
	    break;
	case '^':
	case '#':
	case '~':
	    if (unset(EXTENDEDGLOB))
		break;
	case '(':
	case '|':
	case ')':
	    if (isset(SHGLOB))
		break;
	case '[':
	case ']':
	case '*':
	case '?':
	    for (t = ztokens; *t; t++)
		if (*t == *s) {
		    if (bslash)
			s[-1] = Bnull;
		    else
			*s = (t - ztokens) + Pound;
		    break;
		}
	}
	bslash = 0;
    }
}

/* remove unnecessary Nulargs */

/**/
void
remnulargs(char *s)
{
    int nl = *s;
    char *t = s;

    while (*s)
	if (INULL(*s))
	    chuck(s);
	else
	    s++;
    if (!*t && nl) {
	t[0] = Nularg;
	t[1] = '\0';
    }
}

/* qualifier functions:  mostly self-explanatory, see glob(). */

/* device number */

/**/
static int
qualdev(struct stat *buf, long dv)
{
    return buf->st_dev == dv;
}

/* number of hard links to file */

/**/
static int
qualnlink(struct stat *buf, long ct)
{
    return (range < 0 ? buf->st_nlink < ct :
	    range > 0 ? buf->st_nlink > ct :
	    buf->st_nlink == ct);
}

/* user ID */

/**/
static int
qualuid(struct stat *buf, long uid)
{
    return buf->st_uid == uid;
}

/* group ID */

/**/
static int
qualgid(struct stat *buf, long gid)
{
    return buf->st_gid == gid;
}

/* device special file? */

/**/
static int
qualisdev(struct stat *buf, long junk)
{
    return S_ISBLK(buf->st_mode) || S_ISCHR(buf->st_mode);
}

/* block special file? */

/**/
static int
qualisblk(struct stat *buf, long junk)
{
    return S_ISBLK(buf->st_mode);
}

/* character special file? */

/**/
static int
qualischr(struct stat *buf, long junk)
{
    return S_ISCHR(buf->st_mode);
}

/* directory? */

/**/
static int
qualisdir(struct stat *buf, long junk)
{
    return S_ISDIR(buf->st_mode);
}

/* FIFO? */

/**/
static int
qualisfifo(struct stat *buf, long junk)
{
    return S_ISFIFO(buf->st_mode);
}

/* symbolic link? */

/**/
static int
qualislnk(struct stat *buf, long junk)
{
    return S_ISLNK(buf->st_mode);
}

/* regular file? */

/**/
static int
qualisreg(struct stat *buf, long junk)
{
    return S_ISREG(buf->st_mode);
}

/* socket? */

/**/
static int
qualissock(struct stat *buf, long junk)
{
    return S_ISSOCK(buf->st_mode);
}

/* given flag is set in mode */

/**/
static int
qualflags(struct stat *buf, long mod)
{
    return mode_to_octal(buf->st_mode) & mod;
}

/* mode matches specification */

/**/
static int
qualmodeflags(struct stat *buf, long mod)
{
    long v = mode_to_octal(buf->st_mode), y = mod & 07777, n = mod >> 12;

    return ((v & y) == y && !(v & n));
}

/* regular executable file? */

/**/
static int
qualiscom(struct stat *buf, long mod)
{
    return S_ISREG(buf->st_mode) && (buf->st_mode & S_IXUGO);
}

/* size in required range? */

/**/
static int
qualsize(struct stat *buf, long size)
{
    unsigned long scaled = buf->st_size;

    switch (units) {
    case TT_POSIX_BLOCKS:
	scaled += 511l;
	scaled /= 512l;
	break;
    case TT_KILOBYTES:
	scaled += 1023l;
	scaled /= 1024l;
	break;
    case TT_MEGABYTES:
	scaled += 1048575l;
	scaled /= 1048576l;
	break;
    }

    return (range < 0 ? scaled < (unsigned long) size :
	    range > 0 ? scaled > (unsigned long) size :
	    scaled == (unsigned long) size);
}

/* time in required range? */

/**/
static int
qualtime(struct stat *buf, long days)
{
    time_t now, diff;

    time(&now);
    diff = now - (amc == 0 ? buf->st_atime : amc == 1 ? buf->st_mtime :
		  buf->st_ctime);
    /* handle multipliers indicating units */
    switch (units) {
    case TT_DAYS:
	diff /= 86400l;
	break;
    case TT_HOURS:
	diff /= 3600l;
	break;
    case TT_MINS:
	diff /= 60l;
	break;
    case TT_WEEKS:
	diff /= 604800l;
	break;
    case TT_MONTHS:
	diff /= 2592000l;
	break;
    }

    return (range < 0 ? diff < days :
	    range > 0 ? diff > days :
	    diff == days);
}
