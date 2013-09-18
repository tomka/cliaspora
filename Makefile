PROGRAM  = cliaspora
MANFILE	 = ${PROGRAM}.1
PREFIX   = /usr/local
BINDIR	 = ${PREFIX}/bin
MANDIR	 = ${PREFIX}/man/man1
SOURCES  = ${PROGRAM}.c config.c ssl.c http.c json.c file.c readpass.c str.c
LDFLAGS += -lssl -lcrypto
CFLAGS	+= -std=c99 -Wall -DPROGRAM=\"${PROGRAM}\"

all: ${PROGRAM}

${PROGRAM}: ${SOURCES}
	$(CC) -o $@ ${CFLAGS} ${SOURCES} ${LDFLAGS}

install: ${PROGRAM} ${MANFILE}
	if [ ! -d ${BINDIR} ]; then mkdir -p ${BINDIR}; fi
	if [ ! -d ${MANDIR} ]; then mkdir -p ${MANDIR}; fi
	install -g 0 -m 0755 -o root ${PROGRAM} ${BINDIR}
	install -g 0 -m 0644 -o root ${MANFILE} ${MANDIR}

clean:
	-rm -f ${PROGRAM}

