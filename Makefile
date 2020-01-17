.PHONY: clean lua cjson lfs

CC= gcc
IPATH= -I3rd/lua/src
LPATH= -L3rd/lua/src
MYFLAGS= -std=gnu99 -g -Wall -Wl,-E $(IPATH) 
LIBS= -ldl -lm -llua $(LPATH)

HEADER = $(wildcard src/*.h)
SRCS= $(wildcard src/*.c)
PROG= bin/vscluadbg

all: lua cjson lfs $(PROG)
	
$(PROG): $(SRCS) $(HEADER)
	$(CC) $(MYFLAGS) -o $@ $(SRCS) $(LIBS)

lua: 
	$(MAKE) -C 3rd/lua linux

cjson:
	$(MAKE) -C 3rd/lua-cjson install

lfs:
	$(MAKE) -C 3rd/luafilesystem install

clean:
	rm -f $(PROG)

cleanall:
	$(MAKE) -C 3rd/lua clean
	$(MAKE) -C 3rd/lua-cjson clean
	rm -f src/*.o $(PROG) bin/*.so