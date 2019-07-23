/** @file LexZig.cxx
 ** Lexer for Zig.
 **
 ** Copyright (c) 2013 by SiegeLord <slabode@aim.com>
 ** Converted to lexer object and added further folding features/properties by "Udo Lechner" <dlchnr(at)gmx(dot)net>
 ** Modified for zig, May 2019
 **/
// Copyright 1998-2005 by Neil Hodgson <neilh@scintilla.org>
// The License.txt file describes the conditions under which this software may be distributed.

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <assert.h>
#include <ctype.h>

#include <string>
#include <map>

#include "ILexer.h"
#include "Scintilla.h"
#include "SciLexer.h"

#include "PropSetSimple.h"
#include "WordList.h"
#include "LexAccessor.h"
#include "Accessor.h"
#include "StyleContext.h"
#include "CharacterSet.h"
#include "LexerModule.h"
#include "OptionSet.h"
#include "DefaultLexer.h"

using namespace Scintilla;

static const int NUM_ZIG_KEYWORD_LISTS = 7;
static const int MAX_ZIG_IDENT_CHARS = 1023;

static bool IsStreamCommentStyle(int style) {
	return style == SCE_ZIG_COMMENTBLOCK ||
		   style == SCE_ZIG_COMMENTBLOCKDOC;
}

// Options used for LexerZig
struct OptionsZig {
	bool fold;
	bool foldSyntaxBased;
	bool foldComment;
	bool foldCommentMultiline;
	bool foldCommentExplicit;
	std::string foldExplicitStart;
	std::string foldExplicitEnd;
	bool foldExplicitAnywhere;
	bool foldCompact;
	int  foldAtElseInt;
	bool foldAtElse;
	OptionsZig() {
		fold = false;
		foldSyntaxBased = true;
		foldComment = false;
		foldCommentMultiline = true;
		foldCommentExplicit = true;
		foldExplicitStart = "";
		foldExplicitEnd   = "";
		foldExplicitAnywhere = false;
		foldCompact = true;
		foldAtElseInt = -1;
		foldAtElse = false;
	}
};

static const char * const zigWordLists[NUM_ZIG_KEYWORD_LISTS + 1] = {
			"Primary keywords and identifiers",
			"Built in types",
			"Other keywords", // built in functions go here!
			"Keywords 4",
			"Keywords 5",
			"Keywords 6",
			"Keywords 7",
			0,
		};

struct OptionSetZig : public OptionSet<OptionsZig> {
	OptionSetZig() {
		DefineProperty("fold", &OptionsZig::fold);

		DefineProperty("fold.comment", &OptionsZig::foldComment);

		DefineProperty("fold.compact", &OptionsZig::foldCompact);

		DefineProperty("fold.at.else", &OptionsZig::foldAtElse);

		DefineProperty("fold.zig.syntax.based", &OptionsZig::foldSyntaxBased,
			"Set this property to 0 to disable syntax based folding.");

		DefineProperty("fold.zig.comment.multiline", &OptionsZig::foldCommentMultiline,
			"Set this property to 0 to disable folding multi-line comments when fold.comment=1.");

		DefineProperty("fold.zig.comment.explicit", &OptionsZig::foldCommentExplicit,
			"Set this property to 0 to disable folding explicit fold points when fold.comment=1.");

		DefineProperty("fold.zig.explicit.start", &OptionsZig::foldExplicitStart,
			"The string to use for explicit fold start points, replacing the standard //{.");

		DefineProperty("fold.zig.explicit.end", &OptionsZig::foldExplicitEnd,
			"The string to use for explicit fold end points, replacing the standard //}.");

		DefineProperty("fold.zig.explicit.anywhere", &OptionsZig::foldExplicitAnywhere,
			"Set this property to 1 to enable explicit fold points anywhere, not just in line comments.");

		DefineProperty("lexer.zig.fold.at.else", &OptionsZig::foldAtElseInt,
			"This option enables Zig folding on a \"} else {\" line of an if statement.");

		DefineWordListSets(zigWordLists);
	}
};

