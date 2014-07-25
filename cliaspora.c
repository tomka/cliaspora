/*-
 * Copyright (c) 2014 Marcel Kaiser. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifdef __linux__
# define _GNU_SOURCE
#endif

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <ctype.h>
#include <wchar.h>
#include <err.h>
#include <errno.h>
#include <locale.h>
#include <unistd.h>

#include "types.h"
#include "json.h"
#include "ssl.h"
#include "http.h"
#include "config.h"
#include "file.h"
#include "str.h"
#include "str.h"

#define USER_AGENT "Cliaspora"

typedef struct aspect_s {
	int  id;
	char *name;
	struct aspect_s *next;
} aspect_t;

typedef struct user_attr_s {
	int	 id;		/* Numeric user ID. */
	int	 nc;		/* Notification counter. */
	int	 mc;		/* Unread messages counter */
	int	 fc;		/* Following count. */
	int	 guid;
	char	 *name;		/* Screen name. */
	char	 *did;		/* user@pod.org */
	char	 *avatar;	/* icon. */
	aspect_t *aspects;	/* List of the user's aspects. */
} user_attr_t;

typedef struct msg_idx_s {
	int  aid;		/* Author ID */
	int  mid;		/* Message ID */
	char *subject;
	char *date;
	struct msg_idx_s *next;
} msg_idx_t;

typedef struct contact_s {
	int  id;
	char *name;		/* Screen name. */
	char *handle;		/* user@pod.tld. */
	char *url;		/* URL to profile. */
	char *avatar;		/* Image URL. */
	struct contact_s *next;
} contact_t;

typedef struct session_s {	
	char	    *host;	/* Pod address. */
	char	    *cookie;	/* Session cookie. */
	u_short	    port;	/* Pod port. */
	msg_idx_t   *midx;	/* TOC of your private messages.*/
	contact_t   *contacts;	/* List of your contacts. */
	user_attr_t attr;
} session_t;

typedef struct comment_s {
	char *author;
	char *handle;
	char *date;
	char *text;
} comment_t;

typedef struct post_s {
	int  id;
	int  likes;
	int  reshares;
	int  comments;
	int  root_id;
	char type;
#define POST_TYPE_STATUS  1
#define POST_TYPE_RESHARE 2
	char *author;
	char *handle;
	char *date;
	char *text;
	char *root_author;
	char *root_date;
	char *root_handle;
} post_t;

static int	 upload(session_t *, const char *, const char *, char * const *);
static int	 upload_file(session_t *, const char *);
static int	 close_session(session_t *);
static int	 read_stream(session_t *, const char *);
static int	 get_aspect_id(session_t *, const char *);
static int	 get_contact_id(contact_t *, const char *);
static int	 get_pm_id(session_t *, const char *);
static int	 dpoll(session_t *, const char *, const char *, const char *,
		       const char *);
static int	 post(session_t *, const char *, const char *);
static int	 like(session_t *, int);
static int	 delete_post(session_t *, int);
static int	 comment(session_t *, const char *, int);
static int	 message(session_t *, const char *, const char *, int);
static int	 reply(session_t *, const char *, int);
static int	 reshare(session_t *, int);
static int	 add_aspect(session_t *, const char *, bool);
static int	 add_contact(session_t *, int, int);
static int	 follow_tag(session_t *, const char *);
static int	 get_attributs(session_t *);
extern char	 *readpass(void);
static char	 *get_post_guid(session_t *, int);
static char	 *diaspora_login(const char *, u_short, const char *,
				 const char *);
static void	 groff_printf(const char *, ...);
static void	 show_msg_index(session_t *);
static void	 show_aspects(session_t *);
static void	 free_aspects(aspect_t *);
static void	 show_contacts(contact_t *);
static void	 free_msg_idx(msg_idx_t *);
static void	 usage(void);
static void	 free_contacts(contact_t *);
static void	 cleanup(int);
static aspect_t  *get_aspects(json_node_t *);
static session_t *create_session(void);
static session_t *new_session(const char *, u_short, const char *,
			      const char *);
static msg_idx_t *get_msg_index(session_t *);
static msg_idx_t *new_msg_idx_node(msg_idx_t *);
static contact_t *lookup_user(session_t *, const char *);
static contact_t *new_contact_node(contact_t *);
static contact_t *get_contacts(session_t *);
static contact_t *find_contact_by_id(contact_t *, int);

