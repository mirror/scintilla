// Scintilla source code edit control
/** @file Position.h
 ** Defines global type name Position in the Sci internal namespace.
 **/
// Copyright 2015 by Neil Hodgson <neilh@scintilla.org>
// The License.txt file describes the conditions under which this software may be distributed.

#ifndef POSITION_H
#define POSITION_H

/*
 * Defines type used for internal representation of positions and line numbers. This type may be 32 or 64 bits
 * depending on if documents larger than 2G are desired. In namespace separate from Scintilla. Since this type is
 * internal in nature, the Sci namespace is not conditionally compiled.
 */

/**
 * A Position is a position within a document between two characters or at the beginning or end.
 * Sometimes used as a character index where it identifies the character after the position.
 */

namespace Sci {

/* If you want large file support (documents greater than 2G), define SCI_LARGE_FILE_SUPPORT
 * by default Scintilla restricts document sizes to 2G.
 */
#ifdef SCI_LARGE_FILE_SUPPORT
typedef std::ptrdiff_t Position;
#else
typedef int Position;
#endif

inline Position Clamp(Position val, Position minVal, Position maxVal) {
	if (val > maxVal)
		val = maxVal;
	if (val < minVal)
		val = minVal;
	return val;
}

const Position invalidPosition = -1;

}

#endif
