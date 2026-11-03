# Host-compiled unit tests for the predictive estimator. No Contiki, no Cooja.
CC      ?= gcc
CFLAGS  += -std=c11 -Wall -Wextra -O1 -I../dsme -Ishim

all: run

test_predictor: test_predictor.c ../dsme/dsme-predictor.c
	$(CC) $(CFLAGS) -DDSME_TEST_HOST=1 -o $@ $^

run: test_predictor
	./test_predictor

clean:
	rm -f test_predictor
