/** @file RunStyles.h
 ** Data structure used to store sparse styles.
 **/
// Copyright 1998-2007 by Neil Hodgson <neilh@scintilla.org>
// The License.txt file describes the conditions under which this software may be distributed.

/// Styling buffer using one element for each run rather than using
/// a filled buffer.

#ifndef RUNSTYLES_H
#define RUNSTYLES_H

#ifdef SCI_NAMESPACE
namespace Scintilla {
#endif

class RunStyles {
private:
	Partitioning *starts;
	SplitVector<int> *styles;
	Sci::Position RunFromPosition(Sci::Position position) const;
	Sci::Position SplitRun(Sci::Position position);
	void RemoveRun(Sci::Position run);
	void RemoveRunIfEmpty(Sci::Position run);
	void RemoveRunIfSameAsPrevious(Sci::Position run);
	// Private so RunStyles objects can not be copied
	RunStyles(const RunStyles &);
public:
	RunStyles();
	~RunStyles();
	Sci::Position Length() const;
	int ValueAt(Sci::Position position) const;
	Sci::Position FindNextChange(Sci::Position position, Sci::Position end) const;
	Sci::Position StartRun(Sci::Position position) const;
	Sci::Position EndRun(Sci::Position position) const;
	// Returns true if some values may have changed
	bool FillRange(Sci::Position &position, int value, Sci::Position &fillLength);
	void SetValueAt(Sci::Position position, int value);
	void InsertSpace(Sci::Position position, Sci::Position insertLength);
	void DeleteAll();
	void DeleteRange(Sci::Position position, Sci::Position deleteLength);
	Sci::Position Runs() const;
	bool AllSame() const;
	bool AllSameAs(int value) const;
	Sci::Position Find(int value, Sci::Position start) const;

	void Check() const;
};

#ifdef SCI_NAMESPACE
}
#endif

#endif
