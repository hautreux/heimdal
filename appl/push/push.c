/*
 * Copyright (c) 1997, 1998 Kungliga Tekniska H�gskolan
 * (Royal Institute of Technology, Stockholm, Sweden). 
 * All rights reserved. 
 *
 * Redistribution and use in source and binary forms, with or without 
 * modification, are permitted provided that the following conditions 
 * are met: 
 *
 * 1. Redistributions of source code must retain the above copyright 
 *    notice, this list of conditions and the following disclaimer. 
 *
 * 2. Redistributions in binary form must reproduce the above copyright 
 *    notice, this list of conditions and the following disclaimer in the 
 *    documentation and/or other materials provided with the distribution. 
 *
 * 3. All advertising materials mentioning features or use of this software 
 *    must display the following acknowledgement: 
 *      This product includes software developed by Kungliga Tekniska 
 *      H�gskolan and its contributors. 
 *
 * 4. Neither the name of the Institute nor the names of its contributors 
 *    may be used to endorse or promote products derived from this software 
 *    without specific prior written permission. 
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND 
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE 
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE 
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE 
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL 
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS 
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) 
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT 
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY 
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF 
 * SUCH DAMAGE. 
 */

#include "push_locl.h"
RCSID("$Id$");

#ifdef KRB4
static int use_v4 = 0;
#endif

#ifdef KRB5
static int use_v5 = 1;
static krb5_context context;
#endif

static char *port_str;
static int do_verbose;
static int do_fork;
static int do_leave;
static int do_version;
static int do_help;

static ssize_t
net_read (int fd, void *v, size_t len)
{
#if defined(KRB5)
    return krb5_net_read (context, &fd, v, len);
#elif defined(KRB4)
    return krb_net_read (fd, v, len);
#endif
}

static ssize_t
net_write (int fd, const void *v, size_t len)
{
#if defined(KRB5)
    return krb5_net_write (context, &fd, v, len);
#elif defined(KRB4)
    return krb_net_write (fd, v, len);
#endif
}

struct getargs args[] = {
#ifdef KRB4
    { "krb4",	'4', arg_flag,		&use_v4,	"Use Kerberos V4",
      NULL },
#endif    
#ifdef KRB5
    { "krb5",	'5', arg_flag,		&use_v5,	"Use Kerberos V5",
      NULL },
#endif
    { "verbose",'v', arg_flag,		&do_verbose,	"Verbose",
      NULL },
    { "fork",	'f', arg_flag,		&do_fork,	"Fork deleting proc",
      NULL },
    { "leave",	'l', arg_flag,		&do_leave,	"Leave mail on server",
      NULL },
    { "port",	'p', arg_string,	&port_str,	"Use this port",
      "number-or-service" },
    { "version", 0,  arg_flag,		&do_version,	"Print version",
      NULL },
    { "help",	 0,  arg_flag,		&do_help,	NULL,
      NULL }

};

static void
usage (int ret)
{
    arg_printusage (args,
		    sizeof(args) / sizeof(args[0]),
		    "{po:username | hostname[:username]} filename");
    exit (ret);
}

