#include <QGuiApplication>
#include <QQmlApplicationEngine>

int main(int argc, char* argv[])
{
    QGuiApplication app(argc, argv);
    app.setApplicationName("SCSKiller");
    app.setOrganizationName("SCSKiller");

    QQmlApplicationEngine engine;
    engine.loadFromModule("SCSKiller.Kde", "Main");
    if (engine.rootObjects().isEmpty())
        return 1;

    return app.exec();
}