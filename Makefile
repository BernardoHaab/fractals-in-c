CC = mpicc
CFLAGS = -g -Wextra
LOADLIBES=-lm -lc -fopenmp

# EXECUTABLES=mandelbrot_mpi mandelbrot_zoom
EXECUTABLES=mandelbrot_hibrido

.PHONY: all
all: $(EXECUTABLES)

$(EXECUTABLES): util.o

util.o: util.h

.PHONY: clean
clean:
	rm -vf *.o $(EXECUTABLES) *.pbm *.ppm
