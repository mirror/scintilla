/*
 *  ScintillaMacOSX.h
 *  tutorial
 *
 *  Created by Evan Jones on Sun Sep 01 2002.
 *  Copyright (c) 2002 __MyCompanyName__. All rights reserved.
 *
 */
#include "TView.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <time.h>

#include "Platform.h"
#include "Scintilla.h"
#include "PlatMacOSX.h"


#include "ScintillaWidget.h"
#ifdef SCI_LEXER
#include "SciLexer.h"
#include "PropSet.h"
#include "Accessor.h"
#include "KeyWords.h"
#endif
#include "ContractionState.h"
#include "SVector.h"
#include "CellBuffer.h"
#include "CallTip.h"
#include "KeyMap.h"
#include "Indicator.h"
#include "XPM.h"
#include "LineMarker.h"
#include "Style.h"
#include "AutoComplete.h"
#include "ViewStyle.h"
#include "Document.h"
#include "Editor.h"
#include "SString.h"
#include "ScintillaBase.h"
#include "ScintillaCallTip.h"

static const OSType scintillaMacOSType = 'Scin';
static const OSType scintillaNotifyObject = 'Snob';
static const OSType scintillaNotifyFN = 'Snfn';

namespace Scintilla {

class ScintillaMacOSX : public ScintillaBase, public TView
{
 public:
    HIViewRef vScrollBar;
    HIViewRef hScrollBar;
    SInt32 scrollBarFixedSize;

    bool capturedMouse;
    bool inDragSession; // true if scintilla initiated the drag session

    // Private so ScintillaMacOSX objects can not be copied
    ScintillaMacOSX(const ScintillaMacOSX &) : ScintillaBase(), TView( NULL ) {}
    ScintillaMacOSX &operator=(const ScintillaMacOSX &) { return * this; }

public:
    /** This is the class ID that we've assigned to Scintilla. */
    static const CFStringRef kScintillaClassID;
    static const ControlKind kScintillaKind;

    ScintillaMacOSX( void* windowid );
    virtual ~ScintillaMacOSX();
    //~ static void ClassInit(GtkObjectClass* object_class, GtkWidgetClass *widget_class);

    /** Returns the HIView object kind, needed to subclass TView. */
    virtual ControlKind GetKind() { return kScintillaKind; }

private:
    virtual void Initialise();
    virtual void Finalise();
    virtual void StartDrag();
    Scintilla::Point GetDragPoint(DragRef inDrag);
    OSStatus GetDragData(DragRef inDrag, PasteboardRef &pasteBoard, CFStringRef *textString);
    void SetDragCursor(DragRef inDrag);

    // Drag and drop
    virtual bool DragEnter(DragRef inDrag );
    virtual bool DragWithin(DragRef inDrag );
    virtual bool DragLeave(DragRef inDrag );
    virtual OSStatus DragReceive(DragRef inDrag );
    void DragScroll();
    int scrollSpeed;
    int scrollTicks;

    EventRecord mouseDownEvent;
    MouseTrackingRef mouseTrackingRef;
    MouseTrackingRegionID mouseTrackingID;
    HIPoint GetLocalPoint(::Point pt);

public: // Public for scintilla_send_message
    virtual sptr_t WndProc(unsigned int iMessage, uptr_t wParam, sptr_t lParam);

    void SyncPaint( void* gc, PRectangle rc);
    //void FullPaint( void* gc );
    virtual void Draw( RgnHandle rgn, CGContextRef gc );

    virtual sptr_t DefWndProc(unsigned int iMessage, uptr_t wParam, sptr_t lParam);
    virtual void SetTicking(bool on);
    virtual void SetMouseCapture(bool on);
    virtual bool HaveMouseCapture();
    virtual PRectangle GetClientRectangle();

    virtual void ScrollText(int linesToMove);
    virtual void SetVerticalScrollPos();
    virtual void SetHorizontalScrollPos();
    virtual bool ModifyScrollBars(int nMax, int nPage);
    virtual void ReconfigureScrollBars();
    void Resize(int width, int height);
    static pascal void LiveScrollHandler( ControlHandle control, SInt16 part );
    
    virtual void NotifyChange();
    virtual void NotifyFocus(bool focus);
    virtual void NotifyParent(SCNotification scn);
    void NotifyKey(int key, int modifiers);
    void NotifyURIDropped(const char *list);
    virtual int KeyDefault(int key, int modifiers);
    static pascal OSStatus CommandEventHandler( EventHandlerCallRef inCallRef, EventRef inEvent, void* data );

    bool HasSelection();
    bool CanUndo();
    bool CanRedo();
    bool AlwaysTrue();
    virtual void CopyToClipboard(const SelectionText &selectedText);
    virtual void Copy();
    virtual bool CanPaste();
    virtual void Paste();
    virtual void Paste(bool rectangular);
    virtual void CreateCallTipWindow(PRectangle rc);
    virtual void AddToPopUp(const char *label, int cmd = 0, bool enabled = true);
    virtual void ClaimSelection();

    static sptr_t DirectFunction(ScintillaMacOSX *sciThis,
                                 unsigned int iMessage, uptr_t wParam, sptr_t lParam);

    /** Timer event handler. Simply turns around and calls Tick. */
    virtual void TimerFired( EventLoopTimerRef );
    virtual OSStatus BoundsChanged( UInt32 inOptions, const HIRect& inOriginalBounds, const HIRect& inCurrentBounds, RgnHandle inInvalRgn );
    virtual ControlPartCode HitTest( const HIPoint& where );
    virtual OSStatus SetFocusPart( ControlPartCode desiredFocus, RgnHandle, Boolean, ControlPartCode* outActualFocus );
    virtual OSStatus TextInput( TCarbonEvent& event );

    // Configure the features of this control
    virtual UInt32 GetBehaviors();

    virtual OSStatus MouseDown( HIPoint& location, UInt32 modifiers, EventMouseButton button, UInt32 clickCount, TCarbonEvent& inEvent );
    virtual OSStatus MouseDown( HIPoint& location, UInt32 modifiers, EventMouseButton button, UInt32 clickCount );
    virtual OSStatus MouseDown( EventRecord *rec );
    virtual OSStatus MouseUp( HIPoint& location, UInt32 modifiers, EventMouseButton button, UInt32 clickCount );
    virtual OSStatus MouseUp( EventRecord *rec );
    virtual OSStatus MouseDragged( HIPoint& location, UInt32 modifiers, EventMouseButton button, UInt32 clickCount );
    virtual OSStatus MouseDragged( EventRecord *rec );
    virtual OSStatus MouseEntered( HIPoint& location, UInt32 modifiers, EventMouseButton button, UInt32 clickCount );
    virtual OSStatus MouseExited( HIPoint& location, UInt32 modifiers, EventMouseButton button, UInt32 clickCount );
    virtual OSStatus MouseWheelMoved( EventMouseWheelAxis axis, SInt32 delta, UInt32 modifiers );
    virtual OSStatus ContextualMenuClick( HIPoint& location );

    virtual OSStatus ActiveStateChanged();

    virtual void CallTipClick();

public:
    static HIViewRef Create();
private:
    static OSStatus Construct( HIViewRef inControl, TView** outView );
    
};


}
