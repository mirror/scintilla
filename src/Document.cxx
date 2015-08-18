// Scintilla source code edit control
/** @file Document.cxx
 ** Text document that handles notifications, DBCS, styling, words and end of line.
 **/
// Copyright 1998-2011 by Neil Hodgson <neilh@scintilla.org>
// The License.txt file describes the conditions under which this software may be distributed.

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <ctype.h>

#include <stdexcept>
#include <string>
#include <vector>
#include <algorithm>

#ifdef CXX11_REGEX
#include <regex>
#endif

#include "Platform.h"

#include "ILexer.h"
#include "Scintilla.h"

#include "CharacterSet.h"
#include "Position.h"
#include "SplitVector.h"
#include "Partitioning.h"
#include "RunStyles.h"
#include "CellBuffer.h"
#include "PerLine.h"
#include "CharClassify.h"
#include "Decoration.h"
#include "CaseFolder.h"
#include "Document.h"
#include "RESearch.h"
#include "UniConversion.h"
#include "UnicodeFromUTF8.h"

#ifdef SCI_NAMESPACE
using namespace Scintilla;
#endif

static inline bool IsPunctuation(char ch) {
	return IsASCII(ch) && ispunct(ch);
}

void LexInterface::Colourise(Sci::Position start, Sci::Position end) {
	if (pdoc && instance && !performingStyle) {
		// Protect against reentrance, which may occur, for example, when
		// fold points are discovered while performing styling and the folding
		// code looks for child lines which may trigger styling.
		performingStyle = true;

		Sci::Position lengthDoc = pdoc->PositionLength();
		if (end == -1)
			end = lengthDoc;
		Sci::Position len = end - start;

		PLATFORM_ASSERT(len >= 0);
		PLATFORM_ASSERT(start + len <= lengthDoc);

		int styleStart = 0;
		if (start > 0)
			styleStart = pdoc->StyleAt(start - 1);

		if (len > 0) {
			instance->Lex(start, len, styleStart, pdoc);
			instance->Fold(start, len, styleStart, pdoc);
		}

		performingStyle = false;
	}
}

int LexInterface::LineEndTypesSupported() {
	if (instance) {
		return instance->LineEndTypesSupported();
	}
	return 0;
}

Document::Document() {
	refCount = 0;
	pcf = NULL;
#ifdef _WIN32
	eolMode = SC_EOL_CRLF;
#else
	eolMode = SC_EOL_LF;
#endif
	dbcsCodePage = 0;
	lineEndBitSet = SC_LINE_END_TYPE_DEFAULT;
	endStyled = 0;
	styleClock = 0;
	enteredModification = 0;
	enteredStyling = 0;
	enteredReadOnlyCount = 0;
	insertionSet = false;
	tabInChars = 8;
	indentInChars = 0;
	actualIndentInChars = 8;
	useTabs = true;
	tabIndents = true;
	backspaceUnindents = false;

	matchesValid = false;
	regex = 0;

	UTF8BytesOfLeadInitialise();

	perLineData[ldMarkers] = new LineMarkers();
	perLineData[ldLevels] = new LineLevels();
	perLineData[ldState] = new LineState();
	perLineData[ldMargin] = new LineAnnotation();
	perLineData[ldAnnotation] = new LineAnnotation();

	cb.SetPerLine(this);

	pli = 0;
}

Document::~Document() {
	for (std::vector<WatcherWithUserData>::iterator it = watchers.begin(); it != watchers.end(); ++it) {
		it->watcher->NotifyDeleted(this, it->userData);
	}
	for (int j=0; j<ldSize; j++) {
		delete perLineData[j];
		perLineData[j] = 0;
	}
	delete regex;
	regex = 0;
	delete pli;
	pli = 0;
	delete pcf;
	pcf = 0;
}

void Document::Init() {
	for (int j=0; j<ldSize; j++) {
		if (perLineData[j])
			perLineData[j]->Init();
	}
}

int Document::LineEndTypesSupported() const {
	if ((SC_CP_UTF8 == dbcsCodePage) && pli)
		return pli->LineEndTypesSupported();
	else
		return 0;
}

bool Document::SetDBCSCodePage(int dbcsCodePage_) {
	if (dbcsCodePage != dbcsCodePage_) {
		dbcsCodePage = dbcsCodePage_;
		SetCaseFolder(NULL);
		cb.SetLineEndTypes(lineEndBitSet & LineEndTypesSupported());
		return true;
	} else {
		return false;
	}
}

bool Document::SetLineEndTypesAllowed(int lineEndBitSet_) {
	if (lineEndBitSet != lineEndBitSet_) {
		lineEndBitSet = lineEndBitSet_;
		int lineEndBitSetActive = lineEndBitSet & LineEndTypesSupported();
		if (lineEndBitSetActive != cb.GetLineEndTypes()) {
			ModifiedAt(0);
			cb.SetLineEndTypes(lineEndBitSetActive);
			return true;
		} else {
			return false;
		}
	} else {
		return false;
	}
}

void Document::InsertLine(Sci::Position line) {
	for (int j=0; j<ldSize; j++) {
		if (perLineData[j])
			perLineData[j]->InsertLine(line);
	}
}

void Document::RemoveLine(Sci::Position line) {
	for (int j=0; j<ldSize; j++) {
		if (perLineData[j])
			perLineData[j]->RemoveLine(line);
	}
}

// Increase reference count and return its previous value.
int Document::AddRef() {
	return refCount++;
}

// Decrease reference count and return its previous value.
// Delete the document if reference count reaches zero.
int SCI_METHOD Document::Release() {
	int curRefCount = --refCount;
	if (curRefCount == 0)
		delete this;
	return curRefCount;
}

void Document::SetSavePoint() {
	cb.SetSavePoint();
	NotifySavePoint(true);
}

void Document::TentativeUndo() {
	if (!TentativeActive())
		return;
	CheckReadOnly();
	if (enteredModification == 0) {
		enteredModification++;
		if (!cb.IsReadOnly()) {
			bool startSavePoint = cb.IsSavePoint();
			bool multiLine = false;
			int steps = cb.TentativeSteps();
			//Platform::DebugPrintf("Steps=%d\n", steps);
			for (int step = 0; step < steps; step++) {
				const Sci::Position prevLinesTotal = LinesTotal();
				const Action &action = cb.GetUndoStep();
				if (action.at == removeAction) {
					NotifyModified(DocModification(
									SC_MOD_BEFOREINSERT | SC_PERFORMED_UNDO, action));
				} else if (action.at == containerAction) {
					DocModification dm(SC_MOD_CONTAINER | SC_PERFORMED_UNDO);
					dm.token = action.position;
					NotifyModified(dm);
				} else {
					NotifyModified(DocModification(
									SC_MOD_BEFOREDELETE | SC_PERFORMED_UNDO, action));
				}
				cb.PerformUndoStep();
				if (action.at != containerAction) {
					ModifiedAt(action.position);
				}

				int modFlags = SC_PERFORMED_UNDO;
				// With undo, an insertion action becomes a deletion notification
				if (action.at == removeAction) {
					modFlags |= SC_MOD_INSERTTEXT;
				} else if (action.at == insertAction) {
					modFlags |= SC_MOD_DELETETEXT;
				}
				if (steps > 1)
					modFlags |= SC_MULTISTEPUNDOREDO;
				const Sci::Position linesAdded = LinesTotal() - prevLinesTotal;
				if (linesAdded != 0)
					multiLine = true;
				if (step == steps - 1) {
					modFlags |= SC_LASTSTEPINUNDOREDO;
					if (multiLine)
						modFlags |= SC_MULTILINEUNDOREDO;
				}
				NotifyModified(DocModification(modFlags, action.position, action.lenData,
											   linesAdded, action.data));
			}

			bool endSavePoint = cb.IsSavePoint();
			if (startSavePoint != endSavePoint)
				NotifySavePoint(endSavePoint);
				
			cb.TentativeCommit();
		}
		enteredModification--;
	}
}

int Document::GetMark(Sci::Position line) {
	return static_cast<LineMarkers *>(perLineData[ldMarkers])->MarkValue(line);
}

Sci::Position Document::MarkerNext(Sci::Position lineStart, int mask) const {
	return static_cast<LineMarkers *>(perLineData[ldMarkers])->MarkerNext(lineStart, mask);
}

int Document::AddMark(Sci::Position line, int markerNum) {
	if (line >= 0 && line <= LinesTotal()) {
		int prev = static_cast<LineMarkers *>(perLineData[ldMarkers])->
			AddMark(line, markerNum, LinesTotal());
		DocModification mh(SC_MOD_CHANGEMARKER, PositionLineStart(line), 0, 0, 0, line);
		NotifyModified(mh);
		return prev;
	} else {
		return 0;
	}
}

void Document::AddMarkSet(Sci::Position line, int valueSet) {
	if (line < 0 || line > LinesTotal()) {
		return;
	}
	unsigned int m = valueSet;
	for (int i = 0; m; i++, m >>= 1)
		if (m & 1)
			static_cast<LineMarkers *>(perLineData[ldMarkers])->
				AddMark(line, i, LinesTotal());
	DocModification mh(SC_MOD_CHANGEMARKER, PositionLineStart(line), 0, 0, 0, line);
	NotifyModified(mh);
}

void Document::DeleteMark(Sci::Position line, int markerNum) {
	static_cast<LineMarkers *>(perLineData[ldMarkers])->DeleteMark(line, markerNum, false);
	DocModification mh(SC_MOD_CHANGEMARKER, PositionLineStart(line), 0, 0, 0, line);
	NotifyModified(mh);
}

void Document::DeleteMarkFromHandle(int markerHandle) {
	static_cast<LineMarkers *>(perLineData[ldMarkers])->DeleteMarkFromHandle(markerHandle);
	DocModification mh(SC_MOD_CHANGEMARKER, 0, 0, 0, 0);
	mh.line = -1;
	NotifyModified(mh);
}

void Document::DeleteAllMarks(int markerNum) {
	bool someChanges = false;
	for (Sci::Position line = 0; line < LinesTotal(); line++) {
		if (static_cast<LineMarkers *>(perLineData[ldMarkers])->DeleteMark(line, markerNum, true))
			someChanges = true;
	}
	if (someChanges) {
		DocModification mh(SC_MOD_CHANGEMARKER, 0, 0, 0, 0);
		mh.line = -1;
		NotifyModified(mh);
	}
}

Sci::Position Document::LineFromHandle(int markerHandle) {
	return static_cast<LineMarkers *>(perLineData[ldMarkers])->LineFromHandle(markerHandle);
}

Sci::Position Document::PositionLineStart(Sci::Position line) const {
	return cb.LineStart(line);
}

Sci_Position SCI_METHOD Document::LineStart(Sci_Position line) const {
	return cb.LineStart(static_cast<Sci::Position>(line));
}

bool Document::IsLineStartPosition(Sci::Position position) const {
	return PositionLineStart(LineOfPosition(position)) == position;
}

Sci::Position Document::PositionLineEnd(Sci::Position line) const {
	if (line >= LinesTotal() - 1) {
		return PositionLineStart(line + 1);
	} else {
		Sci::Position position = PositionLineStart(line + 1);
		if (SC_CP_UTF8 == dbcsCodePage) {
			unsigned char bytes[] = {
				static_cast<unsigned char>(cb.CharAt(position-3)),
				static_cast<unsigned char>(cb.CharAt(position-2)),
				static_cast<unsigned char>(cb.CharAt(position-1)),
			};
			if (UTF8IsSeparator(bytes)) {
				return position - UTF8SeparatorLength;
			}
			if (UTF8IsNEL(bytes+1)) {
				return position - UTF8NELLength;
			}
		}
		position--; // Back over CR or LF
		// When line terminator is CR+LF, may need to go back one more
		if ((position > LineStart(line)) && (cb.CharAt(position - 1) == '\r')) {
			position--;
		}
		return position;
	}
}

Sci_Position SCI_METHOD Document::LineEnd(Sci_Position line) const {
	return PositionLineEnd(static_cast<Sci::Position>(line));
}

void SCI_METHOD Document::SetErrorStatus(int status) {
	// Tell the watchers an error has occurred.
	for (std::vector<WatcherWithUserData>::iterator it = watchers.begin(); it != watchers.end(); ++it) {
		it->watcher->NotifyErrorOccurred(this, it->userData, status);
	}
}

Sci::Position Document::LineOfPosition(Sci::Position pos) const {
	return cb.LineFromPosition(pos);
}

Sci_Position SCI_METHOD Document::LineFromPosition(Sci_Position pos) const {
	return cb.LineFromPosition(static_cast<Sci::Position>(pos));
}

Sci::Position Document::LineEndPosition(Sci::Position position) const {
	return PositionLineEnd(LineOfPosition(position));
}

bool Document::IsLineEndPosition(Sci::Position position) const {
	return PositionLineEnd(LineOfPosition(position)) == position;
}

