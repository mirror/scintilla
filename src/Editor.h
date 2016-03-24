// Scintilla source code edit control
/** @file Editor.h
 ** Defines the main editor class.
 **/
// Copyright 1998-2011 by Neil Hodgson <neilh@scintilla.org>
// The License.txt file describes the conditions under which this software may be distributed.

#ifndef EDITOR_H
#define EDITOR_H

#ifdef SCI_NAMESPACE
namespace Scintilla {
#endif

/**
 */
class Timer {
public:
	bool ticking;
	int ticksToWait;
	enum {tickSize = 100};
	TickerID tickerID;

	Timer();
};

/**
 */
class Idler {
public:
	bool state;
	IdlerID idlerID;

	Idler();
};

/**
 * When platform has a way to generate an event before painting,
 * accumulate needed styling range and other work items in
 * WorkNeeded to avoid unnecessary work inside paint handler
 */
class WorkNeeded {
public:
	enum workItems {
		workNone=0,
		workStyle=1,
		workUpdateUI=2
	};
	bool active;
	enum workItems items;
	Sci::Position upTo;

	WorkNeeded() : active(false), items(workNone), upTo(0) {}
	void Reset() {
		active = false;
		items = workNone;
		upTo = 0;
	}
	void Need(workItems items_, Sci::Position pos) {
		if ((items_ & workStyle) && (upTo < pos))
			upTo = pos;
		items = static_cast<workItems>(items | items_);
	}
};

/**
 * Hold a piece of text selected for copying or dragging, along with encoding and selection format information.
 */
class SelectionText {
	std::string s;
public:
	bool rectangular;
	bool lineCopy;
	int codePage;
	int characterSet;
	SelectionText() : rectangular(false), lineCopy(false), codePage(0), characterSet(0) {}
	~SelectionText() {
	}
	void Clear() {
		s.clear();
		rectangular = false;
		lineCopy = false;
		codePage = 0;
		characterSet = 0;
	}
	void Copy(const std::string &s_, int codePage_, int characterSet_, bool rectangular_, bool lineCopy_) {
		s = s_;
		codePage = codePage_;
		characterSet = characterSet_;
		rectangular = rectangular_;
		lineCopy = lineCopy_;
		FixSelectionForClipboard();
	}
	void Copy(const SelectionText &other) {
		Copy(other.s, other.codePage, other.characterSet, other.rectangular, other.lineCopy);
	}
	const char *Data() const {
		return s.c_str();
	}
	size_t Length() const {
		return s.length();
	}
	size_t LengthWithTerminator() const {
		return s.length() + 1;
	}
	bool Empty() const {
		return s.empty();
	}
private:
	void FixSelectionForClipboard() {
		// To avoid truncating the contents of the clipboard when pasted where the
		// clipboard contains NUL characters, replace NUL characters by spaces.
		std::replace(s.begin(), s.end(), '\0', ' ');
	}
};

struct WrapPending {
	// The range of lines that need to be wrapped
	enum { lineLarge = 0x7ffffff };
	Sci::Position start;	// When there are wraps pending, will be in document range
	Sci::Position end;	// May be lineLarge to indicate all of document after start
	WrapPending() {
		start = lineLarge;
		end = lineLarge;
	}
	void Reset() {
		start = lineLarge;
		end = lineLarge;
	}
	void Wrapped(Sci::Position line) {
		if (start == line)
			start++;
	}
	bool NeedsWrap() const {
		return start < end;
	}
	bool AddRange(Sci::Position lineStart, Sci::Position lineEnd) {
		const bool neededWrap = NeedsWrap();
		bool changed = false;
		if (start > lineStart) {
			start = lineStart;
			changed = true;
		}
		if ((end < lineEnd) || !neededWrap) {
			end = lineEnd;
			changed = true;
		}
		return changed;
	}
};

/**
 */
class Editor : public EditModel, public DocWatcher {
	// Private so Editor objects can not be copied
	explicit Editor(const Editor &);
	Editor &operator=(const Editor &);

protected:	// ScintillaBase subclass needs access to much of Editor

	/** On GTK+, Scintilla is a container widget holding two scroll bars
	 * whereas on Windows there is just one window with both scroll bars turned on. */
	Window wMain;	///< The Scintilla parent window
	Window wMargin;	///< May be separate when using a scroll view for wMain

