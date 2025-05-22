CC = mpicc
CFLAGS = -g -Wextra
LOADLIBES=-lm -lc

EXECUTABLES=mandelbrot mandelbrot_mpi mandelbrot_zoom

.PHONY: all
all: $(EXECUTABLES)

$(EXECUTABLES): util.o

util.o: util.h

.PHONY: clean
clean:
	rm -vf *.o $(EXECUTABLES) *.pbm *.ppm
