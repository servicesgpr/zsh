/*
 * params.c - parameters
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
#include "params.pro"

#include "version.h"

/* what level of localness we are at */
 
/**/
int locallevel;
 
/* Variables holding values of special parameters */
 
/**/
char **pparams,		/* $argv        */
     **cdpath,		/* $cdpath      */
     **fignore,		/* $fignore     */
     **fpath,		/* $fpath       */
     **mailpath,	/* $mailpath    */
     **manpath,		/* $manpath     */
     **path,		/* $path        */
     **psvar,		/* $psvar       */
     **watch;		/* $watch       */
 
/**/
char *argzero,		/* $0           */
     *underscore,	/* $_           */
     *home,		/* $HOME        */
     *hostnam,		/* $HOST        */
     *ifs,		/* $IFS         */
     *nullcmd,		/* $NULLCMD     */
     *oldpwd,		/* $OLDPWD      */
     *zoptarg,		/* $OPTARG      */
     *postedit,		/* $POSTEDIT    */
     *prompt,		/* $PROMPT      */
     *prompt2,		/* $PROMPT2     */
     *prompt3,		/* $PROMPT3     */
     *prompt4,		/* $PROMPT4     */
     *pwd,		/* $PWD         */
     *readnullcmd,	/* $READNULLCMD */
     *rprompt,		/* $RPROMPT     */
     *sprompt,		/* $SPROMPT     */
     *term,		/* $TERM        */
     *ttystrname,	/* $TTY         */
     *wordchars,	/* $WORDCHARS   */
     *zsh_name;		/* $ZSH_NAME    */

/**/
long lastval,		/* $?           */
     mypid,		/* $$           */
     lastpid,		/* $!           */
     columns,		/* $COLUMNS     */
     lineno,		/* $LINENO      */
     lines,		/* $LINES       */
     zoptind,		/* $OPTIND      */
     ppid,		/* $PPID        */
     shlvl;		/* $SHLVL       */

/* $histchars */
 
/**/
unsigned char bangchar, hatchar, hashchar;
 
/* $SECONDS = time(NULL) - shtimer.tv_sec */
 
/**/
struct timeval shtimer;
 
/* 0 if this $TERM setup is usable, otherwise it contains TERM_* flags */

/**/
int termflags;
 
/* Nodes for special parameters for parameter hash table */

static
#ifdef HAVE_UNION_INIT
# define BR(X) {X}
struct param
#else
# define BR(X) X
struct iparam {
    struct hashnode *next;
    char *nam;			/* hash data                             */
    int flags;			/* PM_* flags (defined in zsh.h)         */
    void *value;
    void (*func1) _((void));	/* set func                              */
    char *(*func2) _((void));	/* get func                              */
    void (*unsetfn) _((Param, int));	/* unset func                    */
    int ct;			/* output base or field width            */
    char *env;			/* location in environment, if exported  */
    char *ename;		/* name of corresponding environment var */
    Param old;			/* old struct for use with local         */
    int level;			/* if (old != NULL), level of localness  */
}
#endif
special_params[] ={
#define SFN(X) BR(((void (*)_((Param, char *)))(X)))
#define GFN(X) BR(((char *(*)_((Param)))(X)))
#define IPDEF1(A,B,C,D) {NULL,A,PM_INTEGER|PM_SPECIAL|D,BR(NULL),SFN(C),GFN(B),stdunsetfn,10,NULL,NULL,NULL,0}
IPDEF1("#", poundgetfn, nullsetfn, PM_READONLY),
IPDEF1("ERRNO", errnogetfn, nullsetfn, PM_READONLY),
IPDEF1("GID", gidgetfn, gidsetfn, PM_DONTIMPORT | PM_RESTRICTED),
IPDEF1("EGID", egidgetfn, egidsetfn, PM_DONTIMPORT | PM_RESTRICTED),
IPDEF1("HISTSIZE", histsizegetfn, histsizesetfn, PM_RESTRICTED),
IPDEF1("RANDOM", randomgetfn, randomsetfn, 0),
IPDEF1("SECONDS", secondsgetfn, secondssetfn, 0),
IPDEF1("UID", uidgetfn, uidsetfn, PM_DONTIMPORT | PM_RESTRICTED),
IPDEF1("EUID", euidgetfn, euidsetfn, PM_DONTIMPORT | PM_RESTRICTED),
IPDEF1("TTYIDLE", ttyidlegetfn, nullsetfn, PM_READONLY),

#define IPDEF2(A,B,C,D) {NULL,A,PM_SCALAR|PM_SPECIAL|D,BR(NULL),SFN(C),GFN(B),stdunsetfn,0,NULL,NULL,NULL,0}
IPDEF2("USERNAME", usernamegetfn, usernamesetfn, PM_DONTIMPORT|PM_RESTRICTED),
IPDEF2("-", dashgetfn, nullsetfn, PM_READONLY),
IPDEF2("histchars", histcharsgetfn, histcharssetfn, PM_DONTIMPORT),
IPDEF2("HOME", homegetfn, homesetfn, 0),
IPDEF2("TERM", termgetfn, termsetfn, 0),
IPDEF2("WORDCHARS", wordcharsgetfn, wordcharssetfn, 0),
IPDEF2("IFS", ifsgetfn, ifssetfn, PM_DONTIMPORT),
IPDEF2("_", underscoregetfn, nullsetfn, PM_READONLY),

#ifdef LC_ALL
# define LCIPDEF(name) IPDEF2(name, strgetfn, lcsetfn, PM_UNSET)
IPDEF2("LANG", strgetfn, langsetfn, PM_UNSET),
IPDEF2("LC_ALL", strgetfn, lc_allsetfn, PM_UNSET),
# ifdef LC_COLLATE
LCIPDEF("LC_COLLATE"),
# endif
# ifdef LC_CTYPE
LCIPDEF("LC_CTYPE"),
# endif
# ifdef LC_MESSAGES
LCIPDEF("LC_MESSAGES"),
# endif
# ifdef LC_TIME
LCIPDEF("LC_TIME"),
# endif
#endif

#define IPDEF4(A,B) {NULL,A,PM_INTEGER|PM_READONLY|PM_SPECIAL,BR((void *)B),SFN(nullsetfn),GFN(intvargetfn),stdunsetfn,10,NULL,NULL,NULL,0}
IPDEF4("!", &lastpid),
IPDEF4("$", &mypid),
IPDEF4("?", &lastval),
IPDEF4("LINENO", &lineno),
IPDEF4("PPID", &ppid),

#define IPDEF5(A,B,F) {NULL,A,PM_INTEGER|PM_SPECIAL,BR((void *)B),SFN(F),GFN(intvargetfn),stdunsetfn,10,NULL,NULL,NULL,0}
IPDEF5("COLUMNS", &columns, zlevarsetfn),
IPDEF5("LINES", &lines, zlevarsetfn),
IPDEF5("OPTIND", &zoptind, intvarsetfn),
IPDEF5("SHLVL", &shlvl, intvarsetfn),

#define IPDEF7(A,B) {NULL,A,PM_SCALAR|PM_SPECIAL,BR((void *)B),SFN(strvarsetfn),GFN(strvargetfn),stdunsetfn,0,NULL,NULL,NULL,0}
IPDEF7("OPTARG", &zoptarg),
IPDEF7("NULLCMD", &nullcmd),
IPDEF7("POSTEDIT", &postedit),
IPDEF7("READNULLCMD", &readnullcmd),
IPDEF7("RPROMPT", &rprompt),
IPDEF7("PS1", &prompt),
IPDEF7("PS2", &prompt2),
IPDEF7("PS3", &prompt3),
IPDEF7("PS4", &prompt4),
IPDEF7("RPS1", &rprompt),
IPDEF7("SPROMPT", &sprompt),
IPDEF7("0", &argzero),

#define IPDEF8(A,B,C,D) {NULL,A,D|PM_SCALAR|PM_SPECIAL,BR((void *)B),SFN(colonarrsetfn),GFN(colonarrgetfn),stdunsetfn,0,NULL,C,NULL,0}
IPDEF8("CDPATH", &cdpath, "cdpath", 0),
IPDEF8("FIGNORE", &fignore, "fignore", 0),
IPDEF8("FPATH", &fpath, "fpath", 0),
IPDEF8("MAILPATH", &mailpath, "mailpath", 0),
IPDEF8("WATCH", &watch, "watch", 0),
IPDEF8("PATH", &path, "path", PM_RESTRICTED),
IPDEF8("PSVAR", &psvar, "psvar", 0),

#ifdef DYNAMIC
/* MODULE_PATH is not imported for security reasons */
IPDEF8("MODULE_PATH", &module_path, "module_path", PM_DONTIMPORT|PM_RESTRICTED),
#endif

#define IPDEF9F(A,B,C,D) {NULL,A,D|PM_ARRAY|PM_SPECIAL|PM_DONTIMPORT,BR((void *)B),SFN(arrvarsetfn),GFN(arrvargetfn),stdunsetfn,0,NULL,C,NULL,0}
#define IPDEF9(A,B,C) IPDEF9F(A,B,C,0)
IPDEF9("*", &pparams, NULL),
IPDEF9("@", &pparams, NULL),
{NULL, NULL},

/* The following parameters are not avaible in sh/ksh compatibility *
 * mode. All of these has sh compatible equivalents.                */
IPDEF1("ARGC", poundgetfn, nullsetfn, PM_READONLY),
IPDEF2("HISTCHARS", histcharsgetfn, histcharssetfn, PM_DONTIMPORT),
IPDEF4("status", &lastval),
IPDEF7("prompt", &prompt),
IPDEF7("PROMPT", &prompt),
IPDEF7("PROMPT2", &prompt2),
IPDEF7("PROMPT3", &prompt3),
IPDEF7("PROMPT4", &prompt4),
IPDEF8("MANPATH", &manpath, "manpath", 0),
IPDEF9("argv", &pparams, NULL),
IPDEF9("fignore", &fignore, "FIGNORE"),
IPDEF9("cdpath", &cdpath, "CDPATH"),
IPDEF9("fpath", &fpath, "FPATH"),
IPDEF9("mailpath", &mailpath, "MAILPATH"),
IPDEF9("manpath", &manpath, "MANPATH"),
IPDEF9("psvar", &psvar, "PSVAR"),
IPDEF9("watch", &watch, "WATCH"),

/*TEST BEGIN*/
#define IPDEF10(A) {NULL,A,PM_HASHED|PM_SPECIAL|PM_DONTIMPORT,BR((void *)0),SFN(hashsetfn),GFN(hashgetfn),stdunsetfn,0,NULL,NULL,NULL,0}
IPDEF10("testhash"),
/*TEST END*/

#ifdef DYNAMIC
IPDEF9F("module_path", &module_path, "MODULE_PATH", PM_RESTRICTED),
#endif
IPDEF9F("path", &path, "PATH", PM_RESTRICTED),

{NULL, NULL}
};
#undef BR

#define IS_UNSET_VALUE(V) \
	((V) && (!(V)->pm || ((V)->pm->flags & PM_UNSET) || \
		 !(V)->pm->nam || !*(V)->pm->nam))

static Param argvparam;

/* hash table containing the parameters */
 
/**/
HashTable paramtab, realparamtab;

#ifndef DYNAMIC
#define getparamnode gethashnode2
#endif /* DYNAMIC */

/**/
HashTable
newparamtable(int size, char const *name)
{
    HashTable ht = newhashtable(size, name, NULL);

    ht->hash        = hasher;
    ht->emptytable  = emptyhashtable;
    ht->filltable   = NULL;
    ht->addnode     = addhashnode;
    ht->getnode     = getparamnode;
    ht->getnode2    = getparamnode;
    ht->removenode  = removehashnode;
    ht->disablenode = NULL;
    ht->enablenode  = NULL;
    ht->freenode    = freeparamnode;
    ht->printnode   = printparamnode;

    return ht;
}