class LexerZig : public DefaultLexer {
	WordList keywords[NUM_ZIG_KEYWORD_LISTS];
	OptionsZig options;
	OptionSetZig osZig;
public:
	virtual ~LexerZig() {
	}
	void SCI_METHOD Release() override {
		delete this;
	}
	int SCI_METHOD Version() const override {
		return 0;		
		//return lvOriginal;
	}
	const char * SCI_METHOD PropertyNames() override {
		return osZig.PropertyNames();
	}
	int SCI_METHOD PropertyType(const char *name) override {
		return osZig.PropertyType(name);
	}
	const char * SCI_METHOD DescribeProperty(const char *name) override {
		return osZig.DescribeProperty(name);
	}
	Sci_Position SCI_METHOD PropertySet(const char *key, const char *val) override;
	const char * SCI_METHOD DescribeWordListSets() override {
		return osZig.DescribeWordListSets();
	}
	Sci_Position SCI_METHOD WordListSet(int n, const char *wl) override;
	void SCI_METHOD Lex(Sci_PositionU startPos, Sci_Position length, int initStyle, IDocument *pAccess) override;
	void SCI_METHOD Fold(Sci_PositionU startPos, Sci_Position length, int initStyle, IDocument *pAccess) override;
	void * SCI_METHOD PrivateCall(int, void *) override {
		return 0;
	}
	static ILexer4 *LexerFactoryZig() {
		return new LexerZig();
	}
};

Sci_Position SCI_METHOD LexerZig::PropertySet(const char *key, const char *val) {
	if (osZig.PropertySet(&options, key, val)) {
		return 0;
	}
	return -1;
}

Sci_Position SCI_METHOD LexerZig::WordListSet(int n, const char *wl) {
	Sci_Position firstModification = -1;
	if (n < NUM_ZIG_KEYWORD_LISTS) {
		WordList *wordListN = &keywords[n];
		WordList wlNew;
		wlNew.Set(wl);
		if (*wordListN != wlNew) {
			wordListN->Set(wl);
			firstModification = 0;
		}
	}
	return firstModification;
}

