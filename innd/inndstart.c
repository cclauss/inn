/*  $Id$
**
**  Open the privileged port, then exec innd.
*/
#include <pwd.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <grp.h>
#include "configdata.h"
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "clibrary.h"
#include <fcntl.h>
#include "paths.h"
#include <syslog.h> 
#include "libinn.h"
#include "macros.h"
#if	defined(HAVE_RLIMIT)
#if	defined(DO_NEED_TIME)
#include <time.h>
#endif	/* defined(DO_NEED_TIME) */
#include <sys/time.h>
#endif	/* defined(HAVE_RLIMIT) */
#include <sys/resource.h>
#include <errno.h>

/* #define DEBUGGER "/usr/ucb/dbx" */

#if	defined(HAVE_RLIMIT)
/*
**  Set the limit on the number of open files we can have.  I don't
**  like having to do this.
*/
STATIC void
SetDescriptorLimit(i)
    int			i;
{
    struct rlimit	rl;

    if (getrlimit(RLIMIT_NOFILE, &rl) < 0) {
	syslog(L_ERROR, "inndstart cant getrlimit(NOFILE) %m");
	return;
    }
    rl.rlim_cur = i;
    if (setrlimit(RLIMIT_NOFILE, &rl) < 0) {
	syslog(L_ERROR, "inndstart cant setrlimit(NOFILE) %d %m", i);
	return;
    }
}
#endif	/* defined(HAVE_RLIMIT) */