bool Document::IsPositionInLineEnd(Sci::Position position) const {
	return position >= PositionLineEnd(LineOfPosition(position));
}

Sci::Position Document::VCHomePosition(Sci::Position position) const {
	Sci::Position line = LineOfPosition(position);
	Sci::Position startPosition = PositionLineStart(line);
	Sci::Position endLine = PositionLineEnd(line);
	Sci::Position startText = startPosition;
	while (startText < endLine && (cb.CharAt(startText) == ' ' || cb.CharAt(startText) == '\t'))
		startText++;
	if (position == startText)
		return startPosition;
	else
		return startText;
}

int SCI_METHOD Document::SetLevel(Sci_Position line, int level) {
	int prev = static_cast<LineLevels *>(perLineData[ldLevels])->SetLevel(static_cast<Sci::Position>(line), level, LinesTotal());
	if (prev != level) {
		DocModification mh(SC_MOD_CHANGEFOLD | SC_MOD_CHANGEMARKER,
		                   PositionLineStart(static_cast<Sci::Position>(line)), 0, 0, 0, static_cast<Sci::Position>(line));
		mh.foldLevelNow = level;
		mh.foldLevelPrev = prev;
		NotifyModified(mh);
	}
	return prev;
}

int SCI_METHOD Document::GetLevel(Sci_Position line) const {
	return static_cast<LineLevels *>(perLineData[ldLevels])->GetLevel(static_cast<Sci::Position>(line));
}

void Document::ClearLevels() {
	static_cast<LineLevels *>(perLineData[ldLevels])->ClearLevels();
}

static bool IsSubordinate(int levelStart, int levelTry) {
	if (levelTry & SC_FOLDLEVELWHITEFLAG)
		return true;
	else
		return (levelStart & SC_FOLDLEVELNUMBERMASK) < (levelTry & SC_FOLDLEVELNUMBERMASK);
}

Sci::Position Document::GetLastChild(Sci::Position lineParent, int level, Sci::Position lastLine) {
	if (level == -1)
		level = GetLevel(lineParent) & SC_FOLDLEVELNUMBERMASK;
	Sci::Position maxLine = LinesTotal();
	Sci::Position lookLastLine = (lastLine != -1) ? std::min(LinesTotal() - 1, lastLine) : -1;
	Sci::Position lineMaxSubord = lineParent;
	while (lineMaxSubord < maxLine - 1) {
		EnsureStyledTo(PositionLineStart(lineMaxSubord + 2));
		if (!IsSubordinate(level, GetLevel(lineMaxSubord + 1)))
			break;
		if ((lookLastLine != -1) && (lineMaxSubord >= lookLastLine) && !(GetLevel(lineMaxSubord) & SC_FOLDLEVELWHITEFLAG))
			break;
		lineMaxSubord++;
	}
	if (lineMaxSubord > lineParent) {
		if (level > (GetLevel(lineMaxSubord + 1) & SC_FOLDLEVELNUMBERMASK)) {
			// Have chewed up some whitespace that belongs to a parent so seek back
			if (GetLevel(lineMaxSubord) & SC_FOLDLEVELWHITEFLAG) {
				lineMaxSubord--;
			}
		}
	}
	return lineMaxSubord;
}

Sci::Position Document::GetFoldParent(Sci::Position line) const {
	int level = GetLevel(line) & SC_FOLDLEVELNUMBERMASK;
	Sci::Position lineLook = line - 1;
	while ((lineLook > 0) && (
	            (!(GetLevel(lineLook) & SC_FOLDLEVELHEADERFLAG)) ||
	            ((GetLevel(lineLook) & SC_FOLDLEVELNUMBERMASK) >= level))
	      ) {
		lineLook--;
	}
	if ((GetLevel(lineLook) & SC_FOLDLEVELHEADERFLAG) &&
	        ((GetLevel(lineLook) & SC_FOLDLEVELNUMBERMASK) < level)) {
		return lineLook;
	} else {
		return -1;
	}
}

void Document::GetHighlightDelimiters(HighlightDelimiter &highlightDelimiter, Sci::Position line, Sci::Position lastLine) {
	int level = GetLevel(line);
	Sci::Position lookLastLine = std::max(line, lastLine) + 1;

	Sci::Position lookLine = line;
	int lookLineLevel = level;
	int lookLineLevelNum = lookLineLevel & SC_FOLDLEVELNUMBERMASK;
	while ((lookLine > 0) && ((lookLineLevel & SC_FOLDLEVELWHITEFLAG) ||
		((lookLineLevel & SC_FOLDLEVELHEADERFLAG) && (lookLineLevelNum >= (GetLevel(lookLine + 1) & SC_FOLDLEVELNUMBERMASK))))) {
		lookLineLevel = GetLevel(--lookLine);
		lookLineLevelNum = lookLineLevel & SC_FOLDLEVELNUMBERMASK;
	}

	Sci::Position beginFoldBlock = (lookLineLevel & SC_FOLDLEVELHEADERFLAG) ? lookLine : GetFoldParent(lookLine);
	if (beginFoldBlock == -1) {
		highlightDelimiter.Clear();
		return;
	}

	Sci::Position endFoldBlock = GetLastChild(beginFoldBlock, -1, lookLastLine);
	Sci::Position firstChangeableLineBefore = -1;
	if (endFoldBlock < line) {
		lookLine = beginFoldBlock - 1;
		lookLineLevel = GetLevel(lookLine);
		lookLineLevelNum = lookLineLevel & SC_FOLDLEVELNUMBERMASK;
		while ((lookLine >= 0) && (lookLineLevelNum >= SC_FOLDLEVELBASE)) {
			if (lookLineLevel & SC_FOLDLEVELHEADERFLAG) {
				if (GetLastChild(lookLine, -1, lookLastLine) == line) {
					beginFoldBlock = lookLine;
					endFoldBlock = line;
					firstChangeableLineBefore = line - 1;
				}
			}
			if ((lookLine > 0) && (lookLineLevelNum == SC_FOLDLEVELBASE) && ((GetLevel(lookLine - 1) & SC_FOLDLEVELNUMBERMASK) > lookLineLevelNum))
				break;
			lookLineLevel = GetLevel(--lookLine);
			lookLineLevelNum = lookLineLevel & SC_FOLDLEVELNUMBERMASK;
		}
	}
	if (firstChangeableLineBefore == -1) {
		for (lookLine = line - 1, lookLineLevel = GetLevel(lookLine), lookLineLevelNum = lookLineLevel & SC_FOLDLEVELNUMBERMASK;
			lookLine >= beginFoldBlock;
			lookLineLevel = GetLevel(--lookLine), lookLineLevelNum = lookLineLevel & SC_FOLDLEVELNUMBERMASK) {
			if ((lookLineLevel & SC_FOLDLEVELWHITEFLAG) || (lookLineLevelNum > (level & SC_FOLDLEVELNUMBERMASK))) {
				firstChangeableLineBefore = lookLine;
				break;
			}
		}
	}
	if (firstChangeableLineBefore == -1)
		firstChangeableLineBefore = beginFoldBlock - 1;

	Sci::Position firstChangeableLineAfter = -1;
	for (lookLine = line + 1, lookLineLevel = GetLevel(lookLine), lookLineLevelNum = lookLineLevel & SC_FOLDLEVELNUMBERMASK;
		lookLine <= endFoldBlock;
		lookLineLevel = GetLevel(++lookLine), lookLineLevelNum = lookLineLevel & SC_FOLDLEVELNUMBERMASK) {
		if ((lookLineLevel & SC_FOLDLEVELHEADERFLAG) && (lookLineLevelNum < (GetLevel(lookLine + 1) & SC_FOLDLEVELNUMBERMASK))) {
			firstChangeableLineAfter = lookLine;
			break;
		}
	}
	if (firstChangeableLineAfter == -1)
		firstChangeableLineAfter = endFoldBlock + 1;

	highlightDelimiter.beginFoldBlock = beginFoldBlock;
	highlightDelimiter.endFoldBlock = endFoldBlock;
	highlightDelimiter.firstChangeableLineBefore = firstChangeableLineBefore;
	highlightDelimiter.firstChangeableLineAfter = firstChangeableLineAfter;
}

Sci::Position Document::ClampPositionIntoDocument(Sci::Position pos) const {
	return Sci::Clamp(pos, 0, PositionLength());
}

bool Document::IsCrLf(Sci::Position pos) const {
	if (pos < 0)
		return false;
	if (pos >= (Length() - 1))
		return false;
	return (cb.CharAt(pos) == '\r') && (cb.CharAt(pos + 1) == '\n');
}

int Document::LenChar(Sci::Position pos) {
	if (pos < 0) {
		return 1;
	} else if (IsCrLf(pos)) {
		return 2;
	} else if (SC_CP_UTF8 == dbcsCodePage) {
		const unsigned char leadByte = static_cast<unsigned char>(cb.CharAt(pos));
		const int widthCharBytes = UTF8BytesOfLead[leadByte];
		Sci::Position lengthDoc = PositionLength();
		if ((pos + widthCharBytes) > lengthDoc)
			return static_cast<int>(lengthDoc - pos);
		else
			return widthCharBytes;
	} else if (dbcsCodePage) {
		return IsDBCSLeadByte(cb.CharAt(pos)) ? 2 : 1;
	} else {
		return 1;
	}
}

bool Document::InGoodUTF8(Sci::Position pos, Sci::Position &start, Sci::Position &end) const {
	Sci::Position trail = pos;
	while ((trail>0) && (pos-trail < UTF8MaxBytes) && UTF8IsTrailByte(static_cast<unsigned char>(cb.CharAt(trail-1))))
		trail--;
	start = (trail > 0) ? trail-1 : trail;

	const unsigned char leadByte = static_cast<unsigned char>(cb.CharAt(start));
	const int widthCharBytes = UTF8BytesOfLead[leadByte];
	if (widthCharBytes == 1) {
		return false;
	} else {
		int trailBytes = widthCharBytes - 1;
		Sci::Position len = pos - start;
		if (len > trailBytes)
			// pos too far from lead
			return false;
		char charBytes[UTF8MaxBytes] = {static_cast<char>(leadByte),0,0,0};
		for (int b=1; b<widthCharBytes && ((start+b) < Length()); b++)
			charBytes[b] = cb.CharAt(start+b);
		int utf8status = UTF8Classify(reinterpret_cast<const unsigned char *>(charBytes), widthCharBytes);
		if (utf8status & UTF8MaskInvalid)
			return false;
		end = start + widthCharBytes;
		return true;
	}
}

// Normalise a position so that it is not halfway through a two byte character.
// This can occur in two situations -
// When lines are terminated with \r\n pairs which should be treated as one character.
// When displaying DBCS text such as Japanese.
// If moving, move the position in the indicated direction.
Sci::Position Document::MovePositionOutsideChar(Sci::Position pos, int moveDir, bool checkLineEnd) const {
	//Platform::DebugPrintf("NoCRLF %d %d\n", pos, moveDir);
	// If out of range, just return minimum/maximum value.
	if (pos <= 0)
		return 0;
	if (pos >= Length())
		return PositionLength();

	// PLATFORM_ASSERT(pos > 0 && pos < Length());
	if (checkLineEnd && IsCrLf(pos - 1)) {
		if (moveDir > 0)
			return pos + 1;
		else
			return pos - 1;
	}

	if (dbcsCodePage) {
		if (SC_CP_UTF8 == dbcsCodePage) {
			unsigned char ch = static_cast<unsigned char>(cb.CharAt(pos));
			// If ch is not a trail byte then pos is valid intercharacter position
			if (UTF8IsTrailByte(ch)) {
				Sci::Position startUTF = pos;
				Sci::Position endUTF = pos;
				if (InGoodUTF8(pos, startUTF, endUTF)) {
					// ch is a trail byte within a UTF-8 character
					if (moveDir > 0)
						pos = endUTF;
					else
						pos = startUTF;
				}
				// Else invalid UTF-8 so return position of isolated trail byte
			}
		} else {
			// Anchor DBCS calculations at start of line because start of line can
			// not be a DBCS trail byte.
			Sci::Position posStartLine = PositionLineStart(LineOfPosition(pos));
			if (pos == posStartLine)
				return pos;

			// Step back until a non-lead-byte is found.
			Sci::Position posCheck = pos;
			while ((posCheck > posStartLine) && IsDBCSLeadByte(cb.CharAt(posCheck-1)))
				posCheck--;

			// Check from known start of character.
			while (posCheck < pos) {
				int mbsize = IsDBCSLeadByte(cb.CharAt(posCheck)) ? 2 : 1;
				if (posCheck + mbsize == pos) {
					return pos;
				} else if (posCheck + mbsize > pos) {
					if (moveDir > 0) {
						return posCheck + mbsize;
					} else {
						return posCheck;
					}
				}
				posCheck += mbsize;
			}
		}
	}

	return pos;
}