#ifdef DYNAMIC
/**/
static HashNode
getparamnode(HashTable ht, char *nam)
{
    HashNode hn = gethashnode2(ht, nam);
    Param pm = (Param) hn;

    if (pm && pm->u.str && (pm->flags & PM_AUTOLOAD)) {
	char *mn = dupstring(pm->u.str);

	if (!load_module(mn))
	    return NULL;
	hn = gethashnode2(ht, nam);
    }
    return hn;
}
#endif /* DYNAMIC */

/* Copy a parameter hash table */

static HashTable outtable;

/**/
static void
scancopyparams(HashNode hn, int flags)
{
    /* Going into a real parameter, so always use permanent storage */
    Param pm = (Param)hn;
    Param tpm = (Param) zcalloc(sizeof *tpm);
    tpm->nam = ztrdup(pm->nam);
    copyparam(tpm, pm, 0);
    addhashnode(outtable, tpm->nam, tpm);
}

/**/
HashTable
copyparamtable(HashTable ht, char *name)
{
    HashTable nht = newparamtable(ht->hsize, name);
    outtable = nht;
    scanhashtable(ht, 0, 0, 0, scancopyparams, 0);
    outtable = NULL;
    return nht;
}

static unsigned numparamvals;

/**/
static void
scancountparams(HashNode hn, int flags)
{
    ++numparamvals;
    if ((flags & SCANPM_WANTKEYS) && (flags & SCANPM_WANTVALS))
	++numparamvals;
}

static Comp scancomp;
static char **paramvals;

/**/
static void
scanparamvals(HashNode hn, int flags)
{
    struct value v;
    if (numparamvals && (flags & (SCANPM_MATCHVAL|SCANPM_MATCHKEY)) &&
	!(flags & SCANPM_MATCHMANY))
	return;
    v.pm = (Param)hn;
    if ((flags & SCANPM_MATCHKEY) && !domatch(v.pm->nam, scancomp, 0)) {
	return;
    }
    if (flags & SCANPM_WANTKEYS) {
	paramvals[numparamvals++] = v.pm->nam;
	if (!(flags & (SCANPM_WANTVALS|SCANPM_MATCHVAL)))
	    return;
    }
    v.isarr = (PM_TYPE(v.pm->flags) & (PM_ARRAY|PM_HASHED));
    v.inv = 0;
    v.a = 0;
    v.b = -1;
    paramvals[numparamvals] = getstrvalue(&v);
    if (flags & SCANPM_MATCHVAL) {
	if (domatch(paramvals[numparamvals], scancomp, 0)) {
	    numparamvals += ((flags & SCANPM_WANTVALS) ? 1 :
			     !(flags & SCANPM_WANTKEYS));
	} else if (flags & SCANPM_WANTKEYS)
	    --numparamvals;	/* Value didn't match, discard key */
    } else
	++numparamvals;
}

/**/
char **
paramvalarr(HashTable ht, int flags)
{
    MUSTUSEHEAP("paramvalarr");
    numparamvals = 0;
    if (ht)
	scanhashtable(ht, 0, 0, PM_UNSET, scancountparams, flags);
    paramvals = (char **) alloc((numparamvals + 1) * sizeof(char *));
    if (ht) {
	numparamvals = 0;
	scanhashtable(ht, 0, 0, PM_UNSET, scanparamvals, flags);
    }
    paramvals[numparamvals] = 0;
    return paramvals;
}

/* Return the full array (no indexing) referred to by a Value. *
 * The array value is cached for the lifetime of the Value.    */

/**/
static char **
getvaluearr(Value v)
{
    if (v->arr)
	return v->arr;
    else if (PM_TYPE(v->pm->flags) == PM_ARRAY)
	return v->arr = v->pm->gets.afn(v->pm);
    else if (PM_TYPE(v->pm->flags) == PM_HASHED) {
	v->arr = paramvalarr(v->pm->gets.hfn(v->pm), v->isarr);
	/* Can't take numeric slices of associative arrays */
	v->a = 0;
	v->b = numparamvals;
	return v->arr;
    } else
	return NULL;
}

/* empty dummy function for special hash parameters */

static void
shempty()
{
}

/* Create a simple special hash parameter. */

/**/
Param
createspecialhash(char *name, GetNodeFunc get, ScanTabFunc scan)
{
    Param pm;
    HashTable ht;

    if (!(pm = createparam(name, PM_SPECIAL|PM_REMOVABLE|PM_HASHED)))
	return NULL;

    pm->level = locallevel;
    pm->gets.hfn = hashgetfn;
    pm->sets.hfn = hashsetfn;
    pm->unsetfn = stdunsetfn;
    pm->u.hash = ht = newhashtable(7, name, NULL);

    ht->hash        = hasher;
    ht->emptytable  = (TableFunc) shempty;
    ht->filltable   = NULL;
    ht->addnode     = (AddNodeFunc) shempty;
    ht->getnode     = ht->getnode2 = get;
    ht->removenode  = (RemoveNodeFunc) shempty;
    ht->disablenode = NULL;
    ht->enablenode  = NULL;
    ht->freenode    = (FreeNodeFunc) shempty;
    ht->printnode   = NULL;
    ht->scanfunc    = scan;

    return pm;
}

/* Functions for the parameters special parameter. */

static HashNode
getpmparameter(HashTable ht, char *name)
{
    Param rpm, pm = NULL;

    HEAPALLOC {
	pm = (Param) zhalloc(sizeof(struct param));
	pm->nam = dupstring(name);
	pm->flags = PM_SCALAR | PM_READONLY;
	pm->sets.cfn = NULL;
	pm->gets.cfn = strgetfn;
	pm->unsetfn = NULL;
	pm->ct = 0;
	pm->env = NULL;
	pm->ename = NULL;
	pm->old = NULL;
	pm->level = 0;
	if ((rpm = (Param) realparamtab->getnode(realparamtab, name)) &&
	    !(rpm->flags & PM_UNSET))
	    pm->u.str = paramtypestr(rpm);
	else {
	    pm->u.str = "";
	    pm->flags |= PM_UNSET;
	}
    } LASTALLOC;

    return (HashNode) pm;
}

static void
scanpmparameters(HashTable ht, ScanFunc func, int flags)
{
    struct param pm;
    int i;
    HashNode hn;

    pm.flags = PM_SCALAR | PM_READONLY;
    pm.sets.cfn = NULL;
    pm.gets.cfn = strgetfn;
    pm.unsetfn = NULL;
    pm.ct = 0;
    pm.env = NULL;
    pm.ename = NULL;
    pm.old = NULL;
    pm.level = 0;

    for (i = 0; i < realparamtab->hsize; i++)
	for (hn = realparamtab->nodes[i]; hn; hn = hn->next) {
	    pm.nam = hn->nam;
	    if (func != scancountparams)
		pm.u.str = paramtypestr((Param) hn);
	    func((HashNode) &pm, flags);
	}
}

/* Functions for the commands special parameter. */

static void
setpmcommand(Param pm, char *value)
{
    if (isset(RESTRICTED))
	zwarnnam(NULL, "restricted: %s", value, 0);
    else {
	Cmdnam cn = zcalloc(sizeof(*cn));

	cn->flags = HASHED;
	cn->u.cmd = ztrdup(value);

	cmdnamtab->addnode(cmdnamtab, ztrdup(pm->nam), (HashNode) cn);
    }
}

static void
unsetpmcommand(Param pm, int exp)
{
    HashNode hn = cmdnamtab->removenode(cmdnamtab, pm->nam);

    if (hn)
	cmdnamtab->freenode(hn);
}

static void
setpmcommands(Param pm, HashTable ht)
{
    int i;
    HashNode hn;

    for (i = 0; i < ht->hsize; i++)
	for (hn = ht->nodes[i]; hn; hn = hn->next) {
	    Cmdnam cn = zcalloc(sizeof(*cn));
	    struct value v;

	    v.isarr = v.inv = v.a = 0;
	    v.b = -1;
	    v.arr = NULL;
	    v.pm = (Param) hn;

	    cn->flags = HASHED;
	    cn->u.cmd = ztrdup(getstrvalue(&v));

	    cmdnamtab->addnode(cmdnamtab, ztrdup(hn->nam), (HashNode) cn);
	}
}

static HashNode
getpmcommand(HashTable ht, char *name)
{
    Cmdnam cmd;
    Param pm = NULL;

    if (!(cmd = (Cmdnam) cmdnamtab->getnode(cmdnamtab, name)) &&
	isset(HASHLISTALL)) {
	cmdnamtab->filltable(cmdnamtab);
	cmd = (Cmdnam) cmdnamtab->getnode(cmdnamtab, name);
    }
    HEAPALLOC {
	pm = (Param) zhalloc(sizeof(struct param));
	pm->nam = dupstring(name);
	pm->flags = PM_SCALAR;
	pm->sets.cfn = setpmcommand;
	pm->gets.cfn = strgetfn;
	pm->unsetfn = unsetpmcommand;
	pm->ct = 0;
	pm->env = NULL;
	pm->ename = NULL;
	pm->old = NULL;
	pm->level = 0;
	if (cmd) {
	    if (cmd->flags & HASHED)
		pm->u.str = cmd->u.cmd;
	    else {
		pm->u.str = zhalloc(strlen(*(cmd->u.name)) +
				    strlen(name) + 2);
		strcpy(pm->u.str, *(cmd->u.name));
		strcat(pm->u.str, "/");
		strcat(pm->u.str, name);
	    }
	} else {
	    pm->u.str = "";
	    pm->flags |= PM_UNSET;
	}
    } LASTALLOC;

    return (HashNode) pm;
}

static void
scanpmcommands(HashTable ht, ScanFunc func, int flags)
{
    struct param pm;
    int i;
    HashNode hn;
    Cmdnam cmd;

    if (isset(HASHLISTALL))
	cmdnamtab->filltable(cmdnamtab);

    pm.flags = PM_SCALAR;
    pm.sets.cfn = setpmcommand;
    pm.gets.cfn = strgetfn;
    pm.unsetfn = unsetpmcommand;
    pm.ct = 0;
    pm.env = NULL;
    pm.ename = NULL;
    pm.old = NULL;
    pm.level = 0;

    for (i = 0; i < cmdnamtab->hsize; i++)
	for (hn = cmdnamtab->nodes[i]; hn; hn = hn->next) {
	    pm.nam = hn->nam;
	    cmd = (Cmdnam) hn;
	    if (func != scancountparams) {
		if (cmd->flags & HASHED)
		    pm.u.str = cmd->u.cmd;
		else {
		    pm.u.str = zhalloc(strlen(*(cmd->u.name)) +
				       strlen(cmd->nam) + 2);
		    strcpy(pm.u.str, *(cmd->u.name));
		    strcat(pm.u.str, "/");
		    strcat(pm.u.str, cmd->nam);
		}
	    }
	    func((HashNode) &pm, flags);
	}
}

/* Functions for the functions special parameter. */

static void
setfunction(char *name, char *value)
{
    char *val;
    Shfunc shf;
    List list;
    int sn;

    val = ztrdup(value);
    val = metafy(val, strlen(val), META_REALLOC);

    HEAPALLOC {
	list = parse_string(val);
    } LASTALLOC;

    if (!list || list == &dummy_list) {
	zwarnnam(NULL, "invalid function definition", val, 0);
	zsfree(val);
	return;
    }
    PERMALLOC {
	shf = (Shfunc) zalloc(sizeof(*shf));
	shf->funcdef = (List) dupstruct(list);
	shf->flags = 0;

	if (!strncmp(name, "TRAP", 4) &&
	    (sn = getsignum(name + 4)) != -1) {
	    if (settrap(sn, shf->funcdef)) {
		freestruct(shf->funcdef);
		zfree(shf, sizeof(*shf));
		zsfree(val);
		LASTALLOC_RETURN;
	    }
	    sigtrapped[sn] |= ZSIG_FUNC;
	}
	shfunctab->addnode(shfunctab, ztrdup(name), shf);
    } LASTALLOC;

    zsfree(val);
}

static void
setpmfunction(Param pm, char *value)
{
    setfunction(pm->nam, value);
}

