// Scintilla source code edit control
/** @file UndoHistory.h
 ** Manages undo for the document.
 **/
// Copyright 1998-2024 by Neil Hodgson <neilh@scintilla.org>
// The License.txt file describes the conditions under which this software may be distributed.

#ifndef UNDOHISTORY_H
#define UNDOHISTORY_H

namespace Scintilla::Internal {

class UndoActionType {
public:
	ActionType at : 4;
	bool mayCoalesce : 1;
	UndoActionType() noexcept;
};

struct UndoActions {
	std::vector<UndoActionType> types;
	std::vector<Sci::Position> positions;
	std::vector<Sci::Position> lengths;

	void resize(size_t length);
	[[nodiscard]] size_t size() const noexcept;
	void Create(size_t index, ActionType at_, Sci::Position position_ = 0, Sci::Position lenData_ = 0, bool mayCoalesce_ = true) noexcept;
};

class ScrapStack {
	std::string stack;
	size_t current = 0;
public:
	const char *Push(const char *text, size_t length);
	void SetCurrent(size_t position) noexcept;
	void MoveForward(size_t length) noexcept;
	void MoveBack(size_t length) noexcept;
	[[nodiscard]] const char *CurrentText() const noexcept;
	[[nodiscard]] const char *TextAt(size_t position) const noexcept;
};

/**
 *
 */
class UndoHistory {
	UndoActions actions;
	int maxAction = 0;
	int currentAction = 0;
	int undoSequenceDepth = 0;
	int savePoint = 0;
	int tentativePoint = -1;
	std::optional<int> detach;
	std::unique_ptr<ScrapStack> scraps;

	void EnsureUndoRoom();

public:
	UndoHistory();
	~UndoHistory() noexcept;

	const char *AppendAction(ActionType at, Sci::Position position, const char *data, Sci::Position lengthData, bool &startSequence, bool mayCoalesce=true);

	void BeginUndoAction();
	void EndUndoAction();
	void DropUndoSequence() noexcept;
	void DeleteUndoHistory() noexcept;

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
	Action GetUndoStep() const noexcept;
	void CompletedUndoStep() noexcept;
	bool CanRedo() const noexcept;
	int StartRedo() noexcept;
	Action GetRedoStep() const noexcept;
	void CompletedRedoStep() noexcept;
};

}

#endif
