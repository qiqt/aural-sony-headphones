#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QFont>
#include <QFontDatabase>
#include <QDebug>
#include <QIcon>
#include <QPainter>
#include <QPixmap>

#include "backend/HeadphonesController.h"

int main(int argc, char* argv[])
{
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName("Aural");
    QGuiApplication::setOrganizationName("Aural");

    // Use the Basic style so our custom QML styling applies uniformly across platforms
    // (the native Windows/macOS styles forbid most visual customization).
    QQuickStyle::setStyle("Basic");

    // Bundle the design fonts (Manrope for body/UI, Space Grotesk for display headings) so the
    // UI renders identically on any machine and can't pick up an oblique system substitute.
    QFontDatabase::addApplicationFont(":/fonts/SpaceGrotesk.ttf");
    int bodyId = QFontDatabase::addApplicationFont(":/fonts/Manrope.ttf");
    const QStringList bodyFams = QFontDatabase::applicationFontFamilies(bodyId);
    const QString bodyFamily = bodyFams.isEmpty() ? QStringLiteral("Manrope") : bodyFams.first();
    qDebug() << "[Aural] body font:" << bodyFamily;
    QFont appFont(bodyFamily);
    appFont.setItalic(false);
    appFont.setStyleStrategy(QFont::PreferAntialias);
    QGuiApplication::setFont(appFont);

    // Icon font (Material Icons Outlined) — used via ligature names by Icon.qml.
    QFontDatabase::addApplicationFont(":/fonts/MaterialIconsOutlined.otf");

    // Window/taskbar icon, painted from the icon font (no .ico asset pipeline yet):
    // headset glyph on the brand container color.
    {
        QPixmap pm(256, 256);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.setBrush(QColor("#a078ff"));
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(pm.rect(), 56, 56);
        p.setPen(QColor("#340080"));
        QFont iconFont(QStringLiteral("Material Icons Outlined"));
        iconFont.setPixelSize(150);
        p.setFont(iconFont);
        p.drawText(pm.rect(), Qt::AlignCenter, QStringLiteral("headset"));
        p.end();
        QGuiApplication::setWindowIcon(QIcon(pm));
    }

    HeadphonesController controller;

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("Hp", &controller);

    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        []() { QCoreApplication::exit(-1); }, Qt::QueuedConnection);

    engine.loadFromModule("Aural", "Main");
    if (engine.rootObjects().isEmpty())
        return -1;

    return app.exec();
}
