/** @file RunStyles.cxx
 ** Data structure used to store sparse styles.
 **/
// Copyright 1998-2007 by Neil Hodgson <neilh@scintilla.org>
// The License.txt file describes the conditions under which this software may be distributed.

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

#include <stdexcept>
#include <algorithm>

#include "Platform.h"

#include "Scintilla.h"
#include "Position.h"
#include "SplitVector.h"
#include "Partitioning.h"
#include "RunStyles.h"

#ifdef SCI_NAMESPACE
using namespace Scintilla;
#endif

// Find the first run at a position
Sci::Position RunStyles::RunFromPosition(Sci::Position position) const {
	Sci::Position run = starts->PartitionFromPosition(position);
	// Go to first element with this position
	while ((run > 0) && (position == starts->PositionFromPartition(run-1))) {
		run--;
	}
	return run;
}

// If there is no run boundary at position, insert one continuing style.
Sci::Position RunStyles::SplitRun(Sci::Position position) {
	Sci::Position run = RunFromPosition(position);
	Sci::Position posRun = starts->PositionFromPartition(run);
	if (posRun < position) {
		int runStyle = ValueAt(position);
		run++;
		starts->InsertPartition(run, position);
		styles->InsertValue(run, 1, runStyle);
	}
	return run;
}

void RunStyles::RemoveRun(Sci::Position run) {
	starts->RemovePartition(run);
	styles->DeleteRange(run, 1);
}

void RunStyles::RemoveRunIfEmpty(Sci::Position run) {
	if ((run < starts->Partitions()) && (starts->Partitions() > 1)) {
		if (starts->PositionFromPartition(run) == starts->PositionFromPartition(run+1)) {
			RemoveRun(run);
		}
	}
}

void RunStyles::RemoveRunIfSameAsPrevious(Sci::Position run) {
	if ((run > 0) && (run < starts->Partitions())) {
		if (styles->ValueAt(run-1) == styles->ValueAt(run)) {
			RemoveRun(run);
		}
	}
}

RunStyles::RunStyles() {
	starts = new Partitioning(8);
	styles = new SplitVector<int>();
	styles->InsertValue(0, 2, 0);
}

RunStyles::~RunStyles() {
	delete starts;
	starts = NULL;
	delete styles;
	styles = NULL;
}

Sci::Position RunStyles::Length() const {
	return starts->PositionFromPartition(starts->Partitions());
}

int RunStyles::ValueAt(Sci::Position position) const {
	return styles->ValueAt(starts->PartitionFromPosition(position));
}

Sci::Position RunStyles::FindNextChange(Sci::Position position, Sci::Position end) const {
	Sci::Position run = starts->PartitionFromPosition(position);
	if (run < starts->Partitions()) {
		Sci::Position runChange = starts->PositionFromPartition(run);
		if (runChange > position)
			return runChange;
		Sci::Position nextChange = starts->PositionFromPartition(run + 1);
		if (nextChange > position) {
			return nextChange;
		} else if (position < end) {
			return end;
		} else {
			return end + 1;
		}
	} else {
		return end + 1;
	}
}

Sci::Position RunStyles::StartRun(Sci::Position position) const {
	return starts->PositionFromPartition(starts->PartitionFromPosition(position));
}

Sci::Position RunStyles::EndRun(Sci::Position position) const {
	return starts->PositionFromPartition(starts->PartitionFromPosition(position) + 1);
}

bool RunStyles::FillRange(Sci::Position &position, int value, Sci::Position &fillLength) {
	if (fillLength <= 0) {
		return false;
	}
	Sci::Position end = position + fillLength;
	if (end > Length()) {
		return false;
	}
	Sci::Position runEnd = RunFromPosition(end);
	if (styles->ValueAt(runEnd) == value) {
		// End already has value so trim range.
		end = starts->PositionFromPartition(runEnd);
		if (position >= end) {
			// Whole range is already same as value so no action
			return false;
		}
		fillLength = end - position;
	} else {
		runEnd = SplitRun(end);
	}
	Sci::Position runStart = RunFromPosition(position);
	if (styles->ValueAt(runStart) == value) {
		// Start is in expected value so trim range.
		runStart++;
		position = starts->PositionFromPartition(runStart);
		fillLength = end - position;
	} else {
		if (starts->PositionFromPartition(runStart) < position) {
			runStart = SplitRun(position);
			runEnd++;
		}
	}
	if (runStart < runEnd) {
		styles->SetValueAt(runStart, value);
		// Remove each old run over the range
		for (Sci::Position run=runStart+1; run<runEnd; run++) {
			RemoveRun(runStart+1);
		}
		runEnd = RunFromPosition(end);
		RemoveRunIfSameAsPrevious(runEnd);
		RemoveRunIfSameAsPrevious(runStart);
		runEnd = RunFromPosition(end);
		RemoveRunIfEmpty(runEnd);
		return true;
	} else {
		return false;
	}
}

