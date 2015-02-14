include config.mk

# should work on hiredis 0.12.x
hiredis_src_files ?= hiredis/hiredis.c \
                     hiredis/async.c \
                     hiredis/read.c \
                     hiredis/net.c \
                     hiredis/sds.c

# should work on hiredis 0.11.x
# hiredis_src_files ?= hiredis/hiredis.c \
#                      hiredis/async.c \
#                      hiredis/net.c \
#                      hiredis/sds.c

clib := lem/redis.so

all: $(clib)

.PHONY: test


$(clib): lem/redis.c hiredis/async.c
	$(CC) $(CFLAGS) $(LDFLAGS) \
   lem/redis.c $(hiredis_src_files) \
	 -o $@ 
	@echo
	@echo '####################################################################'
	@echo '#'
	@echo '# Build Done - If you have a Redis listening on localhost'
	@echo '#              without a password, you can run the test/benchmark'
	@echo '#              by typing'
	@echo '#'
	@echo '#              # gmake test'
	@echo '#'
	@echo '####################################################################'
	@echo

hiredis/async.c:
	git submodule init
	git submodule update

install: $(clib)
	install -m 644 $< $(cmoddir)/lem

test:
	lem test/test.lua
	@echo 'yay! =)'

clean:
	rm -f lem/redis.so
