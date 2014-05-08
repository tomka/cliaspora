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
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <err.h>
#include <unistd.h>
#include <termios.h>

static struct termios tt_old;

int
rawtty()
{
	struct termios tt;

	if (tcgetattr(fileno(stdin), &tt) == -1) {
		warn("tcgetattr(stdin)"); return (-1);
	}
	tt_old = tt;
	tt.c_lflag &= ~(ICANON | ECHO);
	tt.c_cc[VTIME] = 0;
	tt.c_cc[VMIN] = 1;

	if (tcsetattr(STDIN_FILENO, TCSANOW, &tt) == -1) {
		warn("tcsetattr(stdin)"); return (-1);
	}
	return (0);
}

int
unrawtty()
{
	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &tt_old) == -1) {
		warn("tcsetattr()"); return (-1);
	}
	return (0);
}

char *
readpass()
{
	int	    n = 0;
	static char c, buf[128];

	if (!isatty(fileno(stdin))) {
		(void)fgets(buf, sizeof(buf), stdin);
		(void)strtok(buf, "\n\r");
		return (buf);
	}
	if (rawtty() == -1)
		return (NULL);
	(void)fputs("Password: ", stderr);
	errno = 0;
	for (n = 0; n < sizeof(buf) && read(fileno(stdin), &c, 1) > 0;) {
		if (c == '\b' || c == tt_old.c_cc[VERASE]) {
			if (n > 0)
				(void)fputs("\b \b", stderr), n--;
		} else if (c == '\n' || c == '\r')
			break;
		else
			(void)fputc('*', stderr), buf[n++] = c;
	}
	buf[n++] = '\0';
	(void)unrawtty();
	(void)fputc('\n', stderr);

	return (buf);
}

