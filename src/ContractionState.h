// Scintilla source code edit control
/** @file ContractionState.h
 ** Manages visibility of lines for folding and wrapping.
 **/
// Copyright 1998-2007 by Neil Hodgson <neilh@scintilla.org>
// The License.txt file describes the conditions under which this software may be distributed.

#ifndef CONTRACTIONSTATE_H
#define CONTRACTIONSTATE_H

#ifdef SCI_NAMESPACE
namespace Scintilla {
#endif

/**
 */
class ContractionState {
	// These contain 1 element for every document line.
	RunStyles *visible;
	RunStyles *expanded;
	RunStyles *heights;
	Partitioning *displayLines;
	Sci::Position linesInDocument;

	void EnsureData();

	bool OneToOne() const {
		// True when each document line is exactly one display line so need for
		// complex data structures.
		return visible == 0;
	}

public:
	ContractionState();
	virtual ~ContractionState();

	void Clear();

	Sci::Position LinesInDoc() const;
	Sci::Position LinesDisplayed() const;
	Sci::Position DisplayFromDoc(Sci::Position lineDoc) const;
	Sci::Position DisplayLastFromDoc(Sci::Position lineDoc) const;
	Sci::Position DocFromDisplay(Sci::Position lineDisplay) const;

	void InsertLine(Sci::Position lineDoc);
	void InsertLines(Sci::Position lineDoc, Sci::Position lineCount);
	void DeleteLine(Sci::Position lineDoc);
	void DeleteLines(Sci::Position lineDoc, Sci::Position lineCount);

	bool GetVisible(Sci::Position lineDoc) const;
	bool SetVisible(Sci::Position lineDocStart, Sci::Position lineDocEnd, bool isVisible);
	bool HiddenLines() const;

	bool GetExpanded(Sci::Position lineDoc) const;
	bool SetExpanded(Sci::Position lineDoc, bool isExpanded);
	Sci::Position ContractedNext(Sci::Position lineDocStart) const;

	int GetHeight(Sci::Position lineDoc) const;
	bool SetHeight(Sci::Position lineDoc, int height);

	void ShowAll();
	void Check() const;
};

#ifdef SCI_NAMESPACE
}
#endif

#endif
