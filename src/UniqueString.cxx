// Scintilla source code edit control
/** @file UniqueString.cxx
 ** Define an allocator for UniqueString.
 **/
// Copyright 2017 by Neil Hodgson <neilh@scintilla.org>
// The License.txt file describes the conditions under which this software may be distributed.

#include <cstring>
#include <algorithm>
#include <memory>

#include "UniqueString.h"

namespace Scintilla {

/// Equivalent to strdup but produces a std::unique_ptr<const char[]> allocation to go
/// into collections.
UniqueString UniqueStringCopy(const char *text) {
	if (!text) {
		return UniqueString();
	}
	const size_t len = strlen(text);
	std::unique_ptr<char[]> upcNew(new char[len + 1]);
	memcpy(&upcNew[0], text, len + 1);
	return UniqueString(upcNew.release());
}

}
