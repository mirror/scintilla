// Scintilla source code edit control
/** @file Sci_Position.h
 ** Define the Sci_Position type used in Scintilla's external interfaces.
 ** These need to be available to clients written in C so are not in a C++ namespace.
 **/
// Copyright 2015 by Neil Hodgson <neilh@scintilla.org>
// The License.txt file describes the conditions under which this software may be distributed.

#ifndef SCI_POSITION_H
#define SCI_POSITION_H

#ifdef __cplusplus
	#include <cstddef>

	// Basic signed type used throughout interface
	typedef std::ptrdiff_t Sci_Position;

	// Unsigned variant used for ILexer::Lex and ILexer::Fold
	typedef std::size_t Sci_PositionU;

	// For Sci_CharacterRange  which is defined as long to be compatible with Win32 CHARRANGE
	typedef std::ptrdiff_t Sci_PositionCR;
#else
	#include <stddef.h>

	// Basic signed type used throughout interface
	typedef ptrdiff_t Sci_Position;

	// Unsigned variant used for ILexer::Lex and ILexer::Fold
	typedef size_t Sci_PositionU;

	// For Sci_CharacterRange  which is defined as long to be compatible with Win32 CHARRANGE
	typedef ptrdiff_t Sci_PositionCR;
#endif

#endif
