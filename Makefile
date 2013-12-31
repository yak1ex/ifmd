########################################################################
#
# Makefile: Makefile for ifmd
#
#     Copyright (C) 2013 Yak! / Yasutaka ATARASHI
#
#     This software is distributed under the terms of a zlib/libpng
#     License.
#
#     $Id: d0f2b92c2e64c3ecc337917efe561e377d1fb69b $
#
########################################################################

NAME=ifmd
VER=0_01

CC = i686-w64-mingw32-gcc
CXX = i686-w64-mingw32-g++
ifdef DEBUG
CFLAGS = -DDEBUG -Wall -O3 -flto
CXXFLAGS = -DDEBUG -Wall -O3 -flto -I /usr/local/include -I ./odstream
WINDRESFLAGS = -D DEBUG
else
CFLAGS = -Wall -O3 -flto
CXXFLAGS = -Wall -O3 -flto -I /usr/local/include
endif
LIBS = -L/usr/lib/w32api -lcomctl32 -lole32 -loleaut32 -luuid

.PHONY: all release-clean release-dir strip bump dist release tag dtag retag release clean

all: $(NAME).spi

$(NAME).o: $(NAME).cpp
odstream/odstream.o: odstream/odstream.cpp odstream/odstream.hpp
$(NAME).spi: $(NAME).o $(NAME).ro libodstream.a $(NAME).def discount/libmarkdown.a disphelper/disphelper.o
libodstream.a: odstream/odstream.o
	ar r $@ $^
discount/libmarkdown.a:
	(cd discount; env CC=i686-w64-mingw32-gcc ./configure.sh; env LANG=C make)

release-clean:
	-rm -rf release
release-dir:
	-mkdir release
release/$(NAME).o: release-dir $(NAME).cpp
release/odstream.o: release-dir odstream.cpp odstream.hpp
release/$(NAME).spi: release/$(NAME).o $(NAME).ro release/libodstream.a $(NAME).def
release/libodstream.a: release/odstream.o
	ar r $@ $^

release/%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

%.spi: %.o
	$(LINK.cc) -mwindows -shared -static-libgcc -static-libstdc++ -flto -O3 $^ -o $@ $(LIBS)

$(NAME).ro: $(NAME).rc

%.ro: %.rc
	windres $(WINDRESFLAGS) -O coff $< -o $@

strip: $(NAME).spi
	strip $^

bump:
	./bump.sh $(VER)
	
dist: release-clean release/$(NAME).spi
	-rm -rf source source.zip $(NAME)-$(VER).zip disttemp
	mkdir source
	git archive --worktree-attributes master | tar xf - -C source
	(cd source; zip ../source.zip *)
	-rm -rf source
	mkdir disttemp
	strip release/$(NAME).spi
	cp release/$(NAME).spi $(NAME).txt source.zip disttemp
	(cd disttemp; zip ../$(NAME)-$(VER).zip *)
	-rm -rf disttemp release

tag:
	git tag $(NAME)-$(VER)

dtag:
	-git tag -d $(NAME)-$(VER)

retag: dtag tag

release:
	make bump
	-git a -u
	-git commit -m 'Released as v'$(subst _,.,$(VER))'.'
	make tag strip dist

clean:
	-rm -rf *.o *.a *.spi *.ro release