// NextPosition moves between valid positions - it can not handle a position in the middle of a
// multi-byte character. It is used to iterate through text more efficiently than MovePositionOutsideChar.
// A \r\n pair is treated as two characters.
Sci::Position Document::NextPosition(Sci::Position pos, int moveDir) const {
	// If out of range, just return minimum/maximum value.
	int increment = (moveDir > 0) ? 1 : -1;
	if (pos + increment <= 0)
		return 0;
	if (pos + increment >= PositionLength())
		return PositionLength();

	if (dbcsCodePage) {
		if (SC_CP_UTF8 == dbcsCodePage) {
			if (increment == 1) {
				// Simple forward movement case so can avoid some checks
				const unsigned char leadByte = static_cast<unsigned char>(cb.CharAt(pos));
				if (UTF8IsAscii(leadByte)) {
					// Single byte character or invalid
					pos++;
				} else {
					const int widthCharBytes = UTF8BytesOfLead[leadByte];
					char charBytes[UTF8MaxBytes] = {static_cast<char>(leadByte),0,0,0};
					for (int b=1; b<widthCharBytes; b++)
						charBytes[b] = cb.CharAt(static_cast<Sci::Position>(pos+b));
					int utf8status = UTF8Classify(reinterpret_cast<const unsigned char *>(charBytes), widthCharBytes);
					if (utf8status & UTF8MaskInvalid)
						pos++;
					else
						pos += utf8status & UTF8MaskWidth;
				}
			} else {
				// Examine byte before position
				pos--;
				unsigned char ch = static_cast<unsigned char>(cb.CharAt(pos));
				// If ch is not a trail byte then pos is valid intercharacter position
				if (UTF8IsTrailByte(ch)) {
					// If ch is a trail byte in a valid UTF-8 character then return start of character
					Sci::Position startUTF = pos;
					Sci::Position endUTF = pos;
					if (InGoodUTF8(pos, startUTF, endUTF)) {
						pos = startUTF;
					}
					// Else invalid UTF-8 so return position of isolated trail byte
				}
			}
		} else {
			if (moveDir > 0) {
				int mbsize = IsDBCSLeadByte(cb.CharAt(pos)) ? 2 : 1;
				pos += mbsize;
				if (pos > PositionLength())
					pos = PositionLength();
			} else {
				// Anchor DBCS calculations at start of line because start of line can
				// not be a DBCS trail byte.
				Sci::Position posStartLine = PositionLineStart(LineOfPosition(pos));
				// See http://msdn.microsoft.com/en-us/library/cc194792%28v=MSDN.10%29.aspx
				// http://msdn.microsoft.com/en-us/library/cc194790.aspx
				if ((pos - 1) <= posStartLine) {
					return pos - 1;
				} else if (IsDBCSLeadByte(cb.CharAt(pos - 1))) {
					// Must actually be trail byte
					return pos - 2;
				} else {
					// Otherwise, step back until a non-lead-byte is found.
					Sci::Position posTemp = pos - 1;
					while (posStartLine <= --posTemp && IsDBCSLeadByte(cb.CharAt(posTemp)))
						;
					// Now posTemp+1 must point to the beginning of a character,
					// so figure out whether we went back an even or an odd
					// number of bytes and go back 1 or 2 bytes, respectively.
					return (pos - 1 - ((pos - posTemp) & 1));
				}
			}
		}
	} else {
		pos += increment;
	}

	return pos;
}

bool Document::NextCharacter(Sci::Position &pos, int moveDir) const {
	// Returns true if pos changed
	Sci::Position posNext = NextPosition(pos, moveDir);
	if (posNext == pos) {
		return false;
	} else {
		pos = posNext;
		return true;
	}
}

// Return -1  on out-of-bounds
Sci_Position SCI_METHOD Document::GetRelativePosition(Sci_Position positionStart, Sci_Position characterOffset) const {
	Sci::Position pos = static_cast<Sci::Position>(positionStart);
	if (dbcsCodePage) {
		const int increment = (characterOffset > 0) ? 1 : -1;
		while (characterOffset != 0) {
			const Sci::Position posNext = NextPosition(pos, increment);
			if (posNext == pos)
				return INVALID_POSITION;
			pos = posNext;
			characterOffset -= increment;
		}
	} else {
		pos = static_cast<Sci::Position>(positionStart + characterOffset);
		if ((pos < 0) || (pos > Length()))
			return INVALID_POSITION;
	}
	return pos;
}

Sci::Position Document::GetRelativePositionUTF16(Sci::Position positionStart, Sci::Position characterOffset) const {
	Sci::Position pos = positionStart;
	if (dbcsCodePage) {
		const int increment = (characterOffset > 0) ? 1 : -1;
		while (characterOffset != 0) {
			const Sci::Position posNext = NextPosition(pos, increment);
			if (posNext == pos)
				return INVALID_POSITION;
			if (abs(static_cast<int>(pos-posNext)) > 3)	// 4 byte character = 2*UTF16.
				characterOffset -= increment;
			pos = posNext;
			characterOffset -= increment;
		}
	} else {
		pos = positionStart + characterOffset;
		if ((pos < 0) || (pos > Length()))
			return INVALID_POSITION;
	}
	return pos;
}

int SCI_METHOD Document::GetCharacterAndWidth(Sci_Position position, Sci_Position *pWidth) const {
	int character;
	int bytesInCharacter = 1;
	if (dbcsCodePage) {
		const unsigned char leadByte = static_cast<unsigned char>(cb.CharAt(static_cast<Sci::Position>(position)));
		if (SC_CP_UTF8 == dbcsCodePage) {
			if (UTF8IsAscii(leadByte)) {
				// Single byte character or invalid
				character =  leadByte;
			} else {
				const int widthCharBytes = UTF8BytesOfLead[leadByte];
				unsigned char charBytes[UTF8MaxBytes] = {leadByte,0,0,0};
				for (int b=1; b<widthCharBytes; b++)
					charBytes[b] = static_cast<unsigned char>(cb.CharAt(static_cast<Sci::Position>(position+b)));
				int utf8status = UTF8Classify(charBytes, widthCharBytes);
				if (utf8status & UTF8MaskInvalid) {
					// Report as singleton surrogate values which are invalid Unicode
					character =  0xDC80 + leadByte;
				} else {
					bytesInCharacter = utf8status & UTF8MaskWidth;
					character = UnicodeFromUTF8(charBytes);
				}
			}
		} else {
			if (IsDBCSLeadByte(leadByte)) {
				bytesInCharacter = 2;
				character = (leadByte << 8) | static_cast<unsigned char>(cb.CharAt(static_cast<Sci::Position>(position+1)));
			} else {
				character = leadByte;
			}
		}
	} else {
		character = cb.CharAt(static_cast<Sci::Position>(position));
	}
	if (pWidth) {
		*pWidth = bytesInCharacter;
	}
	return character;
}

int SCI_METHOD Document::CodePage() const {
	return dbcsCodePage;
}

bool SCI_METHOD Document::IsDBCSLeadByte(char ch) const {
	// Byte ranges found in Wikipedia articles with relevant search strings in each case
	unsigned char uch = static_cast<unsigned char>(ch);
	switch (dbcsCodePage) {
		case 932:
			// Shift_jis
			return ((uch >= 0x81) && (uch <= 0x9F)) ||
				((uch >= 0xE0) && (uch <= 0xFC));
				// Lead bytes F0 to FC may be a Microsoft addition.
		case 936:
			// GBK
			return (uch >= 0x81) && (uch <= 0xFE);
		case 949:
			// Korean Wansung KS C-5601-1987
			return (uch >= 0x81) && (uch <= 0xFE);
		case 950:
			// Big5
			return (uch >= 0x81) && (uch <= 0xFE);
		case 1361:
			// Korean Johab KS C-5601-1992
			return
				((uch >= 0x84) && (uch <= 0xD3)) ||
				((uch >= 0xD8) && (uch <= 0xDE)) ||
				((uch >= 0xE0) && (uch <= 0xF9));
	}
	return false;
}

static inline bool IsSpaceOrTab(int ch) {
	return ch == ' ' || ch == '\t';
}

// Need to break text into segments near lengthSegment but taking into
// account the encoding to not break inside a UTF-8 or DBCS character
// and also trying to avoid breaking inside a pair of combining characters.
// The segment length must always be long enough (more than 4 bytes)
// so that there will be at least one whole character to make a segment.
// For UTF-8, text must consist only of valid whole characters.
// In preference order from best to worst:
//   1) Break after space
//   2) Break before punctuation
//   3) Break after whole character

int Document::SafeSegment(const char *text, int length, int lengthSegment) const {
	if (length <= lengthSegment)
		return length;
	int lastSpaceBreak = -1;
	int lastPunctuationBreak = -1;
	int lastEncodingAllowedBreak = 0;
	for (int j=0; j < lengthSegment;) {
		unsigned char ch = static_cast<unsigned char>(text[j]);
		if (j > 0) {
			if (IsSpaceOrTab(text[j - 1]) && !IsSpaceOrTab(text[j])) {
				lastSpaceBreak = j;
			}
			if (ch < 'A') {
				lastPunctuationBreak = j;
			}
		}
		lastEncodingAllowedBreak = j;

		if (dbcsCodePage == SC_CP_UTF8) {
			j += UTF8BytesOfLead[ch];
		} else if (dbcsCodePage) {
			j += IsDBCSLeadByte(ch) ? 2 : 1;
		} else {
			j++;
		}
	}
	if (lastSpaceBreak >= 0) {
		return lastSpaceBreak;
	} else if (lastPunctuationBreak >= 0) {
		return lastPunctuationBreak;
	}
	return lastEncodingAllowedBreak;
}

EncodingFamily Document::CodePageFamily() const {
	if (SC_CP_UTF8 == dbcsCodePage)
		return efUnicode;
	else if (dbcsCodePage)
		return efDBCS;
	else
		return efEightBit;
}

void Document::ModifiedAt(Sci::Position pos) {
	if (endStyled > pos)
		endStyled = pos;
}

void Document::CheckReadOnly() {
	if (cb.IsReadOnly() && enteredReadOnlyCount == 0) {
		enteredReadOnlyCount++;
		NotifyModifyAttempt();
		enteredReadOnlyCount--;
	}
}

// Document only modified by gateways DeleteChars, InsertString, Undo, Redo, and SetStyleAt.
// SetStyleAt does not change the persistent state of a document

bool Document::DeleteChars(Sci::Position pos, Sci::Position len) {
	if (pos < 0)
		return false;
	if (len <= 0)
		return false;
	if ((pos + len) > Length())
		return false;
	CheckReadOnly();
	if (enteredModification != 0) {
		return false;
	} else {
		enteredModification++;
		if (!cb.IsReadOnly()) {
			NotifyModified(
			    DocModification(
			        SC_MOD_BEFOREDELETE | SC_PERFORMED_USER,
			        pos, len,
			        0, 0));
			Sci::Position prevLinesTotal = LinesTotal();
			bool startSavePoint = cb.IsSavePoint();
			bool startSequence = false;
			const char *text = cb.DeleteChars(pos, len, startSequence);
			if (startSavePoint && cb.IsCollectingUndo())
				NotifySavePoint(!startSavePoint);
			if ((pos < Length()) || (pos == 0))
				ModifiedAt(pos);
			else
				ModifiedAt(pos-1);
			NotifyModified(
			    DocModification(
			        SC_MOD_DELETETEXT | SC_PERFORMED_USER | (startSequence?SC_STARTACTION:0),
			        pos, len,
			        LinesTotal() - prevLinesTotal, text));
		}
		enteredModification--;
	}
	return !cb.IsReadOnly();
}

/**
 * Insert a string with a length.
 */
Sci::Position Document::InsertString(Sci::Position position, const char *s, Sci::Position insertLength) {
	if (insertLength <= 0) {
		return 0;
	}
	CheckReadOnly();	// Application may change read only state here
	if (cb.IsReadOnly()) {
		return 0;
	}
	if (enteredModification != 0) {
		return 0;
	}
	enteredModification++;
	insertionSet = false;
	insertion.clear();
	NotifyModified(
		DocModification(
			SC_MOD_INSERTCHECK,
			position, insertLength,
			0, s));
	if (insertionSet) {
		s = insertion.c_str();
		insertLength = static_cast<Sci::Position>(insertion.length());
	}
	NotifyModified(
		DocModification(
			SC_MOD_BEFOREINSERT | SC_PERFORMED_USER,
			position, insertLength,
			0, s));
	Sci::Position prevLinesTotal = LinesTotal();
	bool startSavePoint = cb.IsSavePoint();
	bool startSequence = false;
	const char *text = cb.InsertString(position, s, insertLength, startSequence);
	if (startSavePoint && cb.IsCollectingUndo())
		NotifySavePoint(!startSavePoint);
	ModifiedAt(position);
	NotifyModified(
		DocModification(
			SC_MOD_INSERTTEXT | SC_PERFORMED_USER | (startSequence?SC_STARTACTION:0),
			position, insertLength,
			LinesTotal() - prevLinesTotal, text));
	if (insertionSet) {	// Free memory as could be large
		std::string().swap(insertion);
	}
	enteredModification--;
	return insertLength;
}