static bool IsWhitespace(int c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static bool IsIdentifierStart(int ch) {
	return (IsASCII(ch) && (isalpha(ch) || ch == '_')) || !IsASCII(ch);
}


static bool IsIdentifierContinue(int ch) {
	return (IsASCII(ch) && (isalnum(ch) || ch == '_')) || !IsASCII(ch);
}

static bool IsBuiltInStart(int ch) {
	bool result = (IsASCII(ch) && ch == '@');
	if(result){
		//printf("Found builtin function\n");
	}
	return result;
}

static void ScanWhitespace(Accessor& styler, Sci_Position& pos, Sci_Position max) {
	while (IsWhitespace(styler.SafeGetCharAt(pos, '\0')) && pos < max) {
		if (pos == styler.LineEnd(styler.GetLine(pos)))
			styler.SetLineState(styler.GetLine(pos), 0);
		pos++;
	}
	styler.ColourTo(pos-1, SCE_ZIG_DEFAULT);
}

static void GrabString(char* s, Accessor& styler, Sci_Position start, Sci_Position len) {
	for (Sci_Position ii = 0; ii < len; ii++)
		s[ii] = styler[ii + start];
	s[len] = '\0';
}

static void ScanIdentifier(Accessor& styler, Sci_Position& pos, WordList *keywords) {
	Sci_Position start = pos;
	while (IsIdentifierContinue(styler.SafeGetCharAt(pos, '\0'))){
		pos++;
	}

	char s[MAX_ZIG_IDENT_CHARS + 1];
	Sci_Position len = pos - start;
	len = len > MAX_ZIG_IDENT_CHARS ? MAX_ZIG_IDENT_CHARS : len;
	GrabString(s, styler, start, len);
	bool keyword = false;
	for (int ii = 0; ii < NUM_ZIG_KEYWORD_LISTS; ii++) {
		if (keywords[ii].InList(s)) {
			styler.ColourTo(pos - 1, SCE_ZIG_WORD + ii);
			keyword = true;
			break;
		}
	}
	if (!keyword) {
		styler.ColourTo(pos - 1, SCE_ZIG_IDENTIFIER);
	}

}

static void ScanBuiltin(Accessor& styler, Sci_Position& pos, WordList * keywords) {
	Sci_Position start = pos;
	pos++;
	while (IsIdentifierContinue(styler.SafeGetCharAt(pos, '\0'))){
		pos++;
	}

	char s[MAX_ZIG_IDENT_CHARS + 1];
	Sci_Position len = pos - start;
	len = len > MAX_ZIG_IDENT_CHARS ? MAX_ZIG_IDENT_CHARS : len;
	GrabString(s, styler, start, len);

	int builtinCategory = 2; // get list of builtin names
	if (keywords[builtinCategory].InList(s)) {
		styler.ColourTo(pos - 1, SCE_ZIG_WORD3);
	}else{
		styler.ColourTo(pos - 1, SCE_ZIG_LEXERROR);
	}
	
}


/* Scans a sequence of digits, returning true if it found any. */
static bool ScanDigits(Accessor& styler, Sci_Position& pos, int base) {
	Sci_Position old_pos = pos;
	for (;;) {
		int c = styler.SafeGetCharAt(pos, '\0');
		if (IsADigit(c, base) || c == '_')
			pos++;
		else
			break;
	}
	return old_pos != pos;
}

/* Scans an integer and floating point literals. */
static void ScanNumber(Accessor& styler, Sci_Position& pos) {
	int base = 10;
	int c = styler.SafeGetCharAt(pos, '\0');
	int n = styler.SafeGetCharAt(pos + 1, '\0');
	bool error = false;
	/* Scan the prefix, thus determining the base.
	 * 10 is default if there's no prefix. */
	if (c == '0' && n == 'x') {
		pos += 2;
		base = 16;
	} else if (c == '0' && n == 'b') {
		pos += 2;
		base = 2;
	} else if (c == '0' && n == 'o') {
		pos += 2;
		base = 8;
	}

	/* Scan initial digits. The literal is malformed if there are none. */
	error |= !ScanDigits(styler, pos, base);
	/* See if there's an integer suffix. We mimic the Rust's lexer
	 * and munch it even if there was an error above. */
	c = styler.SafeGetCharAt(pos, '\0');
	if (c == 'u' || c == 'i') {
		pos++;
		c = styler.SafeGetCharAt(pos, '\0');
		n = styler.SafeGetCharAt(pos + 1, '\0');
		if (c == '8') {
			pos++;
		} else if (c == '1' && n == '6') {
			pos += 2;
		} else if (c == '3' && n == '2') {
			pos += 2;
		} else if (c == '6' && n == '4') {
			pos += 2;
		} else if (styler.Match(pos, "size")) {
			pos += 4;
		} else {
			error = true;
		}
	/* See if it's a floating point literal. These literals have to be base 10.
	 */
	} else if (!error) {
		/* If there's a period, it's a floating point literal unless it's
		 * followed by an identifier (meaning this is a method call, e.g.
		 * `1.foo()`) or another period, in which case it's a range (e.g. 1..2)
		 */
		n = styler.SafeGetCharAt(pos + 1, '\0');
		if (c == '.' && !(IsIdentifierStart(n) || n == '.')) {
			error |= base != 10;
			pos++;
			/* It's ok to have no digits after the period. */
			ScanDigits(styler, pos, 10);
		}

		/* Look for the exponentiation. */
		c = styler.SafeGetCharAt(pos, '\0');
		if (c == 'e' || c == 'E') {
			error |= base != 10;
			pos++;
			c = styler.SafeGetCharAt(pos, '\0');
			if (c == '-' || c == '+')
				pos++;
			/* It is invalid to have no digits in the exponent. */
			error |= !ScanDigits(styler, pos, 10);
		}

		/* Scan the floating point suffix. */
		c = styler.SafeGetCharAt(pos, '\0');
		if (c == 'f') {
			error |= base != 10;
			pos++;
			c = styler.SafeGetCharAt(pos, '\0');
			n = styler.SafeGetCharAt(pos + 1, '\0');
			if (c == '3' && n == '2') {
				pos += 2;
			} else if (c == '6' && n == '4') {
				pos += 2;
			} else {
				error = true;
			}
		}
	}

	if (error)
		styler.ColourTo(pos - 1, SCE_ZIG_LEXERROR);
	else
		styler.ColourTo(pos - 1, SCE_ZIG_NUMBER);
}

static bool IsEnclosingChar(int c) {
    return c == '(' || c == ')' // PAREN
	    || c == '{' || c == '}' // BRACE
	    || c == '[' || c == ']'; // BRACKET
}

static bool IsOneCharOperator(int c) {
	return c == ';' // SEMICOLON
        || c == ',' // COMMA
        || c == '.' // PERIOD
	    || c == ':' // COLON
	    || c == '<' || c == '>' // ARROW
        || c == '~' // TILDE
        || c == '+' // PLUS
        || c == '=' // EQUAL
        || c == '-' // MINUS
	    || c == '*' // ASTERISK
	    || c == '/' // SLASH
	    || c == '%' // PERCENT
	    || c == '^' // CARET
	    || c == '!' // EXCLAMATIONMARK
        || c == '&' // AMPERSAND
	    || c == '|' //PIPE
	    || c == '?'; //QUESTIONMARK
}

static bool IsTwoCharOperator(int c, int n) {
	return (c == '.' && n == '.') // DOT2
        || (c == '.' && n == '*') // DOTASTERISK
        || (c == '.' && n == '?') // DOTQUESTIONMARK
        || (c == '*' && n == '*') // ASTERISK2
        || (c == '*' && n == '%') // ASTERISKPERCENT
	    || (c == '!' && n == '=') // EXCLAMATIONMARKEQUAL
        || (c == '<' && n == '<') // LARROW2
	    || (c == '<' && n == '=') // LARROWEQUAL
        || (c == '>' && n == '>') // RARROW2
	    || (c == '>' && n == '=') // RARROWEQUAL
        || (c == '=' && n == '=') // EQUALEQUAL
	    || (c == '=' && n == '>') // EQUALRARROW
        || (c == '-' && n == '>') // MINUSARROW
	    || (c == '-' && n == '%') // MINUSPERCENT
        || (c == '|' && n == '|') // PIPE2
	    || (c == '-' && n == '=') // MINUSEQUAL
        || (c == '&' && n == '=') // AMPERSANDEQUAL
	    || (c == '|' && n == '=') // PIPEEQUAL
        || (c == '+' && n == '=') // PLUSEQUAL
        || (c == '+' && n == '%') // PLUSPERCENT
        || (c == '+' && n == '+') // PLUS2
	    || (c == '*' && n == '=') // ASTERISKEQUAL
        || (c == '/' && n == '=') // SLASHEQUAL
	    || (c == '^' && n == '=') // CARETEQUAL
        || (c == '%' && n == '='); // PERCENTEQUAL
}

static bool IsThreeCharOperator(int c, int n, int n2) {
	return (c == '<' && n == '<' && n2 == '=') // LARROW2EQUAL
        || (c == '>' && n == '>' && n2 == '=') // RARROW2EQUAL
        || (c == '*' && n == '%' && n2 == '=') // ASTERISKPERCENTEQUAL
        || (c == '-' && n == '%' && n2 == '=') // MINUSPERCENTEQUAL
        || (c == '+' && n == '%' && n2 == '=') // PLUSPERCENTEQUAL
        || (c == '[' && n == '*' && n2 == ']') // PTRUNKNOWN   
        || (c == '.' && n == '.' && n2 == '.'); // DOT3
}

static bool IsFourCharOperator(int c, int n, int n2, int n3) {
    return (c =='[' && n =='c' && n2 =='*' && n3 == ']');
}

static bool IsValidCharacterEscape(int c) {
	return c == 'n'  || c == 'r' || c == 't' || c == '\\'
	    || c == '\'' || c == '"' || c == '0';
}

static bool IsValidStringEscape(int c) {
	return IsValidCharacterEscape(c) || c == '\n' || c == '\r';
}

static bool ScanNumericEscape(Accessor &styler, Sci_Position& pos, Sci_Position num_digits, bool stop_asap) {
	for (;;) {
		int c = styler.SafeGetCharAt(pos, '\0');
		if (!IsADigit(c, 16))
			break;
		num_digits--;
		pos++;
		if (num_digits == 0 && stop_asap)
			return true;
	}
	if (num_digits == 0) {
		return true;
	} else {
		return false;
	}
}

/* This is overly permissive for character literals in order to accept UTF-8 encoded
 * character literals. */
static void ScanCharacterLiteralOrLifetime(Accessor &styler, Sci_Position& pos, bool ascii_only) {
	pos++;
	int c = styler.SafeGetCharAt(pos, '\0');
	int n = styler.SafeGetCharAt(pos + 1, '\0');
	bool done = false;
	bool valid_lifetime = !ascii_only && IsIdentifierStart(c);
	bool valid_char = true;
	bool first = true;
	while (!done) {
		switch (c) {
			case '\\':
				done = true;
				if (IsValidCharacterEscape(n)) {
					pos += 2;
				} else if (n == 'x') {
					pos += 2;
					valid_char = ScanNumericEscape(styler, pos, 2, false);
				} else if (n == 'u' && !ascii_only) {
					pos += 2;
					if (styler.SafeGetCharAt(pos, '\0') != '{') {
						// old-style
						valid_char = ScanNumericEscape(styler, pos, 4, false);
					} else {
						int n_digits = 0;
						while (IsADigit(styler.SafeGetCharAt(++pos, '\0'), 16) && n_digits++ < 6) {
						}
						if (n_digits > 0 && styler.SafeGetCharAt(pos, '\0') == '}')
							pos++;
						else
							valid_char = false;
					}
				} else if (n == 'U' && !ascii_only) {
					pos += 2;
					valid_char = ScanNumericEscape(styler, pos, 8, false);
				} else {
					valid_char = false;
				}
				break;
			case '\'':
				valid_char = !first;
				done = true;
				break;
			case '\t':
			case '\n':
			case '\r':
			case '\0':
				valid_char = false;
				done = true;
				break;
			default:
				if (ascii_only && !IsASCII((char)c)) {
					done = true;
					valid_char = false;
				} else if (!IsIdentifierContinue(c) && !first) {
					done = true;
				} else {
					pos++;
				}
				break;
		}
		c = styler.SafeGetCharAt(pos, '\0');
		n = styler.SafeGetCharAt(pos + 1, '\0');

		first = false;
	}
	if (styler.SafeGetCharAt(pos, '\0') == '\'') {
		valid_lifetime = false;
	} else {
		valid_char = false;
	}
	if (valid_lifetime) {
		styler.ColourTo(pos - 1, SCE_ZIG_LIFETIME);
	} else if (valid_char) {
		pos++;
		styler.ColourTo(pos - 1, ascii_only ? SCE_ZIG_BYTECHARACTER : SCE_ZIG_CHARACTER);
	} else {
		styler.ColourTo(pos - 1, SCE_ZIG_LEXERROR);
	}
}

enum CommentState {
	UnknownComment,
	DocComment,
	NotDocComment
};


/*
 * The rule for line-doc comments is as follows... ///N and //! (where N is a non slash) start doc comments.
 * Otherwise it's a normal line comment.
 */
static void ResumeLineComment(Accessor &styler, Sci_Position& pos, Sci_Position max, CommentState state) {
	bool maybe_doc_comment = false;
	int c = styler.SafeGetCharAt(pos, '\0');
	if (c == '/') {
		if (pos < max) {
			pos++;
			c = styler.SafeGetCharAt(pos, '\0');
			if (c != '/') {
				maybe_doc_comment = true;
			}
		}
	} else if (c == '!') {
		maybe_doc_comment = true;
	}

	while (pos < max && c != '\n') {
		if (pos == styler.LineEnd(styler.GetLine(pos)))
			styler.SetLineState(styler.GetLine(pos), 0);
		pos++;
		c = styler.SafeGetCharAt(pos, '\0');
	}

	if (state == DocComment || (state == UnknownComment && maybe_doc_comment))
		styler.ColourTo(pos - 1, SCE_ZIG_COMMENTLINEDOC);
	else
		styler.ColourTo(pos - 1, SCE_ZIG_COMMENTLINE);
}

static void ScanComments(Accessor &styler, Sci_Position& pos, Sci_Position max) {
	pos++;
	int c = styler.SafeGetCharAt(pos, '\0');
	pos++;
	if (c == '/'){
		ResumeLineComment(styler, pos, max, UnknownComment);
	}
}

static void ResumeString(Accessor &styler, Sci_Position& pos, Sci_Position max, bool ascii_only) {
	int c = styler.SafeGetCharAt(pos, '\0');
	bool error = false;
	while (c != '"' && !error) {
		if (pos >= max) {
			error = true;
			break;
		}
		if (pos == styler.LineEnd(styler.GetLine(pos)))
			styler.SetLineState(styler.GetLine(pos), 0);
		if (c == '\\') {
			int n = styler.SafeGetCharAt(pos + 1, '\0');
			if (IsValidStringEscape(n)) {
				pos += 2;
			} else if (n == 'x') {
				pos += 2;
				error = !ScanNumericEscape(styler, pos, 2, true);
			} else if (n == 'u' && !ascii_only) {
				pos += 2;
				if (styler.SafeGetCharAt(pos, '\0') != '{') {
					// old-style
					error = !ScanNumericEscape(styler, pos, 4, true);
				} else {
					int n_digits = 0;
					while (IsADigit(styler.SafeGetCharAt(++pos, '\0'), 16) && n_digits++ < 6) {
					}
					if (n_digits > 0 && styler.SafeGetCharAt(pos, '\0') == '}')
						pos++;
					else
						error = true;
				}
			} else if (n == 'U' && !ascii_only) {
				pos += 2;
				error = !ScanNumericEscape(styler, pos, 8, true);
			} else {
				pos += 1;
				error = true;
			}
		} else {
			if (ascii_only && !IsASCII((char)c))
				error = true;
			else
				pos++;
		}
		c = styler.SafeGetCharAt(pos, '\0');
	}
	if (!error)
		pos++;
	styler.ColourTo(pos - 1, ascii_only ? SCE_ZIG_BYTESTRING : SCE_ZIG_STRING);
}

static void ResumeRawString(Accessor &styler, Sci_Position& pos, Sci_Position max, int num_hashes, bool ascii_only) {
	for (;;) {
		if (pos == styler.LineEnd(styler.GetLine(pos)))
			styler.SetLineState(styler.GetLine(pos), num_hashes);

		int c = styler.SafeGetCharAt(pos, '\0');
		if (c == '"') {
			pos++;
			int trailing_num_hashes = 0;
			while (styler.SafeGetCharAt(pos, '\0') == '#' && trailing_num_hashes < num_hashes) {
				trailing_num_hashes++;
				pos++;
			}
			if (trailing_num_hashes == num_hashes) {
				styler.SetLineState(styler.GetLine(pos), 0);
				break;
			}
		} else if (pos >= max) {
			break;
		} else {
			if (ascii_only && !IsASCII((char)c))
				break;
			pos++;
		}
	}
	styler.ColourTo(pos - 1, SCE_ZIG_BYTESTRINGR);
}

static void ScanMultiLineString(Accessor &styler, Sci_Position& pos, Sci_Position max, bool ascii_only) {
	pos += 2;
	int c = styler.SafeGetCharAt(pos, '\0');
	while (pos < max && c != '\n') {
		pos++;
		c = styler.SafeGetCharAt(pos, '\0');
	}
	styler.ColourTo(pos-1,SCE_ZIG_STRINGMULTILINE);
}

void SCI_METHOD LexerZig::Lex(Sci_PositionU startPos, Sci_Position length, int initStyle, IDocument *pAccess) {
	PropSetSimple props;
	Accessor styler(pAccess, &props);
	Sci_Position pos = startPos;
	Sci_Position max = pos + length;

	styler.StartAt(pos);
	styler.StartSegment(pos);

	if (initStyle == SCE_ZIG_COMMENTLINE || initStyle == SCE_ZIG_COMMENTLINEDOC) {
		ResumeLineComment(styler, pos, max, initStyle == SCE_ZIG_COMMENTLINEDOC ? DocComment : NotDocComment);
	} else if (initStyle == SCE_ZIG_STRING) {
		ResumeString(styler, pos, max, false);
	} else if (initStyle == SCE_ZIG_BYTESTRING) {
		ResumeString(styler, pos, max, true);
	} else if (initStyle == SCE_ZIG_STRINGMULTILINE) {
		ResumeRawString(styler, pos, max, styler.GetLineState(styler.GetLine(pos) - 1), false);
	} else if (initStyle == SCE_ZIG_BYTESTRINGR) {
		ResumeRawString(styler, pos, max, styler.GetLineState(styler.GetLine(pos) - 1), true);
	}

	while (pos < max) {
		int c = styler.SafeGetCharAt(pos, '\0');
		int n = styler.SafeGetCharAt(pos + 1, '\0');
		int n2 = styler.SafeGetCharAt(pos + 2, '\0');
        int n3 = styler.SafeGetCharAt(pos+ + 3, '\0');

		if (pos == 0 && c == '#' && n == '!' && n2 != '[') {
			pos += 2;
			ResumeLineComment(styler, pos, max, NotDocComment);
		} else if (IsWhitespace(c)) {
			ScanWhitespace(styler, pos, max);
		} else if (c == '/' && (n == '/' || n == '*')) {
			ScanComments(styler, pos, max);
		} else if (c == '\\' && n == '\\') {
			ScanMultiLineString(styler, pos, max, true);
		} else if (c == 'b' && n == '"') {
			pos += 2;
			ResumeString(styler, pos, max, true);
		} else if (c == 'b' && n == '\'') {
			pos++;
			ScanCharacterLiteralOrLifetime(styler, pos, true);
		} else if (IsBuiltInStart(c)) {
			ScanBuiltin(styler, pos, keywords);
		} else if (IsIdentifierStart(c)) {
			ScanIdentifier(styler, pos, keywords);
		} else if (IsADigit(c)) {
			ScanNumber(styler, pos);
		} else if(IsFourCharOperator(c,n,n2,n3)){
            pos += 4;
            styler.ColourTo(pos-1,SCE_ZIG_OPERATOR);
        } else if (IsThreeCharOperator(c, n, n2)) {
			pos += 3;
			styler.ColourTo(pos - 1, SCE_ZIG_OPERATOR);
		} else if (IsTwoCharOperator(c, n)) {
			pos += 2;
			styler.ColourTo(pos - 1, SCE_ZIG_OPERATOR);
		} else if (IsOneCharOperator(c)) {
			pos++;
			styler.ColourTo(pos - 1, SCE_ZIG_OPERATOR);
		} else if (IsEnclosingChar(c)) {
			pos++;
			styler.ColourTo(pos - 1, SCE_ZIG_ENCLOSING);
		}  else if (c == '\'') {
			ScanCharacterLiteralOrLifetime(styler, pos, false);
		} else if (c == '"') {
			pos++;
			ResumeString(styler, pos, max, false);
		} else {
			pos++;
			styler.ColourTo(pos - 1, SCE_ZIG_LEXERROR);
		}
	}
	styler.ColourTo(pos - 1, SCE_ZIG_DEFAULT);
	styler.Flush();
}

void SCI_METHOD LexerZig::Fold(Sci_PositionU startPos, Sci_Position length, int initStyle, IDocument *pAccess) {

	if (!options.fold)
		return;

	LexAccessor styler(pAccess);

	Sci_PositionU endPos = startPos + length;
	int visibleChars = 0;
	bool inLineComment = false;
	Sci_Position lineCurrent = styler.GetLine(startPos);
	int levelCurrent = SC_FOLDLEVELBASE;
	if (lineCurrent > 0)
		levelCurrent = styler.LevelAt(lineCurrent-1) >> 16;
	Sci_PositionU lineStartNext = styler.LineStart(lineCurrent+1);
	int levelMinCurrent = levelCurrent;
	int levelNext = levelCurrent;
	char chNext = styler[startPos];
	int styleNext = styler.StyleAt(startPos);
	int style = initStyle;
	const bool userDefinedFoldMarkers = !options.foldExplicitStart.empty() && !options.foldExplicitEnd.empty();
	for (Sci_PositionU i = startPos; i < endPos; i++) {
		char ch = chNext;
		chNext = styler.SafeGetCharAt(i + 1);
		int stylePrev = style;
		style = styleNext;
		styleNext = styler.StyleAt(i + 1);
		bool atEOL = i == (lineStartNext-1);
		if ((style == SCE_ZIG_COMMENTLINE) || (style == SCE_ZIG_COMMENTLINEDOC))
			inLineComment = true;
		if (options.foldComment && options.foldCommentMultiline && IsStreamCommentStyle(style) && !inLineComment) {
			if (!IsStreamCommentStyle(stylePrev)) {
				levelNext++;
			} else if (!IsStreamCommentStyle(styleNext) && !atEOL) {
				// Comments don't end at end of line and the next character may be unstyled.
				levelNext--;
			}
		}
		if (options.foldComment && options.foldCommentExplicit && ((style == SCE_ZIG_COMMENTLINE) || options.foldExplicitAnywhere)) {
			if (userDefinedFoldMarkers) {
				if (styler.Match(i, options.foldExplicitStart.c_str())) {
					levelNext++;
				} else if (styler.Match(i, options.foldExplicitEnd.c_str())) {
					levelNext--;
				}
			} else {
				if ((ch == '/') && (chNext == '/')) {
					char chNext2 = styler.SafeGetCharAt(i + 2);
					if (chNext2 == '{') {
						levelNext++;
					} else if (chNext2 == '}') {
						levelNext--;
					}
				}
			}
		}
		if (options.foldSyntaxBased && (style == SCE_ZIG_OPERATOR)) {
			if (ch == '{') {
				// Measure the minimum before a '{' to allow
				// folding on "} else {"
				if (levelMinCurrent > levelNext) {
					levelMinCurrent = levelNext;
				}
				levelNext++;
			} else if (ch == '}') {
				levelNext--;
			}
		}
		if (!IsASpace(ch))
			visibleChars++;
		if (atEOL || (i == endPos-1)) {
			int levelUse = levelCurrent;
			if (options.foldSyntaxBased && options.foldAtElse) {
				levelUse = levelMinCurrent;
			}
			int lev = levelUse | levelNext << 16;
			if (visibleChars == 0 && options.foldCompact)
				lev |= SC_FOLDLEVELWHITEFLAG;
			if (levelUse < levelNext)
				lev |= SC_FOLDLEVELHEADERFLAG;
			if (lev != styler.LevelAt(lineCurrent)) {
				styler.SetLevel(lineCurrent, lev);
			}
			lineCurrent++;
			lineStartNext = styler.LineStart(lineCurrent+1);
			levelCurrent = levelNext;
			levelMinCurrent = levelCurrent;
			if (atEOL && (i == static_cast<Sci_PositionU>(styler.Length()-1))) {
				// There is an empty line at end of file so give it same level and empty
				styler.SetLevel(lineCurrent, (levelCurrent | levelCurrent << 16) | SC_FOLDLEVELWHITEFLAG);
			}
			visibleChars = 0;
			inLineComment = false;
		}
	}
}

LexerModule lmZig(SCLEX_ZIG, LexerZig::LexerFactoryZig, "zig", zigWordLists);