	/** Style resources may be expensive to allocate so are cached between uses.
	 * When a style attribute is changed, this cache is flushed. */
	bool stylesValid;
	ViewStyle vs;
	int technology;
	Point sizeRGBAImage;
	float scaleRGBAImage;

	MarginView marginView;
	EditView view;

	int cursorMode;

	bool hasFocus;
	bool mouseDownCaptures;

	int xCaretMargin;	///< Ensure this many pixels visible on both sides of caret
	bool horizontalScrollBarVisible;
	int scrollWidth;
	bool verticalScrollBarVisible;
	bool endAtLastLine;
	int caretSticky;
	int marginOptions;
	bool mouseSelectionRectangularSwitch;
	bool multipleSelection;
	bool additionalSelectionTyping;
	int multiPasteMode;

	int virtualSpaceOptions;

	KeyMap kmap;

	Timer timer;
	Timer autoScrollTimer;
	enum { autoScrollDelay = 200 };

	Idler idler;

	Point lastClick;
	unsigned int lastClickTime;
	Point doubleClickCloseThreshold;
	int dwellDelay;
	int ticksToDwell;
	bool dwelling;
	enum { selChar, selWord, selSubLine, selWholeLine } selectionType;
	Point ptMouseLast;
	enum { ddNone, ddInitial, ddDragging } inDragDrop;
	bool dropWentOutside;
	SelectionPosition posDrop;
	Sci::Position hotSpotClickPos;
	int lastXChosen;
	Sci::Position lineAnchorPos;
	Sci::Position originalAnchorPos;
	Sci::Position wordSelectAnchorStartPos;
	Sci::Position wordSelectAnchorEndPos;
	Sci::Position wordSelectInitialCaretPos;
	Sci::Position targetStart;
	Sci::Position targetEnd;
	int searchFlags;
	Sci::Position topLine;
	Sci::Position posTopLine;
	Sci::Position lengthForEncode;

	int needUpdateUI;

	enum { notPainting, painting, paintAbandoned } paintState;
	bool paintAbandonedByStyling;
	PRectangle rcPaint;
	bool paintingAllText;
	bool willRedrawAll;
	WorkNeeded workNeeded;
	int idleStyling;
	bool needIdleStyling;

	int modEventMask;

	SelectionText drag;

	int caretXPolicy;
	int caretXSlop;	///< Ensure this many pixels visible on both sides of caret

	int caretYPolicy;
	int caretYSlop;	///< Ensure this many lines visible on both sides of caret

	int visiblePolicy;
	int visibleSlop;

	Sci::Position searchAnchor;

	bool recordingMacro;

	int foldAutomatic;

	// Wrapping support
	WrapPending wrapPending;

	bool convertPastes;

	Editor();
	virtual ~Editor();
	virtual void Initialise() = 0;
	virtual void Finalise();

	void InvalidateStyleData();
	void InvalidateStyleRedraw();
	void RefreshStyleData();
	void SetRepresentations();
	void DropGraphics(bool freeObjects);
	void AllocateGraphics();

	// The top left visible point in main window coordinates. Will be 0,0 except for
	// scroll views where it will be equivalent to the current scroll position.
	virtual Point GetVisibleOriginInMain() const;
	Point DocumentPointFromView(Point ptView) const;  // Convert a point from view space to document
	Sci::Position TopLineOfMain() const;   // Return the line at Main's y coordinate 0
	virtual PRectangle GetClientRectangle() const;
	virtual PRectangle GetClientDrawingRectangle();
	PRectangle GetTextRectangle() const;

	virtual int LinesOnScreen() const;
	int LinesToScroll() const;
	Sci::Position MaxScrollPos() const;
	SelectionPosition ClampPositionIntoDocument(SelectionPosition sp) const;
	Point LocationFromPosition(SelectionPosition pos);
	Point LocationFromPosition(Sci::Position pos);
	int XFromPosition(Sci::Position pos);
	int XFromPosition(SelectionPosition sp);
	SelectionPosition SPositionFromLocation(Point pt, bool canReturnInvalid=false, bool charPosition=false, bool virtualSpace=true);
	Sci::Position PositionFromLocation(Point pt, bool canReturnInvalid = false, bool charPosition = false);
	SelectionPosition SPositionFromLineX(Sci::Position lineDoc, int x);
	Sci::Position PositionFromLineX(Sci::Position line, int x);
	Sci::Position LineFromLocation(Point pt) const;
	void SetTopLine(Sci::Position topLineNew);