void Document::ChangeInsertion(const char *s, Sci::Position length) {
	insertionSet = true;
	insertion.assign(s, length);
}

int SCI_METHOD Document::AddData(char *data, Sci_Position length) {
	try {
		Sci::Position position = PositionLength();
		InsertString(position, data, static_cast<Sci::Position>(length));
	} catch (std::bad_alloc &) {
		return SC_STATUS_BADALLOC;
	} catch (...) {
		return SC_STATUS_FAILURE;
	}
	return 0;
}

void * SCI_METHOD Document::ConvertToDocument() {
	return this;
}

Sci::Position Document::Undo() {
	Sci::Position newPos = -1;
	CheckReadOnly();
	if ((enteredModification == 0) && (cb.IsCollectingUndo())) {
		enteredModification++;
		if (!cb.IsReadOnly()) {
			bool startSavePoint = cb.IsSavePoint();
			bool multiLine = false;
			int steps = cb.StartUndo();
			//Platform::DebugPrintf("Steps=%d\n", steps);
			Sci::Position coalescedRemovePos = -1;
			Sci::Position coalescedRemoveLen = 0;
			Sci::Position prevRemoveActionPos = -1;
			Sci::Position prevRemoveActionLen = 0;
			for (int step = 0; step < steps; step++) {
				const Sci::Position prevLinesTotal = LinesTotal();
				const Action &action = cb.GetUndoStep();
				if (action.at == removeAction) {
					NotifyModified(DocModification(
									SC_MOD_BEFOREINSERT | SC_PERFORMED_UNDO, action));
				} else if (action.at == containerAction) {
					DocModification dm(SC_MOD_CONTAINER | SC_PERFORMED_UNDO);
					dm.token = action.position;
					NotifyModified(dm);
					if (!action.mayCoalesce) {
						coalescedRemovePos = -1;
						coalescedRemoveLen = 0;
						prevRemoveActionPos = -1;
						prevRemoveActionLen = 0;
					}
				} else {
					NotifyModified(DocModification(
									SC_MOD_BEFOREDELETE | SC_PERFORMED_UNDO, action));
				}
				cb.PerformUndoStep();
				if (action.at != containerAction) {
					ModifiedAt(action.position);
					newPos = action.position;
				}

				int modFlags = SC_PERFORMED_UNDO;
				// With undo, an insertion action becomes a deletion notification
				if (action.at == removeAction) {
					newPos += action.lenData;
					modFlags |= SC_MOD_INSERTTEXT;
					if ((coalescedRemoveLen > 0) &&
						(action.position == prevRemoveActionPos || action.position == (prevRemoveActionPos + prevRemoveActionLen))) {
						coalescedRemoveLen += action.lenData;
						newPos = coalescedRemovePos + coalescedRemoveLen;
					} else {
						coalescedRemovePos = action.position;
						coalescedRemoveLen = action.lenData;
					}
					prevRemoveActionPos = action.position;
					prevRemoveActionLen = action.lenData;
				} else if (action.at == insertAction) {
					modFlags |= SC_MOD_DELETETEXT;
					coalescedRemovePos = -1;
					coalescedRemoveLen = 0;
					prevRemoveActionPos = -1;
					prevRemoveActionLen = 0;
				}
				if (steps > 1)
					modFlags |= SC_MULTISTEPUNDOREDO;
				const Sci::Position linesAdded = LinesTotal() - prevLinesTotal;
				if (linesAdded != 0)
					multiLine = true;
				if (step == steps - 1) {
					modFlags |= SC_LASTSTEPINUNDOREDO;
					if (multiLine)
						modFlags |= SC_MULTILINEUNDOREDO;
				}
				NotifyModified(DocModification(modFlags, action.position, action.lenData,
											   linesAdded, action.data));
			}

			bool endSavePoint = cb.IsSavePoint();
			if (startSavePoint != endSavePoint)
				NotifySavePoint(endSavePoint);
		}
		enteredModification--;
	}
	return newPos;
}

Sci::Position Document::Redo() {
	Sci::Position newPos = -1;
	CheckReadOnly();
	if ((enteredModification == 0) && (cb.IsCollectingUndo())) {
		enteredModification++;
		if (!cb.IsReadOnly()) {
			bool startSavePoint = cb.IsSavePoint();
			bool multiLine = false;
			int steps = cb.StartRedo();
			for (int step = 0; step < steps; step++) {
				const Sci::Position prevLinesTotal = LinesTotal();
				const Action &action = cb.GetRedoStep();
				if (action.at == insertAction) {
					NotifyModified(DocModification(
									SC_MOD_BEFOREINSERT | SC_PERFORMED_REDO, action));
				} else if (action.at == containerAction) {
					DocModification dm(SC_MOD_CONTAINER | SC_PERFORMED_REDO);
					dm.token = action.position;
					NotifyModified(dm);
				} else {
					NotifyModified(DocModification(
									SC_MOD_BEFOREDELETE | SC_PERFORMED_REDO, action));
				}
				cb.PerformRedoStep();
				if (action.at != containerAction) {
					ModifiedAt(action.position);
					newPos = action.position;
				}

				int modFlags = SC_PERFORMED_REDO;
				if (action.at == insertAction) {
					newPos += action.lenData;
					modFlags |= SC_MOD_INSERTTEXT;
				} else if (action.at == removeAction) {
					modFlags |= SC_MOD_DELETETEXT;
				}
				if (steps > 1)
					modFlags |= SC_MULTISTEPUNDOREDO;
				const Sci::Position linesAdded = LinesTotal() - prevLinesTotal;
				if (linesAdded != 0)
					multiLine = true;
				if (step == steps - 1) {
					modFlags |= SC_LASTSTEPINUNDOREDO;
					if (multiLine)
						modFlags |= SC_MULTILINEUNDOREDO;
				}
				NotifyModified(
					DocModification(modFlags, action.position, action.lenData,
									linesAdded, action.data));
			}

			bool endSavePoint = cb.IsSavePoint();
			if (startSavePoint != endSavePoint)
				NotifySavePoint(endSavePoint);
		}
		enteredModification--;
	}
	return newPos;
}

void Document::DelChar(Sci::Position pos) {
	DeleteChars(pos, LenChar(pos));
}

void Document::DelCharBack(Sci::Position pos) {
	if (pos <= 0) {
		return;
	} else if (IsCrLf(pos - 2)) {
		DeleteChars(pos - 2, 2);
	} else if (dbcsCodePage) {
		Sci::Position startChar = NextPosition(pos, -1);
		DeleteChars(startChar, pos - startChar);
	} else {
		DeleteChars(pos - 1, 1);
	}
}

static int NextTab(int pos, int tabSize) {
	return ((pos / tabSize) + 1) * tabSize;
}

static std::string CreateIndentation(int indent, int tabSize, bool insertSpaces) {
	std::string indentation;
	if (!insertSpaces) {
		while (indent >= tabSize) {
			indentation += '\t';
			indent -= tabSize;
		}
	}
	while (indent > 0) {
		indentation += ' ';
		indent--;
	}
	return indentation;
}

int SCI_METHOD Document::GetLineIndentation(Sci_Position line) {
	int indent = 0;
	if ((line >= 0) && (line < LinesTotal())) {
		Sci::Position lineStart = PositionLineStart(static_cast<Sci::Position>(line));
		Sci::Position length = PositionLength();
		for (Sci::Position i = lineStart; i < length; i++) {
			char ch = cb.CharAt(i);
			if (ch == ' ')
				indent++;
			else if (ch == '\t')
				indent = NextTab(indent, tabInChars);
			else
				return indent;
		}
	}
	return indent;
}

Sci::Position Document::SetLineIndentation(Sci::Position line, int indent) {
	int indentOfLine = GetLineIndentation(line);
	if (indent < 0)
		indent = 0;
	if (indent != indentOfLine) {
		std::string linebuf = CreateIndentation(indent, tabInChars, !useTabs);
		Sci::Position thisLineStart = PositionLineStart(line);
		Sci::Position indentPos = GetLineIndentPosition(line);
		UndoGroup ug(this);
		DeleteChars(thisLineStart, indentPos - thisLineStart);
		return thisLineStart + InsertString(thisLineStart, linebuf.c_str(), 
			static_cast<Sci::Position>(linebuf.length()));
	} else {
		return GetLineIndentPosition(line);
	}
}

Sci::Position Document::GetLineIndentPosition(Sci::Position line) const {
	if (line < 0)
		return 0;
	Sci::Position pos = PositionLineStart(line);
	Sci::Position length = PositionLength();
	while ((pos < length) && IsSpaceOrTab(cb.CharAt(pos))) {
		pos++;
	}
	return pos;
}

int Document::GetColumn(Sci::Position pos) {
	int column = 0;
	Sci::Position line = LineOfPosition(pos);
	if ((line >= 0) && (line < LinesTotal())) {
		for (Sci::Position i = PositionLineStart(line); i < pos;) {
			char ch = cb.CharAt(i);
			if (ch == '\t') {
				column = NextTab(column, tabInChars);
				i++;
			} else if (ch == '\r') {
				return column;
			} else if (ch == '\n') {
				return column;
			} else if (i >= Length()) {
				return column;
			} else {
				column++;
				i = NextPosition(i, 1);
			}
		}
	}
	return column;
}

Sci::Position Document::CountCharacters(Sci::Position startPos, Sci::Position endPos) const {
	startPos = MovePositionOutsideChar(startPos, 1, false);
	endPos = MovePositionOutsideChar(endPos, -1, false);
	Sci::Position count = 0;
	Sci::Position i = startPos;
	while (i < endPos) {
		count++;
		if (IsCrLf(i))
			i++;
		i = NextPosition(i, 1);
	}
	return count;
}

Sci::Position Document::CountUTF16(Sci::Position startPos, Sci::Position endPos) const {
	startPos = MovePositionOutsideChar(startPos, 1, false);
	endPos = MovePositionOutsideChar(endPos, -1, false);
	Sci::Position count = 0;
	Sci::Position i = startPos;
	while (i < endPos) {
		count++;
		const Sci::Position next = NextPosition(i, 1);
		if ((next - i) > 3)
			count++;
		i = next;
	}
	return count;
}

Sci::Position Document::FindColumn(Sci::Position line, int column) {
	Sci::Position position = PositionLineStart(line);
	if ((line >= 0) && (line < LinesTotal())) {
		int columnCurrent = 0;
		while ((columnCurrent < column) && (position < Length())) {
			char ch = cb.CharAt(position);
			if (ch == '\t') {
				columnCurrent = NextTab(columnCurrent, tabInChars);
				if (columnCurrent > column)
					return position;
				position++;
			} else if (ch == '\r') {
				return position;
			} else if (ch == '\n') {
				return position;
			} else {
				columnCurrent++;
				position = NextPosition(position, 1);
			}
		}
	}
	return position;
}

void Document::Indent(bool forwards, Sci::Position lineBottom, Sci::Position lineTop) {
	// Dedent - suck white space off the front of the line to dedent by equivalent of a tab
	for (Sci::Position line = lineBottom; line >= lineTop; line--) {
		int indentOfLine = GetLineIndentation(line);
		if (forwards) {
			if (PositionLineStart(line) < PositionLineEnd(line)) {
				SetLineIndentation(line, indentOfLine + IndentSize());
			}
		} else {
			SetLineIndentation(line, indentOfLine - IndentSize());
		}
	}
}

// Convert line endings for a piece of text to a particular mode.
// Stop at len or when a NUL is found.
std::string Document::TransformLineEnds(const char *s, size_t len, int eolModeWanted) {
	std::string dest;
	for (size_t i = 0; (i < len) && (s[i]); i++) {
		if (s[i] == '\n' || s[i] == '\r') {
			if (eolModeWanted == SC_EOL_CR) {
				dest.push_back('\r');
			} else if (eolModeWanted == SC_EOL_LF) {
				dest.push_back('\n');
			} else { // eolModeWanted == SC_EOL_CRLF
				dest.push_back('\r');
				dest.push_back('\n');
			}
			if ((s[i] == '\r') && (i+1 < len) && (s[i+1] == '\n')) {
				i++;
			}
		} else {
			dest.push_back(s[i]);
		}
	}
	return dest;
}