void RunStyles::SetValueAt(Sci::Position position, int value) {
	Sci::Position len = 1;
	FillRange(position, value, len);
}

void RunStyles::InsertSpace(Sci::Position position, Sci::Position insertLength) {
	Sci::Position runStart = RunFromPosition(position);
	if (starts->PositionFromPartition(runStart) == position) {
		int runStyle = ValueAt(position);
		// Inserting at start of run so make previous longer
		if (runStart == 0) {
			// Inserting at start of document so ensure 0
			if (runStyle) {
				styles->SetValueAt(0, 0);
				starts->InsertPartition(1, 0);
				styles->InsertValue(1, 1, runStyle);
				starts->InsertText(0, insertLength);
			} else {
				starts->InsertText(runStart, insertLength);
			}
		} else {
			if (runStyle) {
				starts->InsertText(runStart-1, insertLength);
			} else {
				// Insert at end of run so do not extend style
				starts->InsertText(runStart, insertLength);
			}
		}
	} else {
		starts->InsertText(runStart, insertLength);
	}
}

void RunStyles::DeleteAll() {
	delete starts;
	starts = NULL;
	delete styles;
	styles = NULL;
	starts = new Partitioning(8);
	styles = new SplitVector<int>();
	styles->InsertValue(0, 2, 0);
}

void RunStyles::DeleteRange(Sci::Position position, Sci::Position deleteLength) {
	Sci::Position end = position + deleteLength;
	Sci::Position runStart = RunFromPosition(position);
	Sci::Position runEnd = RunFromPosition(end);
	if (runStart == runEnd) {
		// Deleting from inside one run
		starts->InsertText(runStart, -deleteLength);
		RemoveRunIfEmpty(runStart);
	} else {
		runStart = SplitRun(position);
		runEnd = SplitRun(end);
		starts->InsertText(runStart, -deleteLength);
		// Remove each old run over the range
		for (Sci::Position run=runStart; run<runEnd; run++) {
			RemoveRun(runStart);
		}
		RemoveRunIfEmpty(runStart);
		RemoveRunIfSameAsPrevious(runStart);
	}
}

Sci::Position RunStyles::Runs() const {
	return starts->Partitions();
}

bool RunStyles::AllSame() const {
	for (Sci::Position run = 1; run < starts->Partitions(); run++) {
		if (styles->ValueAt(run) != styles->ValueAt(run - 1))
			return false;
	}
	return true;
}

bool RunStyles::AllSameAs(int value) const {
	return AllSame() && (styles->ValueAt(0) == value);
}

Sci::Position RunStyles::Find(int value, Sci::Position start) const {
	if (start < Length()) {
		Sci::Position run = start ? RunFromPosition(start) : 0;
		if (styles->ValueAt(run) == value)
			return start;
		run++;
		while (run < starts->Partitions()) {
			if (styles->ValueAt(run) == value)
				return starts->PositionFromPartition(run);
			run++;
		}
	}
	return -1;
}

void RunStyles::Check() const {
	if (Length() < 0) {
		throw std::runtime_error("RunStyles: Length can not be negative.");
	}
	if (starts->Partitions() < 1) {
		throw std::runtime_error("RunStyles: Must always have 1 or more partitions.");
	}
	if (starts->Partitions() != styles->Length()-1) {
		throw std::runtime_error("RunStyles: Partitions and styles different lengths.");
	}
	Sci::Position start=0;
	while (start < Length()) {
		Sci::Position end = EndRun(start);
		if (start >= end) {
			throw std::runtime_error("RunStyles: Partition is 0 length.");
		}
		start = end;
	}
	if (styles->ValueAt(styles->Length()-1) != 0) {
		throw std::runtime_error("RunStyles: Unused style at end changed.");
	}
	for (Sci::Position j=1; j<styles->Length()-1; j++) {
		if (styles->ValueAt(j) == styles->ValueAt(j-1)) {
			throw std::runtime_error("RunStyles: Style of a partition same as previous.");
		}
	}
}
