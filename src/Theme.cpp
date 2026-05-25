#include "Theme.h"

namespace mdv {

// --- Palette -----------------------------------------------------------

QColor Theme::commentGray() { return QColor("#636363"); }
QColor Theme::tagGray() { return QColor("#808080"); }
QColor Theme::neutralGray() { return QColor("#adadad"); }
QColor Theme::labelGray() { return QColor("#c8c8c8"); }

QColor Theme::headerBlue() { return QColor("#4a6fbf"); }
QColor Theme::listBlue() { return QColor("#6080b5"); }
QColor Theme::keywordBlue() { return QColor("#739abd"); }
QColor Theme::constantBlue() { return QColor("#509fbe"); }
QColor Theme::variableBlue() { return QColor("#6ca1bc"); }

QColor Theme::typeTeal() { return QColor("#59ab99"); }
QColor Theme::numberGreen() { return QColor("#8aa168"); }
QColor Theme::functionOlive() { return QColor("#a7a776"); }

QColor Theme::selectorTan() { return QColor("#cfbd98"); }
QColor Theme::stringPeach() { return QColor("#be917f"); }
QColor Theme::regexRust() { return QColor("#a16362"); }
QColor Theme::controlPink() { return QColor("#b890b4"); }
QColor Theme::regexpViolet() { return QColor("#646695"); }

QColor Theme::invalidRed() { return QColor("#f44747"); }

// --- Editor surface (theme.colors) -------------------------------------

QColor Theme::editorBackground() { return QColor("#0f0f0f"); }
QColor Theme::editorForeground() { return QColor("#bebebe"); }

// VS Code uses #RRGGBBAA. QColor::fromString understands this in Qt 6.
QColor Theme::editorSelection() { return QColor::fromString("#90bde540"); }
QColor Theme::editorLineHighlight() { return QColor::fromString("#181818bf"); }
QColor Theme::editorLineNumber() { return QColor::fromString("#6691b980"); }
QColor Theme::editorLineNumberActive() {
  return QColor::fromString("#6691b9e6");
}

// --- Token → QTextCharFormat -------------------------------------------

QTextCharFormat Theme::formatFor(Token t) {
  QTextCharFormat f;

  switch(t) {
  case Text:
    f.setForeground(editorForeground());
    break;

  case Comment:
    f.setForeground(commentGray());
    break;
  case Keyword:
    f.setForeground(keywordBlue());
    break;
  case ControlFlow:
    f.setForeground(controlPink());
    break;
  case Operator:
    f.setForeground(neutralGray());
    break;
  case Number:
    f.setForeground(numberGreen());
    break;
  case String:
    f.setForeground(stringPeach());
    break;
  case StringEscape:
    f.setForeground(selectorTan());
    break;
  case Character:
    f.setForeground(keywordBlue());
    break;
  case Regex:
    f.setForeground(regexRust());
    break;
  case Constant:
    f.setForeground(constantBlue());
    break;
  case Variable:
    f.setForeground(variableBlue());
    break;
  case Function:
    f.setForeground(functionOlive());
    break;
  case Type:
    f.setForeground(typeTeal());
    break;
  case Class:
    f.setForeground(typeTeal());
    break;
  case Attribute:
    f.setForeground(variableBlue());
    break;
  case Tag:
    f.setForeground(keywordBlue());
    break;
  case Preprocessor:
    f.setForeground(keywordBlue());
    break;
  case Label:
    f.setForeground(labelGray());
    break;
  case Invalid:
    f.setForeground(invalidRed());
    f.setFontUnderline(true);
    f.setUnderlineStyle(QTextCharFormat::WaveUnderline);
    break;

  case MarkupHeading:
    f.setForeground(keywordBlue());
    f.setFontWeight(QFont::Bold);
    break;
  case MarkupBold:
    f.setForeground(keywordBlue());
    f.setFontWeight(QFont::Bold);
    break;
  case MarkupItalic:
    f.setForeground(controlPink());
    f.setFontItalic(true);
    break;
  case MarkupCode:
    f.setForeground(stringPeach());
    break;
  case MarkupQuote:
    f.setForeground(commentGray());
    break;
  case MarkupList:
    f.setForeground(listBlue());
    break;
  case MarkupInserted:
    f.setForeground(numberGreen());
    break;
  case MarkupDeleted:
    f.setForeground(stringPeach());
    break;
  case MarkupChanged:
    f.setForeground(keywordBlue());
    break;
  }

  return f;
}

}  // namespace mdv
