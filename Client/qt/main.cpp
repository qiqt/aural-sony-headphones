#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QFont>
#include <QFontDatabase>
#include <QDebug>
#include <QWindow>

#include "backend/AppIcon.h"
#include "backend/AppSettings.h"
#include "backend/BuildInfo.h"
#include "backend/HeadphonesController.h"
#include "backend/PlatformWindow.h"
#include "backend/TrayController.h"

int main(int argc, char* argv[])
{
    // QApplication (not QGuiApplication): QSystemTrayIcon and its menu are QtWidgets types.
    QApplication app(argc, argv);
    QApplication::setApplicationName("Aural");
    QApplication::setOrganizationName("Aural");

    // Use the Basic style so our custom QML styling applies uniformly across platforms
    // (the native Windows/macOS styles forbid most visual customization).
    QQuickStyle::setStyle("Basic");

    // Bundle the design fonts (Manrope for body/UI, Space Grotesk for display headings) so the
    // UI renders identically on any machine and can't pick up an oblique system substitute.
    QFontDatabase::addApplicationFont(":/fonts/SpaceGrotesk.ttf");
    int bodyId = QFontDatabase::addApplicationFont(":/fonts/Manrope.ttf");
    const QStringList bodyFams = QFontDatabase::applicationFontFamilies(bodyId);
    const QString bodyFamily = bodyFams.isEmpty() ? QStringLiteral("Manrope") : bodyFams.first();
    qDebug() << "[Aural] build:" << BuildInfo::summary();
    qDebug() << "[Aural] body font:" << bodyFamily;
    QFont appFont(bodyFamily);
    appFont.setItalic(false);
    appFont.setStyleStrategy(QFont::PreferAntialias);
    QApplication::setFont(appFont);

    // Icon font (Material Icons Outlined) - used via ligature names by Icon.qml, and by the
    // window/tray artwork.
    QFontDatabase::addApplicationFont(":/fonts/MaterialIconsOutlined.otf");
    QApplication::setWindowIcon(AppIcon::headset(true));

    HeadphonesController controller;
    AppSettings settings;
    BuildInfo buildInfo;
    PlatformWindow platform;
    TrayController tray(&controller, &settings);

    // Whether closing the window quits is TrayController's call: it depends on both the tray
    // being there and the user's minimise-to-tray preference, and it has to follow that
    // preference when it changes (see TrayController::applyQuitPolicy).

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("Hp", &controller);
    engine.rootContext()->setContextProperty("App", &settings);
    engine.rootContext()->setContextProperty("Platform", &platform);
    engine.rootContext()->setContextProperty("Build", &buildInfo);

    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        []() { QCoreApplication::exit(-1); }, Qt::QueuedConnection);

    engine.loadFromModule("Aural", "Main");
    if (engine.rootObjects().isEmpty())
        return -1;

    if (auto* window = qobject_cast<QWindow*>(engine.rootObjects().constFirst()))
        tray.attachWindow(window);

    return app.exec();
}
