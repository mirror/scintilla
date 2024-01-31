// Scintilla source code edit control
/** @file UndoHistory.h
 ** Manages undo for the document.
 **/
// Copyright 1998-2024 by Neil Hodgson <neilh@scintilla.org>
// The License.txt file describes the conditions under which this software may be distributed.

#ifndef UNDOHISTORY_H
#define UNDOHISTORY_H

namespace Scintilla::Internal {

class UndoAction {
public:
	ActionType at = ActionType::insert;
	bool mayCoalesce = false;
	Sci::Position position = 0;
	std::unique_ptr<char[]> data;
	Sci::Position lenData = 0;

	UndoAction() noexcept;
	void Create(ActionType at_, Sci::Position position_ = 0, const char *data_ = nullptr, Sci::Position lenData_ = 0, bool mayCoalesce_ = true);
	void Clear() noexcept;
};

/**
 *
 */
class UndoHistory {
	std::vector<UndoAction> actions;
	int maxAction;
	int currentAction;
	int undoSequenceDepth;
	int savePoint;
	int tentativePoint;
	std::optional<int> detach;

	void EnsureUndoRoom();

public:
	UndoHistory();

	const char *AppendAction(ActionType at, Sci::Position position, const char *data, Sci::Position lengthData, bool &startSequence, bool mayCoalesce=true);

	void BeginUndoAction();
	void EndUndoAction();
	void DropUndoSequence() noexcept;
	void DeleteUndoHistory();

	/// The save point is a marker in the undo stack where the container has stated that
	/// the buffer was saved. Undo and redo can move over the save point.
	void SetSavePoint() noexcept;
	bool IsSavePoint() const noexcept;
	bool BeforeSavePoint() const noexcept;
	bool BeforeReachableSavePoint() const noexcept;
	bool AfterSavePoint() const noexcept;
	bool AfterDetachPoint() const noexcept;

	// Tentative actions are used for input composition so that it can be undone cleanly
	void TentativeStart() noexcept;
	void TentativeCommit() noexcept;
	bool TentativeActive() const noexcept;
	int TentativeSteps() noexcept;

	/// To perform an undo, StartUndo is called to retrieve the number of steps, then UndoStep is
	/// called that many times. Similarly for redo.
	bool CanUndo() const noexcept;
	int StartUndo() noexcept;
	const UndoAction &GetUndoStep() const noexcept;
	void CompletedUndoStep() noexcept;
	bool CanRedo() const noexcept;
	int StartRedo() noexcept;
	const UndoAction &GetRedoStep() const noexcept;
	void CompletedRedoStep() noexcept;
};

}

#endif
