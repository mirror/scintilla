// Scintilla source code edit control
/** @file UndoHistory.cxx
 ** Manages undo for the document.
 **/
// Copyright 1998-2024 by Neil Hodgson <neilh@scintilla.org>
// The License.txt file describes the conditions under which this software may be distributed.

#include <cstddef>
#include <cstdlib>
#include <cstdint>
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

template <typename T>
void VectorTruncate(std::vector<T> &v, size_t length) noexcept {
	v.erase(v.begin() + length, v.end());
}

constexpr size_t byteMask = UINT8_MAX;
constexpr size_t byteBits = 8;
constexpr size_t maxElementSize = 8;

size_t ReadValue(const uint8_t *bytes, size_t length) noexcept {
	size_t value = 0;
	for (size_t i = 0; i < length; i++) {
		value = (value << byteBits) + bytes[i];
	}
	return value;
}

void WriteValue(uint8_t *bytes, size_t length, size_t value) noexcept {
	for (size_t i = 0; i < length; i++) {
		bytes[length - i - 1] = value & byteMask;
		value = value >> byteBits;
	}
}

size_t ScaledVector::Size() const noexcept {
	return bytes.size() / elementSize;
}

size_t ScaledVector::ValueAt(size_t index) const noexcept {
	return ReadValue(bytes.data() + index * elementSize, elementSize);
}

intptr_t ScaledVector::SignedValueAt(size_t index) const noexcept {
	return ReadValue(bytes.data() + index * elementSize, elementSize);
}

constexpr size_t MaxForBytes(size_t length) noexcept {
	constexpr size_t one = 1;
	return (one << (byteBits * length)) - 1;
}

constexpr size_t SizeForValue(size_t value) noexcept {
	for (size_t i = 1; i < maxElementSize; i++) {
		if (value <= MaxForBytes(i)) {
			return i;
		}
	}
	return 1;
}

void ScaledVector::SetValueAt(size_t index, size_t value) {
	// Check if value fits, if not then expand
	if (value > elementMax) {
		const size_t elementSizeNew = SizeForValue(value);
		const size_t length = bytes.size() / elementSize;
		std::vector<uint8_t> bytesNew(elementSizeNew * length);
		for (size_t i = 0; i < length; i++) {
			const uint8_t *source = bytes.data() + i * elementSize;
			uint8_t *destination = bytesNew.data() + (i+1) * elementSizeNew - elementSize;
			memcpy(destination, source, elementSize);
		}
		std::swap(bytes, bytesNew);
		elementSize = elementSizeNew;
		elementMax = MaxForBytes(elementSize);
	}
	WriteValue(bytes.data() + index * elementSize, elementSize, value);
}

void ScaledVector::ClearValueAt(size_t index) noexcept {
	// 0 fits in any size element so no expansion needed so no exceptions 
	WriteValue(bytes.data() + index * elementSize, elementSize, 0);
}

void ScaledVector::Clear() noexcept {
	bytes.clear();
}

void ScaledVector::Truncate(size_t length) noexcept {
	VectorTruncate(bytes, length * elementSize);
	assert(bytes.size() == length * elementSize);
}

void ScaledVector::ReSize(size_t length) {
	bytes.resize(length * elementSize);
}

void ScaledVector::PushBack() {
	ReSize(Size() + 1);
}

size_t ScaledVector::SizeInBytes() const noexcept {
	return bytes.size();
}

UndoActionType::UndoActionType() noexcept : at(ActionType::insert), mayCoalesce(false) {
}

UndoActions::UndoActions() noexcept = default;

void UndoActions::resize(size_t length) {
	types.resize(length);
	positions.ReSize(length);
	lengths.ReSize(length);
}

size_t UndoActions::size() const noexcept {
	return types.size();
}

void UndoActions::CreateStart(size_t index) noexcept {
	types[index].at = ActionType::start;
	types[index].mayCoalesce = true;
	positions.ClearValueAt(index);
	lengths.ClearValueAt(index);
}

void UndoActions::Create(size_t index, ActionType at_, Sci::Position position_, Sci::Position lenData_, bool mayCoalesce_) {
	types[index].at = at_;
	types[index].mayCoalesce = mayCoalesce_;
	positions.SetValueAt(index, position_);
	lengths.SetValueAt(index, lenData_);
}

const char *ScrapStack::Push(const char *text, size_t length) {
	if (current < stack.length()) {
		stack.resize(current);
	}
	stack.append(text, length);
	current = stack.length();
	return stack.data() + current - length;
}

void ScrapStack::SetCurrent(size_t position) noexcept {
	current = position;
}

void ScrapStack::MoveForward(size_t length) noexcept {
	if ((current + length) <= stack.length()) {
		current += length;
	}
}

void ScrapStack::MoveBack(size_t length) noexcept {
	if (current >= length) {
		current -= length;
	}
}

const char *ScrapStack::CurrentText() const noexcept {
	return stack.data() + current;
}

const char *ScrapStack::TextAt(size_t position) const noexcept {
	return stack.data() + position;
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
	scraps = std::make_unique<ScrapStack>();
	actions.Create(currentAction, ActionType::start);
}