int main(int ac, char *av[])
{
    GID_T		NewsGID;
    UID_T		NewsUID;
    struct sockaddr_in	server;
    register int	i;
    register int	j;
    register int	sock;
#if	defined(SO_REUSEADDR)
    int			on;
#endif	/* defined(SO_REUSEADDR) */
    STRING		*argv;
    char		*p;
    char		pflag[SMBUF];
    char		buff[BUFSIZ];
    char *		env[8];
    struct stat		Sb;
    char		*inndpath;
    struct passwd	*pwd;

    (void)openlog("inndstart", L_OPENLOG_FLAGS, LOG_INN_PROG);

    if (ReadInnConf() < 0) exit(1);

    /* Make sure INND directory exists. */
    if (stat(innconf->pathrun, &Sb) < 0 || !S_ISDIR(Sb.st_mode)) {
	syslog(L_FATAL, "inndstart cant stat %s %m", innconf->pathrun);
	exit(1);
    }
    if (Sb.st_uid == 0) {
	syslog(L_FATAL, "inndstart %s must not be owned by root", innconf->pathrun);
	exit(1);
    }
    pwd = getpwnam(NEWSUSER);
    if (pwd == (struct passwd *)NULL) {
	syslog(L_FATAL, "inndstart getpwnam(%s): %s", NEWSUSER, strerror(errno));
	exit(1);
    } else if (pwd->pw_gid != Sb.st_gid) {
	syslog(L_FATAL, "inndstart %s must have group %s", innconf->pathrun, NEWSGRP);
	exit(1);
    } else if (pwd->pw_uid != Sb.st_uid) {
	syslog(L_FATAL, "inndstart %s must be owned by %s", innconf->pathrun, NEWSUSER);
	exit(1);
    }
    NewsUID = Sb.st_uid;
    NewsGID = Sb.st_gid;

    if (setgroups(1, &NewsGID) < 0)
        syslog(L_ERROR, "inndstart cant setgroups %m");
    
#if	defined(HAVE_RLIMIT)
    if (innconf->rlimitnofile >= 0)
	SetDescriptorLimit(innconf->rlimitnofile);
#endif	/* defined(HAVE_RLIMIT) */

    /* Create a socket and name it. */
    if ((i = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
	syslog(L_FATAL, "inndstart cant socket %m");
	exit(1);
    }
    sock = i;
#if	defined(SO_REUSEADDR)
    on = 1;
    if (setsockopt(i, SOL_SOCKET, SO_REUSEADDR, (caddr_t)&on, sizeof on) < 0)
	syslog(L_ERROR, "inndstart cant setsockopt %m");
#endif	/* defined(SO_REUSEADDR) */
    (void)memset((POINTER)&server, 0, sizeof server);
    server.sin_port = htons(innconf->port);
    for (j = 1; av[j]; j++) {
	if (!strncmp("-P", av[j], 2)) {
	    server.sin_port = htons(atoi(&av[j][2]));
	    break;
	}
    }
    server.sin_family = AF_INET;

    server.sin_addr.s_addr = htonl(INADDR_ANY);
    p = innconf->bindaddress;
    if ((p != NULL) && !EQ(p, "all") && !EQ(p, "any")) {
	server.sin_addr.s_addr = inet_addr(p);
	if (server.sin_addr.s_addr == INADDR_NONE) {
	    syslog(L_FATAL, "inndstart unable to determine bind ip (%s) %m", p);
	    exit(1);
	}
    }
    for (j = 1; av[j]; j++) {
	if (!strncmp("-I", av[j], 2)) {
	    server.sin_addr.s_addr = inet_addr(&av[j][2]);
	    if (server.sin_addr.s_addr == INADDR_NONE) {
		syslog(L_FATAL, "inndstart unable to determine bind ip (%s) %m",
					&av[j][2]);
	    	exit(1);
	    }
	}
    }
    if (bind(i, (struct sockaddr *)&server, sizeof server) < 0) {
	syslog(L_FATAL, "inndstart cant bind %m");
	exit(1);
    }
    (void)sprintf(pflag, "-p%d", i);

    /* Build the new argument vector. */
    argv = NEW(STRING, 2 + ac + 1);
    j = 0;
    inndpath = cpcatpath(innconf->pathbin, "innd");
#if	defined(DEBUGGER)
    argv[j++] = DEBUGGER;
    argv[j++] = inndpath;
    argv[j] = NULL;
    (void)printf("Use -dp%d\n", i);
#else
    argv[j++] = inndpath;
    argv[j++] = pflag;
    for (i = 1; av[i]; ) {
	if ((strncmp(av[i], "-p", 2) != 0) && (strncmp(av[i], "-P", 2) != 0) &&
		(strncmp(av[i], "-I", 2) != 0))
	argv[j++] = av[i++];
	else
	    i++;
    }
    argv[j] = NULL;
#endif	/* defined(DEBUGGER) */

    /* Set our user and group id. */
    (void)setgid(NewsGID);
    if (getgid() != NewsGID)
	syslog(L_ERROR, "inndstart cant setgid to %d %m", NewsGID);
    (void)setuid(NewsUID);
    if (getuid() != NewsUID)
	syslog(L_ERROR, "inndstart cant setuid to %d %m", NewsUID);

    /* Set up the environment.  Note that we're trusting BIND_INADDR and TZ;
       everything else is either from inn.conf or from configure.  These
       should be sanity-checked before being propagated, but that requires
       knowledge of the range of possible values.  Just limiting their
       length doesn't necessarily do anything to prevent exploits and may
       stop things from working that should.  */
    env[0] = NEW(char, (5 + strlen(innconf->pathbin) + 1
                        + strlen(innconf->pathetc) + 23 + 1));
    sprintf(env[0], "PATH=%s:%s:/bin:/usr/bin:/usr/ucb",
            innconf->pathbin, innconf->pathetc);
    env[1] = NEW(char, 7 + strlen(innconf->pathtmp) + 1);
    sprintf(env[1], "TMPDIR=%s", innconf->pathtmp);
    env[2] = NEW(char, 6 + strlen(_PATH_SH) + 1);
    sprintf(env[2], "SHELL=%s", _PATH_SH);
    env[3] = NEW(char, 8 + strlen(NEWSMASTER) + 1);
    sprintf(env[3], "LOGNAME=%s", NEWSMASTER);
    env[4] = NEW(char, 5 + strlen(NEWSMASTER) + 1);
    sprintf(env[4], "USER=%s", NEWSMASTER);
    env[5] = NEW(char, 5 + strlen(innconf->pathnews) + 1);
    sprintf(env[5], "HOME=%s", innconf->pathnews);
    i = 6;
    if ((p = getenv("BIND_INADDR")) != NULL) {
        env[i] = NEW(char, 12 + strlen(p) + 1);
        sprintf(env[i], "BIND_INADDR=%s", p);
        i++;
    }
    if ((p = getenv("TZ")) != NULL) {
        env[i] = NEW(char, 3 + strlen(p) + 1);
        sprintf(env[i], "TZ=%s", p);
        i++;
    }
    env[i] = NULL;

    /* Go exec innd. */
    (void)execve(argv[0], (CSTRING *)argv, (CSTRING *)env);
    syslog(L_FATAL, "inndstart cant exec %s %m", argv[0]);
    _exit(1);
    /* NOTREACHED */
    return(1);
}
