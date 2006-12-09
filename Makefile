CFLAGS+=-g -Os -D_FORTIFY_SOURCE=2 -Wall -W `pkg-config --cflags glib-2.0` 

all: irqbalance

LIBS=bitmap.o irqbalance.o cputree.o  procinterrupts.o irqlist.o placement.o activate.o network.o powermode.o numa.o classify.o

irqbalance: .depend $(LIBS)
	gcc  -g -O2 -D_FORTIFY_SOURCE=2 -Wall  `pkg-config --libs glib-2.0` $(LIBS) -o irqbalance 

clean:
	rm -f irqbalance *~ *.o .depend

# rule for building dependency lists, and writing them to a file
# named ".depend".
.depend:
	rm -f .depend
	gccmakedep -f- -- $(CFLAGS) -- *.c > .depend
