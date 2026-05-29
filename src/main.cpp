#include <QApplication>
#include <QCommandLineParser>
#include <QIcon>

#include "MainWindow.h"
#include "WindowsChrome.h"

int main(int argc, char *argv[]) {
  // QWindowKit's WidgetWindowAgent requires this be set before QApplication
  // is constructed — without it, native sibling HWNDs created for child
  // widgets confuse the frameless hit-testing.
  QCoreApplication::setAttribute(Qt::AA_DontCreateNativeWidgetSiblings);

  QApplication app(argc, argv);

  // Identity comes from CMake (MDV_* compile defs, see CMakeLists.txt), so the
  // version lives in exactly one place: project(VERSION) there.
  QCoreApplication::setOrganizationName(MDV_ORG);
  QCoreApplication::setApplicationName(MDV_APP);
  QCoreApplication::setApplicationVersion(MDV_VERSION);

  // Window icon for X11 title bars, Windows, and macOS. On Wayland this does
  // NOT drive the dock/taskbar icon — that comes from a .desktop file whose
  // basename matches the app_id set below (see
  // resources/dev.gesslar.mdv.desktop).
  app.setWindowIcon(QIcon(QStringLiteral(":/icons/mdv.png")));
  QGuiApplication::setDesktopFileName(QStringLiteral("dev.gesslar.mdv"));

  // Re-broadcast system color-scheme / accent changes into our frameless
  // window, whose QWindowKit chrome otherwise swallows the palette-change
  // propagation (no-op off Windows).
  installChromeThemeTracker();

  QCommandLineParser parser;
  parser.setApplicationDescription("An offline Markdown viewer.");
  parser.addHelpOption();
  parser.addVersionOption();
  parser.addPositionalArgument("file", "Markdown file to open.", "[file]");
  parser.process(app);

  MainWindow window;
  window.show();

  const QStringList args = parser.positionalArguments();
  for(const QString &path : args) window.openFile(path);

  return app.exec();
}