void Document::ConvertLineEnds(int eolModeSet) {
	UndoGroup ug(this);

	for (Sci::Position pos = 0; pos < Length(); pos++) {
		if (cb.CharAt(pos) == '\r') {
			if (cb.CharAt(pos + 1) == '\n') {
				// CRLF
				if (eolModeSet == SC_EOL_CR) {
					DeleteChars(pos + 1, 1); // Delete the LF
				} else if (eolModeSet == SC_EOL_LF) {
					DeleteChars(pos, 1); // Delete the CR
				} else {
					pos++;
				}
			} else {
				// CR
				if (eolModeSet == SC_EOL_CRLF) {
					pos += InsertString(pos + 1, "\n", 1); // Insert LF
				} else if (eolModeSet == SC_EOL_LF) {
					pos += InsertString(pos, "\n", 1); // Insert LF
					DeleteChars(pos, 1); // Delete CR
					pos--;
				}
			}
		} else if (cb.CharAt(pos) == '\n') {
			// LF
			if (eolModeSet == SC_EOL_CRLF) {
				pos += InsertString(pos, "\r", 1); // Insert CR
			} else if (eolModeSet == SC_EOL_CR) {
				pos += InsertString(pos, "\r", 1); // Insert CR
				DeleteChars(pos, 1); // Delete LF
				pos--;
			}
		}
	}

}

bool Document::IsWhiteLine(Sci::Position line) const {
	Sci::Position currentChar = PositionLineStart(line);
	Sci::Position endLine = PositionLineEnd(line);
	while (currentChar < endLine) {
		if (cb.CharAt(currentChar) != ' ' && cb.CharAt(currentChar) != '\t') {
			return false;
		}
		++currentChar;
	}
	return true;
}

Sci::Position Document::ParaUp(Sci::Position pos) const {
	Sci::Position line = LineOfPosition(pos);
	line--;
	while (line >= 0 && IsWhiteLine(line)) { // skip empty lines
		line--;
	}
	while (line >= 0 && !IsWhiteLine(line)) { // skip non-empty lines
		line--;
	}
	line++;
	return PositionLineStart(line);
}

Sci::Position Document::ParaDown(Sci::Position pos) const {
	Sci::Position line = LineOfPosition(pos);
	while (line < LinesTotal() && !IsWhiteLine(line)) { // skip non-empty lines
		line++;
	}
	while (line < LinesTotal() && IsWhiteLine(line)) { // skip empty lines
		line++;
	}
	if (line < LinesTotal())
		return PositionLineStart(line);
	else // end of a document
		return PositionLineEnd(line-1);
}

CharClassify::cc Document::WordCharClass(unsigned char ch) const {
	if ((SC_CP_UTF8 == dbcsCodePage) && (!UTF8IsAscii(ch)))
		return CharClassify::ccWord;
	return charClass.GetClass(ch);
}

/**
 * Used by commmands that want to select whole words.
 * Finds the start of word at pos when delta < 0 or the end of the word when delta >= 0.
 */
Sci::Position Document::ExtendWordSelect(Sci::Position pos, int delta, bool onlyWordCharacters) {
	CharClassify::cc ccStart = CharClassify::ccWord;
	if (delta < 0) {
		if (!onlyWordCharacters)
			ccStart = WordCharClass(cb.CharAt(pos-1));
		while (pos > 0 && (WordCharClass(cb.CharAt(pos - 1)) == ccStart))
			pos--;
	} else {
		if (!onlyWordCharacters && pos < Length())
			ccStart = WordCharClass(cb.CharAt(pos));
		while (pos < (Length()) && (WordCharClass(cb.CharAt(pos)) == ccStart))
			pos++;
	}
	return MovePositionOutsideChar(pos, delta, true);
}

/**
 * Find the start of the next word in either a forward (delta >= 0) or backwards direction
 * (delta < 0).
 * This is looking for a transition between character classes although there is also some
 * additional movement to transit white space.
 * Used by cursor movement by word commands.
 */
Sci::Position Document::NextWordStart(Sci::Position pos, int delta) {
	if (delta < 0) {
		while (pos > 0 && (WordCharClass(cb.CharAt(pos - 1)) == CharClassify::ccSpace))
			pos--;
		if (pos > 0) {
			CharClassify::cc ccStart = WordCharClass(cb.CharAt(pos-1));
			while (pos > 0 && (WordCharClass(cb.CharAt(pos - 1)) == ccStart)) {
				pos--;
			}
		}
	} else {
		CharClassify::cc ccStart = WordCharClass(cb.CharAt(pos));
		while (pos < (Length()) && (WordCharClass(cb.CharAt(pos)) == ccStart))
			pos++;
		while (pos < (Length()) && (WordCharClass(cb.CharAt(pos)) == CharClassify::ccSpace))
			pos++;
	}
	return pos;
}

/**
 * Find the end of the next word in either a forward (delta >= 0) or backwards direction
 * (delta < 0).
 * This is looking for a transition between character classes although there is also some
 * additional movement to transit white space.
 * Used by cursor movement by word commands.
 */
Sci::Position Document::NextWordEnd(Sci::Position pos, int delta) {
	if (delta < 0) {
		if (pos > 0) {
			CharClassify::cc ccStart = WordCharClass(cb.CharAt(pos-1));
			if (ccStart != CharClassify::ccSpace) {
				while (pos > 0 && WordCharClass(cb.CharAt(pos - 1)) == ccStart) {
					pos--;
				}
			}
			while (pos > 0 && WordCharClass(cb.CharAt(pos - 1)) == CharClassify::ccSpace) {
				pos--;
			}
		}
	} else {
		while (pos < Length() && WordCharClass(cb.CharAt(pos)) == CharClassify::ccSpace) {
			pos++;
		}
		if (pos < Length()) {
			CharClassify::cc ccStart = WordCharClass(cb.CharAt(pos));
			while (pos < Length() && WordCharClass(cb.CharAt(pos)) == ccStart) {
				pos++;
			}
		}
	}
	return pos;
}

/**
 * Check that the character at the given position is a word or punctuation character and that
 * the previous character is of a different character class.
 */
bool Document::IsWordStartAt(Sci::Position pos) const {
	if (pos > 0) {
		CharClassify::cc ccPos = WordCharClass(CharAt(pos));
		return (ccPos == CharClassify::ccWord || ccPos == CharClassify::ccPunctuation) &&
			(ccPos != WordCharClass(CharAt(pos - 1)));
	}
	return true;
}

/**
 * Check that the character at the given position is a word or punctuation character and that
 * the next character is of a different character class.
 */
bool Document::IsWordEndAt(Sci::Position pos) const {
	if (pos < Length()) {
		CharClassify::cc ccPrev = WordCharClass(CharAt(pos-1));
		return (ccPrev == CharClassify::ccWord || ccPrev == CharClassify::ccPunctuation) &&
			(ccPrev != WordCharClass(CharAt(pos)));
	}
	return true;
}

/**
 * Check that the given range is has transitions between character classes at both
 * ends and where the characters on the inside are word or punctuation characters.
 */
bool Document::IsWordAt(Sci::Position start, Sci::Position end) const {
	return (start < end) && IsWordStartAt(start) && IsWordEndAt(end);
}

bool Document::MatchesWordOptions(bool word, bool wordStart, Sci::Position pos, Sci::Position length) const {
	return (!word && !wordStart) ||
			(word && IsWordAt(pos, pos + length)) ||
			(wordStart && IsWordStartAt(pos));
}

bool Document::HasCaseFolder(void) const {
	return pcf != 0;
}

void Document::SetCaseFolder(CaseFolder *pcf_) {
	delete pcf;
	pcf = pcf_;
}

Document::CharacterExtracted Document::ExtractCharacter(Sci::Position position) const {
	const unsigned char leadByte = static_cast<unsigned char>(cb.CharAt(position));
	if (UTF8IsAscii(leadByte)) {
		// Common case: ASCII character
		return CharacterExtracted(leadByte, 1);
	}
	const int widthCharBytes = UTF8BytesOfLead[leadByte];
	unsigned char charBytes[UTF8MaxBytes] = { leadByte, 0, 0, 0 };
	for (int b=1; b<widthCharBytes; b++)
		charBytes[b] = static_cast<unsigned char>(cb.CharAt(position + b));
	int utf8status = UTF8Classify(charBytes, widthCharBytes);
	if (utf8status & UTF8MaskInvalid) {
		// Treat as invalid and use up just one byte
		return CharacterExtracted(unicodeReplacementChar, 1);
	} else {
		return CharacterExtracted(UnicodeFromUTF8(charBytes), utf8status & UTF8MaskWidth);
	}
}

/**
 * Find text in document, supporting both forward and backward
 * searches (just pass minPos > maxPos to do a backward search)
 * Has not been tested with backwards DBCS searches yet.
 */
Sci::Position Document::FindText(Sci::Position minPos, Sci::Position maxPos, const char *search,
                        int flags, Sci::Position *length) {
	if (*length <= 0)
		return minPos;
	const bool caseSensitive = (flags & SCFIND_MATCHCASE) != 0;
	const bool word = (flags & SCFIND_WHOLEWORD) != 0;
	const bool wordStart = (flags & SCFIND_WORDSTART) != 0;
	const bool regExp = (flags & SCFIND_REGEXP) != 0;
	if (regExp) {
		if (!regex)
			regex = CreateRegexSearch(&charClass);
		return regex->FindText(this, minPos, maxPos, search, caseSensitive, word, wordStart, flags, length);
	} else {

		const bool forward = minPos <= maxPos;
		const int increment = forward ? 1 : -1;

		// Range endpoints should not be inside DBCS characters, but just in case, move them.
		const Sci::Position startPos = MovePositionOutsideChar(minPos, increment, false);
		const Sci::Position endPos = MovePositionOutsideChar(maxPos, increment, false);

		// Compute actual search ranges needed
		const Sci::Position lengthFind = *length;

		//Platform::DebugPrintf("Find %d %d %s %d\n", startPos, endPos, ft->lpstrText, lengthFind);
		const Sci::Position limitPos = std::max(startPos, endPos);
		Sci::Position pos = startPos;
		if (!forward) {
			// Back all of a character
			pos = NextPosition(pos, increment);
		}
		if (caseSensitive) {
			const Sci::Position endSearch = (startPos <= endPos) ? endPos - lengthFind + 1 : endPos;
			const char charStartSearch =  search[0];
			while (forward ? (pos < endSearch) : (pos >= endSearch)) {
				if (CharAt(pos) == charStartSearch) {
					bool found = (pos + lengthFind) <= limitPos;
					for (Sci::Position indexSearch = 1; (indexSearch < lengthFind) && found; indexSearch++) {
						found = CharAt(pos + indexSearch) == search[indexSearch];
					}
					if (found && MatchesWordOptions(word, wordStart, pos, lengthFind)) {
						return pos;
					}
				}
				if (!NextCharacter(pos, increment))
					break;
			}
		} else if (SC_CP_UTF8 == dbcsCodePage) {
			const size_t maxFoldingExpansion = 4;
			std::vector<char> searchThing(lengthFind * UTF8MaxBytes * maxFoldingExpansion + 1);
			const int lenSearch = static_cast<int>(
				pcf->Fold(&searchThing[0], searchThing.size(), search, lengthFind));
			char bytes[UTF8MaxBytes + 1];
			char folded[UTF8MaxBytes * maxFoldingExpansion + 1];
			while (forward ? (pos < endPos) : (pos >= endPos)) {
				int widthFirstCharacter = 0;
				Sci::Position posIndexDocument = pos;
				int indexSearch = 0;
				bool characterMatches = true;
				for (;;) {
					const unsigned char leadByte = static_cast<unsigned char>(cb.CharAt(posIndexDocument));
					bytes[0] = leadByte;
					int widthChar = 1;
					if (!UTF8IsAscii(leadByte)) {
						const int widthCharBytes = UTF8BytesOfLead[leadByte];
						for (int b=1; b<widthCharBytes; b++) {
							bytes[b] = cb.CharAt(posIndexDocument+b);
						}
						widthChar = UTF8Classify(reinterpret_cast<const unsigned char *>(bytes), widthCharBytes) & UTF8MaskWidth;
					}
					if (!widthFirstCharacter)
						widthFirstCharacter = widthChar;
					if ((posIndexDocument + widthChar) > limitPos)
						break;
					const int lenFlat = static_cast<int>(pcf->Fold(folded, sizeof(folded), bytes, widthChar));
					folded[lenFlat] = 0;
					// Does folded match the buffer
					characterMatches = 0 == memcmp(folded, &searchThing[0] + indexSearch, lenFlat);
					if (!characterMatches)
						break;
					posIndexDocument += widthChar;
					indexSearch += lenFlat;
					if (indexSearch >= lenSearch)
						break;
				}
				if (characterMatches && (indexSearch == static_cast<int>(lenSearch))) {
					if (MatchesWordOptions(word, wordStart, pos, posIndexDocument - pos)) {
						*length = posIndexDocument - pos;
						return pos;
					}
				}
				if (forward) {
					pos += widthFirstCharacter;
				} else {
					if (!NextCharacter(pos, increment))
						break;
				}
			}
		} else if (dbcsCodePage) {
			const size_t maxBytesCharacter = 2;
			const size_t maxFoldingExpansion = 4;
			std::vector<char> searchThing(lengthFind * maxBytesCharacter * maxFoldingExpansion + 1);
			const int lenSearch = static_cast<int>(
				pcf->Fold(&searchThing[0], searchThing.size(), search, lengthFind));
			while (forward ? (pos < endPos) : (pos >= endPos)) {
				int indexDocument = 0;
				int indexSearch = 0;
				bool characterMatches = true;
				while (characterMatches &&
					((pos + indexDocument) < limitPos) &&
					(indexSearch < lenSearch)) {
					char bytes[maxBytesCharacter + 1];
					bytes[0] = cb.CharAt(pos + indexDocument);
					const int widthChar = IsDBCSLeadByte(bytes[0]) ? 2 : 1;
					if (widthChar == 2)
						bytes[1] = cb.CharAt(pos + indexDocument + 1);
					if ((pos + indexDocument + widthChar) > limitPos)
						break;
					char folded[maxBytesCharacter * maxFoldingExpansion + 1];
					const int lenFlat = static_cast<int>(pcf->Fold(folded, sizeof(folded), bytes, widthChar));
					folded[lenFlat] = 0;
					// Does folded match the buffer
					characterMatches = 0 == memcmp(folded, &searchThing[0] + indexSearch, lenFlat);
					indexDocument += widthChar;
					indexSearch += lenFlat;
				}
				if (characterMatches && (indexSearch == static_cast<int>(lenSearch))) {
					if (MatchesWordOptions(word, wordStart, pos, indexDocument)) {
						*length = indexDocument;
						return pos;
					}
				}
				if (!NextCharacter(pos, increment))
					break;
			}
		} else {
			const Sci::Position endSearch = (startPos <= endPos) ? endPos - lengthFind + 1 : endPos;
			std::vector<char> searchThing(lengthFind + 1);
			pcf->Fold(&searchThing[0], searchThing.size(), search, lengthFind);
			while (forward ? (pos < endSearch) : (pos >= endSearch)) {
				bool found = (pos + lengthFind) <= limitPos;
				for (Sci::Position indexSearch = 0; (indexSearch < lengthFind) && found; indexSearch++) {
					char ch = CharAt(pos + indexSearch);
					char folded[2];
					pcf->Fold(folded, sizeof(folded), &ch, 1);
					found = folded[0] == searchThing[indexSearch];
				}
				if (found && MatchesWordOptions(word, wordStart, pos, lengthFind)) {
					return pos;
				}
				if (!NextCharacter(pos, increment))
					break;
			}
		}
	}
	//Platform::DebugPrintf("Not found\n");
	return -1;
}

