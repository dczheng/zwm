SRC      = zwm.c
CC       = gcc #-E
CFLAGS   = -I/usr/local/include \
           -Wall -Wextra \
           -Wno-deprecated-declarations \
           #-Wno-unused-parameter
LDFLAGS  = -L/usr/local/lib -lX11 -lXinerama

OBJ = ${SRC:.c=.o}
zwm: ${OBJ} Makefile
	${CC} -o $@ ${OBJ} ${LDFLAGS} 

.c.o:
	${CC} -c ${CFLAGS} $<

clean:
	rm -f zwm ${OBJ}

