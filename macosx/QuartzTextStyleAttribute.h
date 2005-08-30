/*
 *  QuartzTextStyleAttribute.h
 *  wtf
 *
 *  Created by Evan Jones on Wed Oct 02 2002.
 *  Copyright (c) 2002 __MyCompanyName__. All rights reserved.
 *
 */


#include <Carbon/Carbon.h>

#ifndef _QUARTZ_TEXT_STYLE_ATTRIBUTE_H
#define _QUARTZ_TEXT_STYLE_ATTRIBUTE_H

class QuartzTextStyleAttribute
{
public:
    virtual ByteCount getSize() const = 0;
    virtual ATSUAttributeValuePtr getValuePtr() = 0;
    virtual ATSUAttributeTag getTag() const = 0;
};

class QuartzTextSize : public QuartzTextStyleAttribute
{
public: 
    QuartzTextSize( float points )
    {
        size = X2Fix( points );
    }

    virtual ByteCount getSize() const
    {
        return sizeof( size );
    }

    virtual ATSUAttributeValuePtr getValuePtr()
    {
        return &size;
    }

    virtual ATSUAttributeTag getTag() const
    {
        return kATSUSizeTag;
    }
    
private:
        Fixed size;
};

class QuartzTextStyleAttributeBoolean : public QuartzTextStyleAttribute
{
public:
    QuartzTextStyleAttributeBoolean( bool newVal ) : value( newVal ) {}

    virtual ByteCount getSize() const
    {
        return sizeof( value );
    }
    virtual ATSUAttributeValuePtr getValuePtr()
    {
        return &value;
    }
    
    virtual ATSUAttributeTag getTag() const = 0;
    
private:
        Boolean value;
};

class QuartzTextBold : public QuartzTextStyleAttributeBoolean
{
public:
    QuartzTextBold( bool newVal ) : QuartzTextStyleAttributeBoolean( newVal ) {}
    virtual ATSUAttributeTag getTag() const
    {
        return kATSUQDBoldfaceTag;
    }
};

class QuartzTextItalic : public QuartzTextStyleAttributeBoolean
{
public:
    QuartzTextItalic( bool newVal ) : QuartzTextStyleAttributeBoolean( newVal ) {}
    virtual ATSUAttributeTag getTag() const
    {
        return kATSUQDItalicTag;
    }
};

class QuartzTextUnderline : public QuartzTextStyleAttributeBoolean
{
public:
    QuartzTextUnderline( bool newVal ) : QuartzTextStyleAttributeBoolean( newVal ) {}
    virtual ATSUAttributeTag getTag() const {
        return kATSUQDUnderlineTag;
    }
};

class QuartzFont : public QuartzTextStyleAttribute
{
public:
    /** Create a font style from a name. */
    QuartzFont( const char* name, int length )
    {
        assert( name != NULL && length > 0 && name[length] == '\0' );

        /*CFStringRef fontName = CFStringCreateWithCString( NULL, name, kCFStringEncodingMacRoman );
        
        ATSFontRef fontRef = ATSFontFindFromName( fontName, kATSOptionFlagsDefault );
        assert( fontRef != NULL );
        fontid = fontRef;

        CFRelease( fontName );*/

        OSStatus err;
        err = ATSUFindFontFromName( const_cast<char*>( name ), length, kFontFullName, (unsigned) kFontNoPlatform, kFontRomanScript, (unsigned) kFontNoLanguage, &fontid );
        assert( err == noErr && fontid != kATSUInvalidFontID );
    }

    virtual ByteCount getSize() const
    {
        return sizeof( fontid );
    }

    virtual ATSUAttributeValuePtr getValuePtr()
    {
        return &fontid;
    }

    virtual ATSUAttributeTag getTag() const
    {
        return kATSUFontTag;
    }

    ATSUFontID getFontID() const
    {
        return fontid;
    }

private:
    ATSUFontID fontid;
};


#endif