int
main(int argc, char *argv[])
{
	int	  ch, eflag, mflag, i, len, aspect_id, user_id, pm_id; 
	bool	  public, have_cfg;
	char	  *account, *host, *user, *pass, *p, *ans, *buf, url[256];
	session_t *sp;
	contact_t *contacts;

	if (setlocale(LC_CTYPE, "en_US.UTF-8") == NULL) {
		warnx("Failed to set locale to \"en_US.UTF-8\". Using " \
		    "default locale.");
		if (setlocale(LC_CTYPE, "C") == NULL)
			warnx("Failed to set default locale.");
		warnx("Expect messed up output");
	}

	eflag = mflag = 0; account = NULL;
	while ((ch = getopt(argc, argv, "a:emh")) != -1) {
		switch (ch) {
		case 'a':
			account = optarg;
			break;
		case 'e':
			eflag = 1;
			break;
		case 'm':
			mflag = 1;
			break;
		case 'h':
		case '?':
		default:
			usage();
			break;
		}
	}
	argc -= optind;
	argv += optind;

	if (argc == 0)
		usage();

	(void)signal(SIGINT, cleanup);
	(void)signal(SIGTERM, cleanup);
	(void)signal(SIGHUP, cleanup);
	(void)signal(SIGQUIT, cleanup);

	switch (read_config(account)) {
	case  0:
		have_cfg = true;
		break;
	case -1:
		errx(EXIT_FAILURE, "Failed to read config file");
		/* NOTREACHED */
	default:
		have_cfg = false;
	}
	sp = NULL;
	if (strcmp(argv[0], "session") == 0) {
		if (argc < 2)
			usage();
		if (strcmp(argv[1], "close") == 0) {
			if (!have_cfg)
				errx(EXIT_FAILURE, "There is no session.");
			if ((sp = create_session()) == NULL) {
				errx(EXIT_FAILURE,
				    "Failed to create session.");
			}
			close_session(sp);
			return (EXIT_SUCCESS);
		}
		if (strcmp(argv[1], "new") != 0 || argc < 3)
			usage();
		if ((user = strtok(argv[2], "@")) == NULL)
			usage();
		if ((host = strtok(NULL, "@")) == NULL)
			usage();
		if (argc > 3)
			pass = argv[3];
		else if ((pass = readpass()) == NULL)
			errx(EXIT_FAILURE, "readpass() failed");
		sp = new_session(host, SSL_PORT, user, pass);
		if (sp == NULL)
			errx(EXIT_FAILURE, "Failed to create session.");
	} else if (!have_cfg)
		errx(EXIT_FAILURE, "Please create a session first.");
	else if (strcmp(argv[0], "show") == 0) {
		if (argc < 2)
			usage();
		if ((sp = create_session()) == NULL)
			errx(EXIT_FAILURE, "Failed to create session.");
		if (strcmp(argv[1], "stream") == 0)
			read_stream(sp, "/stream");
		else if (strcmp(argv[1], "activity") == 0)
			read_stream(sp, "/activity");
		else if (strcmp(argv[1], "mystream") == 0) {
			if (cfg.user == NULL || cfg.user[0] == '\0') {
				errx(EXIT_FAILURE, "Username not set. " \
				    "Please create a new session.");
			}
			(void)snprintf(url, sizeof(url), "/u/%s", cfg.user);
			read_stream(sp, url);
		} else
			usage();
	} else if (strcmp(argv[0], "lookup") == 0) {
		if (argc < 2)
			usage();
		if ((sp = create_session()) == NULL)
			errx(EXIT_FAILURE, "Failed to create session.");
		if ((contacts = lookup_user(sp, argv[1])) == NULL)
			errx(EXIT_FAILURE, "Failed to look up %s", argv[1]);
		show_contacts(contacts);
	} else if (strcmp(argv[0], "list") == 0) {
		if (argc < 2)
			usage();
		if ((sp = create_session()) == NULL)
			errx(EXIT_FAILURE, "Failed to create session.");
		if (strcmp(argv[1], "contacts") == 0) {
			if (get_contacts(sp) == NULL)
				errx(EXIT_FAILURE, "Failed to get contacts");
			show_contacts(sp->contacts);
		} else if (strcmp(argv[1], "messages") == 0) {
			if (get_contacts(sp) == NULL)
				warnx("Failed to get contacts");
			if (get_msg_index(sp) == NULL) {
				errx(EXIT_FAILURE,
				    "Failed to get message index");
			}
			show_msg_index(sp);
			exit(0);
		} else if (strcmp(argv[1], "aspects") == 0)
			show_aspects(sp);
		else
			usage();
	} else if (strcmp(argv[0], "like") == 0) {
		if (argc < 2)
			usage();
		if ((sp = create_session()) == NULL)
			errx(EXIT_FAILURE, "Failed to create session.");
		if (like(sp, strtol(argv[1], NULL, 10)) == -1)
			errx(EXIT_FAILURE, "Failed to \"like\" post");
	} else if (strcmp(argv[0], "delete") == 0) {
		if (argc < 2)
			usage();
		if ((sp = create_session()) == NULL)
			errx(EXIT_FAILURE, "Failed to create session.");
		if (delete_post(sp, strtol(argv[1], NULL, 10)) == -1)
			errx(EXIT_FAILURE, "Failed to delete post");
	} else if (strcmp(argv[0], "upload") == 0) {
		if (argc < 3)
			usage();
		if ((sp = create_session()) == NULL)
			errx(EXIT_FAILURE, "Failed to create session.");
		if (strcmp(argv[1], "public") != 0 &&
		    get_aspect_id(sp, argv[1]) == -1) {
			errx(EXIT_FAILURE, "Unknown aspect '%s'", argv[1]);
		}
		if (mflag == 1)
			buf = get_input(eflag == 1 ? false : true);
		if (upload(sp, argv[1], mflag ? buf : NULL, &argv[2]) == -1)
			errx(EXIT_FAILURE, "Failed to upload file(s).");
		if (mflag == 1 && eflag == 1)
			delete_postponed();
	} else if (strcmp(argv[0], "poll") == 0) {
		if (argc < 5)
			usage();
		if ((sp = create_session()) == NULL)
			errx(EXIT_FAILURE, "Failed to create session.");
		for (ans = NULL, len = 0, i = 3; i < argc; i++) {
			if ((p = json_escape_str(argv[i])) == NULL)
				err(EXIT_FAILURE, "json_escape_str()");
			len += strlen(p) + 4;
			if ((ans = realloc(ans, len)) == NULL)
				err(EXIT_FAILURE, "realloc()");
			if (i == 3)
				/* First iteration. Initialize 'ans' */
				*ans = '\0';
			(void)sprintf(ans + strlen(ans), "\"%s\"%s", p,
			    argc - 1 > i ? "," : "");
			free(p);
		}
		if (mflag == 1)
			buf = get_input(eflag == 1 ? false : true);
		if (dpoll(sp, argv[1], mflag ? buf : NULL, argv[2], ans) == -1)
			errx(EXIT_FAILURE, "Failed to start poll.");
		if (mflag == 1 && eflag == 1)
			delete_postponed();
	} else if (strcmp(argv[0], "reshare") == 0) {
		if (argc < 2)
			usage();
		if ((sp = create_session()) == NULL)
			errx(EXIT_FAILURE, "Failed to create session.");
		if (reshare(sp, strtol(argv[1], NULL, 10)) == -1)
			errx(EXIT_FAILURE, "Failed to reshare post");
 	} else if (strcmp(argv[0], "follow") == 0) {
		if (argc < 3)
			usage();
		if ((sp = create_session()) == NULL)
			errx(EXIT_FAILURE,  "Failed to create session.");
		if (strcmp(argv[1], "tag") == 0) {
			if (follow_tag(sp, argv[2]) == -1)
				errx(EXIT_FAILURE, "Failed to follow tag.");
		} else if (strcmp(argv[1], "user") == 0) {
			if (argc < 4)
				usage();
			contacts = lookup_user(sp, argv[2]);
			if (contacts == NULL) {
				errx(EXIT_FAILURE, "Failed to lookup '%s'",
				    argv[2]);
			}
			user_id = get_contact_id(contacts, argv[2]);
			if (user_id == -1) {
				errx(EXIT_FAILURE, "Couldn't find '%s'",
				    argv[2]);
			}
			aspect_id = get_aspect_id(sp, argv[3]);
			if (aspect_id == -1) {
				errx(EXIT_FAILURE, "Aspect '%s' not found",
				    argv[3]);
			}
			if (add_contact(sp, aspect_id, user_id) == -1)
				errx(EXIT_FAILURE, "Failed to follow");
		} else
			usage();
	} else if (strcmp(argv[0], "add") == 0) {
		if (argc < 4)
			usage();
		if ((sp = create_session()) == NULL)
			errx(EXIT_FAILURE, "Failed to create session.");
		if (strcmp(argv[1], "aspect") == 0) {
			public = false;
			if (strcmp(argv[3], "public") == 0)
				public = true;
			else if (strcmp(argv[3], "private") == 0)
				public = false;
			else
				usage();
			if (add_aspect(sp, argv[2], public) == -1)
				errx(EXIT_FAILURE, "Failed to add aspect");
		} else
			usage();
	} else if (strcmp(argv[0], "post") == 0) {
		if (argc < 2)
			usage();
		if ((sp = create_session()) == NULL)
			errx(EXIT_FAILURE, "Failed to create session.");
		if (strcmp(argv[1], "public") != 0 &&
		    get_aspect_id(sp, argv[1]) == -1) {
			errx(EXIT_FAILURE, "Unknown aspect '%s'", argv[1]);
		}
		buf = get_input(eflag == 1 ? false : true);
		if (post(sp, buf, argv[1]) == -1)
			errx(EXIT_FAILURE, "Failed to post");
		if (eflag == 1)
			delete_postponed();
	} else if (strcmp(argv[0], "comment") == 0) {
		if (argc < 2)
			usage();
		if ((sp = create_session()) == NULL)
			errx(EXIT_FAILURE, "Failed to create session.");
		buf = get_input(eflag == 1 ? false : true);
		if (comment(sp, buf, strtol(argv[1], NULL, 10)) == -1)
			errx(EXIT_FAILURE, "Failed to send comment");
		if (eflag == 1)
			delete_postponed();
	} else if (strcmp(argv[0], "message") == 0) {
		if (argc < 2)
			usage();
		if ((sp = create_session()) == NULL)
			errx(EXIT_FAILURE, "Failed to create session.");
		if (get_contacts(sp) == NULL)
			errx(EXIT_FAILURE, "Failed to get contacts");
		if ((pm_id = get_pm_id(sp, argv[1])) == -1) {
			errx(EXIT_FAILURE, "Failed to get %s's PM ID",
			    argv[1]);
		}
		buf = get_input(eflag == 1 ? false : true);
		if (message(sp, argc > 2 ? argv[2] : "No subject", buf,
		    pm_id) == -1)
			errx(EXIT_FAILURE, "Failed to send message");
		if (eflag == 1)
			delete_postponed();
	} else if (strcmp(argv[0], "reply") == 0) {
		if (argc < 2)
			usage();
		if ((sp = create_session()) == NULL)
			errx(EXIT_FAILURE, "Failed to create session.");
		buf = get_input(eflag == 1 ? false : true);
		if (reply(sp, buf, strtol(argv[1], NULL, 10)) == -1) {
			errx(EXIT_FAILURE, "Failed to reply to message %d",
			    (int)strtol(argv[1], NULL, 10));
		}
		if (eflag == 1)
			delete_postponed();
	} else if (strcmp(argv[0], "status") == 0) {
		if ((sp = create_session()) == NULL)
			errx(EXIT_FAILURE, "Failed to create session.");
		(void)puts("NOTIFICATIONS  NEW MESSAGES");
		(void)printf("%-13d  %d\n", sp->attr.nc, sp->attr.mc);
	} else
		usage();
	exit(0);
}

static void
usage()
{

	(void)printf("Usage: cliaspora [options] command args ...\n"	      \
	    "       cliaspora session new <handle> [password]\n"	      \
	    "       cliaspora [-a account] add aspect <aspect-name> "	      \
	    "<public|private>\n"					      \
	    "       cliaspora [-a account] delete <post-ID>\n"		      \
	    "       cliaspora [-a account] follow tag <tagname>\n"	      \
	    "       cliaspora [-a account] follow user <handle> <aspect>\n"   \
	    "       cliaspora [-a account] like <post-ID>\n"		      \
	    "       cliaspora [-a account] list contacts\n"		      \
	    "       cliaspora [-a account] list messages\n"		      \
	    "       cliaspora [-a account] list aspects\n"		      \
	    "       cliaspora [-a account] lookup <name|handle>\n"	      \
	    "       cliaspora [-a account] reshare <post-ID>\n"		      \
	    "       cliaspora [-a account] session close\n"		      \
	    "       cliaspora [-a account] show stream\n"		      \
	    "       cliaspora [-a account] show activity\n"		      \
	    "       cliaspora [-a account] show mystream\n"		      \
	    "       cliaspora [-a account] status\n"			      \
	    "       cliaspora [-a account][-m [-e]] upload <aspect> "	      \
	    "<file> ...\n" 						      \
	    "       cliaspora [-a account][-m [-e]] poll <aspect> <question> "\
	    "<option 1> <option 2> ...\n"	      			      \
	    "       cliaspora [-a account][-e] comment <post-ID>\n"	      \
	    "       cliaspora [-a account][-e] message <handle> [subject]\n"  \
	    "       cliaspora [-a account][-e] post <aspect>\n"		      \
	    "       cliaspora [-a account][-e] reply <message-ID>\n");
	exit(EXIT_FAILURE);
}

