// Scintilla source code edit control
/** @file ContractionState.cxx
 ** Manages visibility of lines for folding and wrapping.
 **/
// Copyright 1998-2007 by Neil Hodgson <neilh@scintilla.org>
// The License.txt file describes the conditions under which this software may be distributed.

#include <string.h>

#include <stdexcept>
#include <algorithm>

#include "Platform.h"

#include "Position.h"
#include "SplitVector.h"
#include "Partitioning.h"
#include "RunStyles.h"
#include "ContractionState.h"

#ifdef SCI_NAMESPACE
using namespace Scintilla;
#endif

ContractionState::ContractionState() : visible(0), expanded(0), heights(0), displayLines(0), linesInDocument(1) {
	//InsertLine(0);
}

ContractionState::~ContractionState() {
	Clear();
}

void ContractionState::EnsureData() {
	if (OneToOne()) {
		visible = new RunStyles();
		expanded = new RunStyles();
		heights = new RunStyles();
		displayLines = new Partitioning(4);
		InsertLines(0, linesInDocument);
	}
}

void ContractionState::Clear() {
	delete visible;
	visible = 0;
	delete expanded;
	expanded = 0;
	delete heights;
	heights = 0;
	delete displayLines;
	displayLines = 0;
	linesInDocument = 1;
}

Sci::Position ContractionState::LinesInDoc() const {
	if (OneToOne()) {
		return linesInDocument;
	} else {
		return displayLines->Partitions() - 1;
	}
}

Sci::Position ContractionState::LinesDisplayed() const {
	if (OneToOne()) {
		return linesInDocument;
	} else {
		return displayLines->PositionFromPartition(LinesInDoc());
	}
}

Sci::Position ContractionState::DisplayFromDoc(Sci::Position lineDoc) const {
	if (OneToOne()) {
		return (lineDoc <= linesInDocument) ? lineDoc : linesInDocument;
	} else {
		if (lineDoc > displayLines->Partitions())
			lineDoc = displayLines->Partitions();
		return displayLines->PositionFromPartition(lineDoc);
	}
}

Sci::Position ContractionState::DisplayLastFromDoc(Sci::Position lineDoc) const {
	return DisplayFromDoc(lineDoc) + GetHeight(lineDoc) - 1;
}

Sci::Position ContractionState::DocFromDisplay(Sci::Position lineDisplay) const {
	if (OneToOne()) {
		return lineDisplay;
	} else {
		if (lineDisplay <= 0) {
			return 0;
		}
		if (lineDisplay > LinesDisplayed()) {
			return displayLines->PartitionFromPosition(LinesDisplayed());
		}
		Sci::Position lineDoc = displayLines->PartitionFromPosition(lineDisplay);
		PLATFORM_ASSERT(GetVisible(lineDoc));
		return lineDoc;
	}
}

void ContractionState::InsertLine(Sci::Position lineDoc) {
	if (OneToOne()) {
		linesInDocument++;
	} else {
		visible->InsertSpace(lineDoc, 1);
		visible->SetValueAt(lineDoc, 1);
		expanded->InsertSpace(lineDoc, 1);
		expanded->SetValueAt(lineDoc, 1);
		heights->InsertSpace(lineDoc, 1);
		heights->SetValueAt(lineDoc, 1);
		Sci::Position lineDisplay = DisplayFromDoc(lineDoc);
		displayLines->InsertPartition(lineDoc, lineDisplay);
		displayLines->InsertText(lineDoc, 1);
	}
}

void ContractionState::InsertLines(Sci::Position lineDoc, Sci::Position lineCount) {
	for (Sci::Position l = 0; l < lineCount; l++) {
		InsertLine(lineDoc + l);
	}
	Check();
}

void ContractionState::DeleteLine(Sci::Position lineDoc) {
	if (OneToOne()) {
		linesInDocument--;
	} else {
		if (GetVisible(lineDoc)) {
			displayLines->InsertText(lineDoc, -heights->ValueAt(lineDoc));
		}
		displayLines->RemovePartition(lineDoc);
		visible->DeleteRange(lineDoc, 1);
		expanded->DeleteRange(lineDoc, 1);
		heights->DeleteRange(lineDoc, 1);
	}
}

void ContractionState::DeleteLines(Sci::Position lineDoc, Sci::Position lineCount) {
	for (Sci::Position l = 0; l < lineCount; l++) {
		DeleteLine(lineDoc);
	}
	Check();
}