static void
unsetpmfunction(Param pm, int exp)
{
    HashNode hn = shfunctab->removenode(shfunctab, pm->nam);

    if (hn)
	shfunctab->freenode(hn);
}

static void
setpmfunctions(Param pm, HashTable ht)
{
    int i;
    HashNode hn;

    for (i = 0; i < ht->hsize; i++)
	for (hn = ht->nodes[i]; hn; hn = hn->next) {
	    struct value v;

	    v.isarr = v.inv = v.a = 0;
	    v.b = -1;
	    v.arr = NULL;
	    v.pm = (Param) hn;

	    setfunction(hn->nam, getstrvalue(&v));
	}
}

static HashNode
getpmfunction(HashTable ht, char *name)
{
    Shfunc shf;
    Param pm = NULL;

    HEAPALLOC {
	pm = (Param) zhalloc(sizeof(struct param));
	pm->nam = dupstring(name);
	pm->flags = PM_SCALAR;
	pm->sets.cfn = setpmfunction;
	pm->gets.cfn = strgetfn;
	pm->unsetfn = unsetpmfunction;
	pm->ct = 0;
	pm->env = NULL;
	pm->ename = NULL;
	pm->old = NULL;
	pm->level = 0;

	if ((shf = (Shfunc) shfunctab->getnode(shfunctab, name))) {
	    if (shf->flags & PM_UNDEFINED)
		pm->u.str = "undefined";
	    else {
		char *t = getpermtext((void *) dupstruct((void *)
							 shf->funcdef)), *h;

		h = dupstring(t);
		zsfree(t);
		unmetafy(h, NULL);

		pm->u.str = h;
	    }
	} else {
	    pm->u.str = "";
	    pm->flags |= PM_UNSET;
	}
    } LASTALLOC;

    return (HashNode) pm;
}

static void
scanpmfunctions(HashTable ht, ScanFunc func, int flags)
{
    struct param pm;
    int i;
    HashNode hn;

    pm.flags = PM_SCALAR;
    pm.sets.cfn = setpmcommand;
    pm.gets.cfn = strgetfn;
    pm.unsetfn = unsetpmcommand;
    pm.ct = 0;
    pm.env = NULL;
    pm.ename = NULL;
    pm.old = NULL;
    pm.level = 0;

    for (i = 0; i < shfunctab->hsize; i++)
	for (hn = shfunctab->nodes[i]; hn; hn = hn->next) {
	    if (!(hn->flags & DISABLED)) {
		pm.nam = hn->nam;
		if (func != scancountparams) {
		    if (((Shfunc) hn)->flags & PM_UNDEFINED)
			pm.u.str = "undefined";
		    else {
			char *t = getpermtext((void *)
					      dupstruct((void *) ((Shfunc) hn)->funcdef));

			unmetafy((pm.u.str = dupstring(t)), NULL);
			zsfree(t);
		    }
		}
		func((HashNode) &pm, flags);
	    }
	}
}

/* Functions for the options special parameter. */

static void
setpmoption(Param pm, char *value)
{
    int n;

    if (!value || (strcmp(value, "on") && strcmp(value, "off")))
	zwarnnam(NULL, "invalid value: %s", value, 0);
    else if (!(n = optlookup(pm->nam)))
	zwarnnam(NULL, "no such option: %s", pm->nam, 0);
    else if (dosetopt(n, (value && strcmp(value, "off")), 0))
	zwarnnam(NULL, "can't change option: %s", pm->nam, 0);
}

static void
unsetpmoption(Param pm, int exp)
{
    int n;

    if (!(n = optlookup(pm->nam)))
	zwarnnam(NULL, "no such option: %s", pm->nam, 0);
    else if (dosetopt(n, 0, 0))
	zwarnnam(NULL, "can't change option: %s", pm->nam, 0);
}

static void
setpmoptions(Param pm, HashTable ht)
{
    int i;
    HashNode hn;

    for (i = 0; i < ht->hsize; i++)
	for (hn = ht->nodes[i]; hn; hn = hn->next) {
	    struct value v;
	    char *val;

	    v.isarr = v.inv = v.a = 0;
	    v.b = -1;
	    v.arr = NULL;
	    v.pm = (Param) hn;

	    val = getstrvalue(&v);
	    if (!val || (strcmp(val, "on") && strcmp(val, "off")))
		zwarnnam(NULL, "invalid value: %s", val, 0);
	    else if (dosetopt(optlookup(hn->nam),
			      (val && strcmp(val, "off")), 0))
		zwarnnam(NULL, "can't change option: %s", hn->nam, 0);
	}
}

static HashNode
getpmoption(HashTable ht, char *name)
{
    Param pm = NULL;
    int n;

    HEAPALLOC {
	pm = (Param) zhalloc(sizeof(struct param));
	pm->nam = dupstring(name);
	pm->flags = PM_SCALAR;
	pm->sets.cfn = setpmoption;
	pm->gets.cfn = strgetfn;
	pm->unsetfn = unsetpmoption;
	pm->ct = 0;
	pm->env = NULL;
	pm->ename = NULL;
	pm->old = NULL;
	pm->level = 0;

	if ((n = optlookup(name)))
	    pm->u.str = dupstring(opts[n] ? "on" : "off");
	else {
	    pm->u.str = "";
	    pm->flags |= PM_UNSET;
	}
    } LASTALLOC;

    return (HashNode) pm;
}

static void
scanpmoptions(HashTable ht, ScanFunc func, int flags)
{
    struct param pm;
    int i;
    HashNode hn;

    pm.flags = PM_SCALAR;
    pm.sets.cfn = setpmoption;
    pm.gets.cfn = strgetfn;
    pm.unsetfn = unsetpmoption;
    pm.ct = 0;
    pm.env = NULL;
    pm.ename = NULL;
    pm.old = NULL;
    pm.level = 0;

    for (i = 0; i < optiontab->hsize; i++)
	for (hn = optiontab->nodes[i]; hn; hn = hn->next) {
	    pm.nam = hn->nam;
	    pm.u.str = opts[((Optname) hn)->optno] ? "on" : "off";
	    func((HashNode) &pm, flags);
	}
}

/* Set up parameter hash table.  This will add predefined  *
 * parameter entries as well as setting up parameter table *
 * entries for environment variables we inherit.           */

/**/
void
createparamtable(void)
{
    Param ip, pm;
    char **new_environ, **envp, **envp2, **sigptr, **t;
    char buf[50], *str, *iname;
    int num_env;

    paramtab = realparamtab = newparamtable(151, "paramtab");

    /* Add the special parameters to the hash table */
    for (ip = special_params; ip->nam; ip++)
	paramtab->addnode(paramtab, ztrdup(ip->nam), ip);
    if (emulation != EMULATE_SH && emulation != EMULATE_KSH)
	while ((++ip)->nam)
	    paramtab->addnode(paramtab, ztrdup(ip->nam), ip);

    argvparam = (Param) paramtab->getnode(paramtab, "*");

    noerrs = 2;

    HEAPALLOC {
	/* Add the standard non-special parameters which have to    *
	 * be initialized before we copy the environment variables. *
	 * We don't want to override whatever values the users has  *
	 * given them in the environment.                           */
	setiparam("MAILCHECK", 60);
	setiparam("LOGCHECK", 60);
	setiparam("KEYTIMEOUT", 40);
	setiparam("LISTMAX", 100);
#ifdef HAVE_SELECT
	setiparam("BAUD", getbaudrate(&shttyinfo));  /* get the output baudrate */
#endif
	setsparam("FCEDIT", ztrdup(DEFAULT_FCEDIT));
	setsparam("TMPPREFIX", ztrdup(DEFAULT_TMPPREFIX));
	setsparam("TIMEFMT", ztrdup(DEFAULT_TIMEFMT));
	setsparam("WATCHFMT", ztrdup(default_watchfmt));
	setsparam("HOST", ztrdup(hostnam));
	setsparam("LOGNAME", ztrdup((str = getlogin()) && *str ? str : cached_username));

	/* Copy the environment variables we are inheriting to dynamic *
	 * memory, so we can do mallocs and frees on it.               */
	num_env = arrlen(environ);
	new_environ = (char **) zalloc(sizeof(char *) * (num_env + 1));
	*new_environ = NULL;

	/* Now incorporate environment variables we are inheriting *
	 * into the parameter hash table.                          */
	for (envp = new_environ, envp2 = environ; *envp2; envp2++) {
	    for (str = *envp2; *str && *str != '='; str++);
	    if (*str == '=') {
		iname = NULL;
		*str = '\0';
		if (!idigit(**envp2) && isident(*envp2) && !strchr(*envp2, '[')) {
		    iname = *envp2;
		    if ((!(pm = (Param) paramtab->getnode(paramtab, iname)) ||
			 !(pm->flags & PM_DONTIMPORT)) &&
			(pm = setsparam(iname, metafy(str + 1, -1, META_DUP))) &&
			!(pm->flags & PM_EXPORTED)) {
			*str = '=';
			pm->flags |= PM_EXPORTED;
			pm->env = *envp++ = ztrdup(*envp2);
			*envp = NULL;
			if (pm->flags & PM_SPECIAL)
			    pm->env = replenv(pm->env, getsparam(pm->nam));
		    }
		}
		*str = '=';
	    }
	}
	environ = new_environ;

	pm = (Param) paramtab->getnode(paramtab, "HOME");
	if (!(pm->flags & PM_EXPORTED)) {
	    pm->flags |= PM_EXPORTED;
	    pm->env = addenv("HOME", home);
	}
	pm = (Param) paramtab->getnode(paramtab, "LOGNAME");
	if (!(pm->flags & PM_EXPORTED)) {
	    pm->flags |= PM_EXPORTED;
	    pm->env = addenv("LOGNAME", pm->u.str);
	}
	pm = (Param) paramtab->getnode(paramtab, "SHLVL");
	if (!(pm->flags & PM_EXPORTED))
	    pm->flags |= PM_EXPORTED;
	sprintf(buf, "%d", (int)++shlvl);
	pm->env = addenv("SHLVL", buf);

	/* Add the standard non-special parameters */
	set_pwd_env();
	setsparam("MACHTYPE", ztrdup(MACHTYPE));
	setsparam("OSTYPE", ztrdup(OSTYPE));
	setsparam("TTY", ztrdup(ttystrname));
	setsparam("VENDOR", ztrdup(VENDOR));
	setsparam("ZSH_NAME", ztrdup(zsh_name));
	setsparam("ZSH_VERSION", ztrdup(ZSH_VERSION));
	setaparam("signals", sigptr = zalloc((SIGCOUNT+4) * sizeof(char *)));
	for (t = sigs; (*sigptr++ = ztrdup(*t++)); );

	pm = createspecialhash("parameters", getpmparameter, scanpmparameters);
	pm->flags |= PM_READONLY;
	pm = createspecialhash("commands", getpmcommand, scanpmcommands);
	pm->sets.hfn = setpmcommands;
	pm = createspecialhash("functions", getpmfunction, scanpmfunctions);
	pm->sets.hfn = setpmfunctions;
	pm = createspecialhash("options", getpmoption, scanpmoptions);
	pm->sets.hfn = setpmoptions;
    } LASTALLOC;

    noerrs = 0;
}

/* assign various functions used for non-special parameters */

/**/
static void
assigngetset(Param pm)
{
    switch (PM_TYPE(pm->flags)) {
    case PM_SCALAR:
	pm->sets.cfn = strsetfn;
	pm->gets.cfn = strgetfn;
	break;
    case PM_INTEGER:
	pm->sets.ifn = intsetfn;
	pm->gets.ifn = intgetfn;
	break;
    case PM_ARRAY:
	pm->sets.afn = arrsetfn;
	pm->gets.afn = arrgetfn;
	break;
    case PM_HASHED:
	pm->sets.hfn = hashsetfn;
	pm->gets.hfn = hashgetfn;
	break;
    default:
	DPUTS(1, "BUG: tried to create param node without valid flag");
	break;
    }
    pm->unsetfn = stdunsetfn;
}

