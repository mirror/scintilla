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

struct Folding {
	int from;
	int to;
	int length;
};

// Table of case folding for non-ASCII bytes in Windows Latin code page 1252
Folding foldings1252[] = {
	{0x8a, 0x9a, 0x01},
	{0x8c, 0x9c, 0x01},
	{0x8e, 0x9e, 0x01},
	{0x9f, 0xff, 0x01},
	{0xc0, 0xe0, 0x17},
	{0xd8, 0xf8, 0x07},
};

struct DocPlus {
	Document document;

	DocPlus(std::string_view svInitial, int codePage) : document(DocumentOption::Default) {
		SetCodePage(codePage);
		document.InsertString(0, svInitial.data(), svInitial.length());
	}

	void SetCodePage(int codePage) {
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
	}

	void SetSBCSFoldings(const Folding *foldings, size_t length) {
		std::unique_ptr<CaseFolderTable> pcft = std::make_unique<CaseFolderTable>();
		pcft->StandardASCII();
		for (size_t block = 0; block < length; block++) {
			for (int fold = 0; fold < foldings[block].length; fold++) {
				pcft->SetTranslation(foldings[block].from + fold, foldings[block].to + fold);
			}
		}
		document.SetCaseFolder(std::move(pcft));
	}

	Sci::Position FindNeedle(const std::string &needle, FindOption options, Sci::Position *length) {
		assert(*length == static_cast<Sci::Position>(needle.length()));
		return document.FindText(0, document.Length(), needle.c_str(), options, length);
	}
	Sci::Position FindNeedleReverse(const std::string &needle, FindOption options, Sci::Position *length) {
		assert(*length == static_cast<Sci::Position>(needle.length()));
		return document.FindText(document.Length(), 0, needle.c_str(), options, length);
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

	// Search ranges are from first argument to just before second argument
	// Arguments are expected to be at character boundaries and will be tweaked if
	// part way through a character.
	SECTION("SearchInLatin") {
		DocPlus doc("abcde", 0);	// a b c d e
		std::string finding = "b";
		Sci::Position lengthFinding = finding.length();
		Sci::Position location = doc.FindNeedle(finding, FindOption::MatchCase, &lengthFinding);
		REQUIRE(location == 1);
		location = doc.FindNeedleReverse(finding, FindOption::MatchCase, &lengthFinding);
		REQUIRE(location == 1);
		location = doc.document.FindText(0, 2, finding.c_str(), FindOption::MatchCase, &lengthFinding);
		REQUIRE(location == 1);
		location = doc.document.FindText(0, 1, finding.c_str(), FindOption::MatchCase, &lengthFinding);
		REQUIRE(location == -1);
	}

	SECTION("InsensitiveSearchInLatin") {
		DocPlus doc("abcde", 0);	// a b c d e
		std::string finding = "B";
		Sci::Position lengthFinding = finding.length();
		Sci::Position location = doc.FindNeedle(finding, FindOption::None, &lengthFinding);
		REQUIRE(location == 1);
		location = doc.FindNeedleReverse(finding, FindOption::None, &lengthFinding);
		REQUIRE(location == 1);
		location = doc.document.FindText(0, 2, finding.c_str(), FindOption::None, &lengthFinding);
		REQUIRE(location == 1);
		location = doc.document.FindText(0, 1, finding.c_str(), FindOption::None, &lengthFinding);
		REQUIRE(location == -1);
	}

	SECTION("InsensitiveSearchIn1252") {
		// In Windows Latin, code page 1252, C6 is AE and E6 is ae
		DocPlus doc("tru\xc6s\xe6t", 0);	// t r u AE s ae t
		doc.SetSBCSFoldings(foldings1252, std::size(foldings1252));

		// Search for upper-case AE
		std::string finding = "\xc6";
		Sci::Position lengthFinding = finding.length();
		Sci::Position location = doc.FindNeedle(finding, FindOption::None, &lengthFinding);
		REQUIRE(location == 3);
		location = doc.document.FindText(4, doc.document.Length(), finding.c_str(), FindOption::None, &lengthFinding);
		REQUIRE(location == 5);
		location = doc.FindNeedleReverse(finding, FindOption::None, &lengthFinding);
		REQUIRE(location == 5);

		// Search for lower-case ae
		finding = "\xe6";
		location = doc.FindNeedle(finding, FindOption::None, &lengthFinding);
		REQUIRE(location == 3);
		location = doc.document.FindText(4, doc.document.Length(), finding.c_str(), FindOption::None, &lengthFinding);
		REQUIRE(location == 5);
		location = doc.FindNeedleReverse(finding, FindOption::None, &lengthFinding);
		REQUIRE(location == 5);
	}

	SECTION("Search2InLatin") {
		// Checks that the initial '_' and final 'f' are ignored since they are outside the search bounds
		DocPlus doc("_abcdef", 0);	// _ a b c d e f
		std::string finding = "cd";
		Sci::Position lengthFinding = finding.length();
		size_t docLength = doc.document.Length() - 1;
		Sci::Position location = doc.document.FindText(1, docLength, finding.c_str(), FindOption::MatchCase, &lengthFinding);
		REQUIRE(location == 3);
		location = doc.document.FindText(docLength, 1, finding.c_str(), FindOption::MatchCase, &lengthFinding);
		REQUIRE(location == 3);
		location = doc.document.FindText(docLength, 1, "bc", FindOption::MatchCase, &lengthFinding);
		REQUIRE(location == 2);
		location = doc.document.FindText(docLength, 1, "ab", FindOption::MatchCase, &lengthFinding);
		REQUIRE(location == 1);
		location = doc.document.FindText(docLength, 1, "de", FindOption::MatchCase, &lengthFinding);
		REQUIRE(location == 4);
		location = doc.document.FindText(docLength, 1, "_a", FindOption::MatchCase, &lengthFinding);
		REQUIRE(location == -1);
		location = doc.document.FindText(docLength, 1, "ef", FindOption::MatchCase, &lengthFinding);
		REQUIRE(location == -1);
		lengthFinding = 3;
		location = doc.document.FindText(docLength, 1, "cde", FindOption::MatchCase, &lengthFinding);
		REQUIRE(location == 3);
	}

	SECTION("SearchInUTF8") {
		DocPlus doc("ab\xCE\x93" "d", CpUtf8);	// a b gamma d
		const std::string finding = "b";
		Sci::Position lengthFinding = finding.length();
		Sci::Position location = doc.FindNeedle(finding, FindOption::MatchCase, &lengthFinding);
		REQUIRE(location == 1);
		location = doc.document.FindText(doc.document.Length(), 0, finding.c_str(), FindOption::MatchCase, &lengthFinding);
		REQUIRE(location == 1);
		location = doc.document.FindText(0, 1, finding.c_str(), FindOption::MatchCase, &lengthFinding);
		REQUIRE(location == -1);
		// Check doesn't try to follow a lead-byte past the search end
		const std::string findingUTF = "\xCE\x93";
		lengthFinding = findingUTF.length();
		location = doc.document.FindText(0, 4, findingUTF.c_str(), FindOption::MatchCase, &lengthFinding);
		REQUIRE(location == 2);
		// Only succeeds as 3 is partway through character so adjusted to 4
		location = doc.document.FindText(0, 3, findingUTF.c_str(), FindOption::MatchCase, &lengthFinding);
		REQUIRE(location == 2);
		location = doc.document.FindText(0, 2, findingUTF.c_str(), FindOption::MatchCase, &lengthFinding);
		REQUIRE(location == -1);
	}

	SECTION("InsensitiveSearchInUTF8") {
		DocPlus doc("ab\xCE\x93" "d", CpUtf8);	// a b gamma d
		const std::string finding = "b";
		Sci::Position lengthFinding = finding.length();
		Sci::Position location = doc.FindNeedle(finding, FindOption::None, &lengthFinding);
		REQUIRE(location == 1);
		location = doc.document.FindText(doc.document.Length(), 0, finding.c_str(), FindOption::None, &lengthFinding);
		REQUIRE(location == 1);
		const std::string findingUTF = "\xCE\x93";
		lengthFinding = findingUTF.length();
		location = doc.FindNeedle(findingUTF, FindOption::None, &lengthFinding);
		REQUIRE(location == 2);
		location = doc.document.FindText(doc.document.Length(), 0, findingUTF.c_str(), FindOption::None, &lengthFinding);
		REQUIRE(location == 2);
		location = doc.document.FindText(0, 4, findingUTF.c_str(), FindOption::None, &lengthFinding);
		REQUIRE(location == 2);
		// Only succeeds as 3 is partway through character so adjusted to 4
		location = doc.document.FindText(0, 3, findingUTF.c_str(), FindOption::None, &lengthFinding);
		REQUIRE(location == 2);
		location = doc.document.FindText(0, 2, findingUTF.c_str(), FindOption::None, &lengthFinding);
		REQUIRE(location == -1);
	}

	SECTION("SearchInShiftJIS") {
		// {CJK UNIFIED IDEOGRAPH-9955} is two bytes: {0xE9, 'b'} in Shift-JIS
		// The 'b' can be incorrectly matched by the search string 'b' when the search
		// does not iterate the text correctly.
		DocPlus doc("ab\xe9" "b ", 932);	// a b {CJK UNIFIED IDEOGRAPH-9955} {space}
		std::string finding = "b";
		// Search forwards
		Sci::Position lengthFinding = finding.length();
		Sci::Position location = doc.FindNeedle(finding, FindOption::MatchCase, &lengthFinding);
		REQUIRE(location == 1);
		// Search backwards
		lengthFinding = finding.length();
		location = doc.document.FindText(doc.document.Length(), 0, finding.c_str(), FindOption::MatchCase, &lengthFinding);
		REQUIRE(location == 1);
	}

	SECTION("InsensitiveSearchInShiftJIS") {
		// {CJK UNIFIED IDEOGRAPH-9955} is two bytes: {0xE9, 'b'} in Shift-JIS
		// The 'b' can be incorrectly matched by the search string 'b' when the search
		// does not iterate the text correctly.
		DocPlus doc("ab\xe9" "b ", 932);	// a b {CJK UNIFIED IDEOGRAPH-9955} {space}
		std::string finding = "b";
		// Search forwards
		Sci::Position lengthFinding = finding.length();
		Sci::Position location = doc.FindNeedle(finding, FindOption::None, &lengthFinding);
		REQUIRE(location == 1);
		// Search backwards
		lengthFinding = finding.length();
		location = doc.document.FindText(doc.document.Length(), 0, finding.c_str(), FindOption::None, &lengthFinding);
		REQUIRE(location == 1);
		std::string finding932 = "\xe9" "b";
		// Search forwards
		lengthFinding = finding932.length();
		location = doc.FindNeedle(finding932, FindOption::None, &lengthFinding);
		REQUIRE(location == 2);
		// Search backwards
		lengthFinding = finding932.length();
		location = doc.document.FindText(doc.document.Length(), 0, finding932.c_str(), FindOption::None, &lengthFinding);
		REQUIRE(location == 2);
		location = doc.document.FindText(0, 3, finding932.c_str(), FindOption::None, &lengthFinding);
		REQUIRE(location == 2);
		location = doc.document.FindText(0, 2, finding932.c_str(), FindOption::None, &lengthFinding);
		REQUIRE(location == -1);
		// Can not test case mapping of double byte text as folder available here does not implement this 
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
