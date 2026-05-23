#include <QApplication>
#include <QCommandLineParser>

#include "MainWindow.h"

int main(int argc, char *argv[]) {
  QApplication app(argc, argv);

  QCoreApplication::setOrganizationName("gesslar");
  QCoreApplication::setApplicationName("mdv");
  QCoreApplication::setApplicationVersion("0.1.0");

  QCommandLineParser parser;
  parser.setApplicationDescription("An offline Markdown viewer.");
  parser.addHelpOption();
  parser.addVersionOption();
  parser.addPositionalArgument("file", "Markdown file to open.", "[file]");
  parser.process(app);

  MainWindow window;
  window.show();

  const QStringList args = parser.positionalArguments();
  for (const QString &path : args) window.openFile(path);

  return app.exec();
}
