# Copyright 2018 Mitchell mitchell.att.foicica.com. See License.txt.
# This makefile is used only for catching compile and test errors when
# backporting fixes and features from the main branch of Scintilla. It likely
# will not produce compiled targets that can be used by a Scintilla-based
# application.
# Usage: make -f check.mak

.SUFFIXES: .cxx .c .o .h .a

INCLUDEDIRS = -Iinclude -Isrc -Ilexlib
CFLAGS = -pedantic -Wall
CXXFLAGS = -std=c++11 -pedantic -pedantic-errors -DSCI_LEXER $(INCLUDEDIRS) \
           -DNDEBUG -Os -Wall
ifndef GTK3
  GTK_CFLAGS = $(shell pkg-config --cflags gtk+-2.0)
else
  GTK_CFLAGS = $(shell pkg-config --cflags gtk+-3.0)
endif

base_src_objs = AutoComplete.o CallTip.o CaseConvert.o CaseFolder.o \
                Catalogue.o CellBuffer.o CharClassify.o ContractionState.o \
                DBCS.o Decoration.o Document.o EditModel.o Editor.o EditView.o \
                ExternalLexer.o Indicator.o KeyMap.o LineMarker.o MarginView.o \
                PerLine.o PositionCache.o RESearch.o RunStyles.o \
                ScintillaBase.o Selection.o Style.o UniConversion.o \
                ViewStyle.o XPM.o
base_lexlib_objs = Accessor.o CharacterCategory.o CharacterSet.o LexerBase.o \
                   LexerModule.o LexerNoExceptions.o LexerSimple.o \
                   PropSetSimple.o StyleContext.o WordList.o
base_lexer_objs = $(addsuffix .o,$(basename $(sort $(notdir $(wildcard lexers/Lex*.cxx)))))

win32_src_objs = $(addprefix win32/, $(base_src_objs))
win32_lexlib_objs = $(addprefix win32/, $(base_lexlib_objs))
win32_lexer_objs = $(addprefix win32/, $(base_lexer_objs))
cocoa_src_objs = $(addprefix cocoa/, $(base_src_objs))
cocoa_lexlib_objs = $(addprefix cocoa/, $(base_lexlib_objs))
cocoa_lexer_objs = $(addprefix cocoa/, $(base_lexer_objs))
gtk_src_objs = $(addprefix gtk/, $(base_src_objs))
gtk_lexlib_objs = $(addprefix gtk/, $(base_lexlib_objs))
gtk_lexer_objs = $(addprefix gtk/, $(base_lexer_objs))
curses_src_objs = $(addprefix curses/, $(base_src_objs))
curses_lexlib_objs = $(addprefix curses/, $(base_lexlib_objs))
curses_lexer_objs = $(addprefix curses/, $(base_lexer_objs))

all: | /tmp/scintilla
	make -C $| -f check.mak -j4 bin/scintilla_win32.a \
		bin/scintilla_cocoa.a bin/scintilla_gtk.a bin/scintilla_curses.a qt
/tmp/scintilla: ; cp -rs `pwd` $@

# Windows platform objects.
$(win32_src_objs): win32/%.o: src/%.cxx
	i686-w64-mingw32-g++ -c $(CXXFLAGS) $< -o $@
$(win32_lexlib_objs): win32/%.o: lexlib/%.cxx
	i686-w64-mingw32-g++ -c $(CXXFLAGS) $< -o $@
$(win32_lexer_objs): win32/%.o: lexers/%.cxx
	i686-w64-mingw32-g++ -c $(CXXFLAGS) $< -o $@
win32/PlatWin.o win32/ScintillaWin.o win32/ScintillaDLL.o win32/HanjaDic.o: win32/%.o: win32/%.cxx
	i686-w64-mingw32-g++ -c $(CXXFLAGS) $< -o $@
bin/scintilla_win32.a: $(win32_src_objs) $(win32_lexlib_objs) \
                       $(win32_lexer_objs) win32/PlatWin.o \
                       win32/ScintillaWin.o win32/ScintillaDLL.o \
                       win32/HanjaDic.o
	i686-w64-mingw32-ar rc $@ $^
	touch $@

# MacOS platform objects.
$(cocoa_src_objs): cocoa/%.o: src/%.cxx
	i386-apple-darwin9-g++ -c $(CXXFLAGS) $< -o $@
$(cocoa_lexlib_objs): cocoa/%.o: lexlib/%.cxx
	i386-apple-darwin9-g++ -c $(CXXFLAGS) $< -o $@
$(cocoa_lexer_objs): cocoa/%.o: lexers/%.cxx
	i386-apple-darwin9-g++ -c $(CXXFLAGS) $< -o $@
cocoa/PlatCocoa.o cocoa/ScintillaCocoa.o cocoa/ScintillaView.o: cocoa/%.o: cocoa/%.mm
	i386-apple-darwin9-clang++-gstdc++ -c $(CXXFLAGS) $< -o $@