static void
cleanup(int unused)
{
	delete_tmpfile();
	(void)puts("\n\nBye!");
	exit(EXIT_SUCCESS);
}

static int
get_pm_id(session_t *sp, const char *handle)
{
	int	   status, id;
	char	   *p, *q;
	contact_t  *ctp;
	ssl_conn_t *cp;

	errno = 0;
	
	for (ctp = sp->contacts; ctp != NULL; ctp = ctp->next) {
		if (strcmp(ctp->handle, handle) == 0)
			break;
	}
	if (ctp == NULL) {
		warnx("'%s' not found in your contacts", handle);
		return (-1);
	}
	if ((cp = ssl_connect(sp->host, sp->port)) == NULL)
		return (-1);
	status = http_get(cp, ctp->url, sp->cookie, "*/*", USER_AGENT);
	if (status == HTTP_UNAUTHORIZED) {
		warnx("You're not logged in. Please create a new session");
		ssl_disconnect(cp); return (-1);
	} else if (status == -1) {
		ssl_disconnect(cp); return (-1);
	} else if (status != HTTP_OK) {
		warnx("Server replied with code %d", status);
		ssl_disconnect(cp); return (-1);
	}
	for (q = NULL; q == NULL && (p = ssl_readln(cp)) != NULL;)
		q = strstr(p, "/conversations/new?contact_id=");
	if (q == NULL) {
		warnx("Unexpected server reply"); ssl_disconnect(cp);
		return (-1);
	}
	if (strtok(q, "=&\" ") == NULL ||
	    (p = strtok(NULL, "=&\" ")) == NULL) {
		warnx("Unexpected server reply"); ssl_disconnect(cp);
		return (-1);
	}
	id = strtol(p, NULL, 10);
	ssl_disconnect(cp);
	
	return (id);
}

static char *
get_post_guid(session_t *sp, int id)
{
	int	    status;
	ssl_conn_t  *cp;
	json_node_t *node, *jp;
	static char *p, *url, guid[64];

	errno = 0;
	if ((url = strduprintf("/posts/%d", id)) == NULL)
		return (NULL);
	if ((cp = ssl_connect(sp->host, sp->port)) == NULL)
		return (NULL);
	status = http_get(cp, url, sp->cookie, "application/json",
	    USER_AGENT);
	free(url);
	if (status == HTTP_UNAUTHORIZED) {
		warnx("You're not logged in. Please create a new session");
		ssl_disconnect(cp); return (NULL);
	} else if (status == -1) {
		ssl_disconnect(cp);
		return (NULL);
	} else if (status != HTTP_OK) {
		warnx("Server replied with code %d", status);
		ssl_disconnect(cp); return (NULL);
	}
	while ((p = ssl_readln(cp)) != NULL && *p != '{')
		;
	if (p == NULL) {
		if (errno == 0)
			warnx("Unexpected server reply");
		ssl_disconnect(cp); return (NULL);
	}
	if ((node = new_json_node()) == NULL) {
		warn("new_json_node()"); ssl_disconnect(cp);
		return (NULL);
	}
	if (parse_json(node, p) == NULL) {
		ssl_disconnect(cp); return (NULL);
	}
	ssl_disconnect(cp);

	for (jp = node->val; jp != NULL; jp = jp->next) {
		if (jp->var != NULL && strcmp(jp->var, "guid") == 0)
			break;
	}
	if (jp != NULL)
		(void)strncpy(guid, (char *)jp->val, sizeof(guid));
	free_json_node(node);
	if (jp == NULL) {
		warnx("Couldn't find post's guid"); return (NULL);
	}
	return (guid);
}

static contact_t *
lookup_user(session_t *sp, const char *handle)
{
	int	    status;
	char	    *url, *p;
	contact_t   *contacts, *ctp;
	ssl_conn_t  *cp;
	json_node_t *node, *jp1, *jp2;

	errno = 0;

	if ((cp = ssl_connect(sp->host, sp->port)) == NULL)
		return (NULL);
	if ((url = strduprintf("/people?q=%s", handle)) == NULL)
		return (NULL);
	status = http_get(cp, url, sp->cookie, "application/json, */*",
	    USER_AGENT);
	free(url);
	if (status == HTTP_UNAUTHORIZED) {
		warnx("You're not logged in. Please create a new session");
		ssl_disconnect(cp); return (NULL);
	} else if (status == -1) {
		ssl_disconnect(cp); return (NULL);
	} else if (status != HTTP_OK) {
		warnx("Server replied with code %d", status);
		ssl_disconnect(cp); return (NULL);
	}
	jp1 = node = new_json_node();
	if (node == NULL) {
		ssl_disconnect(cp); return (NULL);
	}
	while ((p = ssl_readln(cp)) != NULL && *p != '[')
		;
	if (p == NULL) {
		warnx("Unexpected server answer"); ssl_disconnect(cp);
		return (NULL);
	}
	if (*p != '[') {
		if (errno == 0)
			warnx("Server reply not understood");
		ssl_disconnect(cp); return (NULL);
	}
	if (parse_json(node, p) == NULL) {
		ssl_disconnect(cp); free_json_node(node);
		return (NULL);
	}
	ssl_disconnect(cp);
	
	for (contacts = NULL, jp1 = node->val; jp1 != NULL; jp1 = jp1->next) {
		if (contacts == NULL) {
			if ((contacts = new_contact_node(NULL)) == NULL)
				goto error;
			ctp = contacts;
		} else if ((ctp = new_contact_node(contacts)) == NULL)
			goto error;
		if ((jp2 = find_json_node(jp1, "id")) != NULL)
			ctp->id = *(int *)jp2->val;
		else
			ctp->id = 0;
		if ((jp2 = find_json_node(jp1, "name")) != NULL) {
			if ((ctp->name = strdup((char *)jp2->val)) == NULL)
				goto error;
		} else if ((ctp->name = strdup("")) == NULL)
			goto error;
		if ((jp2 = find_json_node(jp1, "avatar")) != NULL) {
			if ((ctp->avatar = strdup((char *)jp2->val)) == NULL)
				goto error;
		} else if ((ctp->avatar = strdup("")) == NULL)
			goto error;
		if ((jp2 = find_json_node(jp1, "handle")) != NULL) {
			if ((ctp->handle = strdup((char *)jp2->val)) == NULL)
				goto error;
		} else if ((ctp->handle = strdup("")) == NULL)
			goto error;
		if ((jp2 = find_json_node(jp1, "url")) != NULL) {
			if ((ctp->url = strdup((char *)jp2->val)) == NULL)
				goto error;
		} else if ((ctp->url = strdup("")) == NULL)
			goto error;
	}
  	free_json_node(node);
	return (contacts);
error:
	free_json_node(node);
	free_contacts(contacts);
	
	return (NULL);
}	

static char *
diaspora_login(const char *host, u_short port, const char *user,
	       const char *pass)
{
	int	   status, tries;
	char	   *cookie, *scookie, *rq, *p, *q, *u, *head, *url, *atok;
	ssl_conn_t *cp;
	const char tmpl[] = "utf8=%%E2%%9C%%93&user%%5Busername%%5D=%s&"  \
                            "user%%5Bpassword%%5D=%s&user%%5Bremember_me" \
			    "%%5D=1&commit=Sign+in&authenticity_token=%s";

	errno = 0;
	if ((cp = ssl_connect(host, port)) == NULL) {
		return (NULL);
	}
	status = http_get(cp, "/users/sign_in", NULL, "*/*", USER_AGENT);
	if (status != 200) {
		warnx("Login failed. Server replied with code %d", status);
		return (NULL);
	}
	scookie = atok = NULL;
	while ((p = ssl_readln(cp)) != NULL && atok == NULL) {
		if (strncmp(p, "Set-Cookie:", 11) == 0) {
			for (q = p; (q = strtok(q, " ;")) != NULL; q = NULL) {
				if (strncmp(q, "_diaspora_session=", 18) != 0)
					continue;
				if ((scookie = strdup(q)) == NULL) {
					warn("strdup()"); ssl_disconnect(cp);
					return (NULL);
				}
			}
		} else if ((q = strstr(p, "name=\"authenticity_token\"")) !=
		    NULL) {
			if ((p = strstr(q, "value=")) != NULL) {
				while (*p != '\0' && *p != '=')
					p++;
				if (*p == '=')
					p++;
				if (*p == '"')
					p++;
				for (q = p; *q != '\0' && *q != '"'; q++)
					;
				*q = '\0';
				atok = strdup(p);
			}
		}
	}
	ssl_disconnect(cp);
	if (atok == NULL)
		warnx("Couldn't get authenticity token");
	if (scookie == NULL)
		warnx("Couldn't get session cookie");

	rq = u = p = head = url = NULL;
	if ((u = urlencode(user)) == NULL || (p = urlencode(pass)) == NULL) {
		free(u); free(p); return (NULL);
	}
	if ((rq = strduprintf(tmpl, u, p, atok == NULL ? "" : atok)) == NULL) {
		free(u); free(p); return (NULL);
	}
	free(u); free(p);

	tries = 0;
	do {
		if (tries > 0)
			sleep(2);
		if ((cp = ssl_connect(host, port)) == NULL) {
			free(rq); return (NULL);
		}
		status = http_post(cp, "/users/sign_in", scookie, "*/*",
		    USER_AGENT, HTTP_POST_TYPE_FORM, rq);
		if (status >= 400) {
			warnx("Login failed. Server replied with code %d",
			    status);
			return (NULL);
		} else if (status == -1)
			return (NULL);
		for (cookie = NULL;
		    (p = ssl_readln(cp)) != NULL && cookie == NULL;) {
			if (strncmp(p, "Set-Cookie:", 11) == 0) {
				for (q = p; (q = strtok(q, " ;")) != NULL;
				    q = NULL) {
					if (strncmp(q,
					    "remember_user_token=", 20) != 0)
						continue;
					if ((cookie = strdup(q)) == NULL) {
						warn("strdup()");
						ssl_disconnect(cp);
						return (NULL);
					}
				}
			}
		}
		ssl_disconnect(cp);
	} while (cookie == NULL && ++tries < 10);

	free(rq); free(scookie); free(atok);

	if (cookie == NULL) {
		if (errno == 0)
			warnx("Login failed.");
		return (NULL);
	}
	return (cookie);
}

