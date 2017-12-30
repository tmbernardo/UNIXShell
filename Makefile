PROGS = usfsh

all : ${PROGS}

usfsh : usfsh.c list.c list.h debug_panic.c
	gcc -o usfsh usfsh.c list.c debug_panic.c

clean :
	rm ${PROGS}