	virtual bool AbandonPaint();
	virtual void RedrawRect(PRectangle rc);
	virtual void DiscardOverdraw();
	virtual void Redraw();
	void RedrawSelMargin(Sci::Position line=-1, bool allAfter=false);
	PRectangle RectangleFromRange(Range r, int overlap);
	void InvalidateRange(Sci::Position start, Sci::Position end);

	bool UserVirtualSpace() const {
		return ((virtualSpaceOptions & SCVS_USERACCESSIBLE) != 0);
	}
	Sci::Position CurrentPosition() const;
	bool SelectionEmpty() const;
	SelectionPosition SelectionStart();
	SelectionPosition SelectionEnd();
	void SetRectangularRange();
	void ThinRectangularRange();
	void InvalidateSelection(SelectionRange newMain, bool invalidateWholeSelection=false);
	void InvalidateWholeSelection();
	void SetSelection(SelectionPosition currentPos_, SelectionPosition anchor_);
	void SetSelection(Sci::Position currentPos_, Sci::Position anchor_);
	void SetSelection(SelectionPosition currentPos_);
	void SetSelection(Sci::Position currentPos_);
	void SetEmptySelection(SelectionPosition currentPos_);
	void SetEmptySelection(Sci::Position currentPos_);
	enum AddNumber { addOne, addEach };
	void MultipleSelectAdd(AddNumber addNumber);
	bool RangeContainsProtected(Sci::Position start, Sci::Position end) const;
	bool SelectionContainsProtected();
	Sci::Position MovePositionOutsideChar(Sci::Position pos, int moveDir, bool checkLineEnd=true) const;
	SelectionPosition MovePositionOutsideChar(SelectionPosition pos, int moveDir, bool checkLineEnd=true) const;
	void MovedCaret(SelectionPosition newPos, SelectionPosition previousPos, bool ensureVisible);
	void MovePositionTo(SelectionPosition newPos, Selection::selTypes selt=Selection::noSel, bool ensureVisible=true);
	void MovePositionTo(Sci::Position newPos, Selection::selTypes selt=Selection::noSel, bool ensureVisible=true);
	SelectionPosition MovePositionSoVisible(SelectionPosition pos, int moveDir);
	SelectionPosition MovePositionSoVisible(Sci::Position pos, int moveDir);
	Point PointMainCaret();
	void SetLastXChosen();

	void ScrollTo(Sci::Position line, bool moveThumb=true);
	virtual void ScrollText(Sci::Position linesToMove);
	void HorizontalScrollTo(int xPos);
	void VerticalCentreCaret();
	void MoveSelectedLines(Sci::Position lineDelta);
	void MoveSelectedLinesUp();
	void MoveSelectedLinesDown();
	void MoveCaretInsideView(bool ensureVisible=true);
	Sci::Position DisplayFromPosition(Sci::Position pos);

	struct XYScrollPosition {
		int xOffset;
		Sci::Position topLine;
		XYScrollPosition(int xOffset_, Sci::Position topLine_) : xOffset(xOffset_), topLine(topLine_) {}
		bool operator==(const XYScrollPosition &other) const {
			return (xOffset == other.xOffset) && (topLine == other.topLine);
		}
	};
	enum XYScrollOptions {
		xysUseMargin=0x1,
		xysVertical=0x2,
		xysHorizontal=0x4,
		xysDefault=xysUseMargin|xysVertical|xysHorizontal};
	XYScrollPosition XYScrollToMakeVisible(const SelectionRange &range, const XYScrollOptions options);
	void SetXYScroll(XYScrollPosition newXY);
	void EnsureCaretVisible(bool useMargin=true, bool vert=true, bool horiz=true);
	void ScrollRange(SelectionRange range);
	void ShowCaretAtCurrentPosition();
	void DropCaret();
	void CaretSetPeriod(int period);
	void InvalidateCaret();
	virtual void UpdateSystemCaret();

	bool Wrapping() const;
	void NeedWrapping(Sci::Position docLineStart=0, Sci::Position docLineEnd=WrapPending::lineLarge);
	bool WrapOneLine(Surface *surface, Sci::Position lineToWrap);
	enum wrapScope {wsAll, wsVisible, wsIdle};
	bool WrapLines(enum wrapScope ws);
	void LinesJoin();
	void LinesSplit(int pixelWidth);

