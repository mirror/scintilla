// Scintilla source code edit control
// PlatMacOSX.cxx - implementation of platform facilities on MacOS X/Carbon
// Based on work by Evan Jones (c) 2002 <ejones@uwaterloo.ca>
// Based on PlatGTK.cxx Copyright 1998-2002 by Neil Hodgson <neilh@scintilla.org>
// The License.txt file describes the conditions under which this software may be distributed.

#include <cstring>
#include <cstdio>
#include <cstdlib>

#include <assert.h>

#include <sys/time.h>

#include <Carbon/Carbon.h>
#include "QuartzTextLayout.h"
#include "TCarbonEvent.h"

#include "Platform.h"
#include "Scintilla.h"
#include "PlatMacOSX.h"
#include "XPM.h"

using namespace Scintilla;

#include "ScintillaWidget.h"


extern sptr_t scintilla_send_message(void* sci, unsigned int iMessage, uptr_t wParam, sptr_t lParam);

inline CGRect PRectangleToCGRect( PRectangle& rc ) {
    return CGRectMake( rc.left, rc.top, rc.Width(), rc.Height() );
}

inline PRectangle CGRectToPRectangle( const CGRect& rect ) {
    PRectangle rc;
    rc.left = (int)( rect.origin.x + 0.5 );
    rc.top = (int)( rect.origin.y + 0.5 );
    rc.right = (int)( rect.origin.x + rect.size.width + 0.5 );
    rc.bottom = (int)( rect.origin.y + rect.size.height + 0.5 );
    return rc;
}

Scintilla::Point Scintilla::Point::FromLong(long lpoint) {
  return Scintilla::Point(
               Platform::LowShortFromLong(lpoint),
               Platform::HighShortFromLong(lpoint));
}

// The Palette is just ignored on Mac OS X. OS X runs "Millions" or "Thousands" of colours.
Scintilla::Palette::Palette() {
}

Scintilla::Palette::~Palette() {
}

void Scintilla::Palette::Release() {
}

// Do nothing if it "wants" a colour. Copy the colour from desired to allocated if it is "finding" a colour.
void Scintilla::Palette::WantFind(ColourPair &cp, bool want) {
    if (want) {
    } else {
        cp.allocated.Set(cp.desired.AsLong());
    }
}

void Scintilla::Palette::Allocate(Window &/*w*/) {
    // OS X always runs in thousands or millions of colours
}

Font::Font() : id(0) {}

Font::~Font() { Release(); }


void Font::Create(const char *faceName, int /*characterSet*/,
                  int size, bool bold, bool italic, bool /*extraFontFlag*/) {
    // TODO: How should I handle the characterSet request?
    Release();

    id = new QuartzTextStyle();

    // Find the font
    QuartzFont font( faceName, strlen( faceName ) );

    // We set Font, Size, Bold, Italic
    QuartzTextSize textSize( size );
    QuartzTextBold isBold( bold );
    QuartzTextItalic isItalic( italic );

    // Actually set the attributes
    QuartzTextStyleAttribute* attributes[] = { &font, &textSize, &isBold, &isItalic };
    reinterpret_cast<QuartzTextStyle*>( id )->setAttributes( attributes, sizeof( attributes ) / sizeof( *attributes ) );

    //ATSStyleRenderingOptions rendering = kATSStyleNoAntiAliasing;
    //reinterpret_cast<QuartzTextStyle*>( id )->setAttribute( kATSUStyleRenderingOptionsTag, sizeof( rendering ), &rendering );

    // TODO: Why do I have to manually set this?
    reinterpret_cast<QuartzTextStyle*>( id )->setFontFeature( kLigaturesType, kCommonLigaturesOffSelector );
}

void Font::Release() {
    if (id)
            delete reinterpret_cast<QuartzTextStyle*>( id );

    id = 0;
}

SurfaceImpl::SurfaceImpl() {
    bitmapData = NULL; // Release will try and delete bitmapData if != NULL
#ifdef SUPPORT_PORT
    port = NULL;
#endif
    gc = NULL;
    Release();
}

SurfaceImpl::~SurfaceImpl() {
    Release();
}

void SurfaceImpl::Release() {
    if ( bitmapData != NULL )
        {
        delete[] bitmapData;
        // We only "own" the graphics context if we are a bitmap context
        if ( gc != NULL ) CGContextRelease( gc );
        }
#ifdef SUPPORT_PORT
    if ( port != NULL && gc != NULL) {
      QDEndCGContext(port, &gc);
    }
#endif
    bitmapData = NULL;
    gc = NULL;

    bitmapWidth = 0;    
    bitmapHeight = 0;
    x = 0;
    y = 0;
    //inited = false;
}

bool SurfaceImpl::Initialised() {
    // We are initalised if the graphics context is not null
  return gc != NULL;// || port != NULL;
}

void SurfaceImpl::Init(WindowID /*wid*/) {
    // To be able to draw, the surface must get a CGContext handle.  We save the graphics port,
    // then aquire/release the context on an as-need basis (see above).
    // XXX Docs on QDBeginCGContext are light, a better way to do this would be good.
    // AFAIK we should not hold onto a context retrieved this way, thus the need for
    // aquire/release of the context.

    Release();
#ifdef SUPPORT_PORT
    // Note, this seems to be unecessary, I have not seen any functionality loss by
    // doing nothing in this method.

    WindowRef window = GetControlOwner(reinterpret_cast<HIViewRef>(wid));
    port = GetWindowPort(window);
    QDBeginCGContext(port, &gc);
#endif
}

void SurfaceImpl::Init(SurfaceID sid, WindowID /*wid*/) {
    Release();
    gc = reinterpret_cast<CGContextRef>( sid );
    CGContextSetLineWidth( gc, 1.0 );
}

void SurfaceImpl::InitPixMap(int width, int height, Surface* /*surface_*/, WindowID /*wid*/) {
    Release();
    // Create a new bitmap context, along with the RAM for the bitmap itself
    bitmapWidth = width;
    bitmapHeight = height;
    
    const int bitmapBytesPerRow   = (width * BYTES_PER_PIXEL);
    const int bitmapByteCount     = (bitmapBytesPerRow * height);

    // create an RGB color space
    CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
    assert( colorSpace != NULL );

    // create the bitmap
    bitmapData = new uint8_t[ bitmapByteCount ];
    assert( bitmapData != NULL );
    if( bitmapData != NULL )
        {
        // create the context
        gc = CGBitmapContextCreate( bitmapData,
                                    width,
                                    height,
                                    BITS_PER_COMPONENT,
                                    bitmapBytesPerRow,
                                    colorSpace,
                                    kCGImageAlphaPremultipliedLast);

        if( gc == NULL )
            {
            // the context couldn't be created for some reason,
            // and we have no use for the bitmap without the context
            delete[] bitmapData;
            bitmapData = NULL;
            }
        }

    // the context retains the color space, so we can release it
    CGColorSpaceRelease( colorSpace );  

    assert( gc != NULL && bitmapData != NULL );

    // "Erase" to white
    CGContextClearRect( gc, CGRectMake( 0, 0, width, height ) );
    CGContextSetRGBFillColor( gc, 1.0, 1.0, 1.0, 1.0 );
    CGContextFillRect( gc, CGRectMake( 0, 0, width, height ) );
}

void SurfaceImpl::PenColour(ColourAllocated fore) {
    if (gc) {
        ColourDesired colour( fore.AsLong() );

        // Set the Stroke color to match
        CGContextSetRGBStrokeColor( gc, colour.GetRed() / 255.0, colour.GetGreen() / 255.0, colour.GetBlue() / 255.0, 1.0 );
    }
}

void SurfaceImpl::FillColour(const ColourAllocated& back) {
    if (gc) {
        ColourDesired colour( back.AsLong() );

        // Set the Fill color to match
        CGContextSetRGBFillColor( gc, colour.GetRed() / 255.0, colour.GetGreen() / 255.0, colour.GetBlue() / 255.0, 1.0 );
    }
}

