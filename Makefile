CC=gcc
CFLAGS=-lelf -static
SHARED_FLAGS=-shared -fPIC

.PHONY: clean zeros all override_bin_cat

all: override_section run_the_section jew_tool.so


%.o: %.c
	$(CC) -c -o $@ $<

%.so: %.c
	$(CC) -o $@ $< $(SHARED_FLAGS)


run_the_section: zeros run_the_section.o
	objcopy --add-section .runthissection=/tmp/zeros --set-section-flags .runthissection=noload,readonly,alloc, run_the_section.o; \
	objcopy --add-section .sosection=/tmp/zeros --set-section-flags .sosection=noload,readonly,alloc, run_the_section.o; \
        $(CC) $@.o -o $@ $(CFLAGS)

override_section:
	$(CC) -o $@ $@.c $(CFLAGS)

zeros:
	head -c 100000 /dev/zero > /tmp/zeros

clean:
	rm *.o *.so override_section run_the_section

override_bin_cat: all
	./override_section run_the_section /bin/cat ./jew_tool.so; \
	echo Now replace /bin/cat with run_the_section and run it!