static int
close_session(session_t *sp)
{
	int	   status, ret;
	ssl_conn_t *cp;
	const char *tmpl = "_method=delete";

	if ((cp = ssl_connect(sp->host, sp->port)) == NULL)
		return (-1);
	status = http_post(cp, "/users/sign_out", sp->cookie, "*/*",
	    USER_AGENT, HTTP_POST_TYPE_FORM, tmpl);

	if (status == -1)
		ret = -1;
	else if (status == HTTP_FOUND || status < 300)
		ret = 0;
	else {
		warnx("Server replied with code %d", status);
		ret = -1;
	}
	ssl_disconnect(cp);

	return (ret);
}

static int
post(session_t *sp, const char *msg, const char *aspect)
{
	int	   aid, ret, status;
	char	   *rq, *p, idstr[24];
	ssl_conn_t *cp;
	const char tmpl[] = "{\"status_message\":{\"text\":\"%s\", "	  \
			     "\"provider_display_name\":\"cliaspora\"},"  \
			     "\"aspect_ids\":\"%s\"}\n\n";

	errno = 0;
	if (strcmp(aspect, "public") == 0)
		(void)strncpy(idstr, "public", sizeof(idstr));
	else if ((aid = get_aspect_id(sp, aspect)) == -1) {
		warnx("Unknown aspect '%s'", aspect); return (-1);
	} else
		(void)snprintf(idstr, sizeof(idstr), "%d", aid);
	if ((p = json_escape_str(msg)) == NULL)
		return (-1);
	if ((rq = strduprintf(tmpl, p, idstr)) == NULL) {
		free(p); return (-1);
	}
	free(p);
	if ((cp = ssl_connect(sp->host, sp->port)) == NULL)
		return (-1);
	status = http_post(cp, "/status_messages", sp->cookie, NULL,
	    USER_AGENT, HTTP_POST_TYPE_JSON, rq);
	free(rq);
	if (status == HTTP_UNAUTHORIZED) {
		warnx("You're not logged in. Please create a new session");
		ret = -1;
	} else if (status == HTTP_FOUND)
		ret = 0;
	else if (status == -1)
		ret = -1;
	else {
		warnx("Server replied with code %d", status);
		ret = -1;
	}
	ssl_disconnect(cp);

	return (ret);
}

static int
dpoll(session_t *sp, const char *aspect, const char *msg, const char *question,
	const char *ansl)
{
	int	   aid, ret, status;
	char	   *rq, *mp, *qp, idstr[24];
	ssl_conn_t *cp;
	const char tmpl[] = "{\"status_message\":{\"text\":\"%s\","	      \
			    "\"provider_display_name\":\"cliaspora\"},"	      \
			    "\"aspect_ids\":\"%s\",\"poll_question\":\"%s\"," \
			    "\"poll_answers\":[%s]}";

	errno = 0;
	if (strcmp(aspect, "public") == 0)
		(void)strncpy(idstr, "public", sizeof(idstr));
	else if ((aid = get_aspect_id(sp, aspect)) == -1) {
                warnx("Unknown aspect '%s'", aspect); return (-1);
	} else
		(void)snprintf(idstr, sizeof(idstr), "%d", aid);
	if (msg != NULL) {
		if ((mp = json_escape_str(msg)) == NULL)
			return (-1);
	} else
		mp = NULL;
	if ((qp = json_escape_str(question)) == NULL) {
		free(mp); return (-1);
	}
	rq = strduprintf(tmpl, msg != NULL ? mp : "", idstr, qp, ansl);
	free(mp); free(qp);
	if (rq == NULL)
		return (-1);
	if ((cp = ssl_connect(sp->host, sp->port)) == NULL) {
		free(rq); return (-1);
	}
	status = http_post(cp, "/status_messages", sp->cookie, NULL,
	    USER_AGENT, HTTP_POST_TYPE_JSON, rq);
	free(rq);
	if (status == HTTP_UNAUTHORIZED) {
		warnx("You're not logged in. Please create a new session");
		ret = -1;
	} else if (status == HTTP_FOUND)
		ret = 0;
	else if (status == -1)
		ret = -1;
	else {
		warnx("Server replied with code %d", status);
		ret = -1;
	}
	ssl_disconnect(cp);
	return (ret);
}

static int
upload(session_t *sp, const char *aspect, const char *msg, char * const *files)
{
	int	   aid, i, j, n, ret, status, id[24];
	char	   *rq, *p, idstr[24], lst[sizeof(id) * (20 + 3) / sizeof(int)];
	ssl_conn_t *cp;
	const char tmpl[] = "{\"status_message\":{\"text\":\"%s\","	\
			    "\"provider_display_name\":\"cliaspora\"}," \
			    "\"aspect_ids\":\"%s\",\"photos\":[%s]}";

	errno = 0;
	if (strcmp(aspect, "public") == 0)
		(void)strncpy(idstr, "public", sizeof(idstr));
	else if ((aid = get_aspect_id(sp, aspect)) == -1) {
                warnx("Unknown aspect '%s'", aspect); return (-1);
	} else
		(void)snprintf(idstr, sizeof(idstr), "%d", aid);
	for (n = 0; n < sizeof(id) / sizeof(int) && *files != NULL; files++) {
		if ((id[n] = upload_file(sp, *files)) == -1)
			warnx("Failed to upload image '%s'", *files);
		else
			n++;
	}
	if (n <= 0)
		return (-1);
	for (i = j = 0; i < n; i++) {
		(void)snprintf(lst + j, sizeof(lst) - j,
		    i < n - 1 ? "\"%d\"," : "\"%d\"", id[i]);
		j = strlen(lst);
	}
	if (msg != NULL) {
		if ((p = json_escape_str(msg)) == NULL)
			return (-1);
	} else
		p = NULL;
	rq = strduprintf(tmpl, msg != NULL ? p : "", idstr, lst);
	free(p);
	if (rq == NULL)
		return (-1);
	if ((cp = ssl_connect(sp->host, sp->port)) == NULL) {
		free(rq); return (-1);
	}
	status = http_post(cp, "/status_messages", sp->cookie, NULL,
	    USER_AGENT, HTTP_POST_TYPE_JSON, rq);
	free(rq);
	if (status == HTTP_UNAUTHORIZED) {
		warnx("You're not logged in. Please create a new session");
		ret = -1;
	} else if (status == HTTP_FOUND)
		ret = 0;
	else if (status == -1)
		ret = -1;
	else {
		warnx("Server replied with code %d", status);
		ret = -1;
	}
	ssl_disconnect(cp);
	return (ret);
}

