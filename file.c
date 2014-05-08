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
#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <unistd.h>
#include <pwd.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "types.h"
#include "file.h"
#include "config.h"

#define TMP_TEMPLATE "/tmp/tmp.XXXXXXXX"

static int  write_postponed(const char *);
static bool have_postponed(void);
static char *edit_file(char *);
static char *read_file(FILE *);
static char *get_postponed_path(void);

static char tmp_path[] = TMP_TEMPLATE;

void
delete_postponed()
{
	char *pp_path;

	if (have_postponed()) {
		if ((pp_path = get_postponed_path()) != NULL) {
			if (remove(pp_path) == -1)
				warn("remove(%s)", pp_path);
		}
	}
}

void
delete_tmpfile()
{
	if (strcmp(tmp_path, TMP_TEMPLATE) != 0)
		(void)remove(tmp_path);
}

char *
get_input(bool fromstdin)
{
	int  c;
	FILE *fp;
	char *path, *buf;

	if (fromstdin) {
		if ((buf = read_file(stdin)) == NULL)
			errx(EXIT_FAILURE, "Failed to read file");
		return (buf);
	}
	if (have_postponed()) {
		do {
			(void)fputs("\nEdit postponed file? (y/n) ", stderr);
			c = tolower(fgetc(stdin));
		} while (c != 'y' && c != 'n');
		if (c == 'y')
			path = get_postponed_path();
		else
			path = NULL;
	} else
		path = NULL;
	do {
		path = edit_file(path);
		if (path == NULL)
			errx(EXIT_FAILURE, "Failed to edit file");
		else
			write_postponed(path);
		do {
			(void)fputs("\nReady to send? (y/n) ", stderr);
			c = tolower(fgetc(stdin));
		} while (c != 'y' && c != 'n');
	} while (c != 'y');
	if ((fp = fopen(path, "r")) == NULL)
		err(EXIT_FAILURE, "fopen(%s)", path);
	if ((buf = read_file(fp)) == NULL)
		errx(EXIT_FAILURE, "Failed to read file");
	delete_tmpfile();
	return (buf);
}

static char *
get_postponed_path()
{
	int	      len;
	static char   *pp_path = NULL;
	struct passwd *pw;

	if (pp_path != NULL)
		return (pp_path);
	if ((pw = getpwuid(getuid())) == NULL) {
		warnx("Couldn't find you in the password file");
		return (NULL);
	}
	endpwent();
	len = strlen(pw->pw_dir) + strlen(PATH_POSTPONED) + 2;

	if ((pp_path = malloc(len)) == NULL) {
		warn("malloc()"); return (NULL);
	}
	(void)snprintf(pp_path, len, "%s/%s", pw->pw_dir, PATH_POSTPONED);

	return (pp_path);
}

static bool
have_postponed()
{
	char	    *pp_path;
	struct stat sb;

	if ((pp_path = get_postponed_path()) == NULL)
		return (false);
	if (stat(pp_path, &sb) == -1) {
		if (errno != ENOENT)
			warn("stat(%s)", pp_path);
		return (false);
	}
	return (true);
}

static char *
edit_file(char *path)
{
	int  fd;
	char *editor, *cmd;

	if (path == NULL) {
		if ((fd = mkstemp(tmp_path)) == -1) {
			warn("mkstemp()"); return (NULL);
		}
		(void)close(fd); path = tmp_path;
	}
	if (cfg.editor == NULL || cfg.editor[0] == '\0') {
		if ((editor = getenv("EDITOR")) == NULL) {
			warnx("Environment variable EDITOR not defined. " \
			    "Using %s", DEFAULT_EDITOR);
			editor = DEFAULT_EDITOR;
		}
	} else
		editor = cfg.editor;
	if ((cmd = malloc(strlen(editor) + strlen(path) + 4)) == NULL) {
		warn("malloc()"); return (NULL);
	}
	(void)sprintf(cmd, "%s %s", editor, path);
	switch (system(cmd)) {
	case  -1:
		warn("system(%s)", cmd); free(cmd);
		return (NULL);
	case 127:
		warnx("Failed to execute command '%s'", cmd);
		free(cmd);
		return (NULL);
	}
	free(cmd);

	return (path);
}

static char *
read_file(FILE *fp)
{
	char   *buf, *p;
	size_t bufsz, blocks, rd;

	bufsz = blocks = 0;
	if ((buf = malloc(BUFSIZ)) == NULL) {
		warn("malloc()"); return (NULL);
	}
	blocks++; bufsz += BUFSIZ; p = buf;
	while ((rd = fread(p, 1, BUFSIZ, fp)) == BUFSIZ) {
		blocks++; bufsz += BUFSIZ;
		if ((p = realloc(buf, bufsz)) == NULL) {
			warn("realloc()"); free(buf); return (NULL);
		}
		buf = p; p = buf + (blocks - 1) * BUFSIZ;
	} 
	if (!feof(fp)) {
		warn("fread()"); free(buf); return (NULL);
	}
	buf[(blocks - 1) * BUFSIZ + rd] = '\0';
	return (buf);
}

static int
write_postponed(const char *path)
{
	int	    rd;
	FILE	    *in, *out;
	char	    *pp_path, buf[BUFSIZ];
	struct stat sb_in, sb_out;

	out = in = NULL;
	if ((pp_path = get_postponed_path()) == NULL)
		return (-1);
	/* Check whether infile == outfile. */
	if (stat(path, &sb_in) == -1) {
		warn("stat(%s)", path);
		goto error;
	}
	if (stat(pp_path, &sb_out) == -1 && errno != ENOENT) {
		warn("stat(%s)", pp_path);
		goto error;
	}
	if (sb_in.st_ino == sb_out.st_ino)
		/* Nothing to do here. */
		return (0);
	if ((in = fopen(path, "r")) == NULL) {
		warn("fopen(%s)", path);
		goto error;
	}
	if ((out = fopen(pp_path, "w+")) == NULL) {
		warn("Couldn't create '%s'", pp_path); 
		goto error;
	}
	while ((rd = fread(buf, 1, sizeof(buf), in)) > 0)
		(void)fwrite(buf, 1, rd, out);
	if (ferror(in)) {
		warn("fread()");
		goto error;
	} else if (ferror(out)) {
		warn("fwrite()");
		goto error;
	}
	(void)fclose(out); (void)fclose(in);

	return (0);
error:
	if (in != NULL)
		(void)fclose(in);
	if (out != NULL)
		(void)fclose(out);
	return (-1);
}