	void PaintSelMargin(Surface *surface, PRectangle &rc);
	void RefreshPixMaps(Surface *surfaceWindow);
	void Paint(Surface *surfaceWindow, PRectangle rcArea);
	Sci::Position FormatRange(bool draw, Sci_RangeToFormat *pfr);
	int TextWidth(int style, const char *text);

	virtual void SetVerticalScrollPos() = 0;
	virtual void SetHorizontalScrollPos() = 0;
	virtual bool ModifyScrollBars(Sci::Position nMax, Sci::Position nPage) = 0;
	virtual void ReconfigureScrollBars();
	void SetScrollBars();
	void ChangeSize();

	void FilterSelections();
	Sci::Position InsertSpace(Sci::Position position, Sci::Position spaces);
	void AddChar(char ch);
	virtual void AddCharUTF(const char *s, unsigned int len, bool treatAsDBCS=false);
	void ClearBeforeTentativeStart();
	void InsertPaste(const char *text, Sci::Position len);
	enum PasteShape { pasteStream=0, pasteRectangular = 1, pasteLine = 2 };
	void InsertPasteShape(const char *text, Sci::Position len, PasteShape shape);
	void ClearSelection(bool retainMultipleSelections = false);
	void ClearAll();
	void ClearDocumentStyle();
	void Cut();
	void PasteRectangular(SelectionPosition pos, const char *ptr, Sci::Position len);
	virtual void Copy() = 0;
	virtual void CopyAllowLine();
	virtual bool CanPaste();
	virtual void Paste() = 0;
	void Clear();
	void SelectAll();
	void Undo();
	void Redo();
	void DelCharBack(bool allowLineStartDeletion);
	virtual void ClaimSelection() = 0;

	static int ModifierFlags(bool shift, bool ctrl, bool alt, bool meta=false);
	virtual void NotifyChange() = 0;
	virtual void NotifyFocus(bool focus);
	virtual void SetCtrlID(int identifier);
	virtual int GetCtrlID() { return ctrlID; }
	virtual void NotifyParent(SCNotification scn) = 0;
	virtual void NotifyStyleToNeeded(Sci::Position endStyleNeeded);
	void NotifyChar(int ch);
	void NotifySavePoint(bool isSavePoint);
	void NotifyModifyAttempt();
	virtual void NotifyDoubleClick(Point pt, int modifiers);
	virtual void NotifyDoubleClick(Point pt, bool shift, bool ctrl, bool alt);
	void NotifyHotSpotClicked(Sci::Position position, int modifiers);
	void NotifyHotSpotClicked(Sci::Position position, bool shift, bool ctrl, bool alt);
	void NotifyHotSpotDoubleClicked(Sci::Position position, int modifiers);
	void NotifyHotSpotDoubleClicked(Sci::Position position, bool shift, bool ctrl, bool alt);
	void NotifyHotSpotReleaseClick(Sci::Position position, int modifiers);
	void NotifyHotSpotReleaseClick(Sci::Position position, bool shift, bool ctrl, bool alt);
	bool NotifyUpdateUI();
	void NotifyPainted();
	void NotifyIndicatorClick(bool click, Sci::Position position, int modifiers);
	void NotifyIndicatorClick(bool click, Sci::Position position, bool shift, bool ctrl, bool alt);
	bool NotifyMarginClick(Point pt, int modifiers);
	bool NotifyMarginClick(Point pt, bool shift, bool ctrl, bool alt);
	void NotifyNeedShown(Sci::Position pos, Sci::Position len);
	void NotifyDwelling(Point pt, bool state);
	void NotifyZoom();

	void NotifyModifyAttempt(Document *document, void *userData);
	void NotifySavePoint(Document *document, void *userData, bool atSavePoint);
	void CheckModificationForWrap(DocModification mh);
	void NotifyModified(Document *document, DocModification mh, void *userData);
	void NotifyDeleted(Document *document, void *userData);
	void NotifyStyleNeeded(Document *doc, void *userData, Sci::Position endPos);
	void NotifyLexerChanged(Document *doc, void *userData);
	void NotifyErrorOccurred(Document *doc, void *userData, int status);
	void NotifyMacroRecord(unsigned int iMessage, uptr_t wParam, sptr_t lParam);