bin/scintilla_cocoa.a: $(cocoa_src_objs) $(cocoa_lexlib_objs) \
                       $(cocoa_lexer_objs) #cocoa/PlatCocoa.o \
                       #cocoa/ScintillaCocoa.o cocoa/ScintillaView.o
	i386-apple-darwin9-ar rc $@ $^
	touch $@

# GTK platform objects.
bin/scintilla_gtk.a: $(gtk_src_objs) $(gtk_lexlib_objs) $(gtk_lexer_objs) \
                     gtk/PlatGTK.o gtk/ScintillaGTK.o \
                     gtk/ScintillaGTKAccessible.o gtk/scintilla-marshal.o
	ar rc $@ $^
	touch $@
$(gtk_src_objs): gtk/%.o: src/%.cxx
	g++ -c $(CXXFLAGS) -DGTK $< -o $@
$(gtk_lexlib_objs): gtk/%.o: lexlib/%.cxx
	g++ -c $(CXXFLAGS) -DGTK $< -o $@
$(gtk_lexer_objs): gtk/%.o: lexers/%.cxx
	g++ -c $(CXXFLAGS) -DGTK $< -o $@
gtk/PlatGTK.o gtk/ScintillaGTK.o gtk/ScintillaGTKAccessible.o: gtk/%.o: gtk/%.cxx
	g++ -c $(CXXFLAGS) -DGTK $(GTK_CFLAGS) $< -o $@
gtk/scintilla-marshal.o: gtk/scintilla-marshal.c
	gcc -c $(CFLAGS) $(GTK_CFLAGS) $< -o $@

# Curses platform objects.
bin/scintilla_curses.a: $(curses_src_objs) $(curses_lexlib_objs) \
                        $(curses_lexer_objs) curses/ScintillaCurses.o
	ar rc $@ $^
	touch $@
$(curses_src_objs): curses/%.o: src/%.cxx
	g++ -c $(CXXFLAGS) -DCURSES -DLPEG_LEXER $< -o $@
$(curses_lexlib_objs): curses/%.o: lexlib/%.cxx
	g++ -c $(CXXFLAGS) -DCURSES -DLPEG_LEXER $< -o $@
$(curses_lexer_objs): curses/%.o: lexers/%.cxx
	g++ -c $(CXXFLAGS) -DCURSES -DLPEG_LEXER $< -o $@
curses/ScintillaCurses.o: curses/ScintillaCurses.cxx
	g++ -c $(CXXFLAGS) -DCURSES -Wno-unused-parameter $< -o $@

# Qt platform objects. (Note: requires libqt4-dev qt4-qmake.)
.PHONY: qt
qt: qt/ScintillaEditBase/Makefile
	make -C $(dir $<)
qt/ScintillaEditBase/Makefile:
	cd qt/ScintillaEditBase && qmake

deps: win32_deps cocoa_deps gtk_deps curses_deps
win32_deps: src/*.cxx lexlib/*.cxx lexers/*.cxx win32/*.cxx
	i686-w64-mingw32-g++ -MM $(CXXFLAGS) $^ | sed -e 's|^\([[:alnum:]-]\+\.o:\)|win32/\1|;' > checkdeps.mak
cocoa_deps: src/*.cxx lexlib/*.cxx lexers/*.cxx #cocoa/*.cxx
	i386-apple-darwin9-g++ -MM $(CXXFLAGS) $^ | sed -e 's|^\([[:alnum:]-]\+\.o:\)|cocoa/\1|;' >> checkdeps.mak
gtk_deps: src/*.cxx lexlib/*.cxx lexers/*.cxx gtk/*.cxx
	g++ -MM $(CXXFLAGS) $^ | sed -e 's|^\([[:alnum:]-]\+\.o:\)|gtk/\1|;' >> checkdeps.mak
curses_deps: src/*.cxx lexlib/*.cxx lexers/*.cxx curses/*.cxx
	g++ -MM $(CXXFLAGS) $^ | sed -e 's|^\([[:alnum:]-]\+\.o:\)|curses/\1|;' >> checkdeps.mak

include checkdeps.mak

clean:
	rm -f bin/*.a bin/*.dll win32/*.o cocoa/*.o gtk/*.o curses/*.o
	rm -rf /tmp/scintilla

.PHONY: test
test: | /tmp/scintilla
	make -C $|/test/unit CXX=g++ clean test
	cd $|/test && lua5.1 test_lexlua.lua

releasedir = /tmp/scintilla$(shell grep -o '[0-9]\+' version.txt)
$(releasedir): ; hg archive $@
zip: $(releasedir)
	cd /tmp && tar czf $<.tgz $(notdir $<)
	cd /tmp && zip -r $<.zip $(notdir $<)
	rm -r $<

upload: LongTermDownload.html doc/ScintillaHistory.html doc/ScintillaDoc.html \
        doc/StyleMetadata.html doc/LPegLexer.html
	scp $^ foicica@web.sourceforge.net:/home/project-web/scintilla/htdocs/