CGImageRef SurfaceImpl::GetImage() {
    // For now, assume that GetImage can only be called on PixMap surfaces
    if ( bitmapData == NULL ) return NULL;

    CGContextFlush( gc );

    // create an RGB color space
    CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
    assert( colorSpace != NULL );

    const int bitmapBytesPerRow   = ((int) bitmapWidth * BYTES_PER_PIXEL);
    const int bitmapByteCount     = (bitmapBytesPerRow * (int) bitmapHeight);
    
    // Create a data provider
    CGDataProviderRef dataProvider = CGDataProviderCreateWithData( NULL, bitmapData, bitmapByteCount, NULL );
    assert( dataProvider != NULL );

    // create the CGImage
    CGImageRef image = CGImageCreate( bitmapWidth,
                                   bitmapHeight,
                                   BITS_PER_COMPONENT,
                                   BITS_PER_PIXEL,
                                   bitmapBytesPerRow,
                                   colorSpace,
                                   kCGImageAlphaPremultipliedLast,
                                   dataProvider,
                                   NULL,
                                   0,
                                   kCGRenderingIntentDefault );
    assert( image != NULL );

    // the image retains the color space, so we can release it
    CGColorSpaceRelease( colorSpace );
    colorSpace = NULL;

    // Done with the data provider
    CGDataProviderRelease( dataProvider );
    dataProvider = NULL;

    return image;
}

int SurfaceImpl::LogPixelsY() {
    return 72;
}

int SurfaceImpl::DeviceHeightFont(int points) {
    int logPix = LogPixelsY();
    return (points * logPix + logPix / 2) / 72;
}

void SurfaceImpl::MoveTo(int x_, int y_) {
    x = x_;
    y = y_;
}

void SurfaceImpl::LineTo(int x_, int y_) {
    CGContextBeginPath( gc );
    // Because Quartz is based on floating point, lines are drawn with half their colour
    // on each side of the line. Integer coordinates specify the INTERSECTION of the pixel
    // divison lines. If you specify exact pixel values, you get a line that
    // is twice as thick but half as intense. To get pixel aligned rendering,
    // we render the "middle" of the pixels by adding 0.5 to the coordinates.
    CGContextMoveToPoint( gc, x + 0.5, y + 0.5 );
    CGContextAddLineToPoint( gc, x_ + 0.5, y_ + 0.5 );
    CGContextStrokePath( gc );
    x = x_;
    y = y_;
}

void SurfaceImpl::Polygon(Scintilla::Point *pts, int npts, ColourAllocated fore,
                      ColourAllocated back) {
    // Allocate memory for the array of points
    CGPoint *points = new CGPoint[ npts ];
    
    for (int i = 0;i < npts;i++) {
        // Quartz floating point issues: plot the MIDDLE of the pixels
        points[i].x = pts[i].x + 0.5;
        points[i].y = pts[i].y + 0.5;
    }

    CGContextBeginPath( gc );

    // Set colours
    FillColour(back);
    PenColour(fore);

    // Draw the polygon
    CGContextAddLines( gc, points, npts );
    // TODO: Should the path be automatically closed, or is that the caller's responsability?
    // Explicitly close the path, so it is closed for stroking AND filling (implicit close = filling only)
    CGContextClosePath( gc ); 
    CGContextDrawPath( gc, kCGPathFillStroke );

    // Deallocate memory
    delete points;
    points = NULL;
}

void SurfaceImpl::RectangleDraw(PRectangle rc, ColourAllocated fore, ColourAllocated back) {
    if ( gc ) {
        CGContextBeginPath( gc );
        FillColour(back);
        PenColour(fore);

        // Quartz integer -> float point conversion fun (see comment in SurfaceImpl::LineTo)
        // We subtract 1 from the Width() and Height() so that all our drawing is within the area defined
        // by the PRectangle. Otherwise, we draw one pixel too far to the right and bottom.
        // TODO: Create some version of PRectangleToCGRect to do this conversion for us?
        CGContextAddRect( gc, CGRectMake( rc.left + 0.5, rc.top + 0.5, rc.Width() - 1, rc.Height() - 1 ) );
        CGContextDrawPath( gc, kCGPathFillStroke );
    }
}

void SurfaceImpl::FillRectangle(PRectangle rc, ColourAllocated back) {
    if ( gc ) {
        //CGContextBeginPath( gc );
        FillColour(back);

        CGRect rect = PRectangleToCGRect( rc );

        CGContextFillRect( gc, rect );
        //CGContextDrawPath( gc, kCGPathFill );
    }
}

void drawImageRefCallback( CGImageRef pattern, CGContextRef gc )
{
    CGContextDrawImage( gc, CGRectMake( 0, 0, CGImageGetWidth( pattern ), CGImageGetHeight( pattern ) ), pattern );
}

void SurfaceImpl::FillRectangle(PRectangle rc, Surface &surfacePattern) {
    SurfaceImpl& patternSurface = static_cast<SurfaceImpl &>(surfacePattern);

    // For now, assume that copy can only be called on PixMap surfaces
    // Shows up black
    CGImageRef image = patternSurface.GetImage();
    if ( image == NULL )
        {
        FillRectangle(rc, ColourAllocated(0));
        return;
        }

    assert( image != NULL );

    const CGPatternCallbacks drawImageCallbacks = { 0, reinterpret_cast<CGPatternDrawPatternCallback>( drawImageRefCallback ), NULL };

    CGPatternRef pattern = CGPatternCreate( image,
                                         CGRectMake( 0, 0, patternSurface.bitmapWidth, patternSurface.bitmapHeight ),
                                         CGAffineTransformIdentity,
                                         patternSurface.bitmapWidth,
                                         patternSurface.bitmapHeight,
                                         kCGPatternTilingNoDistortion,
                                         true,
                                         &drawImageCallbacks
                                         );
    assert( pattern != NULL );

    // Create a pattern color space
    CGColorSpaceRef colorSpace = CGColorSpaceCreatePattern( NULL );
    assert( colorSpace != NULL );

    CGContextSaveGState( gc );
    CGContextSetFillColorSpace( gc, colorSpace );

    // Unlike the documentation, you MUST pass in a "components" parameter:
    // For coloured patterns it is the alpha value.
    const float alpha = 1.0;
    CGContextSetFillPattern( gc, pattern, &alpha );
    CGContextFillRect( gc, PRectangleToCGRect( rc ) );

    CGContextRestoreGState( gc );

    // Free the color space, the pattern and image
    CGColorSpaceRelease( colorSpace );
    colorSpace = NULL;
    CGPatternRelease( pattern );
    pattern = NULL;
    CGImageRelease( image );
    image = NULL;
}

void SurfaceImpl::RoundedRectangle(PRectangle rc, ColourAllocated fore, ColourAllocated back) {
    // TODO: Look at the Win32 API to determine what this is supposed to do:
    //  ::RoundRect(hdc, rc.left + 1, rc.top, rc.right - 1, rc.bottom, 8, 8 );
    
    // Create a rectangle with semicircles at the corners
    const int MAX_RADIUS = 4;
    int radius = Platform::Minimum( MAX_RADIUS, rc.Height()/2 );
    radius = Platform::Minimum( radius, rc.Width()/2 );
    
    // Points go clockwise, starting from just below the top left
    // Corners are kept together, so we can easily create arcs to connect them
    CGPoint corners[4][3] =
        {
            {
                { rc.left, rc.top + radius },
                { rc.left, rc.top },
                { rc.left + radius, rc.top },
            },
            {
                { rc.right - radius - 1, rc.top },
                { rc.right - 1, rc.top },
                { rc.right - 1, rc.top + radius },
            },
            {
                { rc.right - 1, rc.bottom - radius - 1 },
                { rc.right - 1, rc.bottom - 1 },
                { rc.right - radius - 1, rc.bottom - 1 },
            },
            {
                { rc.left + radius, rc.bottom - 1 },
                { rc.left, rc.bottom - 1 },
                { rc.left, rc.bottom - radius - 1 },
            },
        };

    // Align the points in the middle of the pixels
    // TODO: Should I include these +0.5 in the array creation code above?
    for( int i = 0; i < 4*3; ++ i )
        {
        CGPoint* c = (CGPoint*) corners;
        c[i].x += 0.5;
        c[i].y += 0.5;
        }

    PenColour( fore );
    FillColour( back );
    
    // Move to the last point to begin the path
    CGContextBeginPath( gc );
    CGContextMoveToPoint( gc, corners[3][2].x, corners[3][2].y );
    
    for ( int i = 0; i < 4; ++ i )
        {
        CGContextAddLineToPoint( gc, corners[i][0].x, corners[i][0].y );
        CGContextAddArcToPoint( gc, corners[i][1].x, corners[i][1].y, corners[i][2].x, corners[i][2].y, radius );
        }
    
    // Close the path to enclose it for stroking and for filling, then draw it
    CGContextClosePath( gc );
    CGContextDrawPath( gc, kCGPathFillStroke );
}