	void ContainerNeedsUpdate(int flags);
	void PageMove(int direction, Selection::selTypes selt=Selection::noSel, bool stuttered = false);
	enum { cmSame, cmUpper, cmLower };
	virtual std::string CaseMapString(const std::string &s, int caseMapping);
	void ChangeCaseOfSelection(int caseMapping);
	void LineTranspose();
	void Duplicate(bool forLine);
	virtual void CancelModes();
	void NewLine();
	SelectionPosition PositionUpOrDown(SelectionPosition spStart, int direction, int lastX);
	void CursorUpOrDown(int direction, Selection::selTypes selt);
	void ParaUpOrDown(int direction, Selection::selTypes selt);
	Sci::Position StartEndDisplayLine(Sci::Position pos, bool start);
	Sci::Position VCHomeDisplayPosition(Sci::Position position);
	Sci::Position VCHomeWrapPosition(Sci::Position position);
	Sci::Position LineEndWrapPosition(Sci::Position position);
	int HorizontalMove(unsigned int iMessage);
	int DelWordOrLine(unsigned int iMessage);
	virtual int KeyCommand(unsigned int iMessage);
	virtual int KeyDefault(int /* key */, int /*modifiers*/);
	int KeyDownWithModifiers(int key, int modifiers, bool *consumed);
	int KeyDown(int key, bool shift, bool ctrl, bool alt, bool *consumed=0);

	void Indent(bool forwards);

	virtual CaseFolder *CaseFolderForEncoding();
	Sci::Position FindText(uptr_t wParam, sptr_t lParam);
	void SearchAnchor();
	Sci::Position SearchText(unsigned int iMessage, uptr_t wParam, sptr_t lParam);
	Sci::Position SearchInTarget(const char *text, Sci::Position length);
	void GoToLine(Sci::Position lineNo);

	virtual void CopyToClipboard(const SelectionText &selectedText) = 0;
	std::string RangeText(Sci::Position start, Sci::Position end) const;
	void CopySelectionRange(SelectionText *ss, bool allowLineCopy=false);
	void CopyRangeToClipboard(Sci::Position start, Sci::Position end);
	void CopyText(Sci::Position length, const char *text);
	void SetDragPosition(SelectionPosition newPos);
	virtual void DisplayCursor(Window::Cursor c);
	virtual bool DragThreshold(Point ptStart, Point ptNow);
	virtual void StartDrag();
	void DropAt(SelectionPosition position, const char *value, size_t lengthValue, bool moving, bool rectangular);
	void DropAt(SelectionPosition position, const char *value, bool moving, bool rectangular);
	/** PositionInSelection returns true if position in selection. */
	bool PositionInSelection(Sci::Position pos);
	bool PointInSelection(Point pt);
	bool PointInSelMargin(Point pt) const;
	Window::Cursor GetMarginCursor(Point pt) const;
	void TrimAndSetSelection(Sci::Position currentPos_, Sci::Position anchor_);
	void LineSelection(Sci::Position lineCurrentPos_, Sci::Position lineAnchorPos_, bool wholeLine);
	void WordSelection(Sci::Position pos);
	void DwellEnd(bool mouseMoved);
	void MouseLeave();
	virtual void ButtonDownWithModifiers(Point pt, unsigned int curTime, int modifiers);
	virtual void ButtonDown(Point pt, unsigned int curTime, bool shift, bool ctrl, bool alt);
	void ButtonMoveWithModifiers(Point pt, int modifiers);
	void ButtonMove(Point pt);
	void ButtonUp(Point pt, unsigned int curTime, bool ctrl);

	void Tick();
	bool Idle();
	virtual void SetTicking(bool on);
	enum TickReason { tickCaret, tickScroll, tickWiden, tickDwell, tickPlatform };
	virtual void TickFor(TickReason reason);
	virtual bool FineTickerAvailable();
	virtual bool FineTickerRunning(TickReason reason);
	virtual void FineTickerStart(TickReason reason, int millis, int tolerance);
	virtual void FineTickerCancel(TickReason reason);
	virtual bool SetIdle(bool) { return false; }
	virtual void SetMouseCapture(bool on) = 0;
	virtual bool HaveMouseCapture() = 0;
	void SetFocusState(bool focusState);

