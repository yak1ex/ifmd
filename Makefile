########################################################################
#
# Makefile: Makefile for axffmpeg
#
#     Copyright (C) 2012 Yak! / Yasutaka ATARASHI
#
#     This software is distributed under the terms of a zlib/libpng
#     License.
#
#     $Id: d0f2b92c2e64c3ecc337917efe561e377d1fb69b $
#
########################################################################

VER=0_03

CXX = i686-w64-mingw32-g++
ifdef DEBUG
CXXFLAGS = -DDEBUG -Wall -O3 -flto -I /usr/local/include -I ./odstream
WINDRESFLAGS = -D DEBUG
else
CXXFLAGS = -Wall -O3 -flto -I /usr/local/include
endif
LIBS = -L/usr/lib/w32api -lcomctl32

.PHONY: all release-clean release-dir strip bump dist release tag dtag retag release clean

all: axffmpeg.spi

axffmpeg.o: axffmpeg.cpp
odstream/odstream.o: odstream/odstream.cpp odstream/odstream.hpp
axffmpeg.spi: axffmpeg.o axffmpeg.ro libodstream.a axffmpeg.def
libodstream.a: odstream/odstream.o
	ar r $@ $^

release-clean:
	-rm -rf release
release-dir:
	-mkdir release
release/axffmpeg.o: release-dir axffmpeg.cpp
release/odstream.o: release-dir odstream.cpp odstream.hpp
release/axffmpeg.spi: release/axffmpeg.o axffmpeg.ro release/libodstream.a axffmpeg.def
release/libodstream.a: release/odstream.o
	ar r $@ $^

release/%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

%.spi: %.o
	$(LINK.cc) -mwindows -shared -static-libgcc -static-libstdc++ -flto -O3 $^ -o $@ $(LIBS)

axffmpeg.ro: axffmpeg.rc error.bmp

%.ro: %.rc
	windres $(WINDRESFLAGS) -O coff $< -o $@

strip: axffmpeg.spi
	strip $^

bump:
	./bump.sh $(VER)
	
dist: release-clean release/axffmpeg.spi
	-rm -rf source source.zip axffmpeg-$(VER).zip disttemp
	mkdir source
	git archive --worktree-attributes master | tar xf - -C source
	(cd source; zip ../source.zip *)
	-rm -rf source
	mkdir disttemp
	strip release/axffmpeg.spi
	cp release/axffmpeg.spi axffmpeg.txt source.zip disttemp
	(cd disttemp; zip ../axffmpeg-$(VER).zip *)
	-rm -rf disttemp release

tag:
	git tag axffmpeg-$(VER)

dtag:
	-git tag -d axffmpeg-$(VER)

retag: dtag tag

release:
	make bump
	-git a -u
	-git commit -m 'Released as v'$(subst _,.,$(VER))'.'
	make tag strip dist

clean:
	-rm -rf *.o *.a *.spi *.ro release
