# Copyright 2012-2018 Mitchell mitchell.att.foicica.com. See License.txt.

.SUFFIXES: .cxx .c .o .h .a

AR = ar
CXX = g++
INCLUDEDIRS = -I ../include -I ../src -I ../lexlib
CXXFLAGS = -std=c++11 -pedantic -DCURSES -DSCI_LEXER $(INCLUDEDIRS) -Wall \
           -Wextra -Wno-missing-field-initializers
ifdef DEBUG
  CXXFLAGS += -DDEBUG -g
else
  CXXFLAGS += -DNDEBUG -Os
endif
CURSES_FLAGS =

scintilla = ../bin/scintilla.a
lexers = $(addsuffix .o,$(basename $(sort $(notdir $(wildcard ../lexers/Lex*.cxx)))))

vpath %.h ../src ../include ../lexlib
vpath %.cxx ../src ../lexlib ../lexers

all: $(scintilla)
.cxx.o:
	$(CXX) $(CXXFLAGS) $(CURSES_FLAGS) -c $<
$(scintilla): AutoComplete.o CallTip.o CaseConvert.o CaseFolder.o Catalogue.o \
              CellBuffer.o CharacterCategory.o CharClassify.o \
              ContractionState.o Decoration.o Document.o EditModel.o Editor.o \
              EditView.o ExternalLexer.o Indicator.o KeyMap.o LineMarker.o \
              MarginView.o PerLine.o PositionCache.o RESearch.o RunStyles.o \
              ScintillaBase.o Selection.o Style.o UniConversion.o ViewStyle.o \
              XPM.o Accessor.o CharacterSet.o LexerBase.o LexerModule.o \
              LexerNoExceptions.o LexerSimple.o PropSetSimple.o StyleContext.o \
              WordList.o $(lexers) ScintillaCurses.o
	$(AR) rc $@ $^
	touch $@
clean:
	rm -f *.o $(scintilla)