void SurfaceImpl::Ellipse(PRectangle rc, ColourAllocated fore, ColourAllocated back) {
    // Drawing an ellipse with bezier curves. Code modified from:
    // http://www.codeguru.com/gdi/ellipse.shtml
    // MAGICAL CONSTANT to map ellipse to beziers 2/3*(sqrt(2)-1)
    const double EToBConst = 0.2761423749154;

    CGSize offset = CGSizeMake((int)(rc.Width() * EToBConst), (int)(rc.Height() * EToBConst));
    CGPoint centre = CGPointMake((rc.left + rc.right) / 2, (rc.top + rc.bottom) / 2);

    // The control point array
    CGPoint cCtlPt[13];

    // Assign values to all the control points
    cCtlPt[0].x  =
        cCtlPt[1].x  =
        cCtlPt[11].x =
        cCtlPt[12].x = rc.left + 0.5;
    cCtlPt[5].x  =
        cCtlPt[6].x  =
        cCtlPt[7].x  = rc.right - 0.5;
    cCtlPt[2].x  =
        cCtlPt[10].x = centre.x - offset.width + 0.5;
    cCtlPt[4].x  =
        cCtlPt[8].x  = centre.x + offset.width + 0.5;
    cCtlPt[3].x  =
        cCtlPt[9].x  = centre.x + 0.5;

    cCtlPt[2].y  =
        cCtlPt[3].y  =
        cCtlPt[4].y  = rc.top + 0.5;
    cCtlPt[8].y  =
        cCtlPt[9].y  =
        cCtlPt[10].y = rc.bottom - 0.5;
    cCtlPt[7].y  =
        cCtlPt[11].y = centre.y + offset.height + 0.5;
    cCtlPt[1].y =
        cCtlPt[5].y  = centre.y - offset.height + 0.5;
    cCtlPt[0].y =
        cCtlPt[12].y =
        cCtlPt[6].y  = centre.y + 0.5;

    FillColour(back);
    PenColour(fore);

    CGContextBeginPath( gc );
    CGContextMoveToPoint( gc, cCtlPt[0].x, cCtlPt[0].y );

    for ( int i = 1; i < 13; i += 3 )
        {
        CGContextAddCurveToPoint( gc, cCtlPt[i].x, cCtlPt[i].y, cCtlPt[i+1].x, cCtlPt[i+1].y, cCtlPt[i+2].x, cCtlPt[i+2].y );
        }

    // Close the path to enclose it for stroking and for filling, then draw it
    CGContextClosePath( gc );
    CGContextDrawPath( gc, kCGPathFillStroke );
}

void SurfaceImpl::CopyImageRectangle(Surface &surfaceSource, PRectangle srcRect, PRectangle dstRect) 
{
    SurfaceImpl& source = static_cast<SurfaceImpl &>(surfaceSource);
    CGImageRef image = source.GetImage();
    
    CGRect src = PRectangleToCGRect(srcRect);
    CGRect dst = PRectangleToCGRect(dstRect);
    
    /* source from QuickDrawToQuartz2D.pdf on developer.apple.com */
    float w = (float) CGImageGetWidth(image);
    float h = (float) CGImageGetHeight(image); 
    CGRect drawRect = CGRectMake (0, 0, w, h);
    if (!CGRectEqualToRect (src, dst))  
    {
        float sx = CGRectGetWidth(dst) / CGRectGetWidth(src);
        float sy = CGRectGetHeight(dst) / CGRectGetHeight(src);
        float dx = CGRectGetMinX(dst) - (CGRectGetMinX(src) * sx);
        float dy = CGRectGetMinY(dst) - (CGRectGetMinY(src) * sy);
        drawRect = CGRectMake (dx, dy, w*sx, h*sy);
    }
    CGContextSaveGState (gc);  
    CGContextClipToRect (gc, dst);  
    CGContextDrawImage (gc, drawRect, image);
    CGContextRestoreGState (gc); 
}

void SurfaceImpl::Copy(PRectangle rc, Scintilla::Point from, Surface &surfaceSource) {
    // Maybe we have to make the Surface two contexts:
    // a bitmap context which we do all the drawing on, and then a "real" context
    // which we copy the output to when we call "Synchronize". Ugh! Gross and slow!

    // For now, assume that copy can only be called on PixMap surfaces
    SurfaceImpl& source = static_cast<SurfaceImpl &>(surfaceSource);
    
    // Get the CGImageRef
    CGImageRef image = source.GetImage();
    // If we could not get an image reference, fill the rectangle black
    if ( image == NULL )
        {
        FillRectangle( rc, ColourAllocated( 0 ) );
        return;
        }
    
    assert( image != NULL );

    // Now draw the image on the surface

    // Some fancy clipping work is required here: draw only inside of rc
    CGContextSaveGState( gc );
    CGContextClipToRect( gc, PRectangleToCGRect( rc ) );

    //Platform::DebugPrintf(stderr, "Copy: CGContextDrawImage: (%d, %d) - (%d X %d)\n", rc.left - from.x, rc.top - from.y, source.bitmapWidth, source.bitmapHeight );
    CGContextDrawImage( gc, CGRectMake( rc.left - from.x, rc.top - from.y, source.bitmapWidth, source.bitmapHeight ), image );

    // Undo the clipping fun
    CGContextRestoreGState( gc );

    // Done with the image
    CGImageRelease( image );
    image = NULL;
}

QuartzTextLayout* SurfaceImpl::GetTextLayout( Font &font_, const char *s, int len )
{
    // create the text layout
    QuartzTextLayout* textLayout = new QuartzTextLayout( gc );
    textLayout->setText( reinterpret_cast<const UInt8*>( s ), len, ( unicodeMode ? kCFStringEncodingUTF8 : kCFStringEncodingASCII ) );
    textLayout->setStyle( *reinterpret_cast<QuartzTextStyle*>( font_.GetID() ) );

    // TODO: If I could modify Scintilla to use ATSUHighlightText, this would not be required.
    // However, using this setting makes Scintilla's rendering match TextEdit's (except for the antialiasing rules)
    ATSLineLayoutOptions rendering = kATSLineUseDeviceMetrics;
    textLayout->setControl( kATSULineLayoutOptionsTag, sizeof( rendering ), &rendering );

    return textLayout;
}

void SurfaceImpl::DrawTextNoClip(PRectangle rc, Font &font_, int ybase, const char *s, int len,
                                 ColourAllocated fore, ColourAllocated back) {

    FillRectangle(rc, back);
    DrawTextTransparent( rc, font_, ybase, s, len, fore );
}

void SurfaceImpl::DrawTextClipped(PRectangle rc, Font &font_, int ybase, const char *s, int len,
                                  ColourAllocated fore, ColourAllocated back) {

    CGContextSaveGState( gc );
    CGContextClipToRect( gc, PRectangleToCGRect( rc ) );
    DrawTextNoClip( rc, font_, ybase, s, len, fore, back );
    CGContextRestoreGState( gc );
}

void SurfaceImpl::DrawTextTransparent(PRectangle rc, Font &font_, int ybase, const char *s, int len, ColourAllocated fore) {
    PLATFORM_ASSERT( gc != NULL );
    QuartzTextLayout* textLayout = GetTextLayout( font_, s, len );

    // The Quartz RGB fill color influences the ATSUI color
    FillColour(fore);

    // Draw the text, with the Y axis flipped
    textLayout->draw( rc.left, ybase, true );

    // Get rid of the layout object
    delete textLayout;
    textLayout = NULL;
}