const char *Document::SubstituteByPosition(const char *text, Sci::Position *length) {
	if (regex)
		return regex->SubstituteByPosition(this, text, length);
	else
		return 0;
}

Sci::Position Document::LinesTotal() const {
	return cb.Lines();
}

void Document::SetDefaultCharClasses(bool includeWordClass) {
    charClass.SetDefaultCharClasses(includeWordClass);
}

void Document::SetCharClasses(const unsigned char *chars, CharClassify::cc newCharClass) {
    charClass.SetCharClasses(chars, newCharClass);
}

int Document::GetCharsOfClass(CharClassify::cc characterClass, unsigned char *buffer) {
    return charClass.GetCharsOfClass(characterClass, buffer);
}

void SCI_METHOD Document::StartStyling(Sci_Position position, char) {
	endStyled = static_cast<Sci::Position>(position);
}

bool SCI_METHOD Document::SetStyleFor(Sci_Position length, char style) {
	if (enteredStyling != 0) {
		return false;
	} else {
		enteredStyling++;
		Sci::Position prevEndStyled = endStyled;
		if (cb.SetStyleFor(endStyled, static_cast<Sci::Position>(length), style)) {
			DocModification mh(SC_MOD_CHANGESTYLE | SC_PERFORMED_USER,
			                   prevEndStyled, static_cast<Sci::Position>(length));
			NotifyModified(mh);
		}
		endStyled += static_cast<Sci::Position>(length);
		enteredStyling--;
		return true;
	}
}

bool SCI_METHOD Document::SetStyles(Sci_Position length, const char *styles) {
	if (enteredStyling != 0) {
		return false;
	} else {
		enteredStyling++;
		bool didChange = false;
		Sci::Position startMod = 0;
		Sci::Position endMod = 0;
		for (Sci::Position iPos = 0; iPos < static_cast<Sci::Position>(length); iPos++, endStyled++) {
			PLATFORM_ASSERT(endStyled < Length());
			if (cb.SetStyleAt(endStyled, styles[iPos])) {
				if (!didChange) {
					startMod = endStyled;
				}
				didChange = true;
				endMod = endStyled;
			}
		}
		if (didChange) {
			DocModification mh(SC_MOD_CHANGESTYLE | SC_PERFORMED_USER,
			                   startMod, endMod - startMod + 1);
			NotifyModified(mh);
		}
		enteredStyling--;
		return true;
	}
}

void Document::EnsureStyledTo(Sci::Position pos) {
	if ((enteredStyling == 0) && (pos > GetEndStyled())) {
		IncrementStyleClock();
		if (pli && !pli->UseContainerLexing()) {
			Sci::Position lineEndStyled = LineOfPosition(GetEndStyled());
			Sci::Position endStyledTo = PositionLineStart(lineEndStyled);
			pli->Colourise(endStyledTo, pos);
		} else {
			// Ask the watchers to style, and stop as soon as one responds.
			for (std::vector<WatcherWithUserData>::iterator it = watchers.begin();
				(pos > GetEndStyled()) && (it != watchers.end()); ++it) {
				it->watcher->NotifyStyleNeeded(this, it->userData, pos);
			}
		}
	}
}

void Document::LexerChanged() {
	// Tell the watchers the lexer has changed.
	for (std::vector<WatcherWithUserData>::iterator it = watchers.begin(); it != watchers.end(); ++it) {
		it->watcher->NotifyLexerChanged(this, it->userData);
	}
}

int SCI_METHOD Document::SetLineState(Sci_Position line, int state) {
	int statePrevious = static_cast<LineState *>(perLineData[ldState])->SetLineState(static_cast<Sci::Position>(line), state);
	if (state != statePrevious) {
		DocModification mh(SC_MOD_CHANGELINESTATE, PositionLineStart(static_cast<Sci::Position>(line)), 0, 0, 0, static_cast<Sci::Position>(line));
		NotifyModified(mh);
	}
	return statePrevious;
}

int SCI_METHOD Document::GetLineState(Sci_Position line) const {
	return static_cast<LineState *>(perLineData[ldState])->GetLineState(static_cast<Sci::Position>(line));
}

Sci::Position Document::GetMaxLineState() {
	return static_cast<LineState *>(perLineData[ldState])->GetMaxLineState();
}

void SCI_METHOD Document::ChangeLexerState(Sci_Position start, Sci_Position end) {
	DocModification mh(SC_MOD_LEXERSTATE, static_cast<Sci::Position>(start), static_cast<Sci::Position>(end-start), 0, 0, 0);
	NotifyModified(mh);
}

StyledText Document::MarginStyledText(Sci::Position line) const {
	LineAnnotation *pla = static_cast<LineAnnotation *>(perLineData[ldMargin]);
	return StyledText(pla->Length(line), pla->Text(line),
		pla->MultipleStyles(line), pla->Style(line), pla->Styles(line));
}

void Document::MarginSetText(Sci::Position line, const char *text) {
	static_cast<LineAnnotation *>(perLineData[ldMargin])->SetText(line, text);
	DocModification mh(SC_MOD_CHANGEMARGIN, PositionLineStart(line), 0, 0, 0, line);
	NotifyModified(mh);
}

void Document::MarginSetStyle(Sci::Position line, int style) {
	static_cast<LineAnnotation *>(perLineData[ldMargin])->SetStyle(line, style);
	NotifyModified(DocModification(SC_MOD_CHANGEMARGIN, PositionLineStart(line), 0, 0, 0, line));
}

void Document::MarginSetStyles(Sci::Position line, const unsigned char *styles) {
	static_cast<LineAnnotation *>(perLineData[ldMargin])->SetStyles(line, styles);
	NotifyModified(DocModification(SC_MOD_CHANGEMARGIN, PositionLineStart(line), 0, 0, 0, line));
}

void Document::MarginClearAll() {
	Sci::Position maxEditorLine = LinesTotal();
	for (Sci::Position l=0; l<maxEditorLine; l++)
		MarginSetText(l, 0);
	// Free remaining data
	static_cast<LineAnnotation *>(perLineData[ldMargin])->ClearAll();
}

StyledText Document::AnnotationStyledText(Sci::Position line) const {
	LineAnnotation *pla = static_cast<LineAnnotation *>(perLineData[ldAnnotation]);
	return StyledText(pla->Length(line), pla->Text(line),
		pla->MultipleStyles(line), pla->Style(line), pla->Styles(line));
}

void Document::AnnotationSetText(Sci::Position line, const char *text) {
	if (line >= 0 && line < LinesTotal()) {
		const int linesBefore = AnnotationLines(line);
		static_cast<LineAnnotation *>(perLineData[ldAnnotation])->SetText(line, text);
		const int linesAfter = AnnotationLines(line);
		DocModification mh(SC_MOD_CHANGEANNOTATION, PositionLineStart(line), 0, 0, 0, line);
		mh.annotationLinesAdded = linesAfter - linesBefore;
		NotifyModified(mh);
	}
}

void Document::AnnotationSetStyle(Sci::Position line, int style) {
	static_cast<LineAnnotation *>(perLineData[ldAnnotation])->SetStyle(line, style);
	DocModification mh(SC_MOD_CHANGEANNOTATION, PositionLineStart(line), 0, 0, 0, line);
	NotifyModified(mh);
}

void Document::AnnotationSetStyles(Sci::Position line, const unsigned char *styles) {
	if (line >= 0 && line < LinesTotal()) {
		static_cast<LineAnnotation *>(perLineData[ldAnnotation])->SetStyles(line, styles);
	}
}

int Document::AnnotationLines(Sci::Position line) const {
	return static_cast<LineAnnotation *>(perLineData[ldAnnotation])->Lines(line);
}

void Document::AnnotationClearAll() {
	Sci::Position maxEditorLine = LinesTotal();
	for (Sci::Position l=0; l<maxEditorLine; l++)
		AnnotationSetText(l, 0);
	// Free remaining data
	static_cast<LineAnnotation *>(perLineData[ldAnnotation])->ClearAll();
}

void Document::IncrementStyleClock() {
	styleClock = (styleClock + 1) % 0x100000;
}

void SCI_METHOD Document::DecorationFillRange(Sci_Position position, int value, Sci_Position fillLength) {
	Sci::Position positionP = static_cast<Sci::Position>(position);
	Sci::Position fillLengthP = static_cast<Sci::Position>(fillLength);
	if (decorations.FillRange(positionP, value, fillLengthP)) {
		DocModification mh(SC_MOD_CHANGEINDICATOR | SC_PERFORMED_USER,
							positionP, fillLengthP);
		NotifyModified(mh);
	}
}

bool Document::AddWatcher(DocWatcher *watcher, void *userData) {
	WatcherWithUserData wwud(watcher, userData);
	std::vector<WatcherWithUserData>::iterator it =
		std::find(watchers.begin(), watchers.end(), wwud);
	if (it != watchers.end())
		return false;
	watchers.push_back(wwud);
	return true;
}

bool Document::RemoveWatcher(DocWatcher *watcher, void *userData) {
	std::vector<WatcherWithUserData>::iterator it =
		std::find(watchers.begin(), watchers.end(), WatcherWithUserData(watcher, userData));
	if (it != watchers.end()) {
		watchers.erase(it);
		return true;
	}
	return false;
}

void Document::NotifyModifyAttempt() {
	for (std::vector<WatcherWithUserData>::iterator it = watchers.begin(); it != watchers.end(); ++it) {
		it->watcher->NotifyModifyAttempt(this, it->userData);
	}
}

void Document::NotifySavePoint(bool atSavePoint) {
	for (std::vector<WatcherWithUserData>::iterator it = watchers.begin(); it != watchers.end(); ++it) {
		it->watcher->NotifySavePoint(this, it->userData, atSavePoint);
	}
}

void Document::NotifyModified(DocModification mh) {
	if (mh.modificationType & SC_MOD_INSERTTEXT) {
		decorations.InsertSpace(mh.position, mh.length);
	} else if (mh.modificationType & SC_MOD_DELETETEXT) {
		decorations.DeleteRange(mh.position, mh.length);
	}
	for (std::vector<WatcherWithUserData>::iterator it = watchers.begin(); it != watchers.end(); ++it) {
		it->watcher->NotifyModified(this, mh, it->userData);
	}
}

