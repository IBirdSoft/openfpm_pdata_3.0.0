include ../../../example.mk

CC=mpic++

LDIR =

OBJ = main.o

%.o: %.cpp
	$(CC) -O3 -c --std=c++11 -o $@ $< $(INCLUDE_PATH)

example_sussman_circle: $(OBJ)
	$(CC) -o $@ $^ $(CFLAGS) $(LIBS_PATH) $(LIBS)

all: example_sussman_circle

run: all
	mpirun -np 2 ./example_sussman_circle

.PHONY: clean all run

clean:
	rm -f *.o *~ core example_sussman_circle

