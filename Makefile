PLAT ?= none
PLATS = linux macosx

.PHONY: $(PLATS) clean cleanall lua cjson lfs

none:
	@echo "usage: make <PLAT>"
	@echo "  PLAT is one of: $(PLATS)"

$(PLATS):
	$(MAKE) all PLAT=$@

CC= gcc
IPATH= -I3rd/lua/src
LPATH= -L3rd/lua/src

ifeq ($(PLAT), macosx)
MYFLAGS := -std=gnu99 -g -Wall $(IPATH) 
else
MYFLAGS := -std=gnu99 -g -Wall -Wl,-E $(IPATH) 
endif

LIBS= -ldl -lm -llua $(LPATH)
HEADER = $(wildcard src/*.h)
SRCS= $(wildcard src/*.c)
BINROOT= vscext/bin/$(PLAT)
PROG= $(BINROOT)/vscluadbg

all: lua lfs cjson $(PROG)
	
lua: 
	$(MAKE) -C 3rd/lua $(PLAT)

cjson:
	$(MAKE) -C 3rd/lua-cjson install PLAT=$(PLAT)

lfs:
	$(MAKE) -C 3rd/luafilesystem install PLAT=$(PLAT)

$(PROG): $(SRCS) $(HEADER)
	$(CC) $(MYFLAGS) -o $@ $(SRCS) $(LIBS)

clean:
	rm -f $(PROG)

cleanall:
	$(MAKE) -C 3rd/lua clean
	$(MAKE) -C 3rd/lua-cjson clean
	rm -f src/*.o $(PROG) $(BINROOT)/*.so