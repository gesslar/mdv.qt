#pragma once

#include <QString>

// VS Code Codicons (https://github.com/microsoft/vscode-codicons). The
// codicon.ttf file is embedded as a Qt resource and loaded lazily on the
// first call to family(); pass the returned family to QFont before assigning
// the font to a widget whose text is a codepoint from the table below.
//
// Codepoints come straight from upstream codicon.css. Add new entries as we
// need them; the source of truth is
// node_modules/@vscode/codicons/dist/codicon.css.
namespace Codicon {
QString family();

constexpr char16_t Close = 0xEA76;
constexpr char16_t Pinned = 0xEBA0;
constexpr char16_t Settings = 0xEB52;
constexpr char16_t Refresh = 0xEB37;
constexpr char16_t NewFile = 0xEA7F;
constexpr char16_t Trash = 0xEA81;
constexpr char16_t FolderOpened = 0xEAF7;
constexpr char16_t ListFlat = 0xEB84;
constexpr char16_t ChevronLeft = 0xEAB5;
constexpr char16_t ChevronRight = 0xEAB6;
constexpr char16_t ArrowDown = 0xEA9A;
constexpr char16_t ArrowUp = 0xEAA1;
constexpr char16_t CaseSensitive = 0xEAB1;
constexpr char16_t WholeWord = 0xEB7E;
constexpr char16_t Regex = 0xEB38;
constexpr char16_t ChromeClose = 0xEAB8;
constexpr char16_t ChromeMaximize = 0xEAB9;
constexpr char16_t ChromeMinimize = 0xEABA;
constexpr char16_t ChromeRestore = 0xEABB;
}  // namespace Codicon