void SurfaceImpl::MeasureWidths(Font &font_, const char *s, int len, int *positions) {
    // sample at http://developer.apple.com/samplecode/ATSUICurveAccessDemo/listing1.html
    // sample includes use of ATSUGetGlyphInfo which would be better for older
    // OSX systems.  We should expand to using that on older systems as well.
    QuartzTextLayout* textLayout = GetTextLayout( font_, s, len );

    // Get the UNICODE character length
    UniCharCount unicodeLength, unicodePosition;
    OSStatus err;
    err = ATSUGetTextLocation( textLayout->getLayout(), NULL, NULL, NULL, &unicodeLength, NULL );
    assert( err == noErr );

    ATSLayoutRecord *layoutRecords;
    ItemCount numRecords;
    // Get the arrays of glyph information
    verify_noerr( ATSUDirectGetLayoutDataArrayPtrFromTextLayout(
                textLayout->getLayout(), 0,
                kATSUDirectDataLayoutRecordATSLayoutRecordCurrent,
                (void **)&layoutRecords, &numRecords) );
    
    int i;
    for ( unicodePosition = 0, i = 0; i < len && unicodePosition < numRecords; unicodePosition ++ ) {
        if (font_.GetID()) {

            // note: an extra layoutRecord is provided (eg. numRecords will be one
            //       more than unicodeLength).  Each record contains the left x
            //       coordinate, but we need the right x coordinate, so we skip
            //       the first layoutRecord, thus unicodePosition+1.
            long position = Fix2Long( layoutRecords[unicodePosition+1].realPos );
            unsigned char uch = s[i];
            positions[i++] = position;

            // If we are using unicode (UTF8), map the Unicode position back to the UTF8 characters,
            // as 1 unicode character can map to multiple UTF8 characters.
            // See: http://www.tbray.org/ongoing/When/200x/2003/04/26/UTF
            // Or: http://www.cl.cam.ac.uk/~mgk25/unicode.html
            if ( unicodeMode ) {
                unsigned char mask = 0xc0;
                int count = 1;
                // Add one additonal byte for each extra high order one in the byte
                while ( uch >= mask && count < 8 ) {
                    assert( i < len );
                    positions[i++] = position;
                    count ++;
                    mask = mask >> 1 | 0x80; // add an additional one in the highest order position
                }
                assert( count <= 8 );
            }
        } else {
        positions[i++] = (i == 0) ? 1 : positions[i-1] + 1;
        }
    }
    verify_noerr( ATSUDirectReleaseLayoutDataArrayPtr(NULL, kATSUDirectDataLayoutRecordATSLayoutRecordCurrent, (void **)&layoutRecords) );
}

int SurfaceImpl::WidthText(Font &font_, const char *s, int len) {
    if (font_.GetID())
    {
        QuartzTextLayout* textLayout = GetTextLayout( font_, s, len );
        
        // TODO: Maybe I should add some sort of text measurement features to QuartzTextLayout?
        unsigned long actualNumberOfBounds = 0;
        ATSTrapezoid glyphBounds;

        // We get a single bound, since the text should only require one. If it requires more, there is an issue
        if ( ATSUGetGlyphBounds( textLayout->getLayout(), 0, 0, kATSUFromTextBeginning, kATSUToTextEnd, kATSUseDeviceOrigins, 1, &glyphBounds, &actualNumberOfBounds ) != noErr || actualNumberOfBounds != 1 )
        {
            Platform::DebugDisplay( "ATSUGetGlyphBounds failed in WidthText" );
            return 0;
        }

        //Platform::DebugPrintf( "WidthText: \"%*s\" = %ld\n", len, s, Fix2Long( glyphBounds.upperRight.x - glyphBounds.upperLeft.x ) );
        
        return Fix2Long( glyphBounds.upperRight.x - glyphBounds.upperLeft.x );
    }
    return 1;
}

int SurfaceImpl::WidthChar(Font &font_, char ch) {
    char str[2] = { ch, '\0' };
    if (font_.GetID())
    {
        QuartzTextLayout* textLayout = GetTextLayout( font_, str, 1 );

        // TODO: Maybe I should add some sort of text measurement features to QuartzTextLayout?
        unsigned long actualNumberOfBounds = 0;
        ATSTrapezoid glyphBounds;

        // We get a single bound, since the text should only require one. If it requires more, there is an issue
        if ( ATSUGetGlyphBounds( textLayout->getLayout(), 0, 0, kATSUFromTextBeginning, kATSUToTextEnd, kATSUseDeviceOrigins, 1, &glyphBounds, &actualNumberOfBounds ) != noErr || actualNumberOfBounds != 1 )
        {
            Platform::DebugDisplay( "ATSUGetGlyphBounds failed in WidthChar" );
            return 0;
        }

        return Fix2Long( glyphBounds.upperRight.x - glyphBounds.upperLeft.x );
    }
    else
        return 1;
}

// Three possible strategies for determining ascent and descent of font:
// 1) Call ATSUGetGlyphBounds with string containing all letters, numbers and punctuation.
// 2) Use the ascent and descent fields of the font.
// 3) Call ATSUGetGlyphBounds with string as 1 but also including accented capitals.
// Smallest values given by 1 and largest by 3 with 2 in between.
// Techniques 1 and 2 sometimes chop off extreme portions of ascenders and
// descenders but are mostly OK except for accented characters which are
// rarely used in code.

// This string contains a good range of characters to test for size.
const char sizeString[] = "`~!@#$%^&*()-_=+\\|[]{};:\"\'<,>.?/1234567890"
                          "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";

int SurfaceImpl::Ascent(Font &font_) {
    if (!font_.GetID())
        return 1;

    ATSUTextMeasurement ascent = reinterpret_cast<QuartzTextStyle*>( font_.GetID() )->getAttribute<ATSUTextMeasurement>( kATSUAscentTag );
    return Fix2Long( ascent );
}

int SurfaceImpl::Descent(Font &font_) {
    if (!font_.GetID())
        return 1;

    ATSUTextMeasurement descent = reinterpret_cast<QuartzTextStyle*>( font_.GetID() )->getAttribute<ATSUTextMeasurement>( kATSUDescentTag );
    return Fix2Long( descent );
}

int SurfaceImpl::InternalLeading(Font &) {
    // TODO: How do we get EM_Size?
    // internal leading = ascent - descent - EM_size
    return 0;
}

int SurfaceImpl::ExternalLeading(Font &font_) {
    if (!font_.GetID())
        return 1;

    ATSUTextMeasurement lineGap = reinterpret_cast<QuartzTextStyle*>( font_.GetID() )->getAttribute<ATSUTextMeasurement>( kATSULeadingTag );
    return Fix2Long( lineGap );
}

int SurfaceImpl::Height(Font &font_) {
    return Ascent(font_) + Descent(font_);
}

int SurfaceImpl::AverageCharWidth(Font &font_) {

    if (!font_.GetID())
        return 1;

    const int sizeStringLength = (sizeof( sizeString ) / sizeof( sizeString[0] ) - 1);
    int width = WidthText( font_, sizeString, sizeStringLength  );

    return (int) ((width / (float) sizeStringLength) + 0.5);

    /*
    ATSUStyle textStyle = reinterpret_cast<QuartzTextStyle*>( font_.GetID() )->getATSUStyle();
    ATSUFontID fontID;

    ByteCount actualSize = 0;
    if ( ATSUGetAttribute( textStyle, kATSUFontTag, sizeof( fontID ), &fontID, &actualSize ) != noErr )
        {
Platform::DebugDisplay( "ATSUGetAttribute failed" );
        return 1;
        }

    ATSFontMetrics metrics;
    memset( &metrics, 0, sizeof( metrics ) );
    if ( ATSFontGetHorizontalMetrics( fontID, kATSOptionFlagsDefault, &metrics ) != noErr )
    {
        Platform::DebugDisplay( "ATSFontGetHorizontalMetrics failed in AverageCharWidth" );
        return 1;
    }

    printf( "%f %f %f\n", metrics.avgAdvanceWidth * 32, metrics.ascent * 32, metrics.descent * 32 );

    return (int) (metrics.avgAdvanceWidth + 0.5);*/
}

int SurfaceImpl::SetPalette(Scintilla::Palette *, bool) {
    // Mac OS X is always true colour (I think) so this doesn't matter
    return 0;
}

void SurfaceImpl::SetClip(PRectangle rc) {
    CGContextClipToRect( gc, PRectangleToCGRect( rc ) );
}

void SurfaceImpl::FlushCachedState() {
    CGContextSynchronize( gc );
}

void SurfaceImpl::SetUnicodeMode(bool unicodeMode_) {
    unicodeMode = unicodeMode_;
}

void SurfaceImpl::SetDBCSMode(int codePage) {
    // TODO: Implement this
}

Surface *Surface::Allocate() {
    return new SurfaceImpl(  );
}

Window::~Window() {}

void Window::Destroy() {
    if (windowRef) {
        DisposeWindow(reinterpret_cast<WindowRef>( windowRef ));
    }
    id = 0;
}

bool Window::HasFocus() {
    // TODO: Test this
    return HIViewSubtreeContainsFocus( reinterpret_cast<HIViewRef>( id ) );
}

PRectangle Window::GetPosition() {
    // Before any size allocated pretend its 1000 wide so not scrolled
    PRectangle rc(0, 0, 1000, 1000);
    
    // The frame rectangle gives the position of this view inside the parent view
    if (id) {
        HIRect controlFrame;
        HIViewGetFrame( reinterpret_cast<HIViewRef>( id ), &controlFrame );
        rc = CGRectToPRectangle( controlFrame );
    }
    
    return rc;
}