bool ContractionState::GetVisible(Sci::Position lineDoc) const {
	if (OneToOne()) {
		return true;
	} else {
		if (lineDoc >= visible->Length())
			return true;
		return visible->ValueAt(lineDoc) == 1;
	}
}

bool ContractionState::SetVisible(Sci::Position lineDocStart, Sci::Position lineDocEnd, bool isVisible) {
	if (OneToOne() && isVisible) {
		return false;
	} else {
		EnsureData();
		int delta = 0;
		Check();
		if ((lineDocStart <= lineDocEnd) && (lineDocStart >= 0) && (lineDocEnd < LinesInDoc())) {
			for (Sci::Position line = lineDocStart; line <= lineDocEnd; line++) {
				if (GetVisible(line) != isVisible) {
					int difference = isVisible ? heights->ValueAt(line) : -heights->ValueAt(line);
					visible->SetValueAt(line, isVisible ? 1 : 0);
					displayLines->InsertText(line, difference);
					delta += difference;
				}
			}
		} else {
			return false;
		}
		Check();
		return delta != 0;
	}
}

bool ContractionState::HiddenLines() const {
	if (OneToOne()) {
		return false;
	} else {
		return !visible->AllSameAs(1);
	}
}

bool ContractionState::GetExpanded(Sci::Position lineDoc) const {
	if (OneToOne()) {
		return true;
	} else {
		Check();
		return expanded->ValueAt(lineDoc) == 1;
	}
}

bool ContractionState::SetExpanded(Sci::Position lineDoc, bool isExpanded) {
	if (OneToOne() && isExpanded) {
		return false;
	} else {
		EnsureData();
		if (isExpanded != (expanded->ValueAt(lineDoc) == 1)) {
			expanded->SetValueAt(lineDoc, isExpanded ? 1 : 0);
			Check();
			return true;
		} else {
			Check();
			return false;
		}
	}
}

Sci::Position ContractionState::ContractedNext(Sci::Position lineDocStart) const {
	if (OneToOne()) {
		return -1;
	} else {
		Check();
		if (!expanded->ValueAt(lineDocStart)) {
			return lineDocStart;
		} else {
			Sci::Position lineDocNextChange = expanded->EndRun(lineDocStart);
			if (lineDocNextChange < LinesInDoc())
				return lineDocNextChange;
			else
				return -1;
		}
	}
}

int ContractionState::GetHeight(Sci::Position lineDoc) const {
	if (OneToOne()) {
		return 1;
	} else {
		return heights->ValueAt(lineDoc);
	}
}

// Set the number of display lines needed for this line.
// Return true if this is a change.
bool ContractionState::SetHeight(Sci::Position lineDoc, int height) {
	if (OneToOne() && (height == 1)) {
		return false;
	} else if (lineDoc < LinesInDoc()) {
		EnsureData();
		if (GetHeight(lineDoc) != height) {
			if (GetVisible(lineDoc)) {
				displayLines->InsertText(lineDoc, height - GetHeight(lineDoc));
			}
			heights->SetValueAt(lineDoc, height);
			Check();
			return true;
		} else {
			Check();
			return false;
		}
	} else {
		return false;
	}
}

void ContractionState::ShowAll() {
	Sci::Position lines = LinesInDoc();
	Clear();
	linesInDocument = lines;
}

// Debugging checks

void ContractionState::Check() const {
#ifdef CHECK_CORRECTNESS
	for (Sci::Position vline = 0; vline < LinesDisplayed(); vline++) {
		const Sci::Position lineDoc = DocFromDisplay(vline);
		PLATFORM_ASSERT(GetVisible(lineDoc));
	}
	for (Sci::Position lineDoc = 0; lineDoc < LinesInDoc(); lineDoc++) {
		const Sci::Position displayThis = DisplayFromDoc(lineDoc);
		const Sci::Position displayNext = DisplayFromDoc(lineDoc + 1);
		const Sci::Position height = displayNext - displayThis;
		PLATFORM_ASSERT(height >= 0);
		if (GetVisible(lineDoc)) {
			PLATFORM_ASSERT(GetHeight(lineDoc) == height);
		} else {
			PLATFORM_ASSERT(0 == height);
		}
	}
#endif
}
