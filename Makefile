CC = mpicc
CFLAGS = -g -Wextra
LOADLIBES=-lm -fopenmp -lc

EXECUTABLES=mandelbrot mandelbrot_mpi

.PHONY: all
all: $(EXECUTABLES)

$(EXECUTABLES): util.o

util.o: util.h

.PHONY: clean
clean:
	rm -vf *.o $(EXECUTABLES) *.pbm *.ppm
