#include "BackendController.h"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlError>
#include <QDebug>
#include <cstdio>

int main(int argc, char* argv[])
{
    const bool debugStartup = qEnvironmentVariable("SCSKILLER_KDE_DEBUG") == QStringLiteral("1");
    const auto debugLine = [debugStartup](const QString& value) {
        if (!debugStartup)
            return;
        const QByteArray encoded = value.toUtf8();
        std::fprintf(stderr, "%s\n", encoded.constData());
        std::fflush(stderr);
    };

    debugLine(QStringLiteral("SCSKiller KDE UI: entering main"));

    QGuiApplication app(argc, argv);
    debugLine(QStringLiteral("SCSKiller KDE UI: QGuiApplication created"));
    app.setApplicationName("SCSKiller");
    app.setOrganizationName("SCSKiller");

    BackendController backend;
    debugLine(QStringLiteral("SCSKiller CLI launcher: %1").arg(
        backend.launcherPath().isEmpty() ? QStringLiteral("<not found>") : backend.launcherPath()));
    debugLine(QStringLiteral("Default Vulkan layer directory: %1").arg(
        backend.defaultLayerDirectory().isEmpty() ? QStringLiteral("<not found>") : backend.defaultLayerDirectory()));

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("backend", &backend);
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::warnings,
        &app,
        [](const QList<QQmlError>& warnings) {
            for (const auto& warning : warnings)
                qWarning().noquote() << warning.toString();
        });
    engine.loadFromModule("SCSKiller.Kde", "Main");
    debugLine(QStringLiteral("SCSKiller KDE UI: QML root object count = %1")
                  .arg(engine.rootObjects().size()));
    if (engine.rootObjects().isEmpty())
    {
        qCritical().noquote()
            << "SCSKiller KDE UI failed to create its QML root object. "
               "Check Qt Quick and Kirigami runtime modules and plugin search paths.";
        return 1;
    }

    const int exitCode = app.exec();
    debugLine(QStringLiteral("SCSKiller KDE UI: event loop exited with code %1").arg(exitCode));
    return exitCode;
}