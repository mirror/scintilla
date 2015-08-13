// Scintilla source code edit control
/** @file PerLine.h
 ** Manages data associated with each line of the document
 **/
// Copyright 1998-2009 by Neil Hodgson <neilh@scintilla.org>
// The License.txt file describes the conditions under which this software may be distributed.

#ifndef PERLINE_H
#define PERLINE_H

#ifdef SCI_NAMESPACE
namespace Scintilla {
#endif

/**
 * This holds the marker identifier and the marker type to display.
 * MarkerHandleNumbers are members of lists.
 */
struct MarkerHandleNumber {
	int handle;
	int number;
	MarkerHandleNumber *next;
};

/**
 * A marker handle set contains any number of MarkerHandleNumbers.
 */
class MarkerHandleSet {
	MarkerHandleNumber *root;

public:
	MarkerHandleSet();
	~MarkerHandleSet();
	int Length() const;
	int MarkValue() const;	///< Bit set of marker numbers.
	bool Contains(int handle) const;
	bool InsertHandle(int handle, int markerNum);
	void RemoveHandle(int handle);
	bool RemoveNumber(int markerNum, bool all);
	void CombineWith(MarkerHandleSet *other);
};

class LineMarkers : public PerLine {
	SplitVector<MarkerHandleSet *> markers;
	/// Handles are allocated sequentially and should never have to be reused as 32 bit ints are very big.
	int handleCurrent;
public:
	LineMarkers() : handleCurrent(0) {
	}
	virtual ~LineMarkers();
	virtual void Init();
	virtual void InsertLine(Sci::Position line);
	virtual void RemoveLine(Sci::Position line);

	int MarkValue(Sci::Position line);
	Sci::Position MarkerNext(Sci::Position lineStart, int mask) const;
	int AddMark(Sci::Position line, int marker, Sci::Position lines);
	void MergeMarkers(Sci::Position pos);
	bool DeleteMark(Sci::Position line, int markerNum, bool all);
	void DeleteMarkFromHandle(int markerHandle);
	Sci::Position LineFromHandle(int markerHandle);
};

class LineLevels : public PerLine {
	SplitVector<int> levels;
public:
	virtual ~LineLevels();
	virtual void Init();
	virtual void InsertLine(Sci::Position line);
	virtual void RemoveLine(Sci::Position line);

	void ExpandLevels(Sci::Position sizeNew=-1);
	void ClearLevels();
	int SetLevel(Sci::Position line, int level, Sci::Position lines);
	int GetLevel(Sci::Position line) const;
};

class LineState : public PerLine {
	SplitVector<int> lineStates;
public:
	LineState() {
	}
	virtual ~LineState();
	virtual void Init();
	virtual void InsertLine(Sci::Position line);
	virtual void RemoveLine(Sci::Position line);

	int SetLineState(Sci::Position line, int state);
	int GetLineState(Sci::Position line);
	Sci::Position GetMaxLineState() const;
};

class LineAnnotation : public PerLine {
	SplitVector<char *> annotations;
public:
	LineAnnotation() {
	}
	virtual ~LineAnnotation();
	virtual void Init();
	virtual void InsertLine(Sci::Position line);
	virtual void RemoveLine(Sci::Position line);

	bool MultipleStyles(Sci::Position line) const;
	int Style(Sci::Position line) const;
	const char *Text(Sci::Position line) const;
	const unsigned char *Styles(Sci::Position line) const;
	void SetText(Sci::Position line, const char *text);
	void ClearAll();
	void SetStyle(Sci::Position line, int style);
	void SetStyles(Sci::Position line, const unsigned char *styles);
	int Length(Sci::Position line) const;
	int Lines(Sci::Position line) const;
};

typedef std::vector<int> TabstopList;

class LineTabstops : public PerLine {
	SplitVector<TabstopList *> tabstops;
public:
	LineTabstops() {
	}
	virtual ~LineTabstops();
	virtual void Init();
	virtual void InsertLine(Sci::Position line);
	virtual void RemoveLine(Sci::Position line);

	bool ClearTabstops(Sci::Position line);
	bool AddTabstop(Sci::Position line, int x);
	int GetNextTabstop(Sci::Position line, int x) const;
};

#ifdef SCI_NAMESPACE
}
#endif

#endif
