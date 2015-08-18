// Scintilla source code edit control
/** @file DefaultLexer.h
 ** A lexer base class with default empty implementations of methods. 
 ** For lexers that do not support all features so do not need real implementations.
 ** Implementation is temporarily in LexerBase.cxx.
 **/
// Copyright 2015 by Neil Hodgson <neilh@scintilla.org>
// The License.txt file describes the conditions under which this software may be distributed.

#ifndef DEFAULTLEXER_H
#define DEFAULTLEXER_H

#ifdef SCI_NAMESPACE
namespace Scintilla {
#endif

// A simple lexer with no state
class DefaultLexer : public ILexer {
public:
	DefaultLexer();
	virtual ~DefaultLexer();
	void SCI_METHOD Release();
	int SCI_METHOD Version() const;
	const char * SCI_METHOD PropertyNames();
	int SCI_METHOD PropertyType(const char *name);
	const char * SCI_METHOD DescribeProperty(const char *name);
	Sci_Position SCI_METHOD PropertySet(const char *key, const char *val);
	const char * SCI_METHOD DescribeWordListSets();
	Sci_Position SCI_METHOD WordListSet(int n, const char *wl);
	void SCI_METHOD Lex(Sci_PositionU startPos, Sci_Position lengthDoc, int initStyle, IDocument *pAccess) = 0;
	void SCI_METHOD Fold(Sci_PositionU startPos, Sci_Position lengthDoc, int initStyle, IDocument *pAccess);
	void * SCI_METHOD PrivateCall(int operation, void *pointer);
	int SCI_METHOD LineEndTypesSupported();
	int SCI_METHOD AllocateSubStyles(int styleBase, int numberStyles);
	int SCI_METHOD SubStylesStart(int styleBase);
	int SCI_METHOD SubStylesLength(int styleBase);
	int SCI_METHOD StyleFromSubStyle(int subStyle);
	int SCI_METHOD PrimaryStyleFromStyle(int style);
	void SCI_METHOD FreeSubStyles();
	void SCI_METHOD SetIdentifiers(int style, const char *identifiers);
	int SCI_METHOD DistanceToSecondaryStyles();
	const char * SCI_METHOD GetSubStyleBases();
	int SCI_METHOD MaximumNamedStyle();
	const char * SCI_METHOD NameOfStyle(int style);
	const char * SCI_METHOD DescriptionOfStyle(int style);
	const char * SCI_METHOD TagsOfStyle(int style);
};

#ifdef SCI_NAMESPACE
}
#endif

#endif
