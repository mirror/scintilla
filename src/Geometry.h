// Scintilla source code edit control
/** @file Geometry.h
 ** Classes and functions for geometric and colour calculations.
 **/
// Copyright 2020 by Neil Hodgson <neilh@scintilla.org>
// The License.txt file describes the conditions under which this software may be distributed.

#ifndef GEOMETRY_H
#define GEOMETRY_H

namespace Scintilla {

typedef float XYPOSITION;
typedef double XYACCUMULATOR;

// Test if an enum class value has the bit flag(s) of test set.
template <typename T>
constexpr bool FlagSet(T value, T test) {
	return (static_cast<int>(value) & static_cast<int>(test)) == static_cast<int>(test);
}

/**
 * A geometric point class.
 * Point is similar to the Win32 POINT and GTK+ GdkPoint types.
 */
class Point {
public:
	XYPOSITION x;
	XYPOSITION y;

	constexpr explicit Point(XYPOSITION x_=0, XYPOSITION y_=0) noexcept : x(x_), y(y_) {
	}

	static constexpr Point FromInts(int x_, int y_) noexcept {
		return Point(static_cast<XYPOSITION>(x_), static_cast<XYPOSITION>(y_));
	}

	constexpr bool operator!=(Point other) const noexcept {
		return (x != other.x) || (y != other.y);
	}

	constexpr Point operator+(Point other) const noexcept {
		return Point(x + other.x, y + other.y);
	}

	constexpr Point operator-(Point other) const noexcept {
		return Point(x - other.x, y - other.y);
	}

	// Other automatically defined methods (assignment, copy constructor, destructor) are fine
};

struct Interval {
	XYPOSITION left;
	XYPOSITION right;
};

/**
 * A geometric rectangle class.
 * PRectangle is similar to Win32 RECT.
 * PRectangles contain their top and left sides, but not their right and bottom sides.
 */
class PRectangle {
public:
	XYPOSITION left;
	XYPOSITION top;
	XYPOSITION right;
	XYPOSITION bottom;

	constexpr explicit PRectangle(XYPOSITION left_=0, XYPOSITION top_=0, XYPOSITION right_=0, XYPOSITION bottom_ = 0) noexcept :
		left(left_), top(top_), right(right_), bottom(bottom_) {
	}

	static constexpr PRectangle FromInts(int left_, int top_, int right_, int bottom_) noexcept {
		return PRectangle(static_cast<XYPOSITION>(left_), static_cast<XYPOSITION>(top_),
			static_cast<XYPOSITION>(right_), static_cast<XYPOSITION>(bottom_));
	}

	// Other automatically defined methods (assignment, copy constructor, destructor) are fine

	constexpr bool operator==(const PRectangle &rc) const noexcept {
		return (rc.left == left) && (rc.right == right) &&
			(rc.top == top) && (rc.bottom == bottom);
	}
	constexpr bool Contains(Point pt) const noexcept {
		return (pt.x >= left) && (pt.x <= right) &&
			(pt.y >= top) && (pt.y <= bottom);
	}
	constexpr bool ContainsWholePixel(Point pt) const noexcept {
		// Does the rectangle contain all of the pixel to left/below the point
		return (pt.x >= left) && ((pt.x+1) <= right) &&
			(pt.y >= top) && ((pt.y+1) <= bottom);
	}
	constexpr bool Contains(PRectangle rc) const noexcept {
		return (rc.left >= left) && (rc.right <= right) &&
			(rc.top >= top) && (rc.bottom <= bottom);
	}
	constexpr bool Intersects(PRectangle other) const noexcept {
		return (right > other.left) && (left < other.right) &&
			(bottom > other.top) && (top < other.bottom);
	}
	void Move(XYPOSITION xDelta, XYPOSITION yDelta) noexcept {
		left += xDelta;
		top += yDelta;
		right += xDelta;
		bottom += yDelta;
	}
	constexpr XYPOSITION Width() const noexcept { return right - left; }
	constexpr XYPOSITION Height() const noexcept { return bottom - top; }
	constexpr bool Empty() const noexcept {
		return (Height() <= 0) || (Width() <= 0);
	}
};

/**
 * Holds an RGB colour with 8 bits for each component.
 */
constexpr const float componentMaximum = 255.0f;
class ColourDesired {
	int co;
public:
	constexpr explicit ColourDesired(int co_=0) noexcept : co(co_) {
	}

	constexpr ColourDesired(unsigned int red, unsigned int green, unsigned int blue) noexcept :
		co(red | (green << 8) | (blue << 16)) {
	}

	constexpr bool operator==(const ColourDesired &other) const noexcept {
		return co == other.co;
	}

	constexpr int AsInteger() const noexcept {
		return co;
	}

	// Red, green and blue values as bytes 0..255
	constexpr unsigned char GetRed() const noexcept {
		return co & 0xff;
	}
	constexpr unsigned char GetGreen() const noexcept {
		return (co >> 8) & 0xff;
	}
	constexpr unsigned char GetBlue() const noexcept {
		return (co >> 16) & 0xff;
	}

	// Red, green and blue values as float 0..1.0
	constexpr float GetRedComponent() const noexcept {
		return GetRed() / componentMaximum;
	}
	constexpr float GetGreenComponent() const noexcept {
		return GetGreen() / componentMaximum;
	}
	constexpr float GetBlueComponent() const noexcept {
		return GetBlue() / componentMaximum;
	}
};

/**
* Holds an RGBA colour.
*/
class ColourAlpha : public ColourDesired {
public:
	constexpr explicit ColourAlpha(int co_ = 0) noexcept : ColourDesired(co_) {
	}

	constexpr ColourAlpha(unsigned int red, unsigned int green, unsigned int blue) noexcept :
		ColourDesired(red | (green << 8) | (blue << 16)) {
	}

	constexpr ColourAlpha(unsigned int red, unsigned int green, unsigned int blue, unsigned int alpha) noexcept :
		ColourDesired(red | (green << 8) | (blue << 16) | (alpha << 24)) {
	}

	constexpr ColourAlpha(ColourDesired cd, unsigned int alpha) noexcept :
		ColourDesired(cd.AsInteger() | (alpha << 24)) {
	}

	constexpr ColourDesired GetColour() const noexcept {
		return ColourDesired(AsInteger() & 0xffffff);
	}

	constexpr unsigned char GetAlpha() const noexcept {
		return (AsInteger() >> 24) & 0xff;
	}

	constexpr float GetAlphaComponent() const noexcept {
		return GetAlpha() / componentMaximum;
	}

	constexpr ColourAlpha MixedWith(ColourAlpha other) const noexcept {
		const unsigned int red = (GetRed() + other.GetRed()) / 2;
		const unsigned int green = (GetGreen() + other.GetGreen()) / 2;
		const unsigned int blue = (GetBlue() + other.GetBlue()) / 2;
		const unsigned int alpha = (GetAlpha() + other.GetAlpha()) / 2;
		return ColourAlpha(red, green, blue, alpha);
	}
};

/**
* Holds an element of a gradient with an RGBA colour and a relative position.
*/
class ColourStop {
public:
	float position;
	ColourAlpha colour;
	ColourStop(float position_, ColourAlpha colour_) noexcept :
		position(position_), colour(colour_) {
	}
};

}

#endif