/* Create a parameter, so that it can be assigned to.  Returns NULL if the *
 * parameter already exists or can't be created, otherwise returns the     *
 * parameter node.  If a parameter of the same name exists in an outer     *
 * scope, it is hidden by a newly created parameter.  An already existing  *
 * parameter node at the current level may be `created' and returned       *
 * provided it is unset and not special.  If the parameter can't be        *
 * created because it already exists, the PM_UNSET flag is cleared.        */

/**/
Param
createparam(char *name, int flags)
{
    Param pm, oldpm;

    if (name != nulstring) {
	oldpm = (Param) (paramtab == realparamtab ?
			 gethashnode2(paramtab, name) :
			 paramtab->getnode(paramtab, name));

	if (oldpm && oldpm->level == locallevel) {
	    if (!(oldpm->flags & PM_UNSET) || (oldpm->flags & PM_SPECIAL)) {
		oldpm->flags &= ~PM_UNSET;
		return NULL;
	    }
	    if ((oldpm->flags & PM_RESTRICTED) && isset(RESTRICTED)) {
		zerr("%s: restricted", name, 0);
		return NULL;
	    }

	    pm = oldpm;
	    pm->ct = 0;
	    oldpm = pm->old;
	} else {
	    pm = (Param) zcalloc(sizeof *pm);
	    if ((pm->old = oldpm)) {
		/* needed to avoid freeing oldpm */
		paramtab->removenode(paramtab, name);
	    }
	    paramtab->addnode(paramtab, ztrdup(name), pm);
	}

	if (isset(ALLEXPORT) && !oldpm)
	    flags |= PM_EXPORTED;
    } else {
	pm = (Param) alloc(sizeof *pm);
	pm->nam = nulstring;
    }
    pm->flags = flags;

    if(!(pm->flags & PM_SPECIAL))
	assigngetset(pm);
    return pm;
}

/* Copy a parameter */

/**/
void
copyparam(Param tpm, Param pm, int toplevel)
{
    /*
     * Note that tpm, into which we're copying, may not be in permanent
     * storage.  However, the values themselves are later used directly
     * to set the parameter, so must be permanently allocated (in accordance
     * with sets.?fn() usage).
     */
    PERMALLOC {
	tpm->flags = pm->flags;
	if (!toplevel)
	    tpm->flags &= ~PM_SPECIAL;
	switch (PM_TYPE(pm->flags)) {
	case PM_SCALAR:
	    tpm->u.str = ztrdup(pm->gets.cfn(pm));
	    break;
	case PM_INTEGER:
	    tpm->u.val = pm->gets.ifn(pm);
	    break;
	case PM_ARRAY:
	    tpm->u.arr = arrdup(pm->gets.afn(pm));
	    break;
	case PM_HASHED:
	    tpm->u.hash = copyparamtable(pm->gets.hfn(pm), pm->nam);
	    break;
	}
    } LASTALLOC;
    /*
     * If called from inside an associative array, that array is later going
     * to be passed as a real parameter, so we need the gets and sets
     * functions to be useful.  However, the saved associated array is
     * not itself special, so we just use the standard ones.
     * This is also why we switch off PM_SPECIAL.
     */
    if (!toplevel)
	assigngetset(tpm);
}

/* Return 1 if the string s is a valid identifier, else return 0. */

/**/
int
isident(char *s)
{
    char *ss;
    int ne;

    ne = noeval;		/* save the current value of noeval     */
    if (!*s)			/* empty string is definitely not valid */
	return 0;

    /* find the first character in `s' not in the iident type table */
    for (ss = s; *ss; ss++)
	if (!iident(*ss))
	    break;

#if 0
    /* If this exhaust `s' or the next two characters *
     * are [(, then it is a valid identifier.         */
    if (!*ss || (*ss == '[' && ss[1] == '('))
	return 1;

    /* Else if the next character is not [, then it is *
     * definitely not a valid identifier.              */
    if (*ss != '[')
	return 0;

    noeval = 1;
    (void)mathevalarg(++ss, &ss);
    if (*ss == ',')
	(void)mathevalarg(++ss, &ss);
    noeval = ne;		/* restore the value of noeval */
    if (*ss != ']' || ss[1])
	return 0;
    return 1;
#else
    /* If the next character is not [, then it is *
     * definitely not a valid identifier.              */
    if (!*ss)
	return 1;
    if (*ss != '[')
	return 0;

    /* Require balanced [ ] pairs */
    if (skipparens('[', ']', &ss))
	return 0;
    return !*ss;
#endif
}

static char **garr;

/**/
static long
getarg(char **str, int *inv, Value v, int a2, long *w)
{
    int num = 1, word = 0, rev = 0, ind = 0, down = 0, l, i, ishash;
    int beg = 0, hasbeg = 0;
    char *s = *str, *sep = NULL, *t, sav, *d, **ta, **p, *tt;
    long r = 0;
    Comp c;

    ishash = (v->pm && PM_TYPE(v->pm->flags) == PM_HASHED);

    /* first parse any subscription flags */
    if (v->pm && (*s == '(' || *s == Inpar)) {
	int escapes = 0;
	int waste;
	for (s++; *s != ')' && *s != Outpar && s != *str; s++) {
	    switch (*s) {
	    case 'r':
		rev = 1;
		down = ind = 0;
		break;
	    case 'R':
		rev = down = 1;
		ind = 0;
		break;
	    case 'i':
		rev = ind = 1;
		down = 0;
		break;
	    case 'I':
		rev = ind = down = 1;
		break;
	    case 'w':
		/* If the parameter is a scalar, then make subscription *
		 * work on a per-word basis instead of characters.      */
		word = 1;
		break;
	    case 'f':
		word = 1;
		sep = "\n";
		break;
	    case 'e':
		/* obsolate compatibility flag without any real effect */
		break;
	    case 'n':
		t = get_strarg(++s);
		if (!*t)
		    goto flagerr;
		sav = *t;
		*t = '\0';
		num = mathevalarg(s + 1, &d);
		if (!num)
		    num = 1;
		*t = sav;
		s = t;
		break;
	    case 'b':
		hasbeg = 1;
		t = get_strarg(++s);
		if (!*t)
		    goto flagerr;
		sav = *t;
		*t = '\0';
		if ((beg = mathevalarg(s + 1, &d)) > 0)
		    beg--;
		*t = sav;
		s = t;
		break;
	    case 'p':
		escapes = 1;
		break;
	    case 's':
		/* This gives the string that separates words *
		 * (for use with the `w' flag.                */
		t = get_strarg(++s);
		if (!*t)
		    goto flagerr;
		sav = *t;
		*t = '\0';
		sep = escapes ? getkeystring(s + 1, &waste, 1, &waste) :
				dupstring(s + 1);
		*t = sav;
		s = t;
		break;
	    default:
	      flagerr:
		num = 1;
		word = rev = ind = down = 0;
		sep = NULL;
		s = *str - 1;
	    }
	}
	if (s != *str)
	    s++;
    }
    if (num < 0) {
	down = !down;
	num = -num;
    }
    if (v->isarr & SCANPM_WANTKEYS)
	*inv = (ind || !(v->isarr & SCANPM_WANTVALS));
    else if (v->isarr & SCANPM_WANTVALS)
	*inv = 0;
    else {
	if (v->isarr) {
	    if (ind) {
		v->isarr |= SCANPM_WANTKEYS;
		v->isarr &= ~SCANPM_WANTVALS;
	    } else if (rev)
		v->isarr |= SCANPM_WANTVALS;
	    if (!down && ishash)
		v->isarr &= ~SCANPM_MATCHMANY;
	}
	*inv = ind;
    }

    for (t=s, i=0;
	 *t && ((*t != ']' && *t != Outbrack && (ishash || *t != ',')) || i); t++)
	if (*t == '[' || *t == Inbrack)
	    i++;
	else if (*t == ']' || *t == Outbrack)
	    i--;

    if (!*t)
	return 0;
    s = dupstrpfx(s, t - s);
    *str = tt = t;
    if (parsestr(s))
	return 0;
    singsub(&s);

    if (!rev) {
	if (ishash) {
	    HashTable ht = v->pm->gets.hfn(v->pm);
	    if (!ht) {
		ht = newparamtable(17, v->pm->nam);
		v->pm->sets.hfn(v->pm, ht);
	    }
	    if (!(v->pm = (Param) ht->getnode(ht, s))) {
		HashTable tht = paramtab;
		paramtab = ht;
		v->pm = createparam(s, PM_SCALAR|PM_UNSET);
		paramtab = tht;
	    }
	    v->isarr = (*inv ? SCANPM_WANTINDEX : 0);
	    v->a = 0;
	    *inv = 0;	/* We've already obtained the "index" (key) */
	    *w = v->b = -1;
	    r = isset(KSHARRAYS) ? 1 : 0;
	} else if (!(r = mathevalarg(s, &s)) || (isset(KSHARRAYS) && r >= 0))
	    r++;
	if (word && !v->isarr) {
	    s = t = getstrvalue(v);
	    i = wordcount(s, sep, 0);
	    if (r < 0)
		r += i + 1;
	    if (r < 1)
		r = 1;
	    if (r > i)
		r = i;
	    if (!s || !*s)
		return 0;
	    while ((d = findword(&s, sep)) && --r);
	    if (!d)
		return 0;

	    if (!a2 && *tt != ',')
		*w = (long)(s - t) - 1;

	    return (a2 ? s : d + 1) - t;
	} else if (!v->isarr && !word) {
	    s = getstrvalue(v);
	    if (r > 0) {
		for (t = s + r - 1; *s && s < t;)
		    if (*s++ == Meta)
			s++, t++, r++;
	    } else {
		r += ztrlen(s);
		for (t = s + r; *s && s < t; r--)
		    if (*s++ == Meta)
			t++, r++;
		r -= strlen(s);
	    }
	}
    } else {
	if (!v->isarr && !word) {
	    l = strlen(s);
	    if (a2) {
		if (!l || *s != '*') {
		    d = (char *) ncalloc(l + 2);
		    *d = '*';
		    strcpy(d + 1, s);
		    s = d;
		}
	    } else {
		if (!l || s[l - 1] != '*') {
		    d = (char *) ncalloc(l + 2);
		    strcpy(d, s);
		    strcat(d, "*");
		    s = d;
		}
	    }
	}
	tokenize(s);

	if ((c = parsereg(s))) {
	    int len;

	    if (v->isarr) {
		if (ishash) {
		    scancomp = c;
		    if (ind)
			v->isarr |= SCANPM_MATCHKEY;
		    else
			v->isarr |= SCANPM_MATCHVAL;
		    if (down)
			v->isarr |= SCANPM_MATCHMANY;
		    if ((ta = getvaluearr(v)) && *ta) {
			*inv = v->inv;
			*w = v->b;
			return 1;
		    }
		} else
		    ta = getarrvalue(v);
		if (!ta || !*ta)
		    return 0;
		len = arrlen(ta);
		if (beg < 0)
		    beg += len;
		if (beg >= 0 && beg < len) {
		    if (down) {
			if (!hasbeg)
			    beg = len - 1;
			for (r = 1 + beg, p = ta + beg; p >= ta; r--, p--) {
			    if (domatch(*p, c, 0) && !--num)
				return r;
			}
		    } else
			for (r = 1 + beg, p = ta + beg; *p; r++, p++)
			    if (domatch(*p, c, 0) && !--num)
				return r;
		}
	    } else if (word) {
		ta = sepsplit(d = s = getstrvalue(v), sep, 1);
		len = arrlen(ta);
		if (beg < 0)
		    beg += len;
		if (beg >= 0 && beg < len) {
		    if (down) {
			if (!hasbeg)
			    beg = len - 1;
			for (r = 1 + beg, p = ta + beg; p >= ta; p--, r--)
			    if (domatch(*p, c, 0) && !--num)
				break;
			if (p < ta)
			    return 0;
		    } else {
			for (r = 1 + beg, p = ta + beg; *p; r++, p++)
			    if (domatch(*p, c, 0) && !--num)
				break;
			if (!*p)
			    return 0;
		    }
		}
		if (a2)
		    r++;
		for (i = 0; (t = findword(&d, sep)) && *t; i++)
		    if (!--r) {
			r = (long)(t - s + (a2 ? -1 : 1));
			if (!a2 && *tt != ',')
			    *w = r + strlen(ta[i]) - 2;
			return r;
		    }
		return a2 ? -1 : 0;
	    } else {
		d = getstrvalue(v);
		if (!d || !*d)
		    return 0;
		len = strlen(d);
		if (beg < 0)
		    beg += len;
		if (beg >= 0 && beg < len) {
		    if (a2) {
			if (down) {
			    if (!hasbeg)
				beg = len - 1;
			    for (r = beg, t = d + beg; t >= d; r--, t--) {
				sav = *t;
				*t = '\0';
				if (domatch(d, c, 0) && !--num) {
				    *t = sav;
				    return r;
				}
				*t = sav;
			    }
			} else
			    for (r = beg, t = d + beg; *t; r++, t++) {
				sav = *t;
				*t = '\0';
				if (domatch(d, c, 0) && !--num) {
				    *t = sav;
				    return r;
				}
				*t = sav;
			    }
		    } else {
			if (down) {
			    if (!hasbeg)
				beg = len - 1;
			    for (r = beg + 1, t = d + beg; t >= d; r--, t--) {
				if (domatch(t, c, 0) && !--num)
				    return r;
			    }
			} else
			    for (r = beg + 1, t = d + beg; *t; r++, t++)
				if (domatch(t, c, 0) && !--num)
				    return r;
		    }
		}
		return 0;
	    }
	}
    }
    return r;
}