void Window::SetPosition(PRectangle rc) {
    // Moves this view inside the parent view
    if ( id )
    {
        // Set the frame on the view, the function handles the rest
        CGRect r = PRectangleToCGRect( rc );
        HIViewSetFrame( reinterpret_cast<HIViewRef>( id ), &r );
    } 
}

void Window::SetPositionRelative(PRectangle rc, Window window) {
    // used to actually move child windows (ie. listbox/calltip) so we have to move 
    // the window, not the hiview
    if (windowRef) {
        // we go through some contortions here to get an accurate location for our
        // child windows.  This is necessary due to the multiple ways an embedding
        // app may be setup.  See SciTest/main.c (GOOD && BAD) for test case.
        WindowRef relativeWindow = GetControlOwner(reinterpret_cast<HIViewRef>( window.GetID() ));
        WindowRef thisWindow = reinterpret_cast<WindowRef>( windowRef );
      
        Rect portBounds;
        ::GetWindowBounds(relativeWindow, kWindowStructureRgn, &portBounds);
        //fprintf(stderr, "portBounds %d %d %d %d\n", portBounds.left, portBounds.top, portBounds.right, portBounds.bottom);
        PRectangle hbounds = window.GetPosition();
        //fprintf(stderr, "hbounds %d %d %d %d\n", hbounds.left, hbounds.top, hbounds.right, hbounds.bottom);
        HIViewRef parent = HIViewGetSuperview(reinterpret_cast<HIViewRef>( window.GetID() ));
        Rect pbounds;
        GetControlBounds(parent, &pbounds);
        //fprintf(stderr, "pbounds %d %d %d %d\n", pbounds.left, pbounds.top, pbounds.right, pbounds.bottom);

        PRectangle bounds;
        bounds.top = portBounds.top + pbounds.top + hbounds.top + rc.top;
        bounds.bottom = bounds.top + rc.Height();
        bounds.left = portBounds.left + pbounds.left + hbounds.left + rc.left;
        bounds.right = bounds.left + rc.Width();
        //fprintf(stderr, "bounds %d %d %d %d\n", bounds.left, bounds.top, bounds.right, bounds.bottom);

        MoveWindow(thisWindow, bounds.left, bounds.top, false);
        SizeWindow(thisWindow, bounds.Width(), bounds.Height(), true);

        SetPosition(PRectangle(0,0,rc.Width(),rc.Height()));
    } else {
        SetPosition(rc);
    }
}

PRectangle Window::GetClientPosition() {
    // This means, in MacOS X terms, get the "frame bounds". Call GetPosition, just like on Win32.
    return GetPosition();
}

void Window::Show(bool show) {
    if ( id ) {
        HIViewSetVisible( reinterpret_cast<HIViewRef>( id ), show );
    } 
    // this is necessary for calltip/listbox
    if (windowRef) {
        WindowRef thisWindow = reinterpret_cast<WindowRef>( windowRef );
        if (show) {
            ShowWindow( thisWindow );
            DrawControls( thisWindow );
        } else
            HideWindow( thisWindow );
    }
}

void Window::InvalidateAll() {
    if ( id ) {
        OSStatus err;
        err = HIViewSetNeedsDisplay( reinterpret_cast<HIViewRef>( id ), true );
        assert( err == noErr );
    }
}

void Window::InvalidateRectangle(PRectangle rc) {
    if (id) {
        // Create a rectangular region
        RgnHandle region = NewRgn();
        assert( region != NULL );
        SetRectRgn( region, rc.left, rc.top, rc.right, rc.bottom );

        // Make that region invalid
        OSStatus err;
        err = HIViewSetNeedsDisplayInRegion( reinterpret_cast<HIViewRef>( id ), region, true );
        assert( err == noErr );
        DisposeRgn( region );
    }
}

void Window::SetFont(Font &) {
    // TODO: Do I need to implement this? MSDN: specifies the font that a control is to use when drawing text.
}

void Window::SetCursor(Cursor curs) {
    if (id) {
        // TODO: This isn't really implemented correctly. I should be using
        // mouse tracking rectangles to only set the mouse cursor when it is over the control
        ThemeCursor cursor;

        switch ( curs ) {
            case cursorText:
                cursor = kThemeIBeamCursor;
                break;
            case cursorArrow:
                cursor = kThemeArrowCursor;
                break;
            case cursorWait:
                cursor = kThemeWatchCursor;
                break;
            case cursorHoriz:
                cursor = kThemeResizeLeftRightCursor;
                break;
            case cursorVert:
            case cursorReverseArrow:
            case cursorUp:
            default:
                cursor = kThemeArrowCursor;
                break;
        }
        
        SetThemeCursor( cursor );
    }
}

void Window::SetTitle(const char *s) {
    WindowRef window = GetControlOwner(reinterpret_cast<HIViewRef>( id ));
    CFStringRef title = CFStringCreateWithCString(kCFAllocatorDefault, s, kCFStringEncodingMacRoman);
    SetWindowTitleWithCFString(window, title);
    CFRelease(title);
}

ListBox::ListBox() {}

ListBox::~ListBox() {}

static const OSType scintillaListBoxType = 'sclb';

enum { 
    kItemsPerContainer = 1,
    kIconColumn = 'icon', 
    kTextColumn = 'text'
};
static SInt32 kScrollBarWidth = 0;

class LineData {
    int *types;
        CFStringRef *strings;
    int len;
    int maximum;
public:
    LineData() :types(0), strings(0), len(0), maximum(0) {}
    ~LineData() {
        Clear();
    }
    void Clear() {
        delete []types;
        types = 0;
        for (int i=0; i<maximum; i++) {
          if (strings[i]) CFRelease(strings[i]);
        }
        delete []strings;
        strings = 0;
        len = 0;
        maximum = 0;
    }
    void Add(int index, int type, CFStringRef str ) {
        if (index >= maximum) {
            if (index >= len) {
                int lenNew = (index+1) * 2;
                int *typesNew = new int[lenNew];
                CFStringRef *stringsNew = new CFStringRef[lenNew];
                for (int i=0; i<maximum; i++) {
                    typesNew[i] = types[i];
                    stringsNew[i] = strings[i];
                }
                delete []types;
                delete []strings;
                types = typesNew;
                strings = stringsNew;
                len = lenNew;
            }
            while (maximum < index) {
                types[maximum] = 0;
                strings[maximum] = 0;
                maximum++;
            }
        }
        types[index] = type;
        strings[index] = str;
        if (index == maximum) {
            maximum++;
        }
    }
    int GetType(int index) {
        if (index < maximum) {
            return types[index];
        } else {
            return 0;
        }
    }
    CFStringRef GetString(int index) {
        if (index < maximum) {
            return strings[index];
        } else {
            return 0;
        }
    }
};

class ListBoxImpl : public ListBox {
private:
    ControlRef lb;
    XPMSet xset;
    int lineHeight;
    bool unicodeMode;
    int desiredVisibleRows;
    unsigned int maxItemCharacters;
    unsigned int aveCharWidth;
    Font font;

    void InstallDataBrowserCustomCallbacks();
    void ConfigureDataBrowser();

    static pascal OSStatus  WindowEventHandler(EventHandlerCallRef  inCallRef,
                                               EventRef inEvent,
                                               void *inUserData );
    EventHandlerRef eventHandler;

protected:
    WindowRef windowRef;

public:
    LineData ld;
    CallBackAction doubleClickAction;
    void *doubleClickActionData;

    ListBoxImpl() : lb(NULL), lineHeight(10), unicodeMode(false),
            desiredVisibleRows(5), maxItemCharacters(0), aveCharWidth(8),
            doubleClickAction(NULL), doubleClickActionData(NULL)
    {
      if (kScrollBarWidth == 0)
        GetThemeMetric(kThemeMetricScrollBarWidth, &kScrollBarWidth);
    }

    virtual ~ListBoxImpl() {}
    virtual void SetFont(Font &font);
    virtual void Create(Window &parent, int ctrlID, Scintilla::Point pt, int lineHeight_, bool unicodeMode_);
    virtual void SetAverageCharWidth(int width);
    virtual void SetVisibleRows(int rows);
    virtual int GetVisibleRows() const;
    virtual PRectangle GetDesiredRect();
    virtual int CaretFromEdge();
    virtual void Clear();
    virtual void Append(char *s, int type = -1);
    virtual int Length();
    virtual void Select(int n);
    virtual int GetSelection();
    virtual int Find(const char *prefix);
    virtual void GetValue(int n, char *value, int len);
    virtual void Sort();
    virtual void RegisterImage(int type, const char *xpm_data);
    virtual void ClearRegisteredImages();
    void SetDoubleClickAction(CallBackAction action, void *data) {
        doubleClickAction = action;
        doubleClickActionData = data;
    }

