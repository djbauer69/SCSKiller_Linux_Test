#include "BackendController.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

BackendController::BackendController(QObject* parent)
    : QObject(parent),
      m_launcherPath(findLauncher())
{
    connect(&m_process, &QProcess::readyReadStandardOutput, this, [this]() {
        appendOutput(QString::fromLocal8Bit(m_process.readAllStandardOutput()));
    });
    connect(&m_process, &QProcess::readyReadStandardError, this, [this]() {
        const QString text = QString::fromLocal8Bit(m_process.readAllStandardError());
        if (!text.isEmpty())
            appendOutput(QStringLiteral("[stderr]\n") + text);
    });
    connect(&m_process,
            qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this,
            [this](int code, QProcess::ExitStatus status) {
                flushProcessOutput();
                const int result = status == QProcess::NormalExit ? code : 128;
                appendOutput(QStringLiteral("\nCommand finished with exit code %1.\n").arg(result));
                setExitCode(result);
                setBusy(false);
            });
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            appendOutput(QStringLiteral("Could not start SCSKiller CLI at '%1': %2\n")
                             .arg(m_launcherPath, m_process.errorString()));
            setExitCode(127);
            setBusy(false);
        } else {
            appendOutput(QStringLiteral("Process error: %1\n").arg(m_process.errorString()));
        }
    });
}

QString BackendController::findLauncher() const
{
    const QString configured = qEnvironmentVariable("SCSKILLER_CLI");
    if (!configured.isEmpty() && QFileInfo(configured).isExecutable())
        return QFileInfo(configured).absoluteFilePath();

    const QString home = qEnvironmentVariable("SCSKILLER_HOME");
    if (!home.isEmpty()) {
        const QString candidate = QDir(home).filePath(QStringLiteral("bin/scskiller-linux"));
        if (QFileInfo(candidate).isExecutable())
            return QFileInfo(candidate).absoluteFilePath();
    }

    const QString fromPath = QStandardPaths::findExecutable(QStringLiteral("scskiller-linux"));
    if (!fromPath.isEmpty())
        return fromPath;

    const QString sibling = QDir(QCoreApplication::applicationDirPath())
                                .filePath(QStringLiteral("scskiller-linux"));
    if (QFileInfo(sibling).isExecutable())
        return QFileInfo(sibling).absoluteFilePath();

    return {};
}

void BackendController::appendOutput(const QString& text)
{
    if (text.isEmpty())
        return;
    m_output += text;
    emit outputChanged();
}

void BackendController::setBusy(bool busy)
{
    if (m_busy == busy)
        return;
    m_busy = busy;
    emit busyChanged();
}

void BackendController::setExitCode(int exitCode)
{
    if (m_exitCode == exitCode)
        return;
    m_exitCode = exitCode;
    emit exitCodeChanged();
}

void BackendController::flushProcessOutput()
{
    const QString standardOutput = QString::fromLocal8Bit(m_process.readAllStandardOutput());
    const QString standardError = QString::fromLocal8Bit(m_process.readAllStandardError());
    appendOutput(standardOutput);
    if (!standardError.isEmpty())
        appendOutput(QStringLiteral("[stderr]\n") + standardError);
}

bool BackendController::runCommand(const QString& command, const QVariantList& arguments)
{
    if (m_busy)
        return false;

    static const QStringList allowedCommands{
        QStringLiteral("inspect-vulkan"),
        QStringLiteral("record-vulkan"),
        QStringLiteral("record-proton"),
        QStringLiteral("warm-vulkan"),
        QStringLiteral("run-vulkan"),
        QStringLiteral("run-proton-vulkan")
    };

    if (!allowedCommands.contains(command)) {
        appendOutput(QStringLiteral("Command is not available from the experimental GUI: %1\n")
                         .arg(command));
        setExitCode(2);
        return false;
    }

    if (m_launcherPath.isEmpty()) {
        appendOutput(
            QStringLiteral("SCSKiller CLI was not found. Set SCSKILLER_CLI to the installed "
                           "scskiller-linux launcher or put it on PATH.\n"));
        setExitCode(127);
        return false;
    }

    QStringList processArguments{command};
    processArguments.reserve(arguments.size() + 1);
    for (const QVariant& argument : arguments)
        processArguments.push_back(argument.toString());

    m_process.setProgram(m_launcherPath);
    m_process.setArguments(processArguments);
    setExitCode(-1);
    appendOutput(QStringLiteral("\n> %1 %2\n")
                     .arg(m_launcherPath, processArguments.join(QLatin1Char(' '))));
    setBusy(true);
    m_process.start();
    return true;
}

void BackendController::clearOutput()
{
    if (m_busy)
        return;
    m_output.clear();
    emit outputChanged();
}
