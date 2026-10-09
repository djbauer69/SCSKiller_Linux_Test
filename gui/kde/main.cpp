#include "BackendController.h"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>

int main(int argc, char* argv[])
{
    QGuiApplication app(argc, argv);
    app.setApplicationName("SCSKiller");
    app.setOrganizationName("SCSKiller");

    BackendController backend;
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.loadFromModule("SCSKiller.Kde", "Main");
    if (engine.rootObjects().isEmpty())
        return 1;

    return app.exec();
}