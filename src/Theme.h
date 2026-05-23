#pragma once

#include <QColor>
#include <QTextCharFormat>

namespace mdv {

// Blackboard syntax-highlight theme, distilled from the VS Code tokenColors
// in /projects/git/blackboard-themepack/dist/blackboard.color-theme.json.
//
// The original theme uses ~60 TextMate scope rules; this collapses them into
// the ~20 categories a QSyntaxHighlighter actually consumes. The palette is
// kept addressable so consumers can build extra formats if they need to.
class Theme {
public:
  enum Token {
    // Default editor text.
    Text,

    // Generic code.
    Comment,
    Keyword,        // keyword, storage, storage.type, storage.modifier
    ControlFlow,    // keyword.control (if/for/return/...)
    Operator,       // keyword.operator
    Number,         // constant.numeric
    String,         // string
    StringEscape,   // constant.character.escape
    Character,      // constant.character
    Regex,          // string.regexp / constant.regexp
    Constant,       // constant.language, variable.other.constant
    Variable,       // variable, parameter
    Function,       // entity.name.function, support.function
    Type,           // support.type, entity.name.type
    Class,          // entity.name.class, entity.name.namespace
    Attribute,      // entity.other.attribute-name
    Tag,            // entity.name.tag (HTML/XML)
    Preprocessor,   // meta.preprocessor
    Label,          // entity.name.label
    Invalid,        // invalid

    // Markdown / prose.
    MarkupHeading,
    MarkupBold,
    MarkupItalic,
    MarkupCode,     // inline `code`
    MarkupQuote,    // > blockquote marker
    MarkupList,     // - * 1. markers
    MarkupInserted, // diff +
    MarkupDeleted,  // diff -
    MarkupChanged,  // diff !
  };

  static QTextCharFormat formatFor(Token t);

  // Editor surface colors (from theme.colors, not tokenColors).
  static QColor editorBackground();        // #0f0f0f
  static QColor editorForeground();        // #bebebe
  static QColor editorSelection();         // #90bde540
  static QColor editorLineHighlight();     // #181818bf
  static QColor editorLineNumber();        // #6691b980
  static QColor editorLineNumberActive();  // #6691b9e6

  // Named palette (so the highlighter — or anyone — can mix custom formats).
  static QColor commentGray();    // #636363
  static QColor tagGray();        // #808080
  static QColor neutralGray();    // #adadad
  static QColor labelGray();      // #c8c8c8

  static QColor headerBlue();     // #4a6fbf
  static QColor listBlue();       // #6080b5
  static QColor keywordBlue();    // #739abd
  static QColor constantBlue();   // #509fbe
  static QColor variableBlue();   // #6ca1bc

  static QColor typeTeal();       // #59ab99
  static QColor numberGreen();    // #8aa168
  static QColor functionOlive();  // #a7a776

  static QColor selectorTan();    // #cfbd98
  static QColor stringPeach();    // #be917f
  static QColor regexRust();      // #a16362
  static QColor controlPink();    // #b890b4
  static QColor regexpViolet();   // #646695

  static QColor invalidRed();     // #f44747
};

}  // namespace mdv
