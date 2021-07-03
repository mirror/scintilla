/** @file testDocument.cxx
 ** Unit Tests for Scintilla internal data structures
 **/

#include <cstddef>
#include <cstring>
#include <stdexcept>
#include <string_view>
#include <vector>
#include <optional>
#include <algorithm>
#include <memory>

#include "ScintillaTypes.h"

#include "ILoader.h"
#include "ILexer.h"

#include "Debugging.h"

#include "CharacterCategoryMap.h"
#include "Position.h"
#include "SplitVector.h"
#include "Partitioning.h"
#include "RunStyles.h"
#include "CellBuffer.h"
#include "CharClassify.h"
#include "Decoration.h"
#include "CaseFolder.h"
#include "Document.h"

#include "catch.hpp"

using namespace Scintilla;
using namespace Scintilla::Internal;

// Test Document.

struct DocPlus {
	Document document;

	DocPlus(std::string_view svInitial, int codePage) : document(DocumentOption::Default) {
		document.SetDBCSCodePage(codePage);
		if (codePage == CpUtf8) {
			document.SetCaseFolder(std::make_unique<CaseFolderUnicode>());
		} else {
			// This case folder will not handle many DBCS cases. Scintilla uses platform-specific code for DBCS
			// case folding which can not easily be inserted in platform-independent tests.
			std::unique_ptr<CaseFolderTable> pcft = std::make_unique<CaseFolderTable>();
			pcft->StandardASCII();
			document.SetCaseFolder(std::move(pcft));
		}
		document.InsertString(0, svInitial.data(), svInitial.length());
	}

};

TEST_CASE("Document") {

	const char sText[] = "Scintilla";
	const Sci::Position sLength = static_cast<Sci::Position>(strlen(sText));

	SECTION("InsertOneLine") {
		DocPlus doc("", 0);
		const Sci::Position length = doc.document.InsertString(0, sText, sLength);
		REQUIRE(sLength == doc.document.Length());
		REQUIRE(length == sLength);
		REQUIRE(1 == doc.document.LinesTotal());
		REQUIRE(0 == doc.document.LineStart(0));
		REQUIRE(0 == doc.document.LineFromPosition(0));
		REQUIRE(sLength == doc.document.LineStart(1));
		REQUIRE(0 == doc.document.LineFromPosition(static_cast<int>(sLength)));
		REQUIRE(doc.document.CanUndo());
		REQUIRE(!doc.document.CanRedo());
	}

	SECTION("SearchInUTF8") {
		DocPlus doc("ab\xCE\x93" "d", CpUtf8);	// a b gamma d
		std::string finding = "b";
		Sci::Position lengthFinding = finding.length();
		Sci::Position location = doc.document.FindText(0, doc.document.Length(), finding.c_str(), FindOption::MatchCase, &lengthFinding);
		REQUIRE(location == 1);
		location = doc.document.FindText(doc.document.Length(), 0, finding.c_str(), FindOption::MatchCase, &lengthFinding);
		REQUIRE(location == 1);
	}

	SECTION("SearchInShiftJIS") {
		// {CJK UNIFIED IDEOGRAPH-9955} is two bytes: {0xE9, 'b'} in Shift-JIS
		// The 'b' can be incorrectly matched by the search string 'b' when the search
		// does not iterate the text correctly.
		DocPlus doc("ab\xe9" "b ", 932);	// a b {CJK UNIFIED IDEOGRAPH-9955} {space}
		std::string finding = "b";
		// Search forwards
		Sci::Position lengthFinding = finding.length();
		Sci::Position location = doc.document.FindText(0, doc.document.Length(), finding.c_str(), FindOption::MatchCase, &lengthFinding);
		REQUIRE(location == 1);
		// Search backwards
		lengthFinding = finding.length();
		location = doc.document.FindText(doc.document.Length(), 0, finding.c_str(), FindOption::MatchCase, &lengthFinding);
		REQUIRE(location == 1);
	}

	SECTION("GetCharacterAndWidth") {
		Document doc(DocumentOption::Default);
		doc.SetDBCSCodePage(932);
		REQUIRE(doc.CodePage() == 932);
		const Sci::Position length = doc.InsertString(0, "\x84\xff=", 3);
		REQUIRE(3 == length);
		REQUIRE(3 == doc.Length());
		Sci::Position width = 0;
		int ch = doc.GetCharacterAndWidth(0, &width);
		REQUIRE(width == 1);
		REQUIRE(ch == 0x84);
		width = 0;
		ch = doc.GetCharacterAndWidth(1, &width);
		REQUIRE(width == 1);
		REQUIRE(ch == 0xff);
		width = 0;
		ch = doc.GetCharacterAndWidth(2, &width);
		REQUIRE(width == 1);
		REQUIRE(ch == '=');
	}

}
