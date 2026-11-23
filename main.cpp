#include "iss_tracker_window.h"

#include <QApplication>
#include <QDir>
#include <QLockFile>
#include <QStandardPaths>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("ISS Tracker"));
    QCoreApplication::setApplicationName(QStringLiteral("ISS Terminal"));

    const QString runtimeDirectory =
        QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    const QString lockDirectory =
        runtimeDirectory.isEmpty() ? QDir::tempPath() : runtimeDirectory;
    QLockFile instanceLock(lockDirectory + QStringLiteral("/iss-terminal.lock"));
    instanceLock.setStaleLockTime(5000);
    if (!instanceLock.tryLock(100)) return 0;

    IssTrackerWindow window;
    window.show();
    return app.exec();
}
