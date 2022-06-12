SRC      = zwm.c
INC      = log.h
CC       = gcc #-E
CFLAGS   = -I/usr/local/include \
           -Wall -Wextra \
           -Wno-deprecated-declarations \
           #-Wno-unused-parameter
LDFLAGS  = -L/usr/local/lib -lX11 -lXinerama

OBJ = ${SRC:.c=.o}
zwm: ${OBJ}
	${CC} -o $@ ${OBJ} ${LDFLAGS} 

$(OBJ): %.o:%.c ${INC} Makefile
	${CC} -c ${CFLAGS} $<

clean:
	rm -f zwm ${OBJ}

