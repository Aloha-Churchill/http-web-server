COMPILER=gcc
FLAGS=-g -Wall

run: serverexample
	./serverexample

all: serverexample.c
	$(COMPILER) $(FLAGS) -o serverexample serverexample.c

clean:
	rm serverexample