static int
upload_file(session_t *sp, const char *file)
{
	int	    status, id;
	char	    *url, *p;
	ssl_conn_t  *cp;
	json_node_t *node, *jp1, *jp2, *jp3;

	errno = 0;
	if ((p = strrchr(file, '/')) != NULL)
		p++;
	else
		p = (char *)file;
	if ((p = urlencode(p)) == NULL)
		return (-1);
	if ((url = strduprintf("/photos?photo%%5Bpending%%5D=true&" \
	    "set_profile_image=&qqfile=%s", p)) == NULL) {
		free(p); return (-1);
	}
	free(p);
	if ((cp = ssl_connect(sp->host, sp->port)) == NULL) {
		free(url); return (-1);
	}
	status = http_upload(cp, url, sp->cookie, "application/json",
	    USER_AGENT, file);
	free(url);
	if (status == HTTP_UNAUTHORIZED) {
		warnx("You're not logged in. Please create a new session");
		ssl_disconnect(cp); return (-1);
	} else if (status == -1) {
		ssl_disconnect(cp); return (-1);
	} else if (status != HTTP_CREATED && status != HTTP_OK) {
		warnx("Server replied with code %d", status);
		ssl_disconnect(cp); return (-1);
	}	
	while ((p = ssl_readln(cp)) != NULL && *p != '{')
		;
	if (p == NULL) {
		warnx("Unexpected server reply"); ssl_disconnect(cp);
		return (-1);
	}
	if ((node = new_json_node()) == NULL) {
		ssl_disconnect(cp); return (-1);
	}
	if (parse_json(node, p) == NULL) {
		ssl_disconnect(cp); free_json_node(node);
		return (-1);
	}
	ssl_disconnect(cp);
	for (id = -1, jp1 = node->val; jp1 != NULL; jp1 = jp1->next) {
		if (strcmp(jp1->var, "data") == 0) {
			for (jp2 = jp1->val; jp2 != NULL; jp2 = jp2->next) {
				if (strcmp(jp2->var, "photo") == 0) {
					for (jp3 = jp2->val; jp3 != NULL;
					    jp3 = jp3->next) {
						if (strcmp(jp3->var, "id")
						    == 0) {
							id = *(int *)jp3->val;
							break;
						}
					}
					break;
				}
			}
			break;
		}
	}
	free_json_node(node);
	if (id == -1)
		warnx("Couldn't find the image ID");
	return (id);
}

static int
comment(session_t *sp, const char *msg, int id)
{
	int	   ret, status;
	char	   *rq, *url, *p;
	ssl_conn_t *cp;
	const char tmpl[] = "{\"text\":\"%s\"}";

	errno = 0;
	if ((url = strduprintf("/posts/%d/comments", id)) == NULL)
		return (-1);
	if ((p = json_escape_str(msg)) == NULL) {
		free(url); return (-1);
	}
	if ((rq = strduprintf(tmpl, p)) == NULL) {
		free(p); free(url); return (-1);
	}
	free(p);
	if ((cp = ssl_connect(sp->host, sp->port)) == NULL)
		return (-1);
	status = http_post(cp, url, sp->cookie, NULL,
	    USER_AGENT, HTTP_POST_TYPE_JSON, rq);
	free(rq); free(url);
	if (status == HTTP_UNAUTHORIZED) {
		warnx("You're not logged in. Please create a new session");
		ret = -1;
	} else if (status == -1)
		ret = -1;
	else if (status == HTTP_CREATED)
		ret = 0;
	else {
		warnx("Server replied with code %d", status);
		ret = -1;
	}
	ssl_disconnect(cp);

	return (ret);
}

static int
like(session_t *sp, int id)
{
	int	   status, ret;
	char	   *url;
	ssl_conn_t *cp;
	
	errno = 0;
	if ((url = strduprintf("/posts/%d/likes", id)) == NULL)
		return (-1);
	if ((cp = ssl_connect(sp->host, sp->port)) == NULL) {
		free(url); return (-1);
	}
	status = http_post(cp, url, sp->cookie, NULL,
	    USER_AGENT, HTTP_POST_TYPE_JSON, "[]");
	free(url);
	if (status == HTTP_UNAUTHORIZED) {
		warnx("You're not logged in. Please create a new session");
		ret = -1;
	} else if (status == -1)
		ret = -1;
	else if (status == HTTP_CREATED)
		ret = 0;
	else {
		warnx("Server replied with code %d", status);
		ret = -1;
	}
	ssl_disconnect(cp);

	return (ret);
}

static int
delete_post(session_t *sp, int id)
{
	int	   status, ret;
	char	   *url;
	ssl_conn_t *cp;
	
	errno = 0;
	if ((url = strduprintf("/posts/%d", id)) == NULL)
		return (-1);
	if ((cp = ssl_connect(sp->host, sp->port)) == NULL) {
		free(url); return (-1);
	}
	status = http_delete(cp, url, sp->cookie, USER_AGENT);
	free(url);
	if (status == HTTP_UNAUTHORIZED) {
		warnx("You're not logged in. Please create a new session");
		ret = -1;
	} else if (status == HTTP_FOUND)
		ret = 0;
	else if (status == -1)
		ret = -1;
	else {
		warnx("Server replied with code %d", status);
		ret = -1;
	}
	ssl_disconnect(cp);

	return (ret);
}

static int
message(session_t *sp, const char *subject, const char *msg, int id)
{
	int	   ret, status;
	char	   *rq, *m, *s;
	ssl_conn_t *cp;
	const char tmpl[] = "contact_ids=%d&conversation%%5bsubject" \
			    "%%5d=%s&conversation%%5btext%%5d=%s\n";

	errno = 0;
	if ((m = urlencode(msg)) == NULL)
		return (-1);
	if ((s = urlencode(subject)) == NULL) {
		free(m); return (-1);
	}
	if ((rq = strduprintf(tmpl, id, s, m)) == NULL) {
		free(m); free(s); return (-1);
	}
	free(m); free(s);
	if ((cp = ssl_connect(sp->host, sp->port)) == NULL)
		return (-1);
	status = http_post(cp, "/conversations", sp->cookie, NULL,
	    USER_AGENT, HTTP_POST_TYPE_FORM, rq);
	free(rq);
	if (status == HTTP_UNAUTHORIZED) {
		warnx("You're not logged in. Please create a new session");
		ret = -1;
	} else if (status == HTTP_FOUND)
		ret = 0;
	else if (status == -1)
		ret = -1;
	else {
		warnx("Server replied with code %d", status);
		ret = -1;
	}
	ssl_disconnect(cp);

	return (ret);
}

static int
reply(session_t *sp, const char *msg, int msg_id)
{
	int	   ret, status;
	char	   *rq, *url, *p;
	ssl_conn_t *cp;

	errno = 0;

	if ((p = urlencode(msg)) == NULL)
		return (-1);
	if ((url = strduprintf("/conversations/%d/messages",
	    msg_id)) == NULL) {
		free(p); return (-1);
	}
	if ((rq = strduprintf("message%%5btext%%5d=%s\n", p)) == NULL) {
		free(p); free(url); return (-1);
	}
	free(p);
	if ((cp = ssl_connect(sp->host, sp->port)) == NULL)
		return (-1);
	status = http_post(cp, url, sp->cookie, NULL, USER_AGENT,
	    HTTP_POST_TYPE_FORM, rq);
	free(url); free(rq);
	if (status == HTTP_UNAUTHORIZED) {
		warnx("You're not logged in. Please create a new session");
		ret = -1;
	} else if (status == HTTP_FOUND)
		ret = 0;
	else if (status == -1)
		ret = -1;
	else {
		warnx("Server replied with code %d", status);
		ret = -1;
	}
	ssl_disconnect(cp);
	return (ret);
}

static int
reshare(session_t *sp, int id)
{
	int	   status, ret;
	char	   *rq, *guid;
	ssl_conn_t *cp;
	const char tmpl[] = "{\"root_guid\":\"%s\"}";

	errno = 0;
	if ((guid = get_post_guid(sp, id)) == NULL)
		return (-1);
	if ((rq = strduprintf(tmpl, guid)) == NULL)
		return (-1);
	if ((cp = ssl_connect(sp->host, sp->port)) == NULL) {
		free(rq); return (-1);
	}
	status = http_post(cp, "/reshares", sp->cookie, NULL,
	    USER_AGENT, HTTP_POST_TYPE_JSON, rq);
	free(rq);
	if (status == HTTP_UNAUTHORIZED) {
		warnx("You're not logged in. Please create a new session");
		ret = -1;
	} else if (status == HTTP_CREATED)
		ret = 0;
	else if (status == -1)
		ret = -1;
	else {
		warnx("Server replied with code %d", status);
		ret = -1;
	}
	ssl_disconnect(cp);

	return (ret);
}

static int
add_aspect(session_t *sp, const char *name, bool visible)
{
	int	   status, ret;
	char	   *rq;
	ssl_conn_t *cp;
	const char tmpl[] = "aspect%%5bname%%5d=%s&aspect%%5b" \
			    "contacts_visible%%5d=%d";

	errno = 0;
	if ((rq = strduprintf(tmpl, name, visible ? 1 : 0)) == NULL)
		return (-1);
	if ((cp = ssl_connect(sp->host, sp->port)) == NULL) {
		free(rq); return (-1);
	}
	status = http_post(cp, "/aspects", sp->cookie, NULL,
	    USER_AGENT, HTTP_POST_TYPE_FORM, rq);
	free(rq);
	if (status == HTTP_UNAUTHORIZED) {
		warnx("You're not logged in. Please create a new session");
		ret = -1;
	} else if (status == HTTP_FOUND)
		ret = 0;
	else if (status == -1)
		ret = -1;
	else {
		warnx("Server replied with code %d", status);
		ret = -1;
	}
	ssl_disconnect(cp);

	return (ret);
}

