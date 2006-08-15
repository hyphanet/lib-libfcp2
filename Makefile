# Ultra simple makefile

CFLAGS+=-g  -I. -std=c99
LDFLAGS+=-g 

libfcp2_objs=fcp2.o
example_objs=example.o

all: libfcp2.a example


%.o: %.c
	gcc $(CFLAGS) -c $<

libfcp2.a: fcp2.o
	ar cr $@ $(libfcp2_objs)

example: $(example_objs)
	gcc $(LDFLAGS) -L. -o $@ $(example_objs) -lfcp2

clean:
	rm -f *.o libfcp2.a example
