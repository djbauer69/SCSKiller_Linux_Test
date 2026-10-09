#pragma once

#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QVariantList>

class BackendController final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString output READ output NOTIFY outputChanged)
    Q_PROPERTY(int exitCode READ exitCode NOTIFY exitCodeChanged)
    Q_PROPERTY(QString launcherPath READ launcherPath CONSTANT)

public:
    explicit BackendController(QObject* parent = nullptr);

    bool busy() const { return m_busy; }
    QString output() const { return m_output; }
    int exitCode() const { return m_exitCode; }
    QString launcherPath() const { return m_launcherPath; }

    Q_INVOKABLE bool runCommand(const QString& command, const QVariantList& arguments);
    Q_INVOKABLE void clearOutput();

signals:
    void busyChanged();
    void outputChanged();
    void exitCodeChanged();

private:
    QString findLauncher() const;
    void appendOutput(const QString& text);
    void setBusy(bool busy);
    void setExitCode(int exitCode);
    void flushProcessOutput();

    QProcess m_process;
    bool m_busy = false;
    QString m_output;
    int m_exitCode = -1;
    QString m_launcherPath;
};