static int
follow_tag(session_t *sp, const char *tag)
{
	int	   status, ret;
	char	   *rq, *p;
	ssl_conn_t *cp;
	
	errno = 0;

	while (*tag == '\0' && *tag != '#')
		tag++;
	if ((p = urlencode(tag)) == NULL)
		return (-1);
	if ((rq = strduprintf("name=%s", p)) == NULL) {
		free(p); return (-1);
	}
	free(p);
	if ((cp = ssl_connect(sp->host, sp->port)) == NULL) {
		free(rq); return (-1);
	}
	status = http_post(cp, "/tag_followings", sp->cookie, NULL,
	    USER_AGENT, HTTP_POST_TYPE_FORM, rq);
	free(rq);
	if (status == HTTP_UNAUTHORIZED) {
		warnx("You're not logged in. Please create a new session");
		ret = -1;
	} else if (status == HTTP_CREATED)
		ret = 0;
	else if (status == -1)
		ret = -1;
	else {
		warnx("Server replied with code %d", status);
		ret = -1;
	}
	ssl_disconnect(cp);

	return (ret);
}

static int
add_contact(session_t *sp, int aspect, int id)
{
	int	   status, ret;
	char	   *rq;
	ssl_conn_t *cp;
	const char tmpl[] = "aspect_id=%d&person_id=%d&_method=POST";

	errno = 0;
	if ((rq = strduprintf(tmpl, aspect, id)) == NULL)
		return (-1);
	if ((cp = ssl_connect(sp->host, sp->port)) == NULL) {
		free(rq);
		return (-1);
	}
	status = http_post(cp, "/aspect_memberships.json", sp->cookie, NULL,
	    USER_AGENT, HTTP_POST_TYPE_FORM, rq);
	free(rq);
	if (status == HTTP_UNAUTHORIZED) {
		warnx("You're not logged in. Please create a new session");
		ret = -1;
	} else if (status == HTTP_OK)
		ret = 0;
	else if (status == -1)
		ret = -1;
	else {
		warnx("Server replied with code %d", status);
		ret = -1;
	}
	ssl_disconnect(cp);

	return (ret);
}

static void
free_session(session_t *sp)
{
	free(sp->cookie);
	free(sp->host);
	free(sp);
}

static session_t *
new_session(const char *host, u_short port, const char *user, const char *pass)
{
	char	  account[256];
	session_t *sp;

	if ((sp = malloc(sizeof(session_t))) == NULL) {
		warn("malloc()"); return (NULL);
	}
	sp->cookie	 = sp->host = NULL;
	sp->port	 = port;
	sp->attr.name	 = sp->attr.did = NULL;
	sp->attr.aspects = NULL;

	if ((sp->host = strdup(host)) == NULL) {
		free_session(sp); return (NULL);
	}
	sp->cookie = diaspora_login(host, port, user, pass);
	if (sp->cookie == NULL)
		return (NULL);
	if (get_attributs(sp) == -1) {
		free_session(sp); return (NULL);
	}
	if ((cfg.cookie = strdup(sp->cookie)) == NULL) {
		warn("strdup()"); return (NULL);
	}
	if ((cfg.host = strdup(sp->host)) == NULL) {
		warn("strdup()"); return (NULL);
	}
	if ((cfg.user = strdup(user)) == NULL) {
		warn("strdup()"); return (NULL);
	}
	cfg.port = port;
	(void)snprintf(account, sizeof(account), "%s@%s", user, host);
	write_config(account);

	return (sp);
}

static session_t *
create_session()
{
	session_t *sp;

	errno = 0;
	if (cfg.host == NULL || cfg.host[0] == '\0') {
		warnx("Host not defined. Corrupted config file?");
		return (NULL);
	}
	if (cfg.cookie == NULL || cfg.cookie[0] == '\0') {
		warnx("Cookie not defined. Corrupted config file?");
		return (NULL);
	}
	if ((sp = malloc(sizeof(session_t))) == NULL)
		return (NULL);

	sp->host	 = cfg.host;
	sp->port	 = cfg.port;
	sp->cookie	 = cfg.cookie; 
	sp->attr.name	 = sp->attr.did = NULL;
	sp->attr.aspects = NULL;

	if (get_attributs(sp) == -1) {
		free_session(sp); return (NULL);
	}
	return (sp);
}

static int
get_attributs(session_t *sp)
{
	int	    status, n;
	char	    *p, *q;
	ssl_conn_t  *cp;	
	json_node_t *jnode, *jp;

	errno = 0;
	if ((cp = ssl_connect(sp->host, sp->port)) == NULL)
		return (-1);
	status = http_get(cp, "/stream", sp->cookie, "*/*", USER_AGENT);
	if (status == HTTP_UNAUTHORIZED) {
		warnx("You're not logged in. Please create a new session");
		ssl_disconnect(cp); return (-1);
	} else if (status == -1) {
		ssl_disconnect(cp); return (-1);
	}
	while ((p = ssl_readln(cp)) != NULL) {
		if ((q = strstr(p, "gon.user")) != NULL ||
		    (q = strstr(p, "window.current_user_attributes")) != NULL)
			break;
	}
	if (q == NULL) {
		warnx("Unexpected server reply"); ssl_disconnect(cp);
		return (-1);
	}
	p = q;
	if ((q = strchr(p, '=')) != NULL) {
		while (*q != '\0' && *q != '{')
			q++;
	}
	n = 0; p = q;
	do {
		if (*p == '{')
			n++;
		else if (*p == '}')
			n--;
		p++;
	} while (n > 0);
	*p = '\0';

	if ((jnode = new_json_node()) == NULL)
		return (-1);
	if (parse_json(jnode, q) == NULL) {
		free_json_node(jnode); return (-1);
	}
	ssl_disconnect(cp);

	free(sp->attr.name);
	free(sp->attr.did);
	free_aspects(sp->attr.aspects);

	sp->attr.aspects = get_aspects(jnode);

	if ((jp = find_json_node(jnode, "id")) != NULL)
		sp->attr.id = *(int *)jp->val;
	if ((jp = find_json_node(jnode, "guid")) != NULL)
		sp->attr.guid = *(int *)jp->val;
	if ((jp = find_json_node(jnode, "notifications_count")) != NULL)
		sp->attr.nc = *(int *)jp->val;
	if ((jp = find_json_node(jnode, "unread_messages_count")) != NULL)
		sp->attr.mc = *(int *)jp->val;
	if ((jp = find_json_node(jnode, "following_count")) != NULL)
		sp->attr.fc = *(int *)jp->val;
	if ((jp = find_json_node(jnode, "avatar")) != NULL) {
		if ((jp = find_json_node(jp, "medium")) != NULL) {
			sp->attr.avatar = strdup((char *)jp->val);
			if (sp->attr.avatar == NULL)
				return (-1);
		}
	}
	if ((jp = find_json_node(jnode, "name")) != NULL) {
		if ((sp->attr.name = strdup((char *)jp->val)) == NULL)
			return (-1);
	}
	if ((jp = find_json_node(jnode, "diaspora_id")) != NULL) {
		if ((sp->attr.did = strdup((char *)jp->val)) == NULL)
			return (-1);
	}
	free_json_node(jnode);

	return (0);
}

static void
free_aspects(aspect_t *aspect)
{
	aspect_t *next;

	while (aspect != NULL) {
		next = aspect->next;
		free(aspect->name); free(aspect);
		aspect = next;
	}
}

static aspect_t *
get_aspects(json_node_t *node)
{
	int	    saved_errno;
	aspect_t    *aspects, *ap;
	json_node_t *jp;

	errno = 0;
	aspects = ap = NULL;
	if ((node = find_json_node(node, "aspects")) == NULL)
		return (NULL);
	for (node = node->val; node != NULL; node = node->next) {
		if (aspects == NULL) {
			if ((aspects = malloc(sizeof(aspect_t))) == NULL)
				return (NULL);
			aspects->name = NULL; aspects->id = -1;
			aspects->next = NULL;
			ap = aspects;
		} else if (ap->name != NULL && ap->id != -1) {
			if ((ap->next = malloc(sizeof(aspect_t))) == NULL)
				return (NULL);
			ap = ap->next;
			ap->name = NULL; ap->id = -1;
			ap->next = NULL;
		}
		for (jp = node->val; jp != NULL; jp = jp->next) {
			if (jp->type == JSON_TYPE_STRING && jp->var != NULL &&
			    strcmp(jp->var, "name") == 0) {
				ap->name = strdup((char *)jp->val);
				if (ap->name == NULL) {
					saved_errno = errno;
					free_aspects(aspects);
					errno = saved_errno;
					return (NULL);
				}
			} else if (jp->type == JSON_TYPE_NUMBER &&
			    jp->var != NULL && strcmp(jp->var, "id") == 0) {
				ap->id = *(int *)jp->val;
			}
		}
	}
	if (aspects->name == NULL || aspects->id == -1) {
		free(aspects); return (NULL);
	}
	return (aspects);
}

