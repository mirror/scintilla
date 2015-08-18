// Scintilla source code edit control
/** @file LexerBase.h
 ** A simple lexer with no state.
 **/
// Copyright 1998-2010 by Neil Hodgson <neilh@scintilla.org>
// The License.txt file describes the conditions under which this software may be distributed.

#ifndef LEXERBASE_H
#define LEXERBASE_H

#ifdef SCI_NAMESPACE
namespace Scintilla {
#endif

// A simple lexer with no state
class LexerBase : public DefaultLexer {
protected:
	PropSetSimple props;
	enum { numWordLists = KEYWORDSET_MAX + 1 };
	WordList *keyWordLists[numWordLists + 1];
public:
	LexerBase();
	virtual ~LexerBase();
	Sci_Position SCI_METHOD PropertySet(const char *key, const char *val);
	Sci_Position SCI_METHOD WordListSet(int n, const char *wl);
};

#ifdef SCI_NAMESPACE
}
#endif

#endif