    int IconWidth();
    void ShowHideScrollbar();
#ifdef DB_TABLE_ROW_HEIGHT
    void SetRowHeight(DataBrowserItemID itemID);
#endif

    void DrawRow(DataBrowserItemID item,
                 DataBrowserPropertyID property,
                 DataBrowserItemState itemState,
                 const Rect *theRect);

    virtual void SetList(const char* list, char separator, char typesep);
};

ListBox *ListBox::Allocate() {
    ListBoxImpl *lb = new ListBoxImpl();
    return lb;
}

void ListBoxImpl::Create(Window &/*parent*/, int /*ctrlID*/, Scintilla::Point /*pt*/,
                         int lineHeight_, bool unicodeMode_) {
    lineHeight = lineHeight_;
    unicodeMode = unicodeMode_;
    
    WindowClass windowClass = kHelpWindowClass;
    WindowAttributes attributes = kWindowNoAttributes;
    Rect contentBounds;
    WindowRef outWindow;

    contentBounds.top = contentBounds.left = 0;
    contentBounds.right = 100;
    contentBounds.bottom = lineHeight * desiredVisibleRows;

    CreateNewWindow(windowClass, attributes, &contentBounds, &outWindow);

    InstallStandardEventHandler(GetWindowEventTarget(outWindow));

    ControlRef root;
    CreateRootControl(outWindow, &root);

    CreateDataBrowserControl(outWindow, &contentBounds, kDataBrowserListView, &lb);

#ifdef DB_TABLE_ROW_HEIGHT
    // XXX does not seem to have any effect
    SetDataBrowserTableViewRowHeight(lb, lineHeight);
#endif

    // get rid of the frame, forces databrowser to the full size
    // of the window
    Boolean frameAndFocus = false;
    SetControlData(lb, kControlNoPart, kControlDataBrowserIncludesFrameAndFocusTag,
           sizeof(frameAndFocus), &frameAndFocus);

    ListBoxImpl* lbThis = this;
    SetControlProperty( lb, scintillaListBoxType, 0, sizeof( this ), &lbThis );

    ConfigureDataBrowser();
    InstallDataBrowserCustomCallbacks();

    // install event handlers
    static const EventTypeSpec kWindowEvents[] =
        {
          { kEventClassMouse, kEventMouseDown },
          { kEventClassMouse, kEventMouseMoved },
        };

    eventHandler = NULL;
    InstallWindowEventHandler( outWindow, WindowEventHandler,
                   GetEventTypeCount( kWindowEvents ), 
                   kWindowEvents, this, &eventHandler );

    id = lb;
    SetControlVisibility(lb, true, true);
    SetControl(lb);
    SetWindow(outWindow);
}

pascal OSStatus ListBoxImpl::WindowEventHandler(
    EventHandlerCallRef inCallRef,
    EventRef            inEvent,
    void*               inUserData )
{

    switch (GetEventClass(inEvent)) {
    case kEventClassMouse:
      switch (GetEventKind(inEvent))
        {
        case kEventMouseMoved:
          {
            SetThemeCursor( kThemeArrowCursor );
            break;
          }
        case kEventMouseDown:
          {
            // we cannot handle the double click from the databrowser notify callback as 
            // calling doubleClickAction causes the listbox to be destroyed.  It is
            // safe to do it from this event handler since the destroy event will be queued
            // until we're done here.
            TCarbonEvent        event( inEvent );
            EventMouseButton inMouseButton;
            event.GetParameter<EventMouseButton>( kEventParamMouseButton, typeMouseButton, &inMouseButton );
    
            UInt32 inClickCount;
            event.GetParameter( kEventParamClickCount, &inClickCount );
            if (inMouseButton == kEventMouseButtonPrimary && inClickCount == 2) {
              // handle our single mouse click now
              ListBoxImpl* listbox = reinterpret_cast<ListBoxImpl*>( inUserData );
              const WindowRef window = GetControlOwner(listbox->lb);
              const HIViewRef rootView = HIViewGetRoot( window );
              HIViewRef targetView = NULL;
              HIViewGetViewForMouseEvent( rootView, inEvent, &targetView );
              if ( targetView == listbox->lb )
                {
                  if (listbox->doubleClickAction != NULL) {
                listbox->doubleClickAction(listbox->doubleClickActionData);
                  }
                }
            }
          }
      }
    }
    return eventNotHandledErr;
}

#ifdef DB_TABLE_ROW_HEIGHT
void ListBoxImpl::SetRowHeight(DataBrowserItemID itemID)
{
    // XXX does not seem to have any effect
    SetDataBrowserTableViewItemRowHeight(lb, itemID, lineHeight);
}
#endif

void ListBoxImpl::DrawRow(DataBrowserItemID item,
         DataBrowserPropertyID property,
         DataBrowserItemState itemState,
         const Rect *theRect)
{
    Rect row = *theRect;
    row.left = 0;

    ColourPair fore;

    if (itemState == kDataBrowserItemIsSelected) {
      long        systemVersion;
      Gestalt( gestaltSystemVersion, &systemVersion );
      //  Panther DB starts using kThemeBrushSecondaryHighlightColor for inactive browser hilighting
      if ( (systemVersion >= 0x00001030) && (IsControlActive( lb ) == false) )
          SetThemePen( kThemeBrushSecondaryHighlightColor, 32, true );
      else
        //SetThemePen( kThemeBrushPrimaryHighlightColor, 32, true );
        SetThemePen( kThemeBrushAlternatePrimaryHighlightColor, 32, true );

      PaintRect(&row);
      fore = ColourDesired(0xff,0xff,0xff);
    }

    int widthPix = xset.GetWidth() + 2;
    int pixId = ld.GetType(item - 1);
    XPM *pxpm = xset.Get(pixId);

    char s[255];
    GetValue(item - 1, s, 255);

    Surface *surfaceItem = Surface::Allocate();
    if (surfaceItem) {
        CGContextRef    cgContext;
        GrafPtr        port;
        Rect bounds;

        GetControlBounds(lb, &bounds);
        GetPort( &port );
        QDBeginCGContext( port, &cgContext );

        CGContextSaveGState( cgContext );
        CGContextTranslateCTM(cgContext, 0, bounds.bottom - bounds.top);
        CGContextScaleCTM(cgContext, 1.0, -1.0);

        surfaceItem->Init(cgContext, NULL);

        int left = row.left;
        if (pxpm) {
            PRectangle rc(left + 1, row.top,
                    left + 1 + widthPix, row.bottom);
            pxpm->Draw(surfaceItem, rc);
        }

        // draw the text
        PRectangle trc(left + 2 + widthPix, row.top, row.right, row.bottom);
        int ascent = surfaceItem->Ascent(font) - surfaceItem->InternalLeading(font);
        int ytext = trc.top + ascent + 1;
        trc.bottom = ytext + surfaceItem->Descent(font) + 1;
        surfaceItem->DrawTextTransparent( trc, font, ytext, s, strlen(s), fore.allocated );

        CGContextRestoreGState( cgContext );
        QDEndCGContext( port, &cgContext );
        delete surfaceItem;
    }
}


pascal void ListBoxDrawItemCallback(ControlRef browser, DataBrowserItemID item,
                    DataBrowserPropertyID property,
                    DataBrowserItemState itemState,
                    const Rect *theRect, SInt16 gdDepth,
                    Boolean colorDevice)
{
    if (property != kIconColumn) return;
    ListBoxImpl* lbThis = NULL;
    OSStatus err;
    err = GetControlProperty( browser, scintillaListBoxType, 0, sizeof( lbThis ), NULL, &lbThis );
    // adjust our rect
    lbThis->DrawRow(item, property, itemState, theRect);

}