	Sci::Position PositionAfterArea(PRectangle rcArea) const;
	void StyleToPositionInView(Sci::Position pos);
	int PositionAfterMaxStyling(int posMax, bool scrolling) const;
	void StartIdleStyling(bool truncatedLastStyling);
	void StyleAreaBounded(PRectangle rcArea, bool scrolling);
	void IdleStyling();
	virtual void IdleWork();
	virtual void QueueIdleWork(WorkNeeded::workItems items, Sci::Position upTo=0);

	virtual bool PaintContains(PRectangle rc);
	bool PaintContainsMargin();
	void CheckForChangeOutsidePaint(Range r);
	void SetBraceHighlight(Sci::Position pos0, Sci::Position pos1, int matchStyle);

	void SetAnnotationHeights(Sci::Position start, Sci::Position end);
	virtual void SetDocPointer(Document *document);

	void SetAnnotationVisible(int visible);

	Sci::Position ExpandLine(Sci::Position line);
	void SetFoldExpanded(Sci::Position lineDoc, bool expanded);
	void FoldLine(Sci::Position line, int action);
	void FoldExpand(Sci::Position line, int action, int level);
	Sci::Position ContractedFoldNext(Sci::Position lineStart) const;
	void EnsureLineVisible(Sci::Position lineDoc, bool enforcePolicy);
	void FoldChanged(Sci::Position line, int levelNow, int levelPrev);
	void NeedShown(Sci::Position pos, Sci::Position len);
	void FoldAll(int action);

	Sci::Position GetTag(char *tagValue, int tagNumber);
	Sci::Position ReplaceTarget(bool replacePatterns, const char *text, Sci::Position length=-1);

	bool PositionIsHotspot(Sci::Position position) const;
	bool PointIsHotspot(Point pt);
	void SetHotSpotRange(Point *pt);
	Range GetHotSpotRange() const;
	void SetHoverIndicatorPosition(Sci::Position position);
	void SetHoverIndicatorPoint(Point pt);

	int CodePage() const;
	virtual bool ValidCodePage(int /* codePage */) const { return true; }
	Sci::Position WrapCount(Sci::Position line);
	void AddStyledText(char *buffer, Sci::Position appendLength);

	virtual sptr_t DefWndProc(unsigned int iMessage, uptr_t wParam, sptr_t lParam) = 0;
	void StyleSetMessage(unsigned int iMessage, uptr_t wParam, sptr_t lParam);
	sptr_t StyleGetMessage(unsigned int iMessage, uptr_t wParam, sptr_t lParam);

	static const char *StringFromEOLMode(int eolMode);

	static sptr_t StringResult(sptr_t lParam, const char *val);
	static sptr_t BytesResult(sptr_t lParam, const unsigned char *val, size_t len);

public:
	// Public so the COM thunks can access it.
	bool IsUnicodeMode() const;
	// Public so scintilla_send_message can use it.
	virtual sptr_t WndProc(unsigned int iMessage, uptr_t wParam, sptr_t lParam);
	// Public so scintilla_set_id can use it.
	int ctrlID;
	// Public so COM methods for drag and drop can set it.
	int errorStatus;
	friend class AutoSurface;
	friend class SelectionLineIterator;
};

/**
 * A smart pointer class to ensure Surfaces are set up and deleted correctly.
 */
class AutoSurface {
private:
	Surface *surf;
public:
	AutoSurface(Editor *ed, int technology = -1) : surf(0) {
		if (ed->wMain.GetID()) {
			surf = Surface::Allocate(technology != -1 ? technology : ed->technology);
			if (surf) {
				surf->Init(ed->wMain.GetID());
				surf->SetUnicodeMode(SC_CP_UTF8 == ed->CodePage());
				surf->SetDBCSMode(ed->CodePage());
			}
		}
	}
	AutoSurface(SurfaceID sid, Editor *ed, int technology = -1) : surf(0) {
		if (ed->wMain.GetID()) {
			surf = Surface::Allocate(technology != -1 ? technology : ed->technology);
			if (surf) {
				surf->Init(sid, ed->wMain.GetID());
				surf->SetUnicodeMode(SC_CP_UTF8 == ed->CodePage());
				surf->SetDBCSMode(ed->CodePage());
			}
		}
	}
	~AutoSurface() {
		delete surf;
	}
	Surface *operator->() const {
		return surf;
	}
	operator Surface *() const {
		return surf;
	}
};

#ifdef SCI_NAMESPACE
}
#endif

#endif
