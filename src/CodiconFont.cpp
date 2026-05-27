#include "CodiconFont.h"

#include <QFontDatabase>
#include <QStringList>

QString Codicon::family() {
  // QFontDatabase::addApplicationFont returns the same id on repeated calls
  // for the same path, but the lambda still only runs once thanks to the
  // function-local static.
  static const QString family = []() {
    const int id = QFontDatabase::addApplicationFont(
        QStringLiteral(":/fonts/codicon.ttf"));
    if(id < 0) return QString();
    const QStringList families = QFontDatabase::applicationFontFamilies(id);
    return families.isEmpty() ? QString() : families.first();
  }();
  return family;
}