void ListBoxImpl::ConfigureDataBrowser()
{
    DataBrowserViewStyle viewStyle;
    DataBrowserSelectionFlags selectionFlags;
    ::GetDataBrowserViewStyle(lb, &viewStyle);
    
    ::SetDataBrowserHasScrollBars(lb, false, true);
    ::SetDataBrowserListViewHeaderBtnHeight(lb, 0);
    ::GetDataBrowserSelectionFlags(lb, &selectionFlags);
    ::SetDataBrowserSelectionFlags(lb, selectionFlags |= kDataBrowserSelectOnlyOne);
    // if you change the hilite style, also change the style in ListBoxDrawItemCallback
    ::SetDataBrowserTableViewHiliteStyle(lb, kDataBrowserTableViewFillHilite);
  
    Rect insetRect;
    ::GetDataBrowserScrollBarInset(lb, &insetRect);
    
    insetRect.right = kScrollBarWidth - 1;
    ::SetDataBrowserScrollBarInset(lb, &insetRect);
  
    switch (viewStyle)
    {
      case kDataBrowserListView:
      {  
        DataBrowserListViewColumnDesc iconCol;
        iconCol.headerBtnDesc.version = kDataBrowserListViewLatestHeaderDesc;
        iconCol.headerBtnDesc.minimumWidth = 0;
        iconCol.headerBtnDesc.maximumWidth = 600;
        iconCol.headerBtnDesc.titleOffset = 0;
        iconCol.headerBtnDesc.titleString = NULL;
        iconCol.headerBtnDesc.initialOrder = kDataBrowserOrderIncreasing;
          
        iconCol.headerBtnDesc.btnFontStyle.flags = kControlUseJustMask;
        iconCol.headerBtnDesc.btnFontStyle.just = teFlushLeft;
        
        iconCol.headerBtnDesc.btnContentInfo.contentType = kControlContentTextOnly;
        
        iconCol.propertyDesc.propertyID = kIconColumn;
        iconCol.propertyDesc.propertyType = kDataBrowserCustomType;
        iconCol.propertyDesc.propertyFlags = kDataBrowserListViewSelectionColumn;
        
        ::AddDataBrowserListViewColumn(lb, &iconCol, kDataBrowserListViewAppendColumn);
      }  break;
      
    }
}

void ListBoxImpl::InstallDataBrowserCustomCallbacks()
{
    DataBrowserCustomCallbacks callbacks;

    callbacks.version = kDataBrowserLatestCustomCallbacks;
    verify_noerr(InitDataBrowserCustomCallbacks(&callbacks));
    callbacks.u.v1.drawItemCallback = NewDataBrowserDrawItemUPP(ListBoxDrawItemCallback);
    callbacks.u.v1.hitTestCallback = NULL;//NewDataBrowserHitTestUPP(ListBoxHitTestCallback);
    callbacks.u.v1.trackingCallback = NULL;//NewDataBrowserTrackingUPP(ListBoxTrackingCallback); 
    callbacks.u.v1.editTextCallback = NULL;
    callbacks.u.v1.dragRegionCallback = NULL;
    callbacks.u.v1.acceptDragCallback = NULL;
    callbacks.u.v1.receiveDragCallback = NULL;

    SetDataBrowserCustomCallbacks(lb, &callbacks);
}

void ListBoxImpl::SetFont(Font &font_) {
    // Having to do this conversion is LAME
    QuartzTextStyle *ts = reinterpret_cast<QuartzTextStyle*>( font_.GetID() );
    ControlFontStyleRec style;
    ATSUAttributeValuePtr value;
    ATSUFontID        fontID;
    style.flags = kControlUseFontMask | kControlUseSizeMask | kControlAddToMetaFontMask;
    ts->getAttribute( kATSUFontTag, sizeof(fontID), &fontID, NULL );
    ATSUFontIDtoFOND(fontID, &style.font, NULL);
    ts->getAttribute( kATSUSizeTag, sizeof(Fixed), &value, NULL );
    style.size = ((SInt16)FixRound((SInt32)value));
    SetControlFontStyle(lb, &style);

#ifdef DB_TABLE_ROW_HEIGHT
    //  XXX this doesn't *stick*
    ATSUTextMeasurement ascent = ts->getAttribute<ATSUTextMeasurement>( kATSUAscentTag );
    ATSUTextMeasurement descent = ts->getAttribute<ATSUTextMeasurement>( kATSUDescentTag );
    lineHeight = Fix2Long( ascent ) + Fix2Long( descent );
    SetDataBrowserTableViewRowHeight(lb, lineHeight + lineLeading);
#endif

    // !@&^#%$ we cant copy Font, but we need one for our custom drawing
    Str255 fontName;
    FMGetFontFamilyName(style.font, fontName);
    font.Create((const char *)fontName, 0, style.size, false, false);
}

void ListBoxImpl::SetAverageCharWidth(int width) {
        aveCharWidth = width;
}

void ListBoxImpl::SetVisibleRows(int rows) {
    desiredVisibleRows = rows;
}

int ListBoxImpl::GetVisibleRows() const {
    // XXX Windows & GTK do this, but it seems incorrect to me.  Other logic
    //     to do with visible rows is essentially the same across platforms.
    return desiredVisibleRows;
    /*
    // This would be more correct
    int rows = Length();
    if ((rows == 0) || (rows > desiredVisibleRows))
        rows = desiredVisibleRows;
    return rows;
    */
}

PRectangle ListBoxImpl::GetDesiredRect() {
    PRectangle rcDesired = GetPosition();

    // XXX because setting the line height on the table doesnt 
    //     *stick*, we'll have to suffer and just use whatever
    //     the table desides is the correct height.
    UInt16 itemHeight;// = lineHeight; 
    GetDataBrowserTableViewRowHeight(lb, &itemHeight);

    int rows = Length();
    if ((rows == 0) || (rows > desiredVisibleRows))
        rows = desiredVisibleRows;

    rcDesired.bottom = itemHeight * rows;
    int width = maxItemCharacters;
    if (width < 12)
        width = 12;
    rcDesired.right = rcDesired.left + width * (aveCharWidth+aveCharWidth/3);
    if (Length() > rows) 
        rcDesired.right += kScrollBarWidth;
    rcDesired.right += IconWidth();
    return rcDesired;
}

void ListBoxImpl::ShowHideScrollbar() {
    int rows = Length();
    if (rows > desiredVisibleRows) {
        ::SetDataBrowserHasScrollBars(lb, false, true);
    } else {
        ::SetDataBrowserHasScrollBars(lb, false, false);
    }
}

int ListBoxImpl::IconWidth() {
    return xset.GetWidth() + 2;
}

int ListBoxImpl::CaretFromEdge() {
    return 0;
}

void ListBoxImpl::Clear() {
    // passing NULL to "items" arg 4 clears the list
    maxItemCharacters = 0;
    ld.Clear();
    AddDataBrowserItems (lb, kDataBrowserNoItem, 0, NULL, kDataBrowserItemNoProperty);
}

void ListBoxImpl::Append(char *s, int type) {
    int count = Length();
    CFStringRef r = CFStringCreateWithCString(NULL, s, kTextEncodingMacRoman);
    ld.Add(count, type, r);
  
    DataBrowserItemID items[1];
    items[0] = count + 1;
    AddDataBrowserItems (lb, kDataBrowserNoItem, 1, items, kDataBrowserItemNoProperty);
    ShowHideScrollbar();
}

void ListBoxImpl::SetList(const char* list, char separator, char typesep) {
    // XXX copied from PlatGTK, should be in base class
    Clear();
    int count = strlen(list) + 1;
    char *words = new char[count];
    if (words) {
        memcpy(words, list, count);
        char *startword = words;
        char *numword = NULL;
        int i = 0;
        for (; words[i]; i++) {
            if (words[i] == separator) {
                words[i] = '\0';
                if (numword)
                    *numword = '\0';
                Append(startword, numword?atoi(numword + 1):-1);
                startword = words + i + 1;
                numword = NULL;
            } else if (words[i] == typesep) {
                numword = words + i;
            }
        }
        if (startword) {
            if (numword)
                *numword = '\0';
            Append(startword, numword?atoi(numword + 1):-1);
        }
        delete []words;
    }
}

int ListBoxImpl::Length() {
    UInt32 numItems = 0;
    GetDataBrowserItemCount(lb, kDataBrowserNoItem, false, kDataBrowserItemAnyState, &numItems);
    return (int)numItems;
}

void ListBoxImpl::Select(int n) {
    DataBrowserItemID items[1];
    items[0] = n + 1;
    SetDataBrowserSelectedItems(lb, 1, items, kDataBrowserItemsAssign);
    RevealDataBrowserItem(lb, items[0], kIconColumn, kDataBrowserRevealOnly);
    // force update on selection
    Draw1Control(lb);
}

int ListBoxImpl::GetSelection() {
    Handle selectedItems = NewHandle(0);
    GetDataBrowserItems(lb, kDataBrowserNoItem, true, kDataBrowserItemIsSelected, selectedItems);
    UInt32 numSelectedItems = GetHandleSize(selectedItems)/sizeof(DataBrowserItemID);
    HLock( selectedItems );
    DataBrowserItemID *individualItem = (DataBrowserItemID*)( *selectedItems );
    DataBrowserItemID selected[numSelectedItems];
    selected[0] = *individualItem;
    HUnlock( selectedItems );
    return selected[0] - 1;
}