/**/
int
getindex(char **pptr, Value v)
{
    int a, b, inv = 0;
    char *s = *pptr, *tbrack;

    *s++ = '[';
    for (tbrack = s; *tbrack && *tbrack != ']' && *tbrack != Outbrack; tbrack++)
	if (itok(*tbrack))
	    *tbrack = ztokens[*tbrack - Pound];
    if (*tbrack == Outbrack)
	*tbrack = ']';
    if ((s[0] == '*' || s[0] == '@') && s[1] == ']') {
	if ((v->isarr || IS_UNSET_VALUE(v)) && s[0] == '@')
	    v->isarr |= SCANPM_ISVAR_AT;
	v->a = 0;
	v->b = -1;
	s += 2;
    } else {
	long we = 0, dummy;

	a = getarg(&s, &inv, v, 0, &we);

	if (inv) {
	    if (!v->isarr && a != 0) {
		char *t, *p;
		t = getstrvalue(v);
		if (a > 0) {
		    for (p = t + a - 1; p-- > t; )
			if (*p == Meta)
			    a--;
		} else
		    a = -ztrlen(t + a + strlen(t));
	    }
	    if (a > 0 && (isset(KSHARRAYS) || (v->pm->flags & PM_HASHED)))
		a--;
	    if (v->isarr != SCANPM_WANTINDEX) {
		v->inv = 1;
		v->isarr = 0;
		v->a = v->b = a;
	    }
	    if (*s == ',') {
		zerr("invalid subscript", NULL, 0);
		while (*s != ']' && *s != Outbrack)
		    s++;
		*pptr = s;
		return 1;
	    }
	    if (*s == ']' || *s == Outbrack)
		s++;
	} else {
	    if (a > 0)
		a--;
	    if (*s == ',') {
		s++;
		b = getarg(&s, &inv, v, 1, &dummy);
		if (b > 0)
		    b--;
	    } else {
		b = we ? we : a;
	    }
	    if (*s == ']' || *s == Outbrack) {
		s++;
		if (v->isarr && a == b)
		    v->isarr = 0;
		v->a = a;
		v->b = b;
	    } else
		s = *pptr;
	}
    }
    *pptr = s;
    return 0;
}


/**/
Value
getvalue(char **pptr, int bracks)
{
  return fetchvalue(pptr, bracks, 0);
}

/**/
Value
fetchvalue(char **pptr, int bracks, int flags)
{
    char *s, *t;
    char sav;
    Value v;
    int ppar = 0;

    s = t = *pptr;
    garr = NULL;

    if (idigit(*s)) {
	if (bracks >= 0)
	    ppar = zstrtol(s, &s, 10);
	else
	    ppar = *s++ - '0';
    }
    else if (iident(*s))
	while (iident(*s))
	    s++;
    else if (*s == Quest)
	*s++ = '?';
    else if (*s == Pound)
	*s++ = '#';
    else if (*s == String)
	*s++ = '$';
    else if (*s == Qstring)
	*s++ = '$';
    else if (*s == Star)
	*s++ = '*';
    else if (*s == '#' || *s == '-' || *s == '?' || *s == '$' ||
	     *s == '_' || *s == '!' || *s == '@' || *s == '*')
	s++;
    else
	return NULL;

    if ((sav = *s))
	*s = '\0';
    if (ppar) {
	v = (Value) hcalloc(sizeof *v);
	v->pm = argvparam;
	v->inv = 0;
	v->a = v->b = ppar - 1;
	if (sav)
	    *s = sav;
    } else {
	Param pm;
	int isvarat;

        isvarat = !strcmp(t, "@");
	pm = (Param) paramtab->getnode(paramtab, *t == '0' ? "0" : t);
	if (sav)
	    *s = sav;
	*pptr = s;
	if (!pm || (pm->flags & PM_UNSET))
	    return NULL;
	v = (Value) hcalloc(sizeof *v);
	if (PM_TYPE(pm->flags) & (PM_ARRAY|PM_HASHED)) {
	    /* Overload v->isarr as the flag bits for hashed arrays. */
	    v->isarr = flags | (isvarat ? SCANPM_ISVAR_AT : 0);
	    /* If no flags were passed, we need something to represent *
	     * `true' yet differ from an explicit WANTVALS.  This is a *
	     * bit of a hack, but makes some sense:  When no subscript *
	     * is provided, all values are substituted.                */
	    if (!v->isarr)
		v->isarr = SCANPM_MATCHMANY;
	}
	v->pm = pm;
	v->inv = 0;
	v->a = 0;
	v->b = -1;
	if (bracks > 0 && (*s == '[' || *s == Inbrack)) {
	    if (getindex(&s, v)) {
		*pptr = s;
		return v;
	    }
	} else if (v->isarr && iident(*t) && isset(KSHARRAYS))
	    v->b = 0, v->isarr = 0;
    }
    if (!bracks && *s)
	return NULL;
    *pptr = s;
    if (v->a > MAX_ARRLEN ||
	v->a < -MAX_ARRLEN) {
	zerr("subscript too %s: %d", (v->a < 0) ? "small" : "big", v->a);
	return NULL;
    }
    if (v->b > MAX_ARRLEN ||
	v->b < -MAX_ARRLEN) {
	zerr("subscript too %s: %d", (v->b < 0) ? "small" : "big", v->b);
	return NULL;
    }
    return v;
}

/**/
char *
getstrvalue(Value v)
{
    char *s, **ss;
    static char buf[(sizeof(long) * 8) + 4];

    if (!v)
	return hcalloc(1);
    HEAPALLOC {
	if (v->inv && !(v->pm->flags & PM_HASHED)) {
	    sprintf(buf, "%d", v->a);
	    s = dupstring(buf);
	    LASTALLOC_RETURN s;
	}

	switch(PM_TYPE(v->pm->flags)) {
	case PM_HASHED:
	    /* (!v->isarr) should be impossible unless emulating ksh */
	    if (!v->isarr && emulation == EMULATE_KSH) {
		s = dupstring("[0]");
		if (getindex(&s, v) == 0)
		    s = getstrvalue(v);
		LASTALLOC_RETURN s;
	    } /* else fall through */
	case PM_ARRAY:
	    ss = getvaluearr(v);
	    if (v->isarr)
		s = sepjoin(ss, NULL);
	    else {
		if (v->a < 0)
		    v->a += arrlen(ss);
		s = (v->a >= arrlen(ss) || v->a < 0) ? (char *) hcalloc(1) : ss[v->a];
	    }
	    LASTALLOC_RETURN s;
	case PM_INTEGER:
	    convbase(buf, v->pm->gets.ifn(v->pm), v->pm->ct);
	    s = dupstring(buf);
	    break;
	case PM_SCALAR:
	    s = v->pm->gets.cfn(v->pm);
	    break;
	default:
	    s = NULL;
	    DPUTS(1, "BUG: param node without valid type");
	    break;
	}

	if (v->a == 0 && v->b == -1) {
	    LASTALLOC_RETURN s;
	}
	if (v->a < 0)
	    v->a += strlen(s);
	if (v->b < 0)
	    v->b += strlen(s);
	s = (v->a > (int)strlen(s)) ? dupstring("") : dupstring(s + v->a);
	if (v->b < v->a)
	    s[0] = '\0';
	else if (v->b - v->a < (int)strlen(s))
	    s[v->b - v->a + 1 + (s[v->b - v->a] == Meta)] = '\0';
    } LASTALLOC;
    return s;
}

static char *nular[] = {"", NULL};

/**/
char **
getarrvalue(Value v)
{
    char **s;

    if (!v)
	return arrdup(nular);
    else if (IS_UNSET_VALUE(v))
	return arrdup(&nular[1]);
    if (v->inv) {
	char buf[DIGBUFSIZE];

	s = arrdup(nular);
	sprintf(buf, "%d", v->a);
	s[0] = dupstring(buf);
	return s;
    }
    s = getvaluearr(v);
    if (v->a == 0 && v->b == -1)
	return s;
    if (v->a < 0)
	v->a += arrlen(s);
    if (v->b < 0)
	v->b += arrlen(s);
    if (v->a > arrlen(s) || v->a < 0)
	s = arrdup(nular);
    else
	s = arrdup(s) + v->a;
    if (v->b < v->a)
	s[0] = NULL;
    else if (v->b - v->a < arrlen(s))
	s[v->b - v->a + 1] = NULL;
    return s;
}

/**/
long
getintvalue(Value v)
{
    if (!v || v->isarr)
	return 0;
    if (v->inv)
	return v->a;
    if (PM_TYPE(v->pm->flags) == PM_INTEGER)
	return v->pm->gets.ifn(v->pm);
    return matheval(getstrvalue(v));
}

/**/
static void
setstrvalue(Value v, char *val)
{
    char buf[(sizeof(long) * 8) + 4];

    if (v->pm->flags & PM_READONLY) {
	zerr("read-only variable: %s", v->pm->nam, 0);
	zsfree(val);
	return;
    }
    if ((v->pm->flags & PM_RESTRICTED) && isset(RESTRICTED)) {
	zerr("%s: restricted", v->pm->nam, 0);
	zsfree(val);
	return;
    }
    v->pm->flags &= ~PM_UNSET;
    switch (PM_TYPE(v->pm->flags)) {
    case PM_SCALAR:
	MUSTUSEHEAP("setstrvalue");
	if (v->a == 0 && v->b == -1) {
	    (v->pm->sets.cfn) (v->pm, val);
	    if (v->pm->flags & (PM_LEFT | PM_RIGHT_B | PM_RIGHT_Z) && !v->pm->ct)
		v->pm->ct = strlen(val);
	} else {
	    char *z, *x;
	    int zlen;

	    z = dupstring((v->pm->gets.cfn) (v->pm));
	    zlen = strlen(z);
	    if (v->inv && unset(KSHARRAYS))
		v->a--, v->b--;
	    if (v->a < 0) {
		v->a += zlen;
		if (v->a < 0)
		    v->a = 0;
	    }
	    if (v->a > zlen)
		v->a = zlen;
	    if (v->b < 0)
		v->b += zlen;
	    if (v->b > zlen - 1)
		v->b = zlen - 1;
	    x = (char *) zalloc(v->a + strlen(val) + zlen - v->b);
	    strncpy(x, z, v->a);
	    strcpy(x + v->a, val);
	    strcat(x + v->a, z + v->b + 1);
	    (v->pm->sets.cfn) (v->pm, x);
	    zsfree(val);
	}
	break;
    case PM_INTEGER:
	if (val) {
	    (v->pm->sets.ifn) (v->pm, matheval(val));
	    zsfree(val);
	}
	if (!v->pm->ct && lastbase != -1)
	    v->pm->ct = lastbase;
	break;
    case PM_ARRAY:
	MUSTUSEHEAP("setstrvalue");
	{
	    char **ss = (char **) zalloc(2 * sizeof(char *));

	    ss[0] = val;
	    ss[1] = NULL;
	    setarrvalue(v, ss);
	}
	break;
    }
    if ((!v->pm->env && !(v->pm->flags & PM_EXPORTED) &&
	 !(isset(ALLEXPORT) && !v->pm->old)) ||
	(v->pm->flags & PM_ARRAY) || v->pm->ename)
	return;
    if (PM_TYPE(v->pm->flags) == PM_INTEGER)
	convbase(val = buf, v->pm->gets.ifn(v->pm), v->pm->ct);
    else
	val = v->pm->gets.cfn(v->pm);
    if (v->pm->env)
	v->pm->env = replenv(v->pm->env, val);
    else {
	v->pm->flags |= PM_EXPORTED;
	v->pm->env = addenv(v->pm->nam, val);
    }
}