static void
show_aspects(session_t *sp)
{
	wchar_t  w[50];
	aspect_t *ap;

	(void)wprintf(L"NAME                 ASPECT ID\n");
	for (ap = sp->attr.aspects; ap != NULL; ap = ap->next) {
		(void)mbstowcs(w, ap->name, 50); w[20] = L'\0';
		(void)wprintf(L"%-20S ", w);
		(void)wprintf(L"%d\n", ap->id);
	}
}

static int
get_aspect_id(session_t *sp, const char *name)
{
	aspect_t *ap;

	for (ap = sp->attr.aspects; ap != NULL; ap = ap->next) {
		if (strcmp(ap->name, name) == 0)
			return (ap->id);
	}
	return (-1);
}

static int
get_contact_id(contact_t *contacts, const char *handle)
{
	for (; contacts != NULL; contacts = contacts->next) {
		if (strcmp(contacts->handle, handle) == 0)
			return (contacts->id);
	}
	return (-1);
}


static msg_idx_t *
new_msg_idx_node(msg_idx_t *node)
{
	
	if (node == NULL) {
		if ((node = malloc(sizeof(msg_idx_t))) == NULL)
			return (NULL);
	} else {
		for (; node->next != NULL; node = node->next)
			;
		if ((node->next = malloc(sizeof(msg_idx_t))) == NULL)
			return (NULL);
		node = node->next;
	}
	node->aid  = node->mid  = -1;
	node->next = NULL;
	node->date = node->subject = NULL;

	return (node);
}

static void
free_msg_idx(msg_idx_t *idx)
{
	msg_idx_t *next;

	for (; idx != NULL; idx = next) {
		free(idx->subject); free(idx->date);
		next = idx->next;
	}
}

static msg_idx_t *
get_msg_index(session_t *sp)
{
	int	    page, status;
	bool	    complete, error;
	msg_idx_t   *index, *ip;
	const char  tmpl[] = "/conversations?page=%d";
	char	    url[sizeof(tmpl) + 16], *p;
	ssl_conn_t  *cp;
	json_node_t *node, *jp1, *jp2, *jp3, *jp4;
	errno = 0;

	jp1 = node = new_json_node();
	if (node == NULL)
		return (NULL);
	for (complete = error = false, page = 1; !error && !complete; page++) {
		(void)snprintf(url, sizeof(url), tmpl, page);
		if ((cp = ssl_connect(sp->host, sp->port)) == NULL)
			return (NULL);
		status = http_get(cp, url, sp->cookie,
		    "application/json, */*", USER_AGENT);
		if (status == HTTP_UNAUTHORIZED) {
			warnx("You're not logged in. Please create a " \
			    "new session");
			error = true;
		} else if (status != HTTP_OK && status != HTTP_FOUND) {
			error = true;
			warnx("Server replied with code %d", status);
		} else if (status == -1)
			error = true;
		while (!error && !complete && (p = ssl_readln(cp)) != NULL) {
			if (strncmp(p, "[]", 2) == 0)
				complete = true;
			else if (*p == '[') {
				if (parse_json(jp1, p) == NULL)
					error = true;
				break;
			}
		}
		if (!error && !complete) {
			if ((jp1 = json_add_node(jp1)) == NULL)
				error = true;
		}
		ssl_disconnect(cp);
	}
	if (error) {
		free_json_node(node);
		return (NULL);
	}
	for (index = NULL, jp1 = node; jp1 != NULL; jp1 = jp1->next) {
		if (jp1->type != JSON_TYPE_ARRAY)
			continue;
		for (jp2 = jp1->val; jp2 != NULL; jp2 = jp2->next) {
			jp3 = find_json_node(jp2, "conversation");
			if (jp3 != NULL) {
				if (index == NULL) {
					ip = index = new_msg_idx_node(NULL);
					if (ip == NULL)
						goto error;
				} else {
					ip = new_msg_idx_node(index);
					if (ip == NULL)
						goto error;
				}
				jp4 = find_json_node(jp3->val, "subject");
				if (jp4 != NULL) {
					ip->subject = strdup((char *)jp4->val);
					if (ip->subject == NULL)
						goto error;
				}
				jp4 = find_json_node(jp3->val, "author_id");
				if (jp4 != NULL) {
					ip->aid = *(int *)jp4->val;
				}
				jp4 = find_json_node(jp3->val, "id");
				if (jp4 != NULL) {
					ip->mid = *(int *)jp4->val;
				}
				jp4 = find_json_node(jp3->val, "created_at");
				if (jp4 != NULL) {
					ip->date = strdup((char *)jp4->val);
					if (ip->date == NULL)
						goto error;
				}
			}
		}
	}
  	free_json_node(node);
	return ((sp->midx = index));
error:
	free_json_node(node);
	free_msg_idx(index);

	return (NULL);
}

static void
show_msg_index(session_t *sp)
{
	char	  *name;
	wchar_t   w[50];
	contact_t *ctp;
	msg_idx_t *idx;

	(void)wprintf(L"DATE                 MSG ID  " \
	    "FROM ID FROM            SUBJECT\n");
	for (idx = sp->midx; idx != NULL; idx = idx->next) {
		name = NULL;
		if (sp->contacts != NULL &&
		    (ctp = find_contact_by_id(sp->contacts,
			idx->aid)) != NULL) {
			name = ctp->name;
		} else if (sp->attr.id == idx->aid)
			name = sp->attr.name;
		(void)wprintf(L"%-20s ", idx->date);
		(void)wprintf(L"%-7d %-7d ", idx->mid, idx->aid);
		(void)mbstowcs(w, name != NULL ? name : "?", 50);
		w[15] = L'\0';
		(void)wprintf(L"%-15S ", w);
		(void)mbstowcs(w, idx->subject, 50); w[27] = L'\0';
		(void)wprintf(L"%-27S\n", w);
	}
}


static contact_t *
new_contact_node(contact_t *ctp)
{
	if (ctp == NULL) {
		if ((ctp = malloc(sizeof(contact_t))) == NULL)
			return (NULL);
	} else {
		for (; ctp->next != NULL; ctp = ctp->next)
			;
		if ((ctp->next = malloc(sizeof(contact_t))) == NULL)
			return (NULL);
		ctp = ctp->next;
	}
	ctp->id   = -1;
	ctp->next = NULL;
	ctp->name = ctp->handle = NULL;
	ctp->url  = ctp->avatar = NULL;

	return (ctp);
}

static contact_t *
get_contacts(session_t *sp)
{
	int	    status;
	char	    *p;
	contact_t   *contacts, *ctp;
	ssl_conn_t  *cp;
	json_node_t *node, *jp1, *jp2;

	errno = 0;
	if ((cp = ssl_connect(sp->host, sp->port)) == NULL)
		return (NULL);
	status = http_get(cp, "/contacts", sp->cookie,
	    "application/json, */*", USER_AGENT);
	if (status == HTTP_UNAUTHORIZED) {
		warnx("You're not logged in. Please create a new session");
		ssl_disconnect(cp);
		return (NULL);
	} else if (status == -1) {
		ssl_disconnect(cp);
		return (NULL);
	} else if (status != HTTP_OK && status != HTTP_FOUND) {
		warnx("Server replied with code %d", status);
		ssl_disconnect(cp); return (NULL);
	}
	jp1 = node = new_json_node();
	if (node == NULL)
		return (NULL);
	while ((p = ssl_readln(cp)) != NULL && *p != '[')
		;
	if (*p != '[') {
		if (errno == 0)
			warnx("Server reply not understood");
		ssl_disconnect(cp);
		return (NULL);
	}
	if (parse_json(node, p) == NULL) {
		ssl_disconnect(cp); free_json_node(node);
		return (NULL);
	}
	ssl_disconnect(cp);
	
	for (contacts = NULL, jp1 = node->val; jp1 != NULL; jp1 = jp1->next) {
		if (contacts == NULL) {
			if ((contacts = new_contact_node(NULL)) == NULL)
				goto error;
			ctp = contacts;
		} else if ((ctp = new_contact_node(contacts)) == NULL)
			goto error;
		if ((jp2 = find_json_node(jp1, "id")) != NULL)
			ctp->id = *(int *)jp2->val;
		else
			ctp->id = -1;
		if ((jp2 = find_json_node(jp1, "name")) != NULL) {
			if ((ctp->name = strdup((char *)jp2->val)) == NULL)
				goto error;
		} else if ((ctp->name = strdup("")) == NULL)
			goto error;
		if ((jp2 = find_json_node(jp1, "avatar")) != NULL) {
			if ((ctp->avatar = strdup((char *)jp2->val)) == NULL)
				goto error;
		} else if ((ctp->avatar = strdup("")) == NULL)
			goto error;
		if ((jp2 = find_json_node(jp1, "handle")) != NULL) {
			if ((ctp->handle = strdup((char *)jp2->val)) == NULL)
				goto error;
		} else if ((ctp->handle = strdup("")) == NULL)
			goto error;
		if ((jp2 = find_json_node(jp1, "url")) != NULL) {
			if ((ctp->url = strdup((char *)jp2->val)) == NULL)
				goto error;
		} else if ((ctp->url = strdup("")) == NULL)
			goto error;
	}
  	free_json_node(node);
	return ((sp->contacts = contacts));
error:
	free_json_node(node);
	free_contacts(contacts);

	return (NULL);
}	


