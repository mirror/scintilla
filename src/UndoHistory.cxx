// Scintilla source code edit control
/** @file UndoHistory.cxx
 ** Manages undo for the document.
 **/
// Copyright 1998-2024 by Neil Hodgson <neilh@scintilla.org>
// The License.txt file describes the conditions under which this software may be distributed.

#include <cstddef>
#include <cstdlib>
#include <cassert>
#include <cstring>
#include <cstdio>
#include <cstdarg>
#include <climits>

#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include <optional>
#include <algorithm>
#include <memory>

#include "ScintillaTypes.h"

#include "Debugging.h"

#include "Position.h"
#include "SplitVector.h"
#include "Partitioning.h"
#include "RunStyles.h"
#include "SparseVector.h"
#include "ChangeHistory.h"
#include "CellBuffer.h"
#include "UndoHistory.h"

namespace Scintilla::Internal {

UndoAction::UndoAction() noexcept = default;

void UndoAction::Create(ActionType at_, Sci::Position position_, const char *data_, Sci::Position lenData_, bool mayCoalesce_) {
	position = position_;
	at = at_;
	mayCoalesce = mayCoalesce_;
	lenData = lenData_;
	data = nullptr;
	if (lenData_) {
		data = std::make_unique<char[]>(lenData_);
		memcpy(&data[0], data_, lenData_);
	}
}

void UndoAction::Clear() noexcept {
	data = nullptr;
	lenData = 0;
}

// The undo history stores a sequence of user operations that represent the user's view of the
// commands executed on the text.
// Each user operation contains a sequence of text insertion and text deletion actions.
// All the user operations are stored in a list of individual actions with 'start' actions used
// as delimiters between user operations.
// Initially there is one start action in the history.
// As each action is performed, it is recorded in the history. The action may either become
// part of the current user operation or may start a new user operation. If it is to be part of the
// current operation, then it overwrites the current last action. If it is to be part of a new
// operation, it is appended after the current last action.
// After writing the new action, a new start action is appended at the end of the history.
// The decision of whether to start a new user operation is based upon two factors. If a
// compound operation has been explicitly started by calling BeginUndoAction and no matching
// EndUndoAction (these calls nest) has been called, then the action is coalesced into the current
// operation. If there is no outstanding BeginUndoAction call then a new operation is started
// unless it looks as if the new action is caused by the user typing or deleting a stream of text.
// Sequences that look like typing or deletion are coalesced into a single user operation.

UndoHistory::UndoHistory() {

	actions.resize(3);
	maxAction = 0;
	currentAction = 0;
	undoSequenceDepth = 0;
	savePoint = 0;
	tentativePoint = -1;

	actions[currentAction].Create(ActionType::start);
}

void UndoHistory::EnsureUndoRoom() {
	// Have to test that there is room for 2 more actions in the array
	// as two actions may be created by the calling function
	if (static_cast<size_t>(currentAction) >= (actions.size() - 2)) {
		// Run out of undo nodes so extend the array
		actions.resize(actions.size() * 2);
	}
}

const char *UndoHistory::AppendAction(ActionType at, Sci::Position position, const char *data, Sci::Position lengthData,
	bool &startSequence, bool mayCoalesce) {
	EnsureUndoRoom();
	//Platform::DebugPrintf("%% %d action %d %d %d\n", at, position, lengthData, currentAction);
	//Platform::DebugPrintf("^ %d action %d %d\n", actions[currentAction - 1].at,
	//	actions[currentAction - 1].position, actions[currentAction - 1].lenData);
	if (currentAction < savePoint) {
		savePoint = -1;
		if (!detach) {
			detach = currentAction;
		}
	} else if (detach && (*detach > currentAction)) {
		detach = currentAction;
	}
	const int oldCurrentAction = currentAction;
	if (currentAction >= 1) {
		if (0 == undoSequenceDepth) {
			// Top level actions may not always be coalesced
			ptrdiff_t targetAct = -1;
			const UndoAction *actPrevious = &(actions[currentAction + targetAct]);
			// Container actions may forward the coalesce state of Scintilla Actions.
			while ((actPrevious->at == ActionType::container) && actPrevious->mayCoalesce) {
				targetAct--;
				actPrevious = &(actions[currentAction + targetAct]);
			}
			// See if current action can be coalesced into previous action
			// Will work if both are inserts or deletes and position is same
			if ((currentAction == savePoint) || (currentAction == tentativePoint)) {
				currentAction++;
			} else if (!actions[currentAction].mayCoalesce) {
				// Not allowed to coalesce if this set
				currentAction++;
			} else if (!mayCoalesce || !actPrevious->mayCoalesce) {
				currentAction++;
			} else if (at == ActionType::container || actions[currentAction].at == ActionType::container) {
				;	// A coalescible containerAction
			} else if ((at != actPrevious->at) && (actPrevious->at != ActionType::start)) {
				currentAction++;
			} else if ((at == ActionType::insert) &&
			           (position != (actPrevious->position + actPrevious->lenData))) {
				// Insertions must be immediately after to coalesce
				currentAction++;
			} else if (at == ActionType::remove) {
				if ((lengthData == 1) || (lengthData == 2)) {
					if ((position + lengthData) == actPrevious->position) {
						; // Backspace -> OK
					} else if (position == actPrevious->position) {
						; // Delete -> OK
					} else {
						// Removals must be at same position to coalesce
						currentAction++;
					}
				} else {
					// Removals must be of one character to coalesce
					currentAction++;
				}
			} else {
				// Action coalesced.
			}

		} else {
			// Actions not at top level are always coalesced unless this is after return to top level
			if (!actions[currentAction].mayCoalesce)
				currentAction++;
		}
	} else {
		currentAction++;
	}
	startSequence = oldCurrentAction != currentAction;
	const int actionWithData = currentAction;
	actions[currentAction].Create(at, position, data, lengthData, mayCoalesce);
	currentAction++;
	actions[currentAction].Create(ActionType::start);
	maxAction = currentAction;
	return actions[actionWithData].data.get();
}

void UndoHistory::BeginUndoAction() {
	EnsureUndoRoom();
	if (undoSequenceDepth == 0) {
		if (actions[currentAction].at != ActionType::start) {
			currentAction++;
			actions[currentAction].Create(ActionType::start);
			maxAction = currentAction;
		}
		actions[currentAction].mayCoalesce = false;
	}
	undoSequenceDepth++;
}

void UndoHistory::EndUndoAction() {
	PLATFORM_ASSERT(undoSequenceDepth > 0);
	EnsureUndoRoom();
	undoSequenceDepth--;
	if (0 == undoSequenceDepth) {
		if (actions[currentAction].at != ActionType::start) {
			currentAction++;
			actions[currentAction].Create(ActionType::start);
			maxAction = currentAction;
		}
		actions[currentAction].mayCoalesce = false;
	}
}

void UndoHistory::DropUndoSequence() noexcept {
	undoSequenceDepth = 0;
}

void UndoHistory::DeleteUndoHistory() {
	for (int i = 1; i < maxAction; i++)
		actions[i].Clear();
	maxAction = 0;
	currentAction = 0;
	actions[currentAction].Create(ActionType::start);
	savePoint = 0;
	tentativePoint = -1;
}

void UndoHistory::SetSavePoint() noexcept {
	savePoint = currentAction;
	detach.reset();
}

bool UndoHistory::IsSavePoint() const noexcept {
	return savePoint == currentAction;
}

bool UndoHistory::BeforeSavePoint() const noexcept {
	return (savePoint < 0) || (savePoint > currentAction);
}

bool UndoHistory::BeforeReachableSavePoint() const noexcept {
	return (savePoint >= 0) && !detach && (savePoint > currentAction);
}

bool UndoHistory::AfterSavePoint() const noexcept {
	return (savePoint >= 0) && (savePoint <= currentAction);
}

bool UndoHistory::AfterDetachPoint() const noexcept {
	return detach && (*detach < currentAction);
}

void UndoHistory::TentativeStart() noexcept {
	tentativePoint = currentAction;
}

void UndoHistory::TentativeCommit() noexcept {
	tentativePoint = -1;
	// Truncate undo history
	maxAction = currentAction;
}

bool UndoHistory::TentativeActive() const noexcept {
	return tentativePoint >= 0;
}

int UndoHistory::TentativeSteps() noexcept {
	// Drop any trailing startAction
	if (actions[currentAction].at == ActionType::start && currentAction > 0)
		currentAction--;
	if (tentativePoint >= 0)
		return currentAction - tentativePoint;
	else
		return -1;
}

bool UndoHistory::CanUndo() const noexcept {
	return (currentAction > 0) && (maxAction > 0);
}

int UndoHistory::StartUndo() noexcept {
	// Drop any trailing startAction
	if (actions[currentAction].at == ActionType::start && currentAction > 0)
		currentAction--;

	// Count the steps in this action
	int act = currentAction;
	while (actions[act].at != ActionType::start && act > 0) {
		act--;
	}
	return currentAction - act;
}

const UndoAction &UndoHistory::GetUndoStep() const noexcept {
	return actions[currentAction];
}

void UndoHistory::CompletedUndoStep() noexcept {
	currentAction--;
}

bool UndoHistory::CanRedo() const noexcept {
	return maxAction > currentAction;
}

int UndoHistory::StartRedo() noexcept {
	// Drop any leading startAction
	if (currentAction < maxAction && actions[currentAction].at == ActionType::start)
		currentAction++;

	// Count the steps in this action
	int act = currentAction;
	while (act < maxAction && actions[act].at != ActionType::start) {
		act++;
	}
	return act - currentAction;
}

const UndoAction &UndoHistory::GetRedoStep() const noexcept {
	return actions[currentAction];
}

void UndoHistory::CompletedRedoStep() noexcept {
	currentAction++;
}

}