/**/
static void
setintvalue(Value v, long val)
{
    char buf[DIGBUFSIZE];

    if (v->pm->flags & PM_READONLY) {
	zerr("read-only variable: %s", v->pm->nam, 0);
	return;
    }
    if ((v->pm->flags & PM_RESTRICTED) && isset(RESTRICTED)) {
	zerr("%s: restricted", v->pm->nam, 0);
	return;
    }
    switch (PM_TYPE(v->pm->flags)) {
    case PM_SCALAR:
    case PM_ARRAY:
	sprintf(buf, "%ld", val);
	setstrvalue(v, ztrdup(buf));
	break;
    case PM_INTEGER:
	(v->pm->sets.ifn) (v->pm, val);
	setstrvalue(v, NULL);
	break;
    }
}

/**/
static void
setarrvalue(Value v, char **val)
{
    if (v->pm->flags & PM_READONLY) {
	zerr("read-only variable: %s", v->pm->nam, 0);
	freearray(val);
	return;
    }
    if ((v->pm->flags & PM_RESTRICTED) && isset(RESTRICTED)) {
	zerr("%s: restricted", v->pm->nam, 0);
	freearray(val);
	return;
    }
    if (!(PM_TYPE(v->pm->flags) & (PM_ARRAY|PM_HASHED))) {
	freearray(val);
	zerr("attempt to assign array value to non-array", NULL, 0);
	return;
    }
    if (v->a == 0 && v->b == -1) {
	if (PM_TYPE(v->pm->flags) == PM_HASHED)
	    arrhashsetfn(v->pm, val);
	else
	    (v->pm->sets.afn) (v->pm, val);
    } else {
	char **old, **new, **p, **q, **r;
	int n, ll, i;

	if ((PM_TYPE(v->pm->flags) == PM_HASHED)) {
	    freearray(val);
	    zerr("attempt to set slice of associative array", NULL, 0);
	    return;
	}
	if (v->inv && unset(KSHARRAYS))
	    v->a--, v->b--;
	q = old = v->pm->gets.afn(v->pm);
	n = arrlen(old);
	if (v->a < 0)
	    v->a += n;
	if (v->b < 0)
	    v->b += n;
	if (v->a < 0)
	    v->a = 0;
	if (v->b < 0)
	    v->b = 0;

	ll = v->a + arrlen(val);
	if (v->b < n)
	    ll += n - v->b;

	p = new = (char **) zcalloc(sizeof(char *) * (ll + 1));

	for (i = 0; i < v->a; i++)
	    *p++ = i < n ? ztrdup(*q++) : ztrdup("");
	for (r = val; *r;)
	    *p++ = ztrdup(*r++);
	if (v->b + 1 < n)
	    for (q = old + v->b + 1; *q;)
		*p++ = ztrdup(*q++);
	*p = NULL;

	(v->pm->sets.afn) (v->pm, new);
	freearray(val);
    }
}

/* Retrieve an integer parameter */

/**/
long
getiparam(char *s)
{
    Value v;

    if (!(v = getvalue(&s, 1)))
	return 0;
    return getintvalue(v);
}

/* Retrieve a scalar (string) parameter */

/**/
char *
getsparam(char *s)
{
    Value v;

    if (!(v = getvalue(&s, 0)))
	return NULL;
    return getstrvalue(v);
}

/* Retrieve an array parameter */

/**/
char **
getaparam(char *s)
{
    Value v;

    if (!idigit(*s) && (v = getvalue(&s, 0)) &&
	PM_TYPE(v->pm->flags) == PM_ARRAY)
	return v->pm->gets.afn(v->pm);
    return NULL;
}

/* Retrieve an assoc array parameter as an array */

/**/
char **
gethparam(char *s)
{
    Value v;

    if (!idigit(*s) && (v = getvalue(&s, 0)) &&
	PM_TYPE(v->pm->flags) == PM_HASHED)
	return paramvalarr(v->pm->gets.hfn(v->pm), SCANPM_WANTVALS);
    return NULL;
}

/**/
Param
setsparam(char *s, char *val)
{
    Value v;
    char *t = s;
    char *ss;

    if (!isident(s)) {
	zerr("not an identifier: %s", s, 0);
	zsfree(val);
	errflag = 1;
	return NULL;
    }
    if ((ss = strchr(s, '['))) {
	*ss = '\0';
	if (!(v = getvalue(&s, 1)))
	    createparam(t, PM_ARRAY);
	*ss = '[';
	v = NULL;
    } else {
	if (!(v = getvalue(&s, 1)))
	    createparam(t, PM_SCALAR);
	else if ((PM_TYPE(v->pm->flags) & (PM_ARRAY|PM_HASHED)) &&
		 !(v->pm->flags & (PM_SPECIAL|PM_TIED)) && unset(KSHARRAYS)) {
	    unsetparam(t);
	    createparam(t, PM_SCALAR);
	    v = NULL;
	}
    }
    if (!v && !(v = getvalue(&t, 1))) {
	zsfree(val);
	return NULL;
    }
    setstrvalue(v, val);
    return v->pm;
}

/**/
Param
setaparam(char *s, char **val)
{
    Value v;
    char *t = s;
    char *ss;

    if (!isident(s)) {
	zerr("not an identifier: %s", s, 0);
	freearray(val);
	errflag = 1;
	return NULL;
    }
    if ((ss = strchr(s, '['))) {
	*ss = '\0';
	if (!(v = getvalue(&s, 1)))
	    createparam(t, PM_ARRAY);
	*ss = '[';
	if (PM_TYPE(v->pm->flags) == PM_HASHED) {
	    zerr("attempt to set slice of associative array", NULL, 0);
	    freearray(val);
	    errflag = 1;
	    return NULL;
	}
	v = NULL;
    } else {
	if (!(v = getvalue(&s, 1)))
	    createparam(t, PM_ARRAY);
	else if (!(PM_TYPE(v->pm->flags) & (PM_ARRAY|PM_HASHED)) &&
		 !(v->pm->flags & (PM_SPECIAL|PM_TIED))) {
	    int uniq = v->pm->flags & PM_UNIQUE;
	    unsetparam(t);
	    createparam(t, PM_ARRAY | uniq);
	    v = NULL;
	}
    }
    if (!v)
	if (!(v = getvalue(&t, 1)))
	    return NULL;
    if (isset(KSHARRAYS) && !ss)
	/* the whole array should be set instead of only the first element */
	v->b = -1;
    setarrvalue(v, val);
    return v->pm;
}

/**/
Param
sethparam(char *s, char **val)
{
    Value v;
    char *t = s;

    if (!isident(s)) {
	zerr("not an identifier: %s", s, 0);
	freearray(val);
	errflag = 1;
	return NULL;
    }
    if (strchr(s, '[')) {
	freearray(val);
	zerr("nested associative arrays not yet supported", NULL, 0);
	errflag = 1;
	return NULL;
    } else {
	if (!(v = getvalue(&s, 1)))
	    createparam(t, PM_HASHED);
	else if (!(PM_TYPE(v->pm->flags) & PM_HASHED) &&
		 !(v->pm->flags & PM_SPECIAL)) {
	    unsetparam(t);
	    createparam(t, PM_HASHED);
	    v = NULL;
	}
    }
    if (!v)
	if (!(v = getvalue(&t, 1)))
	    return NULL;
    setarrvalue(v, val);
    return v->pm;
}

/**/
Param
setiparam(char *s, long val)
{
    Value v;
    char *t = s;
    Param pm;

    if (!isident(s)) {
	zerr("not an identifier: %s", s, 0);
	errflag = 1;
	return NULL;
    }
    if (!(v = getvalue(&s, 1))) {
	pm = createparam(t, PM_INTEGER);
	DPUTS(!pm, "BUG: parameter not created");
	pm->u.val = val;
	return pm;
    }
    setintvalue(v, val);
    return v->pm;
}

/* Unset a parameter */

/**/
void
unsetparam(char *s)
{
    Param pm;

    if ((pm = (Param) paramtab->getnode(paramtab, s)))
	unsetparam_pm(pm, 0, 1);
}

/* Unset a parameter */

/**/
void
unsetparam_pm(Param pm, int altflag, int exp)
{
    Param oldpm, altpm;

    if ((pm->flags & PM_READONLY) && pm->level <= locallevel) {
	zerr("read-only variable: %s", pm->nam, 0);
	return;
    }
    if ((pm->flags & PM_RESTRICTED) && isset(RESTRICTED)) {
	zerr("%s: restricted", pm->nam, 0);
	return;
    }
    pm->unsetfn(pm, exp);
    if ((pm->flags & PM_EXPORTED) && pm->env) {
	delenv(pm->env);
	zsfree(pm->env);
	pm->env = NULL;
    }

    /* remove it under its alternate name if necessary */
    if (pm->ename && !altflag) {
	altpm = (Param) paramtab->getnode(paramtab, pm->ename);
	if (altpm)
	    unsetparam_pm(altpm, 1, exp);
    }

    /*
     * If this was a local variable, we need to keep the old
     * struct so that it is resurrected at the right level.
     * This is partly because when an array/scalar value is set
     * and the parameter used to be the other sort, unsetparam()
     * is called.  Beyond that, there is an ambiguity:  should
     * foo() { local bar; unset bar; } make the global bar
     * available or not?  The following makes the answer "no".
     *
     * Some specials, such as those used in zle, still need removing
     * from the parameter table; they have the PM_REMOVABLE flag.
     */
    if ((locallevel && locallevel >= pm->level) ||
	(pm->flags & (PM_SPECIAL|PM_REMOVABLE)) == PM_SPECIAL)
	return;

    /* remove parameter node from table */
    paramtab->removenode(paramtab, pm->nam);

    if (pm->old) {
	oldpm = pm->old;
	paramtab->addnode(paramtab, oldpm->nam, oldpm);
	if ((PM_TYPE(oldpm->flags) == PM_SCALAR) &&
	    oldpm->sets.cfn == strsetfn)
	    adduserdir(oldpm->nam, oldpm->u.str, 0, 0);
    }

    /* Even removable specials shouldn't be deleted. */
    if (!(pm->flags & PM_SPECIAL))
	paramtab->freenode((HashNode) pm); /* free parameter node */
}

/* Standard function to unset a parameter.  This is mostly delegated to *
 * the specific set function.                                           */

/**/
void
stdunsetfn(Param pm, int exp)
{
    switch (PM_TYPE(pm->flags)) {
	case PM_SCALAR: pm->sets.cfn(pm, NULL); break;
	case PM_ARRAY:  pm->sets.afn(pm, NULL); break;
        case PM_HASHED: pm->sets.hfn(pm, NULL); break;
    }
    pm->flags |= PM_UNSET;
}

/* Function to get value of an integer parameter */

/**/
static long
intgetfn(Param pm)
{
    return pm->u.val;
}

/* Function to set value of an integer parameter */

/**/
static void
intsetfn(Param pm, long x)
{
    pm->u.val = x;
}

