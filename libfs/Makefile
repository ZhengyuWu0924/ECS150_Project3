# Target library
objs := fs.o
lib := libfs.a
CC := gcc
CFLAGS := -Wall -Werror

all: $(lib)
$(lib): fs.o disk.o
	ar rcs $(lib) fs.o disk.o

## TODO: Phase 1
%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<
clean:
	rm -f $(objs)
