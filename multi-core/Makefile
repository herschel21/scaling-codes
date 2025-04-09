CC := gcc
CFLAGS := -O3 -fopenmp

SRCS := $(wildcard *.c)

OBJS := $(SRCS:.c=.o)
EXES := $(SRCS:.c=)

all: $(EXES)

%: %.o
	$(CC) $< -o $@ $(CFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(EXES)

.PHONY: all clean