/* Function to get value of a scalar (string) parameter */

/**/
char *
strgetfn(Param pm)
{
    return pm->u.str ? pm->u.str : (char *) hcalloc(1);
}

/* Function to set value of a scalar (string) parameter */

/**/
static void
strsetfn(Param pm, char *x)
{
    zsfree(pm->u.str);
    pm->u.str = x;
    adduserdir(pm->nam, x, 0, 0);
}

/* Function to get value of an array parameter */

/**/
static char **
arrgetfn(Param pm)
{
    static char *nullarray = NULL;

    return pm->u.arr ? pm->u.arr : &nullarray;
}

/* Function to set value of an array parameter */

/**/
static void
arrsetfn(Param pm, char **x)
{
    if (pm->u.arr && pm->u.arr != x)
	freearray(pm->u.arr);
    if (pm->flags & PM_UNIQUE)
	uniqarray(x);
    pm->u.arr = x;
    /* Arrays tied to colon-arrays may need to fix the environment */
    if (pm->ename && x)
	arrfixenv(pm->ename, x);
}

/* Function to get value of an association parameter */

/**/
static HashTable
hashgetfn(Param pm)
{
    return pm->u.hash;
}

/* Flag to freeparamnode to unset the struct */

static int delunset;

/* Function to set value of an association parameter */

/**/
static void
hashsetfn(Param pm, HashTable x)
{
    if (pm->u.hash && pm->u.hash != x) {
	/* The parameters in the hash table need to be unset *
	 * before being deleted.                             */
	int odelunset = delunset;
	delunset = 1;
	deletehashtable(pm->u.hash);
	delunset = odelunset;
    }
    pm->u.hash = x;
}

/* Function to set value of an association parameter using key/value pairs */

/**/
static void
arrhashsetfn(Param pm, char **val)
{
    /* Best not to shortcut this by using the existing hash table,   *
     * since that could cause trouble for special hashes.  This way, *
     * it's up to pm->sets.hfn() what to do.                         */
    int alen = arrlen(val);
    HashTable opmtab = paramtab, ht;
    char **aptr = val;
    Value v = (Value) hcalloc(sizeof *v);
    v->b = -1;

    if (alen % 2) {
	freearray(val);
	zerr("bad set of key/value pairs for associative array\n",
	     NULL, 0);
	return;
    }
    ht = paramtab = newparamtable(17, pm->nam);
    while (*aptr) {
	/* The parameter name is ztrdup'd... */
	v->pm = createparam(*aptr, PM_SCALAR|PM_UNSET);
	/*
	 * createparam() doesn't return anything if the parameter
	 * already existed.
	 */
	if (!v->pm)
	    v->pm = (Param) paramtab->getnode(paramtab, *aptr);
	zsfree(*aptr++);
	/* ...but we can use the value without copying. */
	setstrvalue(v, *aptr++);
    }
    paramtab = opmtab;
    pm->sets.hfn(pm, ht);
    free(val);		/* not freearray() */
}

/* This function is used as the set function for      *
 * special parameters that cannot be set by the user. */

/**/
void
nullsetfn(Param pm, char *x)
{
    zsfree(x);
}

/* Function to get value of generic special integer *
 * parameter.  data is pointer to global variable   *
 * containing the integer value.                    */

/**/
long
intvargetfn(Param pm)
{
    return *((long *)pm->u.data);
}

/* Function to set value of generic special integer *
 * parameter.  data is pointer to global variable   *
 * where the value is to be stored.                 */

/**/
void
intvarsetfn(Param pm, long x)
{
    *((long *)pm->u.data) = x;
}

/* Function to set value of any ZLE-related integer *
 * parameter.  data is pointer to global variable   *
 * where the value is to be stored.                 */

/**/
void
zlevarsetfn(Param pm, long x)
{
    if ((long *)pm->u.data == & columns) {
	if(x <= 0)
	    x = tccolumns > 0 ? tccolumns : 80;
	if (x > 2)
	    termflags &= ~TERM_NARROW;
	else
	    termflags |= TERM_NARROW;
    } else if ((long *)pm->u.data == & lines) {
	if(x <= 0)
	    x = tclines > 0 ? tclines : 24;
	if (x > 2)
	    termflags &= ~TERM_SHORT;
	else
	    termflags |= TERM_SHORT;
    }

    *((long *)pm->u.data) = x;
}

/* Function to set value of generic special scalar    *
 * parameter.  data is pointer to a character pointer *
 * representing the scalar (string).                  */

/**/
void
strvarsetfn(Param pm, char *x)
{
    char **q = ((char **)pm->u.data);

    zsfree(*q);
    *q = x;
}

/* Function to get value of generic special scalar    *
 * parameter.  data is pointer to a character pointer *
 * representing the scalar (string).                  */

/**/
char *
strvargetfn(Param pm)
{
    char *s = *((char **)pm->u.data);

    if (!s)
	return hcalloc(1);
    return s;
}

/* Function to get value of generic special array  *
 * parameter.  data is a pointer to the pointer to *
 * a pointer (a pointer to a variable length array *
 * of pointers).                                   */

/**/
char **
arrvargetfn(Param pm)
{
    return *((char ***)pm->u.data);
}

/* Function to set value of generic special array parameter.    *
 * data is pointer to a variable length array of pointers which *
 * represents this array of scalars (strings).  If pm->ename is *
 * non NULL, then it is a colon separated environment variable  *
 * version of this array which will need to be updated.         */

/**/
void
arrvarsetfn(Param pm, char **x)
{
    char ***dptr = (char ***)pm->u.data;

    if (*dptr != x)
	freearray(*dptr);
    if (pm->flags & PM_UNIQUE)
	uniqarray(x);
    *dptr = x ? x : mkarray(NULL);
    if (pm->ename && x)
	arrfixenv(pm->ename, x);
}

/**/
char *
colonarrgetfn(Param pm)
{
    char ***dptr = (char ***)pm->u.data;
    return *dptr ? zjoin(*dptr, ':') : "";
}

/**/
void
colonarrsetfn(Param pm, char *x)
{
    char ***dptr = (char ***)pm->u.data;

    /*
     * If this is tied to a parameter (rather than internal) array,
     * the array itself may be NULL.  Otherwise, we have to make
     * sure it doesn't ever get null.
     */
    if (*dptr)
	freearray(*dptr);
    *dptr = x ? colonsplit(x, pm->flags & PM_UNIQUE) :
	(pm->flags & PM_TIED) ? NULL : mkarray(NULL);
    if (pm->ename)
	arrfixenv(pm->nam, *dptr);
    zsfree(x);
}

/**/
int
uniqarray(char **x)
{
    int changes = 0;
    char **t, **p = x;

    if (!x || !*x)
	return 0;
    while (*++p)
	for (t = x; t < p; t++)
	    if (!strcmp(*p, *t)) {
		zsfree(*p);
		for (t = p--; (*t = t[1]) != NULL; t++);
		changes++;
		break;
	    }
    return changes;
}

/* Function to get value of special parameter `#' and `ARGC' */

/**/
long
poundgetfn(Param pm)
{
    return arrlen(pparams);
}

/* Function to get value for special parameter `RANDOM' */

/**/
long
randomgetfn(Param pm)
{
    return rand() & 0x7fff;
}

/* Function to set value of special parameter `RANDOM' */

/**/
void
randomsetfn(Param pm, long v)
{
    srand((unsigned int)v);
}

/* Function to get value for special parameter `SECONDS' */

/**/
long
secondsgetfn(Param pm)
{
    return time(NULL) - shtimer.tv_sec;
}

/* Function to set value of special parameter `SECONDS' */

/**/
void
secondssetfn(Param pm, long x)
{
    shtimer.tv_sec = time(NULL) - x;
    shtimer.tv_usec = 0;
}

/* Function to get value for special parameter `USERNAME' */

/**/
char *
usernamegetfn(Param pm)
{
    return get_username();
}

/* Function to set value of special parameter `USERNAME' */

/**/
void
usernamesetfn(Param pm, char *x)
{
#if defined(HAVE_SETUID) && defined(HAVE_GETPWNAM)
    struct passwd *pswd;

    if (x && (pswd = getpwnam(x)) && (pswd->pw_uid != cached_uid)) {
# ifdef HAVE_INITGROUPS
	initgroups(x, pswd->pw_gid);
# endif
	if(!setgid(pswd->pw_gid) && !setuid(pswd->pw_uid)) {
	    zsfree(cached_username);
	    cached_username = ztrdup(pswd->pw_name);
	    cached_uid = pswd->pw_uid;
	}
    }
#endif /* HAVE_SETUID && HAVE_GETPWNAM */
}

/* Function to get value for special parameter `UID' */

/**/
long
uidgetfn(Param pm)
{
    return getuid();
}

/* Function to set value of special parameter `UID' */

/**/
void
uidsetfn(Param pm, uid_t x)
{
#ifdef HAVE_SETUID
    setuid(x);
#endif
}

/* Function to get value for special parameter `EUID' */

/**/
long
euidgetfn(Param pm)
{
    return geteuid();
}

/* Function to set value of special parameter `EUID' */

/**/
void
euidsetfn(Param pm, uid_t x)
{
#ifdef HAVE_SETEUID
    seteuid(x);
#endif
}

/* Function to get value for special parameter `GID' */

/**/
long
gidgetfn(Param pm)
{
    return getgid();
}

/* Function to set value of special parameter `GID' */

/**/
void
gidsetfn(Param pm, gid_t x)
{
#ifdef HAVE_SETUID
    setgid(x);
#endif
}

/* Function to get value for special parameter `EGID' */

/**/
long
egidgetfn(Param pm)
{
    return getegid();
}

/* Function to set value of special parameter `EGID' */

/**/
void
egidsetfn(Param pm, gid_t x)
{
#ifdef HAVE_SETEUID
    setegid(x);
#endif
}

/**/
long
ttyidlegetfn(Param pm)
{
    struct stat ttystat;

    if (SHTTY == -1 || fstat(SHTTY, &ttystat))
	return -1;
    return time(NULL) - ttystat.st_atime;
}

/* Function to get value for special parameter `IFS' */

/**/
char *
ifsgetfn(Param pm)
{
    return ifs;
}

/* Function to set value of special parameter `IFS' */

/**/
void
ifssetfn(Param pm, char *x)
{
    zsfree(ifs);
    ifs = x;
    inittyptab();
}

/* Functions to set value of special parameters `LANG' and `LC_*' */

#ifdef LC_ALL
static struct localename {
    char *name;
    int category;
} lc_names[] = {
#ifdef LC_COLLATE
    {"LC_COLLATE", LC_COLLATE},
#endif
#ifdef LC_CTYPE
    {"LC_CTYPE", LC_CTYPE},
#endif
#ifdef LC_MESSAGES
    {"LC_MESSAGES", LC_MESSAGES},
#endif
#ifdef LC_TIME
    {"LC_TIME", LC_TIME},
#endif
    {NULL, 0}
};

/**/
static void
setlang(char *x)
{
    struct localename *ln;

    setlocale(LC_ALL, x ? x : "");
    for (ln = lc_names; ln->name; ln++)
	if ((x = getsparam(ln->name)))
	    setlocale(ln->category, x);
}

/**/
void
lc_allsetfn(Param pm, char *x)
{
    strsetfn(pm, x);
    if (!x)
	setlang(getsparam("LANG"));
    else
	setlocale(LC_ALL, x);
}

/**/
void
langsetfn(Param pm, char *x)
{
    strsetfn(pm, x);
    setlang(x);
}

/**/
void
lcsetfn(Param pm, char *x)
{
    struct localename *ln;

    strsetfn(pm, x);
    if (getsparam("LC_ALL"))
	return;
    if (!x)
	x = getsparam("LANG");

    for (ln = lc_names; ln->name; ln++)
	if (!strcmp(ln->name, pm->nam))
	    setlocale(ln->category, x ? x : "");
}
#endif

/* Function to get value for special parameter `HISTSIZE' */

/**/
long
histsizegetfn(Param pm)
{
    return histsiz;
}

