// ScintillaDocument.h
// Wrapper for Scintilla document object so it can be manipulated independently.
// Copyright (c) 2011 Archaeopteryx Software, Inc. d/b/a Wingware

#ifndef SCINTILLADOCUMENT_H
#define SCINTILLADOCUMENT_H

#include <QObject>
#include "Sci_Position.h"

class WatcherHelper;

#ifdef SCI_NAMESPACE
namespace Scintilla {
#endif

#ifndef EXPORT_IMPORT_API
#ifdef WIN32
#ifdef MAKING_LIBRARY
#define EXPORT_IMPORT_API __declspec(dllexport)
#else
// Defining dllimport upsets moc
#define EXPORT_IMPORT_API __declspec(dllimport)
//#define EXPORT_IMPORT_API
#endif
#else
#define EXPORT_IMPORT_API
#endif
#endif

class EXPORT_IMPORT_API ScintillaDocument : public QObject
{
    Q_OBJECT

    void *pdoc;
    WatcherHelper *docWatcher;

public:
    explicit ScintillaDocument(QObject *parent = 0, void *pdoc_=0);
    virtual ~ScintillaDocument();
    void *pointer();

    Sci_Position line_from_position(Sci_Position pos);
    bool is_cr_lf(Sci_Position pos);
    bool delete_chars(Sci_Position pos, Sci_Position len);
    Sci_Position undo();
    Sci_Position redo();
    bool can_undo();
    bool can_redo();
    void delete_undo_history();
    bool set_undo_collection(bool collect_undo);
    bool is_collecting_undo();
    void begin_undo_action();
    void end_undo_action();
    void set_save_point();
    bool is_save_point();
    void set_read_only(bool read_only);
    bool is_read_only();
    void insert_string(Sci_Position position, QByteArray &str);
    QByteArray get_char_range(Sci_Position position, Sci_Position length);
    char style_at(Sci_Position position);
    Sci_Position line_start(Sci_Position lineno);
    Sci_Position line_end(Sci_Position lineno);
    Sci_Position line_end_position(Sci_Position pos);
    Sci_Position length();
    Sci_Position lines_total();
    void start_styling(Sci_Position position, char flags);
    bool set_style_for(Sci_Position length, char style);
    int get_end_styled();
    void ensure_styled_to(Sci_Position position);
    void set_current_indicator(int indic);
    void decoration_fill_range(Sci_Position position, int value, Sci_Position fillLength);
    int decorations_value_at(int indic, Sci_Position position);
    Sci_Position decorations_start(int indic, Sci_Position position);
    Sci_Position decorations_end(int indic, Sci_Position position);
    int get_code_page();
    void set_code_page(int code_page);
    int get_eol_mode();
    void set_eol_mode(int eol_mode);
    Sci_Position move_position_outside_char(Sci_Position pos, int move_dir, bool check_line_end);
    
    int get_character(Sci_Position pos); // Calls GetCharacterAndWidth(pos, NULL)

private:
    void emit_modify_attempt();
    void emit_save_point(bool atSavePoint);
    void emit_modified(Sci_Position position, int modification_type, const QByteArray& text, Sci_Position length,
	Sci_Position linesAdded, Sci_Position line, int foldLevelNow, int foldLevelPrev);
    void emit_style_needed(Sci_Position pos);
    void emit_lexer_changed();
    void emit_error_occurred(int status);

signals:
    void modify_attempt();
    void save_point(bool atSavePoint);
    void modified(Sci_Position position, int modification_type, const QByteArray& text, Sci_Position length,
	Sci_Position linesAdded, Sci_Position line, int foldLevelNow, int foldLevelPrev);
    void style_needed(Sci_Position pos);
    void lexer_changed();
    void error_occurred(int status);

    friend class ::WatcherHelper;

};

#ifdef SCI_NAMESPACE
}
#endif

#endif // SCINTILLADOCUMENT_H
