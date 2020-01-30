// Scintilla source code edit control
/** @file PlatWin.h
 ** Implementation of platform facilities on Windows.
 **/
// Copyright 1998-2011 by Neil Hodgson <neilh@scintilla.org>
// The License.txt file describes the conditions under which this software may be distributed.

#ifndef PLATWIN_H
#define PLATWIN_H

namespace Scintilla {

extern void Platform_Initialise(void *hInstance);
extern void Platform_Finalise(bool fromDllMain);

RECT RectFromPRectangle(PRectangle prc) noexcept;

constexpr HWND HwndFromWindowID(WindowID wid) noexcept {
	return static_cast<HWND>(wid);
}

inline HWND HwndFromWindow(const Window &w) noexcept {
	return HwndFromWindowID(w.GetID());
}

#if defined(USE_D2D)
extern bool LoadD2D();
extern ID2D1Factory *pD2DFactory;
extern IDWriteFactory *pIDWriteFactory;
#endif

}

#endif
