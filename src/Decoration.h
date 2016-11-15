/** @file Decoration.h
 ** Visual elements added over text.
 **/
// Copyright 1998-2007 by Neil Hodgson <neilh@scintilla.org>
// The License.txt file describes the conditions under which this software may be distributed.

#ifndef DECORATION_H
#define DECORATION_H

#ifdef SCI_NAMESPACE
namespace Scintilla {
#endif

class Decoration {
public:
	Decoration *next;
	RunStyles rs;
	int indicator;

	explicit Decoration(int indicator_);
	~Decoration();

	bool Empty() const;
};

class DecorationList {
	int currentIndicator;
	int currentValue;
	Decoration *current;
	Sci::Position lengthDocument;
	Decoration *DecorationFromIndicator(int indicator);
	Decoration *Create(int indicator, Sci::Position length);
	void Delete(int indicator);
	void DeleteAnyEmpty();
public:
	Decoration *root;
	bool clickNotified;

	DecorationList();
	~DecorationList();

	void SetCurrentIndicator(int indicator);
	int GetCurrentIndicator() const { return currentIndicator; }

	void SetCurrentValue(int value);
	int GetCurrentValue() const { return currentValue; }

	// Returns true if some values may have changed
	bool FillRange(Sci::Position &position, int value, Sci::Position &fillLength);

	void InsertSpace(Sci::Position position, Sci::Position insertLength);
	void DeleteRange(Sci::Position position, Sci::Position deleteLength);

	int AllOnFor(Sci::Position position) const;
	int ValueAt(int indicator, Sci::Position position);
	Sci::Position Start(int indicator, Sci::Position position);
	Sci::Position End(int indicator, Sci::Position position);
};

#ifdef SCI_NAMESPACE
}
#endif

#endif