bool Document::IsWordPartSeparator(char ch) const {
	return (WordCharClass(ch) == CharClassify::ccWord) && IsPunctuation(ch);
}

Sci::Position Document::WordPartLeft(Sci::Position pos) {
	if (pos > 0) {
		--pos;
		char startChar = cb.CharAt(pos);
		if (IsWordPartSeparator(startChar)) {
			while (pos > 0 && IsWordPartSeparator(cb.CharAt(pos))) {
				--pos;
			}
		}
		if (pos > 0) {
			startChar = cb.CharAt(pos);
			--pos;
			if (IsLowerCase(startChar)) {
				while (pos > 0 && IsLowerCase(cb.CharAt(pos)))
					--pos;
				if (!IsUpperCase(cb.CharAt(pos)) && !IsLowerCase(cb.CharAt(pos)))
					++pos;
			} else if (IsUpperCase(startChar)) {
				while (pos > 0 && IsUpperCase(cb.CharAt(pos)))
					--pos;
				if (!IsUpperCase(cb.CharAt(pos)))
					++pos;
			} else if (IsADigit(startChar)) {
				while (pos > 0 && IsADigit(cb.CharAt(pos)))
					--pos;
				if (!IsADigit(cb.CharAt(pos)))
					++pos;
			} else if (IsPunctuation(startChar)) {
				while (pos > 0 && IsPunctuation(cb.CharAt(pos)))
					--pos;
				if (!IsPunctuation(cb.CharAt(pos)))
					++pos;
			} else if (isspacechar(startChar)) {
				while (pos > 0 && isspacechar(cb.CharAt(pos)))
					--pos;
				if (!isspacechar(cb.CharAt(pos)))
					++pos;
			} else if (!IsASCII(startChar)) {
				while (pos > 0 && !IsASCII(cb.CharAt(pos)))
					--pos;
				if (IsASCII(cb.CharAt(pos)))
					++pos;
			} else {
				++pos;
			}
		}
	}
	return pos;
}

Sci::Position Document::WordPartRight(Sci::Position pos) {
	char startChar = cb.CharAt(pos);
	Sci::Position length = PositionLength();
	if (IsWordPartSeparator(startChar)) {
		while (pos < length && IsWordPartSeparator(cb.CharAt(pos)))
			++pos;
		startChar = cb.CharAt(pos);
	}
	if (!IsASCII(startChar)) {
		while (pos < length && !IsASCII(cb.CharAt(pos)))
			++pos;
	} else if (IsLowerCase(startChar)) {
		while (pos < length && IsLowerCase(cb.CharAt(pos)))
			++pos;
	} else if (IsUpperCase(startChar)) {
		if (IsLowerCase(cb.CharAt(pos + 1))) {
			++pos;
			while (pos < length && IsLowerCase(cb.CharAt(pos)))
				++pos;
		} else {
			while (pos < length && IsUpperCase(cb.CharAt(pos)))
				++pos;
		}
		if (IsLowerCase(cb.CharAt(pos)) && IsUpperCase(cb.CharAt(pos - 1)))
			--pos;
	} else if (IsADigit(startChar)) {
		while (pos < length && IsADigit(cb.CharAt(pos)))
			++pos;
	} else if (IsPunctuation(startChar)) {
		while (pos < length && IsPunctuation(cb.CharAt(pos)))
			++pos;
	} else if (isspacechar(startChar)) {
		while (pos < length && isspacechar(cb.CharAt(pos)))
			++pos;
	} else {
		++pos;
	}
	return pos;
}

bool IsLineEndChar(char c) {
	return (c == '\n' || c == '\r');
}

Sci::Position Document::ExtendStyleRange(Sci::Position pos, int delta, bool singleLine) {
	int sStart = cb.StyleAt(pos);
	if (delta < 0) {
		while (pos > 0 && (cb.StyleAt(pos) == sStart) && (!singleLine || !IsLineEndChar(cb.CharAt(pos))))
			pos--;
		pos++;
	} else {
		while (pos < (Length()) && (cb.StyleAt(pos) == sStart) && (!singleLine || !IsLineEndChar(cb.CharAt(pos))))
			pos++;
	}
	return pos;
}

static char BraceOpposite(char ch) {
	switch (ch) {
	case '(':
		return ')';
	case ')':
		return '(';
	case '[':
		return ']';
	case ']':
		return '[';
	case '{':
		return '}';
	case '}':
		return '{';
	case '<':
		return '>';
	case '>':
		return '<';
	default:
		return '\0';
	}
}

// TODO: should be able to extend styled region to find matching brace
Sci::Position Document::BraceMatch(Sci::Position position, Sci::Position /*maxReStyle*/) {
	char chBrace = CharAt(position);
	char chSeek = BraceOpposite(chBrace);
	if (chSeek == '\0')
		return - 1;
	char styBrace = static_cast<char>(StyleAt(position));
	int direction = -1;
	if (chBrace == '(' || chBrace == '[' || chBrace == '{' || chBrace == '<')
		direction = 1;
	int depth = 1;
	position = NextPosition(position, direction);
	while ((position >= 0) && (position < Length())) {
		char chAtPos = CharAt(position);
		char styAtPos = static_cast<char>(StyleAt(position));
		if ((position > GetEndStyled()) || (styAtPos == styBrace)) {
			if (chAtPos == chBrace)
				depth++;
			if (chAtPos == chSeek)
				depth--;
			if (depth == 0)
				return position;
		}
		Sci::Position positionBeforeMove = position;
		position = NextPosition(position, direction);
		if (position == positionBeforeMove)
			break;
	}
	return - 1;
}

/**
 * Implementation of RegexSearchBase for the default built-in regular expression engine
 */
class BuiltinRegex : public RegexSearchBase {
public:
	explicit BuiltinRegex(CharClassify *charClassTable) : search(charClassTable) {}

	virtual ~BuiltinRegex() {
	}

	virtual Sci::Position FindText(Document *doc, Sci::Position minPos, Sci::Position maxPos, const char *s,
                        bool caseSensitive, bool word, bool wordStart, int flags,
                        Sci::Position *length);

	virtual const char *SubstituteByPosition(Document *doc, const char *text, Sci::Position *length);

private:
	RESearch search;
	std::string substituted;
};

namespace {

/**
* RESearchRange keeps track of search range.
*/
class RESearchRange {
public:
	const Document *doc;
	int increment;
	Sci::Position startPos;
	Sci::Position endPos;
	Sci::Position lineRangeStart;
	Sci::Position lineRangeEnd;
	Sci::Position lineRangeBreak;
	RESearchRange(const Document *doc_, Sci::Position minPos, Sci::Position maxPos) : doc(doc_) {
		increment = (minPos <= maxPos) ? 1 : -1;

		// Range endpoints should not be inside DBCS characters, but just in case, move them.
		startPos = doc->MovePositionOutsideChar(minPos, 1, false);
		endPos = doc->MovePositionOutsideChar(maxPos, 1, false);

		lineRangeStart = doc->LineOfPosition(startPos);
		lineRangeEnd = doc->LineOfPosition(endPos);
		if ((increment == 1) &&
			(startPos >= doc->PositionLineEnd(lineRangeStart)) &&
			(lineRangeStart < lineRangeEnd)) {
			// the start position is at end of line or between line end characters.
			lineRangeStart++;
			startPos = doc->PositionLineStart(lineRangeStart);
		} else if ((increment == -1) &&
			(startPos <= doc->PositionLineStart(lineRangeStart)) &&
			(lineRangeStart > lineRangeEnd)) {
			// the start position is at beginning of line.
			lineRangeStart--;
			startPos = doc->PositionLineEnd(lineRangeStart);
		}
		lineRangeBreak = lineRangeEnd + increment;
	}
	Range LineRange(Sci::Position line) const {
		Range range(doc->PositionLineStart(line), doc->PositionLineEnd(line));
		if (increment == 1) {
			if (line == lineRangeStart)
				range.start = startPos;
			if (line == lineRangeEnd)
				range.end = endPos;
		} else {
			if (line == lineRangeEnd)
				range.start = endPos;
			if (line == lineRangeStart)
				range.end = startPos;
		}
		return range;
	}
};

// Define a way for the Regular Expression code to access the document
class DocumentIndexer : public CharacterIndexer {
	Document *pdoc;
	Sci::Position end;
public:
	DocumentIndexer(Document *pdoc_, Sci::Position end_) :
		pdoc(pdoc_), end(end_) {
	}

	virtual ~DocumentIndexer() {
	}