static int
do_connect (char *host, int port, int nodelay)
{
    struct hostent *h;
    struct sockaddr_in addr;
    char **p;
    int s;

    h = roken_gethostbyname (host);
    if (h == NULL)
	errx (1, "gethostbyname: %s", hstrerror(h_errno));
    memset (&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = port;
    for (p = h->h_addr_list; *p; ++p) {
	memcpy(&addr.sin_addr, *p, sizeof(addr.sin_addr));

	s = socket (AF_INET, SOCK_STREAM, 0);
	if (s < 0)
	    err (1, "socket");
	if (connect(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
	    warn ("connect(%s)", host);
	    close (s);
	    continue;
	} else {
	    break;
	}
    }
    if (*p == NULL)
	return -1;
    else {
	if(setsockopt(s, IPPROTO_TCP, TCP_NODELAY,
		      (void *)&nodelay, sizeof(nodelay)) < 0)
	    err (1, "setsockopt TCP_NODELAY");

	return s;
    }
}

typedef enum { INIT = 0, GREAT, USER, PASS, STAT, RETR, DELE, QUIT } pop_state;

#define PUSH_BUFSIZ 65536

#define STEP 16

struct write_state {
    struct iovec *iovecs;
    size_t niovecs, maxiovecs, allociovecs;
    int fd;
};

static void
write_state_init (struct write_state *w, int fd)
{
#ifdef UIO_MAXIOV
    w->maxiovecs = UIO_MAXIOV;
#else
    w->maxiovecs = 16;
#endif
    w->allociovecs = min(STEP, w->maxiovecs);
    w->niovecs = 0;
    w->iovecs = malloc(w->allociovecs * sizeof(*w->iovecs));
    if (w->iovecs == NULL)
	err (1, "malloc");
    w->fd = fd;
}

static void
write_state_add (struct write_state *w, void *v, size_t len)
{
    if(w->niovecs == w->allociovecs) {				
	if(w->niovecs == w->maxiovecs) {				
	    if(writev (w->fd, w->iovecs, w->niovecs) < 0)		
		err(1, "writev");				
	    w->niovecs = 0;					
	} else {						
	    w->allociovecs = min(w->allociovecs + STEP, w->maxiovecs);	
	    w->iovecs = realloc (w->iovecs,				
				 w->allociovecs * sizeof(*w->iovecs));	
	    if (w->iovecs == NULL)					
		errx (1, "realloc");				
	}							
    }								
    w->iovecs[w->niovecs].iov_base = v;				
    w->iovecs[w->niovecs].iov_len  = len;				
    ++w->niovecs;							
}

static void
write_state_flush (struct write_state *w)
{
    if (w->niovecs) {
	if (writev (w->fd, w->iovecs, w->niovecs) < 0)
	    err (1, "writev");
	w->niovecs = 0;
    }
}

static void
write_state_destroy (struct write_state *w)
{
    free (w->iovecs);
}

static int
doit(int s,
     char *host,
     char *user,
     char *outfilename,
     int leavep,
     int verbose,
     int forkp)
{
    int ret;
    char out_buf[PUSH_BUFSIZ];
    size_t out_len = 0;
    char *out_ptr = out_buf;
    char in_buf[PUSH_BUFSIZ + 1];	/* sentinel */
    size_t in_len = 0;
    char *in_ptr = in_buf;
    pop_state state = INIT;
    unsigned count, bytes;
    unsigned asked_for = 0, retrieved = 0, asked_deleted = 0, deleted = 0;
    int out_fd;
    char from_line[128];
    size_t from_line_length;
    time_t now;
    struct write_state write_state;

    out_fd = open(outfilename, O_WRONLY | O_APPEND | O_CREAT, 0666);
    if (out_fd < 0)
	err (1, "open %s", outfilename);

    if (verbose)
	fprintf (stderr, "%s@%s -> %s\n", user, host, outfilename);

    now = time(NULL);
    from_line_length = snprintf (from_line, sizeof(from_line),
				 "From %s %s", "push", ctime(&now));

    out_len = snprintf (out_buf, sizeof(out_buf),
			"USER %s\r\nPASS hej\r\nSTAT\r\n",
			user);
    if (net_write (s, out_buf, out_len) != out_len)
	err (1, "write");
    if (verbose > 1)
	write (STDERR_FILENO, out_buf, out_len);

    write_state_init (&write_state, out_fd);

    while(state != QUIT) {
	fd_set readset, writeset;

	FD_ZERO(&readset);
	FD_ZERO(&writeset);
	FD_SET(s,&readset);
	if ((state == STAT || state == RETR)  && asked_for < count
	    || (state == DELE && asked_deleted < count))
	    FD_SET(s,&writeset);
	ret = select (s + 1, &readset, &writeset, NULL, NULL);
	if (ret < 0)
	    if (errno == EAGAIN)
		continue;
	    else
		err (1, "select");

	if (FD_ISSET(s, &readset)) {
	    char *beg, *p;
	    size_t rem;

	    ret = read (s, in_ptr, sizeof(in_buf) - in_len - 1);
	    if (ret < 0)
		err (1, "read");
	    else if (ret == 0)
		errx (1, "EOF during read");

	    in_len += ret;
	    in_ptr += ret;
	    *in_ptr = '\0';

	    beg = in_buf;
	    rem = in_len;
	    while(rem > 1
		  && (p = strstr(beg, "\r\n")) != NULL) {
		if (state == RETR) {
		    char *copy = beg;

		    if (beg[0] == '.') {
			if (beg[1] == '\r' && beg[2] == '\n') {
			    state = STAT;
			    rem -= p - beg + 2;
			    beg = p + 2;
			    if (++retrieved == count) {
				write_state_flush (&write_state);
				if (fsync (out_fd) < 0)
				    err (1, "fsync");
				close(out_fd);
				if (leavep) {
				    state = QUIT;
				    net_write (s, "QUIT\r\n", 6);
				    if (verbose > 1)
				      net_write (STDERR_FILENO, "QUIT\r\n", 6);
				} else {
				    if (forkp) {
					pid_t pid;

					pid = fork();
					if (pid < 0)
					    warn ("fork");
					else if(pid != 0) {
					    if(verbose)
						fprintf (stderr,
							 "(exiting)");
					    return 0;
					}
				    }

				    state = DELE;
				    if (verbose)
					fprintf (stderr, "deleting... ");
				}
			    }
			    continue;
			} else
			    ++copy;
		    }
		    *p = '\n';
		    write_state_add(&write_state, copy, p - copy + 1);
		    rem -= p - beg + 2;
		    beg = p + 2;
		} else if (rem >= 3 && strncmp (beg, "+OK", 3) == 0) {
		    if (state == STAT) {
			write_state_add(&write_state,
					from_line, from_line_length);
			state = RETR;
		    } else if (state == DELE) {
			if (++deleted == count) {
			    state = QUIT;
			    net_write (s, "QUIT\r\n", 6);
			    if (verbose > 1)
				net_write (STDERR_FILENO, "QUIT\r\n", 6);
			    break;
			}
		    } else if (++state == STAT) {
			if(sscanf (beg + 4, "%u %u", &count, &bytes) != 2)
			    errx(1, "Bad STAT-line: %.*s", p - beg, beg);
			if (verbose)
			    fprintf (stderr, "%u message(s) (%u bytes). "
				     "fetching... ",
				     count, bytes);
			if (count == 0) {
			    state = QUIT;
			    net_write (s, "QUIT\r\n", 6);
			    if (verbose > 1)
				net_write (STDERR_FILENO, "QUIT\r\n", 6);
			    break;
			}
		    }

		    rem -= p - beg + 2;
		    beg = p + 2;
		} else
		    errx (1, "Bad response: %.*s", p - beg, beg);
	    }
	    write_state_flush (&write_state);

	    memmove (in_buf, beg, rem);
	    in_len = rem;
	    in_ptr = in_buf + rem;
	}
	if (FD_ISSET(s, &writeset)) {
	    if (state == STAT || state == RETR)
		out_len = snprintf (out_buf, sizeof(out_buf),
				    "RETR %u\r\n", ++asked_for);
	    else if(state == DELE)
		out_len = snprintf (out_buf, sizeof(out_buf),
				    "DELE %u\r\n", ++asked_deleted);
	    if (net_write (s, out_buf, out_len) != out_len)
		err (1, "write");
	    if (verbose > 1)
		write (STDERR_FILENO, out_buf, out_len);
	}
    }
    if (verbose)
	fprintf (stderr, "Done\n");
    write_state_destroy (&write_state);
    return 0;
}

#ifdef KRB5
static int
do_v5 (char *host,
       int port,
       char *user,
       char *filename,
       int leavep,
       int verbose,
       int forkp)
{
    krb5_error_code ret;
    krb5_ccache ccache;
    krb5_auth_context auth_context = NULL;
    krb5_principal server;
    int s;

    s = do_connect (host, port, 1);
    if (s < 0)
	return 1;

    ret = krb5_cc_default (context, &ccache);
    if (ret) {
	warnx ("krb5_cc_default: %s", krb5_get_err_text (context, ret));
	return 1;
    }
    ret = krb5_sname_to_principal (context,
				   host,
				   "pop",
				   KRB5_NT_SRV_HST,
				   &server);
    if (ret) {
	warnx ("krb5_sname_to_principal: %s",
	       krb5_get_err_text (context, ret));
	return 1;
    }

    ret = krb5_sendauth (context,
			 &auth_context,
			 &s,
			 "KPOPV1.0",
			 NULL,
			 server,
			 0,
			 NULL,
			 NULL,
			 ccache,
			 NULL,
			 NULL,
			 NULL);
    krb5_free_principal (context, server);
    krb5_cc_close (context, ccache);
    if (ret) {
	warnx ("krb5_sendauth: %s",
	       krb5_get_err_text (context, ret));
	return 1;
    }
    return doit (s, host, user, filename, leavep, verbose, forkp);
}
#endif

#ifdef KRB4
static int
do_v4 (char *host,
       int port,
       char *user,
       char *filename,
       int leavep,
       int verbose,
       int forkp)
{
    KTEXT_ST ticket;
    MSG_DAT msg_data;
    CREDENTIALS cred;
    des_key_schedule sched;
    int s;
    int ret;

    s = do_connect (host, port, 1);
    if (s < 0)
	return 1;
    ret = krb_sendauth(0,
		       s,
		       &ticket, 
		       "pop",
		       host,
		       krb_realmofhost(host),
		       getpid(),
		       &msg_data,
		       &cred,
		       sched,
		       NULL,
		       NULL,
		       "KPOPV0.1");
    if(ret) {
	warnx("krb_sendauth: %s", krb_get_err_text(ret));
	return 1;
    }
    return doit (s, host, user, filename, leavep, verbose, forkp);
}
#endif /* KRB4 */

static void
parse_pobox (char *a0, char *a1,
	     char **host, char **user, char **filename)
{
    char *h, *u, *f;
    char *p;

    h = a0;
    f = a1;
    p = strchr (h, ':');
    if (p) {
	*p++ = '\0';
	if (strcmp (h, "po") == 0) {
	    h = getenv("MAILHOST");
	    if (h == NULL)
		errx (1, "MAILHOST not set");
	}
	u = p;
    } else {
	struct passwd *pwd = getpwuid(getuid());
	if (pwd == NULL)
	    errx (1, "Who are you?");
	u = pwd->pw_name;
    }
    *host = h;
    *user = u;
    *filename = f;
}

int
main(int argc, char **argv)
{
    int port = 0;
    int optind = 0;
    int ret = 1;
    char *host, *user, *filename;
#ifdef KRB5
    krb5_context context;
    krb5_init_context (&context);
#endif

    set_progname (argv[0]);

    if (getarg (args, sizeof(args) / sizeof(args[0]), argc, argv,
		&optind))
	usage (1);

    argc -= optind;
    argv += optind;

    if (do_help)
	usage (0);

    if (do_version) {
	printf ("%s (%s-%s)\n", __progname, PACKAGE, VERSION);
	return 0;
    }
	
    if (argc != 2)
	usage (1);

    if (port_str) {
	struct servent *s = roken_getservbyname (port_str, "tcp");

	if (s)
	    port = s->s_port;
	else {
	    char *ptr;

	    port = strtol (port_str, &ptr, 10);
	    if (port == 0 && ptr == port_str)
		errx (1, "Bad port `%s'", port_str);
	    port = htons(port);
	}
    }
    if (port == 0)
#ifdef KRB5
	port = krb5_getportbyname (context, "kpop", "tcp", 1109);
#elif defined(KRB5)
	port = k_getportbyname ("kpop", "tcp", 1109);
#endif

    parse_pobox (argv[0], argv[1],
		 &host, &user, &filename);

#ifdef KRB5
    if (ret && use_v5) {
	ret = do_v5 (host, port, user, filename,
		     do_leave, do_verbose, do_fork);
    }
#endif

#ifdef KRB4
    if (ret && use_v4) {
	ret = do_v4 (host, port, user, filename,
		     do_leave, do_verbose, do_fork);
    }
#endif /* KRB4 */
    return ret;
}
