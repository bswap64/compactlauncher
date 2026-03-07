#include "launcher.h"
#include "main_frame.h"
#include <QApplication>
#include <QCoreApplication>
#include <QIcon>
#include <QString>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

int main(int argc, char* argv[]) {
#ifdef _WIN32
    SetEnvironmentVariableA("_JAVA_OPTIONS", nullptr);
#else
    unsetenv("_JAVA_OPTIONS");
#endif

    QApplication app(argc, argv);
    QCoreApplication::setApplicationName("Compact Launcher");
    QCoreApplication::setOrganizationName("Compact Launcher");

    app.setWindowIcon(QIcon(":/icon.png"));

    std::string logPath = QCoreApplication::applicationDirPath().toStdString() + "/compactlauncher.log";
    Logger::instance().open(logPath);
    LOG("OnInit started");

    MainFrame w;
    w.show();

    return app.exec();
}
