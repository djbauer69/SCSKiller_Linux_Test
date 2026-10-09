#include "BackendController.h"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlError>
#include <QDebug>
#include <cstdio>

int main(int argc, char* argv[])
{
    std::fprintf(stderr, "SCSKiller KDE UI: entering main\\n");
    std::fflush(stderr);

    QGuiApplication app(argc, argv);
    std::fprintf(stderr, "SCSKiller KDE UI: QGuiApplication created\\n");
    std::fflush(stderr);
    app.setApplicationName("SCSKiller");
    app.setOrganizationName("SCSKiller");

    BackendController backend;
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
    std::fprintf(stderr, "SCSKiller KDE UI: QML root object count = %lld\\n",
                 static_cast<long long>(engine.rootObjects().size()));
    std::fflush(stderr);
    if (engine.rootObjects().isEmpty())
    {
        qCritical().noquote()
            << "SCSKiller KDE UI failed to create its QML root object. "
               "Check Qt Quick and Kirigami runtime modules and plugin search paths.";
        return 1;
    }

    const int exitCode = app.exec();
    std::fprintf(stderr, "SCSKiller KDE UI: event loop exited with code %d\\n", exitCode);
    std::fflush(stderr);
    return exitCode;
}