/* Function to set value of special parameter `HISTSIZE' */

/**/
void
histsizesetfn(Param pm, long v)
{
    if ((histsiz = v) <= 2)
	histsiz = 2;
    resizehistents();
}

/* Function to get value for special parameter `ERRNO' */

/**/
long
errnogetfn(Param pm)
{
    return errno;
}

/* Function to get value for special parameter `histchar' */

/**/
char *
histcharsgetfn(Param pm)
{
    static char buf[4];

    buf[0] = bangchar;
    buf[1] = hatchar;
    buf[2] = hashchar;
    buf[3] = '\0';
    return buf;
}

/* Function to set value of special parameter `histchar' */

/**/
void
histcharssetfn(Param pm, char *x)
{
    if (x) {
	bangchar = x[0];
	hatchar = (bangchar) ? x[1] : '\0';
	hashchar = (hatchar) ? x[2] : '\0';
	zsfree(x);
    } else {
	bangchar = '!';
	hashchar = '#';
	hatchar = '^';
    }
    inittyptab();
}

/* Function to get value for special parameter `HOME' */

/**/
char *
homegetfn(Param pm)
{
    return home;
}

/* Function to set value of special parameter `HOME' */

/**/
void
homesetfn(Param pm, char *x)
{
    zsfree(home);
    if (x && isset(CHASELINKS) && (home = xsymlink(x)))
	zsfree(x);
    else
	home = x ? x : ztrdup("");
    finddir(NULL);
}

/* Function to get value for special parameter `WORDCHARS' */

/**/
char *
wordcharsgetfn(Param pm)
{
    return wordchars;
}

/* Function to set value of special parameter `WORDCHARS' */

/**/
void
wordcharssetfn(Param pm, char *x)
{
    zsfree(wordchars);
    wordchars = x;
    inittyptab();
}

/* Function to get value for special parameter `_' */

/**/
char *
underscoregetfn(Param pm)
{
    return underscore;
}

/* Function to get value for special parameter `TERM' */

/**/
char *
termgetfn(Param pm)
{
    return term;
}

/* Function to set value of special parameter `TERM' */

/**/
void
termsetfn(Param pm, char *x)
{
    zsfree(term);
    term = x ? x : ztrdup("");

    /* If non-interactive, delay setting up term till we need it. */
    if (unset(INTERACTIVE) || !*term)
	termflags |= TERM_UNKNOWN;
    else 
	init_term();
}

/* We could probably replace the replenv with the actual code to *
 * do the replacing, since we've already scanned for the string. */

/**/
static void
arrfixenv(char *s, char **t)
{
    char **ep, *u;
    int len_s;
    Param pm;

    MUSTUSEHEAP("arrfixenv");
    if (t == path)
	cmdnamtab->emptytable(cmdnamtab);
    u = t ? zjoin(t, ':') : "";
    len_s = strlen(s);
    pm = (Param) paramtab->getnode(paramtab, s);
    for (ep = environ; *ep; ep++)
	if (!strncmp(*ep, s, len_s) && (*ep)[len_s] == '=') {
	    pm->env = replenv(*ep, u);
	    return;
	}
    if (isset(ALLEXPORT))
	pm->flags |= PM_EXPORTED;
    if (pm->flags & PM_EXPORTED)
	pm->env = addenv(s, u);
}

/* Given *name = "foo", it searchs the environment for string *
 * "foo=bar", and returns a pointer to the beginning of "bar" */

/**/
char *
zgetenv(char *name)
{
    char **ep, *s, *t;
 
    for (ep = environ; *ep; ep++) {
	for (s = *ep, t = name; *s && *s == *t; s++, t++);
	if (*s == '=' && !*t)
	    return s + 1;
    }
    return NULL;
}

/* Change the value of an existing environment variable */

/**/
char *
replenv(char *e, char *value)
{
    char **ep, *s;
    int len_value;

    for (ep = environ; *ep; ep++)
	if (*ep == e) {
	    for (len_value = 0, s = value;
		 *s && (*s++ != Meta || *s++ != 32); len_value++);
	    s = e;
	    while (*s++ != '=');
	    *ep = (char *) zrealloc(e, s - e + len_value + 1);
	    s = s - e + *ep - 1;
	    while (*s++)
		if ((*s = *value++) == Meta)
		    *s = *value++ ^ 32;
	    return *ep;
	}
    return NULL;
}

/* Given strings *name = "foo", *value = "bar", *
 * return a new string *str = "foo=bar".        */

/**/
static char *
mkenvstr(char *name, char *value)
{
    char *str, *s;
    int len_name, len_value;

    len_name = strlen(name);
    for (len_value = 0, s = value;
	 *s && (*s++ != Meta || *s++ != 32); len_value++);
    s = str = (char *) zalloc(len_name + len_value + 2);
    strcpy(s, name);
    s += len_name;
    *s = '=';
    while (*s++)
	if ((*s = *value++) == Meta)
	    *s = *value++ ^ 32;
    return str;
}

/* Given *name = "foo", *value = "bar", add the    *
 * string "foo=bar" to the environment.  Return a  *
 * pointer to the location of this new environment *
 * string.                                         */

/**/
char *
addenv(char *name, char *value)
{
    char **ep, *s, *t;
    int num_env;

    /* First check if there is already an environment *
     * variable matching string `name'.               */
    for (ep = environ; *ep; ep++) {
	for (s = *ep, t = name; *s && *s == *t; s++, t++);
	if (*s == '=' && !*t) {
	    zsfree(*ep);
	    return *ep = mkenvstr(name, value);
	}
    }

    /* Else we have to make room and add it */
    num_env = arrlen(environ);
    environ = (char **) zrealloc(environ, (sizeof(char *)) * (num_env + 2));

    /* Now add it at the end */
    ep = environ + num_env;
    *ep = mkenvstr(name, value);
    *(ep + 1) = NULL;
    return *ep;
}

/* Delete a pointer from the list of pointers to environment *
 * variables by shifting all the other pointers up one slot. */

/**/
void
delenv(char *x)
{
    char **ep;

    for (ep = environ; *ep; ep++) {
	if (*ep == x)
	    break;
    }
    if (*ep)
	for (; (ep[0] = ep[1]); ep++);
}

/**/
static void
convbase(char *s, long v, int base)
{
    int digs = 0;
    unsigned long x;

    if (v < 0)
	*s++ = '-', v = -v;
    if (base <= 1)
	base = 10;

    if (base != 10) {
	sprintf(s, "%d#", base);
	s += strlen(s);
    }
    for (x = v; x; digs++)
	x /= base;
    if (!digs)
	digs = 1;
    s[digs--] = '\0';
    x = v;
    while (digs >= 0) {
	int dig = x % base;

	s[digs--] = (dig < 10) ? '0' + dig : dig - 10 + 'A';
	x /= base;
    }
}

/* Start a parameter scope */

/**/
void
startparamscope(void)
{
    locallevel++;
}

/* End a parameter scope: delete the parameters local to the scope. */

/**/
void
endparamscope(void)
{
    locallevel--;
    scanhashtable(paramtab, 0, 0, 0, scanendscope, 0);
}

/**/
static void
scanendscope(HashNode hn, int flags)
{
    Param pm = (Param)hn;
    if(pm->level > locallevel)
	unsetparam_pm(pm, 0, 0);
}


/**********************************/
/* Parameter Hash Table Functions */
/**********************************/

/**/
void
freeparamnode(HashNode hn)
{
    Param pm = (Param) hn;
 
    /* Since the second flag to unsetfn isn't used, I don't *
     * know what its value should be.                       */
    if (delunset)
	pm->unsetfn(pm, 1);
    zsfree(pm->nam);
    /* If this variable was tied by the user, ename was ztrdup'd */
    if (pm->flags & PM_TIED)
	zsfree(pm->ename);
    zfree(pm, sizeof(struct param));
}

/* Return a string describing the type of a parameter. */

/**/
char *
paramtypestr(Param pm)
{
    char *val;
    int f = pm->flags;

    if (!(f & PM_UNSET)) {
	switch (PM_TYPE(f)) {
	case PM_SCALAR:  val = "scalar"; break;
	case PM_ARRAY:   val = "array"; break;
	case PM_INTEGER: val = "integer"; break;
	case PM_HASHED:  val = "association"; break;
	}
	val = dupstring(val);
	if (f & PM_LEFT)
	    val = dyncat(val, "-left");
	if (f & PM_RIGHT_B)
	    val = dyncat(val, "-right_blanks");
	if (f & PM_RIGHT_Z)
	    val = dyncat(val, "-right_zeros");
	if (f & PM_LOWER)
	    val = dyncat(val, "-lower");
	if (f & PM_UPPER)
	    val = dyncat(val, "-upper");
	if (f & PM_READONLY)
	    val = dyncat(val, "-readonly");
	if (f & PM_TAGGED)
	    val = dyncat(val, "-tag");
	if (f & PM_EXPORTED)
	    val = dyncat(val, "-export");
	if (f & PM_UNIQUE)
	    val = dyncat(val, "-unique");
    } else
	val = dupstring("");

    return val;
}

/* Print a parameter */

/**/
void
printparamnode(HashNode hn, int printflags)
{
    Param p = (Param) hn;
    char *t, **u;

    if (p->flags & PM_UNSET)
	return;

    /* Print the attributes of the parameter */
    if (printflags & PRINT_TYPE) {
	if (p->flags & PM_AUTOLOAD)
	    printf("undefined ");
	if (p->flags & PM_INTEGER)
	    printf("integer ");
	else if (p->flags & PM_ARRAY)
	    printf("array ");
	else if (p->flags & PM_HASHED)
	    printf("association ");
	if (p->level)
	    printf("local ");
	if (p->flags & PM_LEFT)
	    printf("left justified %d ", p->ct);
	if (p->flags & PM_RIGHT_B)
	    printf("right justified %d ", p->ct);
	if (p->flags & PM_RIGHT_Z)
	    printf("zero filled %d ", p->ct);
	if (p->flags & PM_LOWER)
	    printf("lowercase ");
	if (p->flags & PM_UPPER)
	    printf("uppercase ");
	if (p->flags & PM_READONLY)
	    printf("readonly ");
	if (p->flags & PM_TAGGED)
	    printf("tagged ");
	if (p->flags & PM_EXPORTED)
	    printf("exported ");
    }

    if (printflags & PRINT_NAMEONLY) {
	zputs(p->nam, stdout);
	putchar('\n');
	return;
    }

    quotedzputs(p->nam, stdout);

    if (p->flags & PM_AUTOLOAD) {
	putchar('\n');
	return;
    }
    if (printflags & PRINT_KV_PAIR)
	putchar(' ');
    else
	putchar('=');

    /* How the value is displayed depends *
     * on the type of the parameter       */
    switch (PM_TYPE(p->flags)) {
    case PM_SCALAR:
	/* string: simple output */
	if (p->gets.cfn && (t = p->gets.cfn(p)))
	    quotedzputs(t, stdout);
	break;
    case PM_INTEGER:
	/* integer */
	printf("%ld", p->gets.ifn(p));
	break;
    case PM_ARRAY:
	/* array */
	if (!(printflags & PRINT_KV_PAIR))
	    putchar('(');
	u = p->gets.afn(p);
	if(*u) {
	    quotedzputs(*u++, stdout);
	    while (*u) {
		putchar(' ');
		quotedzputs(*u++, stdout);
	    }
	}
	if (!(printflags & PRINT_KV_PAIR))
	    putchar(')');
	break;
    case PM_HASHED:
	/* association */
	if (!(printflags & PRINT_KV_PAIR))
	    putchar('(');
	{
            HashTable ht = p->gets.hfn(p);
            if (ht)
		scanhashtable(ht, 0, 0, PM_UNSET,
			      ht->printnode, PRINT_KV_PAIR);
	}
	if (!(printflags & PRINT_KV_PAIR))
	    putchar(')');
	break;
    }
    if (printflags & PRINT_KV_PAIR)
	putchar(' ');
    else
	putchar('\n');
}