static void
free_contacts(contact_t *ctp)
{

	contact_t *next;

	for (; ctp != NULL; ctp = next) {
		free(ctp->name); free(ctp->handle); 
		free(ctp->url);  free(ctp->avatar);
		next = ctp->next; free(ctp);
	}
}

static contact_t *
find_contact_by_id(contact_t *ctp, int id)
{
	for (; ctp != NULL && ctp->id != id; ctp = ctp->next)
		;
	return (ctp);
}

static void
show_contacts(contact_t *contacts)
{
	wchar_t	  w[50];
	contact_t *ctp;

	(void)wprintf(L"ID       NAME                     HANDLE\n");
	for (ctp = contacts; ctp != NULL; ctp = ctp->next) {
		(void)wprintf(L"%-8d ", ctp->id);
		(void)mbstowcs(w, ctp->name, 50); w[24] = L'\0';
		(void)wprintf(L"%-24S ", w);
		(void)mbstowcs(w, ctp->handle, 50); w[40] = L'\0';
		(void)wprintf(L"%-40S\n", w);
	}
}

static void
groff_printf(const char *fmt, ...)
{
	char	*str;
	va_list ap;

	va_start(ap, fmt);
	while (*fmt != '\0') {
		if (*fmt == '%') {
			if (*++fmt == '\0') {
				va_end(ap); return;
			}
		} else {
			putchar(*fmt++);
			continue;
		}
		switch(*fmt++) {
		case '%':
			putchar('%');
			break;	
		case 'c':
			(void)printf("%c", va_arg(ap, int));
			break;
		case 'd':
			(void)printf("%d", va_arg(ap, int));
			break;
		case 's':
			for (str = va_arg(ap, char *); *str != '\0'; str++) {
				if (*str == '.')
					(void)fputs("\\\[char46]", stdout);
				else if (*str == '\'')
					(void)fputs("\\\'", stdout);
				else if (*str == '\\')
					(void)fputs("\\\\", stdout);
				else 
					(void)putchar(*str);
			}
			break;
		}
	}
	va_end(ap);
}

static void
show_comment(json_node_t *cnode)
{
	comment_t   commentb;
	json_node_t *jp, *jp2;

	(void)memset(&commentb, 0, sizeof(commentb));
	for (jp = cnode; jp != NULL; jp = jp->next) {
		if (strcmp(jp->var, "created_at") == 0)
			commentb.date = (char *)jp->val;
		else if (strcmp(jp->var, "text") == 0)
			commentb.text = (char *)jp->val;
		else if (strcmp(jp->var, "author") == 0) {
			for (jp2 = jp->val; jp2 != NULL; jp2 = jp2->next) {
				if (strcmp(jp2->var, "name") == 0)
					commentb.author = (char *)jp2->val;
				else if (strcmp(jp2->var, "diaspora_id") == 0)
					commentb.handle = (char *)jp2->val;
			}
		}
	}	
	(void)puts("\n.in 4\n");
	groff_printf("\\fB%s <%s> on %s\\fP\n.br\n", commentb.author,
	    commentb.handle, commentb.date);
	groff_printf("%s\n.in\n", commentb.text);
}

static void
show_post(json_node_t *pnode)
{
	post_t	    postb;
	json_node_t *jp, *jp2, *jp3, *comments;

	(void)memset(&postb, 0, sizeof(postb));
	comments = NULL;
	for (jp = pnode; jp != NULL; jp = jp->next) {
		if (strcmp(jp->var, "id") == 0)
			postb.id = *(int *)jp->val;
		else if (strcmp(jp->var, "created_at") == 0)
			postb.date = (char *)jp->val;
		else if (strcmp(jp->var, "text") == 0)
			postb.text = (char *)jp->val;
		else if (strcmp(jp->var, "root") == 0 &&
		    jp->type == JSON_TYPE_OBJECT) {
			for (jp2 = jp->val; jp2 != NULL; jp2 = jp2->next) {
				if (strcmp(jp2->var, "created_at") == 0)
					postb.root_date = (char *)jp2->val;
				else if (strcmp(jp2->var, "id") == 0)
					postb.root_id = *(int *)jp2->val;
				else if (strcmp(jp2->var, "author") == 0) {
					for (jp3 = jp2->val; jp3 != NULL;
					    jp3 = jp3->next) {
						if (strcmp(jp3->var, "name")
						    == 0) {
							postb.root_author =
							    (char *)jp3->val;
						} else if (strcmp(jp3->var,
						    "diaspora_id") == 0) {
							postb.root_handle =
							    (char *)jp3->val;
						}
					}
				}
			}
		} else if (strcmp(jp->var, "post_type") == 0) {
			if (strcmp((char *)jp->val, "Reshare") == 0)
				postb.type = POST_TYPE_RESHARE;
			else
				postb.type = POST_TYPE_STATUS;
		} else if (strcmp(jp->var, "author") == 0) {
			for (jp2 = jp->val; jp2 != NULL; jp2 = jp2->next) {
				if (strcmp(jp2->var, "name") == 0)
					postb.author = (char *)jp2->val;
				else if (strcmp(jp2->var, "diaspora_id") == 0)
					postb.handle = (char *)jp2->val;
			}
		} else if (strcmp(jp->var, "interactions") == 0) {
			for (jp2 = jp->val; jp2 != NULL; jp2 = jp2->next) {
				if (strcmp(jp2->var, "likes_count") == 0)
					postb.likes = *(int *)jp2->val;
				else if (strcmp(jp2->var, "reshares_count")
				    == 0)
					postb.reshares = *(int *)jp2->val;
				else if (strcmp(jp2->var, "comments_count")
				    == 0)
					postb.comments = *(int *)jp2->val;
				else if (strcmp(jp2->var, "comments") == 0)
					comments = jp2->val;
			}
		}
	}	
	(void)puts(".PGNH");
	groff_printf("\\fB%s <%s> on %s POST-ID: %d\\fP\n.br\n",
	    postb.author, postb.handle, postb.date, postb.id);

	if (postb.type == POST_TYPE_RESHARE) {
		groff_printf(".in 2\n\\fB%s <%s> on %s POST-ID: %d" \
		    "\\fP\n.br\n", postb.root_author, postb.root_handle,
		    postb.root_date, postb.root_id);
	}
	groff_printf("%s\n", postb.text);
	if (postb.type == POST_TYPE_RESHARE)
		(void)puts(".in");
	(void)printf(".rj 1\n\\fBCOMMENTS: %d LIKES: %d RESHARES: %d\\fP",
	    postb.comments, postb.likes, postb.reshares);
	if (comments == NULL || postb.comments == 0) {
		(void)puts("\n\n\n");
	} else {
		for (jp = comments; jp != NULL; jp = jp->next)
			show_comment(jp->val);
		(void)puts("\n");
	}
}

static int
read_stream(session_t *sp, const char *url)
{
	int	   status;
	char	   *p;
	ssl_conn_t *cp;
	json_node_t *node, *pstp;

	if ((cp = ssl_connect(sp->host, sp->port)) == NULL)
		return (-1);
	status = http_get(cp, url, sp->cookie,
	    "application/json, */*", USER_AGENT);
	if (status == HTTP_UNAUTHORIZED) {
		warnx("You're not logged in. Please create a new session");
		ssl_disconnect(cp); return (-1);
	} else if (status == -1) {
		ssl_disconnect(cp); return (-1);
	} else if (status != HTTP_OK) {
		warnx("Server replied with code %d", status);
		ssl_disconnect(cp); return (-1);
	}
	if ((node = new_json_node()) == NULL) {
		warnx("new_json_node()");
		ssl_disconnect(cp); return (-1);
	}
	while ((p = ssl_readln(cp)) != NULL && *p != '[')
		;
	if (p == NULL) {
		warnx("Unexpected server reply");
		ssl_disconnect(cp); return (-1);
	}
	if (parse_json(node, p) == NULL) {
		warnx("parse_json() failed");
		ssl_disconnect(cp); return (-1);
	}
	ssl_disconnect(cp);
	for (pstp = node->val; pstp != NULL; pstp = pstp->next)
		show_post(pstp->val);
	free_json_node(node);
	return (0);
}

