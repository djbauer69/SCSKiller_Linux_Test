#include "BackendController.h"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlError>
#include <QDebug>

int main(int argc, char* argv[])
{
    QGuiApplication app(argc, argv);
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
    if (engine.rootObjects().isEmpty())
    {
        qCritical().noquote()
            << "SCSKiller KDE UI failed to create its QML root object. "
               "Check Qt Quick and Kirigami runtime modules and plugin search paths.";
        return 1;
    }

    return app.exec();
}