int ListBoxImpl::Find(const char *prefix) {
    int count = Length();
    char s[255];
    for (int i = 0; i < count; i++) {
        GetValue(i, s, 255);
        if (s[0] != NULL && (0 == strncmp(prefix, s, strlen(prefix)))) {
            return i;
        }
    }
    return - 1;
}

void ListBoxImpl::GetValue(int n, char *value, int len) {
    CFStringRef textString = ld.GetString(n);
    if (textString == NULL) {
        value[0] = '\0';
        return;
    }
    CFIndex numUniChars = CFStringGetLength( textString );

    // XXX how do we know the encoding of the listbox?
    CFStringEncoding encoding = kCFStringEncodingUTF8; //( IsUnicodeMode() ? kCFStringEncodingUTF8 : kCFStringEncodingASCII);
    CFIndex maximumByteLength = CFStringGetMaximumSizeForEncoding( numUniChars, encoding ) + 1;
    char* text = new char[maximumByteLength];
    CFIndex usedBufferLength = 0;
    CFStringGetBytes( textString, CFRangeMake( 0, numUniChars ), encoding,
              '?', false, reinterpret_cast<UInt8*>( text ),
              maximumByteLength, &usedBufferLength );
    text[usedBufferLength] = '\0'; // null terminate the ASCII/UTF8 string

    if (text && len > 0) {
        strncpy(value, text, len);
        value[len - 1] = '\0';
    } else {
        value[0] = '\0';
    }
    delete text;
}

void ListBoxImpl::Sort() {
    // TODO: Implement this
    fprintf(stderr, "ListBox::Sort\n");
}

void ListBoxImpl::RegisterImage(int type, const char *xpm_data) {
    xset.Add(type, xpm_data);
}

void ListBoxImpl::ClearRegisteredImages() {
    xset.Clear();
}

Menu::Menu() : id(0) { }

void Menu::CreatePopUp() {
    // TODO: Could I just feed a constant menu ID parameter, or does
    // it really need to be unique?
    static int nextMenuID = 1;
    Destroy();
    OSStatus err;
    err = CreateNewMenu( nextMenuID++, 0, reinterpret_cast<MenuRef*>( &id ) );
    assert( noErr == err && id != NULL );
}

void Menu::Destroy() {
    if ( id != NULL )
    {
        ReleaseMenu( reinterpret_cast<MenuRef>( id ) );
        id = NULL;
    }
}

void Menu::Show(Point pt, Window &) {
    UInt32 userSelection = 0;
    SInt16 menuId = 0;
    MenuItemIndex menuItem = 0;
    ::Point globalPoint;
    globalPoint.h = pt.x;
    globalPoint.v = pt.y;
    OSStatus err;
    err = ContextualMenuSelect( reinterpret_cast<MenuRef>( id ), globalPoint,
                                false, kCMHelpItemRemoveHelp, NULL,
                                NULL, &userSelection,
                                &menuId,
                                &menuItem
                                );
    // The system should always handle the command for us, so we should get userCanceledErr
    assert( /*noErr == err ||*/ userCanceledErr == err );
}

// TODO: Consider if I should be using GetCurrentEventTime instead of gettimeoday
ElapsedTime::ElapsedTime() {
    struct timeval curTime;
    int retVal;
    retVal = gettimeofday( &curTime, NULL );
    assert( retVal == 0 );
    
    bigBit = curTime.tv_sec;
    littleBit = curTime.tv_usec;
}

double ElapsedTime::Duration(bool reset) {
    struct timeval curTime;
    int retVal;
    retVal = gettimeofday( &curTime, NULL );
    assert( retVal == 0 );
    long endBigBit = curTime.tv_sec;
    long endLittleBit = curTime.tv_usec;
    double result = 1000000.0 * (endBigBit - bigBit);
    result += endLittleBit - littleBit;
    result /= 1000000.0;
    if (reset) {
        bigBit = endBigBit;
        littleBit = endLittleBit;
    }
    return result;
}

ColourDesired Platform::Chrome() {
    RGBColor c;
    GetThemeBrushAsColor(kThemeBrushButtonActiveDarkShadow , 24, true, &c);
    return ColourDesired(c.red>>8, c.green>>8, c.blue>>8);
}

ColourDesired Platform::ChromeHighlight() {
    RGBColor c;
    GetThemeBrushAsColor(kThemeBrushButtonInactiveLightShadow , 24, true, &c);
    return ColourDesired(c.red>>8, c.green>>8, c.blue>>8);
}

static Str255 PlatformDefaultFontName;
const char *Platform::DefaultFont() {
    long id = HighShortFromLong(GetScriptVariable(smCurrentScript, smScriptAppFondSize));
    FMGetFontFamilyName(id, PlatformDefaultFontName);
    return (const char *)PlatformDefaultFontName;
}

int Platform::DefaultFontSize() {
    return LowShortFromLong(GetScriptVariable(smCurrentScript, smScriptAppFondSize));
}

unsigned int Platform::DoubleClickTime() {
    // Convert from ticks to milliseconds. I think it would be better to use the events to tell us
    // when we have a double and triple click, but what do I know?
    return static_cast<unsigned int>( TicksToEventTime( GetDblTime() ) / kEventDurationMillisecond );
}

bool Platform::MouseButtonBounce() {
    return false;
}

bool Platform::WaitMouseMoved(Scintilla::Point pt) { 
    ::Point mpt;
    mpt.v = pt.x;
    mpt.h = pt.y;
    return ::WaitMouseMoved(mpt);
}

bool Platform::IsKeyDown(int keyCode) {
    return false;
    // TODO: Map Scintilla/Windows key codes to Mac OS X key codes
    // TODO: Do I need this?
    /*
    // Inspired by code at: http://www.sover.net/~jams/Morgan/docs/GameInputMethods.txt

    // Get the keys
    KeyMap keys;
    GetKeys( keys );
    
    // Calculate the key map index
    long keyMapIndex = keys[keyCode/8];
    // Calculate the individual bit to check
    short bitToCheck = keyCode % 8;
    // Check the status of the key
    return ( keyMapIndex >> bitToCheck ) & 0x01;
     */
}

long Platform::SendScintilla(WindowID w, unsigned int msg, unsigned long wParam, long lParam) {
    return scintilla_send_message( w, msg, wParam, lParam );
}

bool Platform::IsDBCSLeadByte(int /*codePage*/, char /*ch*/) {
    // TODO: Implement this
    return false;
}

int Platform::DBCSCharLength(int /*codePage*/, const char* /*s*/) {
    // TODO: Implement this
    return 1;
}

int Platform::DBCSCharMaxLength() {
    // TODO: Implement this
    //return CFStringGetMaximumSizeForEncoding( 1, CFStringEncoding encoding );
    return 2;
}

// These are utility functions not really tied to a platform
int Platform::Minimum(int a, int b) {
    if (a < b)
        return a;
    else
        return b;
}

int Platform::Maximum(int a, int b) {
    if (a > b)
        return a;
    else
        return b;
}

//#define TRACE
#ifdef TRACE

void Platform::DebugDisplay(const char *s) {
    fprintf( stderr, s );
}

void Platform::DebugPrintf(const char *format, ...) {
    const int BUF_SIZE = 2000;
    char buffer[BUF_SIZE];

    va_list pArguments;
    va_start(pArguments, format);
    vsnprintf(buffer, BUF_SIZE, format, pArguments);
    va_end(pArguments);
    Platform::DebugDisplay(buffer);
}

#else

void Platform::DebugDisplay(const char *) {}

void Platform::DebugPrintf(const char *, ...) {}

#endif

// Not supported for GTK+
static bool assertionPopUps = true;

bool Platform::ShowAssertionPopUps(bool assertionPopUps_) {
    bool ret = assertionPopUps;
    assertionPopUps = assertionPopUps_;
    return ret;
}

void Platform::Assert(const char *c, const char *file, int line) {
    char buffer[2000];
    sprintf(buffer, "Assertion [%s] failed at %s %d", c, file, line);
    strcat(buffer, "\r\n");
    Platform::DebugDisplay(buffer);
    abort();
}

int Platform::Clamp(int val, int minVal, int maxVal) {
    assert( minVal <= maxVal );
    if (val > maxVal)
        val = maxVal;
    if (val < minVal)
        val = minVal;
    return val;
}
