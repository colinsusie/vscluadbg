# $Id: Makefile,v 1.36 2009/09/21 17:02:44 mascarenhas Exp $

T= lfs

CONFIG= ./config

include $(CONFIG)

TARGET= src/lfs.so
SRCS= src/$T.c
OBJS= src/$T.o

lib: $(TARGET)

$(TARGET): $(OBJS)
	MACOSX_DEPLOYMENT_TARGET="10.3"; export MACOSX_DEPLOYMENT_TARGET; $(CC) $(LIB_OPTION) -o $(TARGET) $(OBJS)

test: lib
	LUA_CPATH=./src/?.so lua tests/test.lua

install: $(TARGET)
	mkdir -p $(DESTDIR)$(LUA_LIBDIR)
	cp $(TARGET) $(DESTDIR)$(LUA_LIBDIR)

clean:
	rm -f $(TARGET) $(OBJS)