UndoHistory::~UndoHistory() noexcept = default;

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
			ptrdiff_t targetAct = currentAction - 1;
			// Container actions may forward the coalesce state of Scintilla Actions.
			while ((targetAct > 0) && (actions.types[targetAct].at == ActionType::container) && actions.types[targetAct].mayCoalesce) {
				targetAct--;
			}
			// See if current action can be coalesced into previous action
			// Will work if both are inserts or deletes and position is same
			if ((currentAction == savePoint) || (currentAction == tentativePoint)) {
				currentAction++;
			} else if (!actions.types[currentAction].mayCoalesce) {
				// Not allowed to coalesce if this set
				currentAction++;
			} else if (!mayCoalesce || !actions.types[targetAct].mayCoalesce) {
				currentAction++;
			} else if (at == ActionType::container || actions.types[currentAction].at == ActionType::container) {
				;	// A coalescible containerAction
			} else if ((at != actions.types[targetAct].at) && (actions.types[targetAct].at != ActionType::start)) {
				currentAction++;
			} else if ((at == ActionType::insert) &&
			           (position != (actions.positions.SignedValueAt(targetAct) + actions.lengths.SignedValueAt(targetAct)))) {
				// Insertions must be immediately after to coalesce
				currentAction++;
			} else if (at == ActionType::remove) {
				if ((lengthData == 1) || (lengthData == 2)) {
					if ((position + lengthData) == actions.positions.SignedValueAt(targetAct)) {
						; // Backspace -> OK
					} else if (position == actions.positions.SignedValueAt(targetAct)) {
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
			if (!actions.types[currentAction].mayCoalesce)
				currentAction++;
		}
	} else {
		currentAction++;
	}
	startSequence = oldCurrentAction != currentAction;
	const char *dataNew = lengthData ? scraps->Push(data, lengthData) : nullptr;
	actions.Create(currentAction, at, position, lengthData, mayCoalesce);
	currentAction++;
	actions.Create(currentAction, ActionType::start);
	maxAction = currentAction;
	return dataNew;
}

void UndoHistory::BeginUndoAction() {
	EnsureUndoRoom();
	if (undoSequenceDepth == 0) {
		if (actions.types[currentAction].at != ActionType::start) {
			currentAction++;
			actions.Create(currentAction, ActionType::start);
			maxAction = currentAction;
		}
		actions.types[currentAction].mayCoalesce = false;
	}
	undoSequenceDepth++;
}

void UndoHistory::EndUndoAction() {
	PLATFORM_ASSERT(undoSequenceDepth > 0);
	EnsureUndoRoom();
	undoSequenceDepth--;
	if (0 == undoSequenceDepth) {
		if (actions.types[currentAction].at != ActionType::start) {
			currentAction++;
			actions.Create(currentAction, ActionType::start);
			maxAction = currentAction;
		}
		actions.types[currentAction].mayCoalesce = false;
	}
}

void UndoHistory::DropUndoSequence() noexcept {
	undoSequenceDepth = 0;
}

void UndoHistory::DeleteUndoHistory() noexcept {
	for (int i = 1; i < maxAction; i++) {
		actions.lengths.ClearValueAt(i);
	}
	maxAction = 0;
	currentAction = 0;
	actions.CreateStart(currentAction);
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
	if (actions.types[currentAction].at == ActionType::start && currentAction > 0)
		currentAction--;
	if (tentativePoint >= 0)
		return currentAction - tentativePoint;
	return -1;
}

bool UndoHistory::CanUndo() const noexcept {
	return (currentAction > 0) && (maxAction > 0);
}

int UndoHistory::StartUndo() noexcept {
	// Drop any trailing startAction
	if (actions.types[currentAction].at == ActionType::start && currentAction > 0)
		currentAction--;

	// Count the steps in this action
	int act = currentAction;
	while (actions.types[act].at != ActionType::start && act > 0) {
		act--;
	}
	return currentAction - act;
}

Action UndoHistory::GetUndoStep() const noexcept {
	Action acta {
		actions.types[currentAction].at,
		actions.types[currentAction].mayCoalesce,
		actions.positions.SignedValueAt(currentAction),
		nullptr,
		actions.lengths.SignedValueAt(currentAction)
	};
	if (acta.lenData) {
		acta.data = scraps->CurrentText() - acta.lenData;
	}
	return acta;
}

void UndoHistory::CompletedUndoStep() noexcept {
	scraps->MoveBack(actions.lengths.ValueAt(currentAction));
	currentAction--;
}

bool UndoHistory::CanRedo() const noexcept {
	return maxAction > currentAction;
}

int UndoHistory::StartRedo() noexcept {
	// Drop any leading startAction
	if (currentAction < maxAction && actions.types[currentAction].at == ActionType::start)
		currentAction++;

	// Count the steps in this action
	int act = currentAction;
	while (act < maxAction && actions.types[act].at != ActionType::start) {
		act++;
	}
	return act - currentAction;
}

Action UndoHistory::GetRedoStep() const noexcept {
	Action acta{
		actions.types[currentAction].at,
		actions.types[currentAction].mayCoalesce,
		actions.positions.SignedValueAt(currentAction),
		nullptr,
		actions.lengths.SignedValueAt(currentAction)
	};
	if (acta.lenData) {
		acta.data = scraps->CurrentText();
	}
	return acta;
}

void UndoHistory::CompletedRedoStep() noexcept {
	scraps->MoveForward(actions.lengths.ValueAt(currentAction));
	currentAction++;
}

}
