#include <QApplication>
#include <QCommandLineParser>
#include <QIcon>

#include "MainWindow.h"

int main(int argc, char *argv[]) {
  QApplication app(argc, argv);

  QCoreApplication::setOrganizationName("gesslar");
  QCoreApplication::setApplicationName("mdv");
  QCoreApplication::setApplicationVersion("2.0.0");

  // Window icon for X11 title bars, Windows, and macOS. On Wayland this does
  // NOT drive the dock/taskbar icon — that comes from a .desktop file whose
  // basename matches the app_id set below (see resources/mdv.desktop).
  app.setWindowIcon(QIcon(QStringLiteral(":/icons/mdv.png")));
  QGuiApplication::setDesktopFileName(QStringLiteral("mdv"));

  QCommandLineParser parser;
  parser.setApplicationDescription("An offline Markdown viewer.");
  parser.addHelpOption();
  parser.addVersionOption();
  parser.addPositionalArgument("file", "Markdown file to open.", "[file]");
  parser.process(app);

  MainWindow window;
  window.show();

  const QStringList args = parser.positionalArguments();
  for (const QString &path : args)
    window.openFile(path);

  return app.exec();
}