	virtual char CharAt(Sci::Position index) {
		if (index < 0 || index >= end)
			return 0;
		else
			return pdoc->CharAt(index);
	}
};

#ifdef CXX11_REGEX

class ByteIterator : public std::iterator<std::bidirectional_iterator_tag, char> {
public:
	const Document *doc;
	Sci::Position position;
	ByteIterator(const Document *doc_ = 0, Sci::Position position_ = 0) : doc(doc_), position(position_) {
	}
	ByteIterator(const ByteIterator &other) {
		doc = other.doc;
		position = other.position;
	}
	ByteIterator &operator=(const ByteIterator &other) {
		if (this != &other) {
			doc = other.doc;
			position = other.position;
		}
		return *this;
	}
	char operator*() const {
		return doc->CharAt(position);
	}
	ByteIterator &operator++() {
		position++;
		return *this;
	}
	ByteIterator operator++(int) {
		ByteIterator retVal(*this);
		position++;
		return retVal;
	}
	ByteIterator &operator--() {
		position--;
		return *this;
	}
	bool operator==(const ByteIterator &other) const {
		return doc == other.doc && position == other.position;
	}
	bool operator!=(const ByteIterator &other) const {
		return doc != other.doc || position != other.position;
	}
	Sci::Position Pos() const {
		return position;
	}
	Sci::Position PosRoundUp() const {
		return position;
	}
};

// On Windows, wchar_t is 16 bits wide and on Unix it is 32 bits wide.
// Would be better to use sizeof(wchar_t) or similar to differentiate
// but easier for now to hard-code platforms.
// C++11 has char16_t and char32_t but neither Clang nor Visual C++
// appear to allow specializing basic_regex over these.

#ifdef _WIN32
#define WCHAR_T_IS_16 1
#else
#define WCHAR_T_IS_16 0
#endif

#if WCHAR_T_IS_16

// On Windows, report non-BMP characters as 2 separate surrogates as that
// matches wregex since it is based on wchar_t.
class UTF8Iterator : public std::iterator<std::bidirectional_iterator_tag, wchar_t> {
	// These 3 fields determine the iterator position and are used for comparisons
	const Document *doc;
	Sci::Position position;
	size_t characterIndex;
	// Remaining fields are derived from the determining fields so are excluded in comparisons
	unsigned int lenBytes;
	size_t lenCharacters;
	wchar_t buffered[2];
public:
	UTF8Iterator(const Document *doc_ = 0, Sci::Position position_ = 0) :
		doc(doc_), position(position_), characterIndex(0), lenBytes(0), lenCharacters(0) {
		buffered[0] = 0;
		buffered[1] = 0;
		if (doc) {
			ReadCharacter();
		}
	}
	UTF8Iterator(const UTF8Iterator &other) {
		doc = other.doc;
		position = other.position;
		characterIndex = other.characterIndex;
		lenBytes = other.lenBytes;
		lenCharacters = other.lenCharacters;
		buffered[0] = other.buffered[0];
		buffered[1] = other.buffered[1];
	}
	UTF8Iterator &operator=(const UTF8Iterator &other) {
		if (this != &other) {
			doc = other.doc;
			position = other.position;
			characterIndex = other.characterIndex;
			lenBytes = other.lenBytes;
			lenCharacters = other.lenCharacters;
			buffered[0] = other.buffered[0];
			buffered[1] = other.buffered[1];
		}
		return *this;
	}
	wchar_t operator*() const {
		assert(lenCharacters != 0);
		return buffered[characterIndex];
	}
	UTF8Iterator &operator++() {
		if ((characterIndex + 1) < (lenCharacters)) {
			characterIndex++;
		} else {
			position += lenBytes;
			ReadCharacter();
			characterIndex = 0;
		}
		return *this;
	}
	UTF8Iterator operator++(int) {
		UTF8Iterator retVal(*this);
		if ((characterIndex + 1) < (lenCharacters)) {
			characterIndex++;
		} else {
			position += lenBytes;
			ReadCharacter();
			characterIndex = 0;
		}
		return retVal;
	}
	UTF8Iterator &operator--() {
		if (characterIndex) {
			characterIndex--;
		} else {
			position = doc->NextPosition(position, -1);
			ReadCharacter();
			characterIndex = lenCharacters - 1;
		}
		return *this;
	}
	bool operator==(const UTF8Iterator &other) const {
		// Only test the determining fields, not the character widths and values derived from this
		return doc == other.doc &&
			position == other.position &&
			characterIndex == other.characterIndex;
	}
	bool operator!=(const UTF8Iterator &other) const {
		// Only test the determining fields, not the character widths and values derived from this
		return doc != other.doc ||
			position != other.position ||
			characterIndex != other.characterIndex;
	}
	Sci::Position Pos() const {
		return position;
	}
	Sci::Position PosRoundUp() const {
		if (characterIndex)
			return position + lenBytes;	// Force to end of character
		else
			return position;
	}
private:
	void ReadCharacter() {
		Document::CharacterExtracted charExtracted = doc->ExtractCharacter(position);
		lenBytes = charExtracted.widthBytes;
		if (charExtracted.character == unicodeReplacementChar) {
			lenCharacters = 1;
			buffered[0] = static_cast<wchar_t>(charExtracted.character);
		} else {
			lenCharacters = UTF16FromUTF32Character(charExtracted.character, buffered);
		}
	}
};

#else

// On Unix, report non-BMP characters as single characters

class UTF8Iterator : public std::iterator<std::bidirectional_iterator_tag, wchar_t> {
	const Document *doc;
	Sci::Position position;
public:
	UTF8Iterator(const Document *doc_=0, Sci::Position position_=0) : doc(doc_), position(position_) {
	}
	UTF8Iterator(const UTF8Iterator &other) {
		doc = other.doc;
		position = other.position;
	}
	UTF8Iterator &operator=(const UTF8Iterator &other) {
		if (this != &other) {
			doc = other.doc;
			position = other.position;
		}
		return *this;
	}
	wchar_t operator*() const {
		Document::CharacterExtracted charExtracted = doc->ExtractCharacter(position);
		return charExtracted.character;
	}
	UTF8Iterator &operator++() {
		position = doc->NextPosition(position, 1);
		return *this;
	}
	UTF8Iterator operator++(int) {
		UTF8Iterator retVal(*this);
		position = doc->NextPosition(position, 1);
		return retVal;
	}
	UTF8Iterator &operator--() {
		position = doc->NextPosition(position, -1);
		return *this;
	}
	bool operator==(const UTF8Iterator &other) const {
		return doc == other.doc && position == other.position;
	}
	bool operator!=(const UTF8Iterator &other) const {
		return doc != other.doc || position != other.position;
	}
	Sci::Position Pos() const {
		return position; 
	}
	Sci::Position PosRoundUp() const {
		return position; 
	}
};

#endif

std::regex_constants::match_flag_type MatchFlags(const Document *doc, Sci::Position startPos, Sci::Position endPos) {
	std::regex_constants::match_flag_type flagsMatch = std::regex_constants::match_default;
	if (!doc->IsLineStartPosition(startPos))
		flagsMatch |= std::regex_constants::match_not_bol;
	if (!doc->IsLineEndPosition(endPos))
		flagsMatch |= std::regex_constants::match_not_eol;
	return flagsMatch;
}

template<typename Iterator, typename Regex>
bool MatchOnLines(const Document *doc, const Regex &regexp, const RESearchRange &resr, RESearch &search) {
	bool matched = false;
	std::match_results<Iterator> match;

	// MSVC and libc++ have problems with ^ and $ matching line ends inside a range
	// If they didn't then the line by line iteration could be removed for the forwards
	// case and replaced with the following 4 lines:
	//	Iterator uiStart(doc, startPos);
	//	Iterator uiEnd(doc, endPos);
	//	flagsMatch = MatchFlags(doc, startPos, endPos);
	//	matched = std::regex_search(uiStart, uiEnd, match, regexp, flagsMatch);

	// Line by line.
	for (Sci::Position line = resr.lineRangeStart; line != resr.lineRangeBreak; line += resr.increment) {
		const Range lineRange = resr.LineRange(line);
		Iterator itStart(doc, lineRange.start);
		Iterator itEnd(doc, lineRange.end);
		std::regex_constants::match_flag_type flagsMatch = MatchFlags(doc, lineRange.start, lineRange.end);
		matched = std::regex_search(itStart, itEnd, match, regexp, flagsMatch);
		// Check for the last match on this line.
		if (matched) {
			if (resr.increment == -1) {
				while (matched) {
					Iterator itNext(doc, match[0].second.PosRoundUp());
					flagsMatch = MatchFlags(doc, itNext.Pos(), lineRange.end);
					std::match_results<Iterator> matchNext;
					matched = std::regex_search(itNext, itEnd, matchNext, regexp, flagsMatch);
					if (matched) {
						if (match[0].first == match[0].second) {
							// Empty match means failure so exit
							return false;
						}
						match = matchNext;
					}
				}
				matched = true;
			}
			break;
		}
	}
	if (matched) {
		for (size_t co = 0; co < match.size(); co++) {
			search.bopat[co] = match[co].first.Pos();
			search.eopat[co] = match[co].second.PosRoundUp();
			Sci::Position lenMatch = search.eopat[co] - search.bopat[co];
			search.pat[co].resize(lenMatch);
			for (Sci::Position iPos = 0; iPos < lenMatch; iPos++) {
				search.pat[co][iPos] = doc->CharAt(iPos + search.bopat[co]);
			}
		}
	}
	return matched;
}

Sci::Position Cxx11RegexFindText(Document *doc, Sci::Position minPos, Sci::Position maxPos, const char *s,
	bool caseSensitive, Sci::Position *length, RESearch &search) {
	const RESearchRange resr(doc, minPos, maxPos);
	try {
		//ElapsedTime et;
		std::regex::flag_type flagsRe = std::regex::ECMAScript;
		// Flags that apper to have no effect:
		// | std::regex::collate | std::regex::extended;
		if (!caseSensitive)
			flagsRe = flagsRe | std::regex::icase;

		// Clear the RESearch so can fill in matches
		search.Clear();

		bool matched = false;
		if (SC_CP_UTF8 == doc->dbcsCodePage) {
			unsigned int lenS = static_cast<unsigned int>(strlen(s));
			std::vector<wchar_t> ws(lenS + 1);
#if WCHAR_T_IS_16
			size_t outLen = UTF16FromUTF8(s, lenS, &ws[0], lenS);
#else
			size_t outLen = UTF32FromUTF8(s, lenS, reinterpret_cast<unsigned int *>(&ws[0]), lenS);
#endif
			ws[outLen] = 0;
			std::wregex regexp;
#if defined(__APPLE__)
			// Using a UTF-8 locale doesn't change to Unicode over a byte buffer so '.'
			// is one byte not one character. 
			// However, on OS X this makes wregex act as Unicode
			std::locale localeU("en_US.UTF-8");
			regexp.imbue(localeU);
#endif
			regexp.assign(&ws[0], flagsRe);
			matched = MatchOnLines<UTF8Iterator>(doc, regexp, resr, search);

		} else {
			std::regex regexp;
			regexp.assign(s, flagsRe);
			matched = MatchOnLines<ByteIterator>(doc, regexp, resr, search);
		}

		Sci::Position posMatch = -1;
		if (matched) {
			posMatch = search.bopat[0];
			*length = search.eopat[0] - search.bopat[0];
		}
		// Example - search in doc/ScintillaHistory.html for
		// [[:upper:]]eta[[:space:]]
		// On MacBook, normally around 1 second but with locale imbued -> 14 seconds.
		//double durSearch = et.Duration(true);
		//Platform::DebugPrintf("Search:%9.6g \n", durSearch);
		return posMatch;
	} catch (std::regex_error &) {
		// Failed to create regular expression
		throw RegexError();
	} catch (...) {
		// Failed in some other way
		return -1;
	}
}

#endif

}

Sci::Position BuiltinRegex::FindText(Document *doc, Sci::Position minPos, Sci::Position maxPos, const char *s,
                        bool caseSensitive, bool, bool, int flags,
                        Sci::Position *length) {

#ifdef CXX11_REGEX
	if (flags & SCFIND_CXX11REGEX) {
			return Cxx11RegexFindText(doc, minPos, maxPos, s,
			caseSensitive, length, search);
	}
#endif

	const RESearchRange resr(doc, minPos, maxPos);

	const bool posix = (flags & SCFIND_POSIX) != 0;

	const char *errmsg = search.Compile(s, *length, caseSensitive, posix);
	if (errmsg) {
		return -1;
	}
	// Find a variable in a property file: \$(\([A-Za-z0-9_.]+\))
	// Replace first '.' with '-' in each property file variable reference:
	//     Search: \$(\([A-Za-z0-9_-]+\)\.\([A-Za-z0-9_.]+\))
	//     Replace: $(\1-\2)
	Sci::Position pos = -1;
	Sci::Position lenRet = 0;
	const char searchEnd = s[*length - 1];
	const char searchEndPrev = (*length > 1) ? s[*length - 2] : '\0';
	for (Sci::Position line = resr.lineRangeStart; line != resr.lineRangeBreak; line += resr.increment) {
		Sci::Position startOfLine = doc->PositionLineStart(line);
		Sci::Position endOfLine = doc->PositionLineEnd(line);
		if (resr.increment == 1) {
			if (line == resr.lineRangeStart) {
				if ((resr.startPos != startOfLine) && (s[0] == '^'))
					continue;	// Can't match start of line if start position after start of line
				startOfLine = resr.startPos;
			}
			if (line == resr.lineRangeEnd) {
				if ((resr.endPos != endOfLine) && (searchEnd == '$') && (searchEndPrev != '\\'))
					continue;	// Can't match end of line if end position before end of line
				endOfLine = resr.endPos;
			}
		} else {
			if (line == resr.lineRangeEnd) {
				if ((resr.endPos != startOfLine) && (s[0] == '^'))
					continue;	// Can't match start of line if end position after start of line
				startOfLine = resr.endPos;
			}
			if (line == resr.lineRangeStart) {
				if ((resr.startPos != endOfLine) && (searchEnd == '$') && (searchEndPrev != '\\'))
					continue;	// Can't match end of line if start position before end of line
				endOfLine = resr.startPos;
			}
		}

		DocumentIndexer di(doc, endOfLine);
		int success = search.Execute(di, startOfLine, endOfLine);
		if (success) {
			pos = search.bopat[0];
			// Ensure only whole characters selected
			search.eopat[0] = doc->MovePositionOutsideChar(search.eopat[0], 1, false);
			lenRet = search.eopat[0] - search.bopat[0];
			// There can be only one start of a line, so no need to look for last match in line
			if ((resr.increment == -1) && (s[0] != '^')) {
				// Check for the last match on this line.
				int repetitions = 1000;	// Break out of infinite loop
				while (success && (search.eopat[0] <= endOfLine) && (repetitions--)) {
					success = search.Execute(di, pos+1, endOfLine);
					if (success) {
						if (search.eopat[0] <= minPos) {
							pos = search.bopat[0];
							lenRet = search.eopat[0] - search.bopat[0];
						} else {
							success = 0;
						}
					}
				}
			}
			break;
		}
	}
	*length = lenRet;
	return pos;
}

const char *BuiltinRegex::SubstituteByPosition(Document *doc, const char *text, Sci::Position *length) {
	substituted.clear();
	DocumentIndexer di(doc, doc->PositionLength());
	search.GrabMatches(di);
	for (Sci::Position j = 0; j < *length; j++) {
		if (text[j] == '\\') {
			if (text[j + 1] >= '0' && text[j + 1] <= '9') {
				unsigned int patNum = text[j + 1] - '0';
				Sci::Position len = search.eopat[patNum] - search.bopat[patNum];
				if (!search.pat[patNum].empty())	// Will be null if try for a match that did not occur
					substituted.append(search.pat[patNum].c_str(), len);
				j++;
			} else {
				j++;
				switch (text[j]) {
				case 'a':
					substituted.push_back('\a');
					break;
				case 'b':
					substituted.push_back('\b');
					break;
				case 'f':
					substituted.push_back('\f');
					break;
				case 'n':
					substituted.push_back('\n');
					break;
				case 'r':
					substituted.push_back('\r');
					break;
				case 't':
					substituted.push_back('\t');
					break;
				case 'v':
					substituted.push_back('\v');
					break;
				case '\\':
					substituted.push_back('\\');
					break;
				default:
					substituted.push_back('\\');
					j--;
				}
			}
		} else {
			substituted.push_back(text[j]);
		}
	}
	*length = static_cast<Sci::Position>(substituted.length());
	return substituted.c_str();
}

#ifndef SCI_OWNREGEX

#ifdef SCI_NAMESPACE

RegexSearchBase *Scintilla::CreateRegexSearch(CharClassify *charClassTable) {
	return new BuiltinRegex(charClassTable);
}

#else

RegexSearchBase *CreateRegexSearch(CharClassify *charClassTable) {
	return new BuiltinRegex(charClassTable);
}

#endif

#endif
