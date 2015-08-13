// Scintilla source code edit control
/** @file CellBuffer.h
 ** Manages the text of the document.
 **/
// Copyright 1998-2004 by Neil Hodgson <neilh@scintilla.org>
// The License.txt file describes the conditions under which this software may be distributed.

#ifndef CELLBUFFER_H
#define CELLBUFFER_H

#ifdef SCI_NAMESPACE
namespace Scintilla {
#endif

// Interface to per-line data that wants to see each line insertion and deletion
class PerLine {
public:
	virtual ~PerLine() {}
	virtual void Init()=0;
	virtual void InsertLine(Sci::Position line)=0;
	virtual void RemoveLine(Sci::Position line)=0;
};

/**
 * The line vector contains information about each of the lines in a cell buffer.
 */
class LineVector {

	Partitioning starts;
	PerLine *perLine;

public:

	LineVector();
	~LineVector();
	void Init();
	void SetPerLine(PerLine *pl);

	void InsertText(Sci::Position line, Sci::Position delta);
	void InsertLine(Sci::Position line, Sci::Position position, bool lineStart);
	void SetLineStart(Sci::Position line, Sci::Position position);
	void RemoveLine(Sci::Position line);
	Sci::Position Lines() const {
		return starts.Partitions();
	}
	Sci::Position LineFromPosition(Sci::Position pos) const;
	Sci::Position LineStart(Sci::Position line) const {
		return starts.PositionFromPartition(line);
	}

	int MarkValue(Sci::Position line);
	int AddMark(Sci::Position line, int marker);
	void MergeMarkers(Sci::Position pos);
	void DeleteMark(Sci::Position line, int markerNum, bool all);
	void DeleteMarkFromHandle(int markerHandle);
	Sci::Position LineFromHandle(int markerHandle);

	void ClearLevels();
	int SetLevel(Sci::Position line, int level);
	int GetLevel(Sci::Position line);

	int SetLineState(Sci::Position line, int state);
	int GetLineState(Sci::Position line);
	Sci::Position GetMaxLineState();

};

enum actionType { insertAction, removeAction, startAction, containerAction };

/**
 * Actions are used to store all the information required to perform one undo/redo step.
 */
class Action {
public:
	actionType at;
	Sci::Position position;
	char *data;
	Sci::Position lenData;
	bool mayCoalesce;

	Action();
	~Action();
	void Create(actionType at_, Sci::Position position_=0, const char *data_=0, Sci::Position lenData_=0, bool mayCoalesce_=true);
	void Destroy();
	void Grab(Action *source);
};

/**
 *
 */
class UndoHistory {
	Action *actions;
	int lenActions;
	int maxAction;
	int currentAction;
	int undoSequenceDepth;
	int savePoint;
	int tentativePoint;

	void EnsureUndoRoom();

	// Private so UndoHistory objects can not be copied
	UndoHistory(const UndoHistory &);

public:
	UndoHistory();
	~UndoHistory();

	const char *AppendAction(actionType at, Sci::Position position, const char *data, Sci::Position length, bool &startSequence, bool mayCoalesce=true);

	void BeginUndoAction();
	void EndUndoAction();
	void DropUndoSequence();
	void DeleteUndoHistory();

	/// The save point is a marker in the undo stack where the container has stated that
	/// the buffer was saved. Undo and redo can move over the save point.
	void SetSavePoint();
	bool IsSavePoint() const;

	// Tentative actions are used for input composition so that it can be undone cleanly
	void TentativeStart();
	void TentativeCommit();
	bool TentativeActive() const { return tentativePoint >= 0; }
	int TentativeSteps();

	/// To perform an undo, StartUndo is called to retrieve the number of steps, then UndoStep is
	/// called that many times. Similarly for redo.
	bool CanUndo() const;
	int StartUndo();
	const Action &GetUndoStep() const;
	void CompletedUndoStep();
	bool CanRedo() const;
	int StartRedo();
	const Action &GetRedoStep() const;
	void CompletedRedoStep();
};

/**
 * Holder for an expandable array of characters that supports undo and line markers.
 * Based on article "Data Structures in a Bit-Mapped Text Editor"
 * by Wilfred J. Hansen, Byte January 1987, page 183.
 */
class CellBuffer {
private:
	SplitVector<char> substance;
	SplitVector<char> style;
	bool readOnly;
	int utf8LineEnds;

	bool collectingUndo;
	UndoHistory uh;

	LineVector lv;

	bool UTF8LineEndOverlaps(Sci::Position position) const;
	void ResetLineEnds();
	/// Actions without undo
	void BasicInsertString(Sci::Position position, const char *s, Sci::Position insertLength);
	void BasicDeleteChars(Sci::Position position, Sci::Position deleteLength);

public:

	CellBuffer();
	~CellBuffer();

	/// Retrieving positions outside the range of the buffer works and returns 0
	char CharAt(Sci::Position position) const;
	void GetCharRange(char *buffer, Sci::Position position, Sci::Position lengthRetrieve) const;
	char StyleAt(Sci::Position position) const;
	void GetStyleRange(unsigned char *buffer, Sci::Position position, Sci::Position lengthRetrieve) const;
	const char *BufferPointer();
	const char *RangePointer(Sci::Position position, Sci::Position rangeLength);
	Sci::Position GapPosition() const;

	Sci::Position Length() const;
	void Allocate(Sci::Position newSize);
	int GetLineEndTypes() const { return utf8LineEnds; }
	void SetLineEndTypes(int utf8LineEnds_);
	void SetPerLine(PerLine *pl);
	Sci::Position Lines() const;
	Sci::Position LineStart(Sci::Position line) const;
	Sci::Position LineFromPosition(Sci::Position pos) const { return lv.LineFromPosition(pos); }
	void InsertLine(Sci::Position line, Sci::Position position, bool lineStart);
	void RemoveLine(Sci::Position line);
	const char *InsertString(Sci::Position position, const char *s, Sci::Position insertLength, bool &startSequence);

	/// Setting styles for positions outside the range of the buffer is safe and has no effect.
	/// @return true if the style of a character is changed.
	bool SetStyleAt(Sci::Position position, char styleValue);
	bool SetStyleFor(Sci::Position position, Sci::Position length, char styleValue);

	const char *DeleteChars(Sci::Position position, Sci::Position deleteLength, bool &startSequence);

	bool IsReadOnly() const;
	void SetReadOnly(bool set);

	/// The save point is a marker in the undo stack where the container has stated that
	/// the buffer was saved. Undo and redo can move over the save point.
	void SetSavePoint();
	bool IsSavePoint() const;

	void TentativeStart();
	void TentativeCommit();
	bool TentativeActive() const;
	int TentativeSteps();

	bool SetUndoCollection(bool collectUndo);
	bool IsCollectingUndo() const;
	void BeginUndoAction();
	void EndUndoAction();
	void AddUndoAction(Sci::Position token, bool mayCoalesce);
	void DeleteUndoHistory();

	/// To perform an undo, StartUndo is called to retrieve the number of steps, then UndoStep is
	/// called that many times. Similarly for redo.
	bool CanUndo() const;
	int StartUndo();
	const Action &GetUndoStep() const;
	void PerformUndoStep();
	bool CanRedo() const;
	int StartRedo();
	const Action &GetRedoStep() const;
	void PerformRedoStep();
};

#ifdef SCI_NAMESPACE
}
#endif

#endif
