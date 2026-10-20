#include "AppIcon.h"

#include <QColor>
#include <QFont>
#include <QPainter>
#include <QPixmap>

namespace AppIcon {

QIcon headset(bool connected)
{
    // Brand container colour when a device is connected, a muted slate when it is not, so
    // the tray icon reads as a state at a glance rather than as decoration.
    const QColor background = connected ? QColor("#a078ff") : QColor("#4a5268");
    const QColor glyph = connected ? QColor("#340080") : QColor("#c8cede");

    QPixmap pm(256, 256);
    pm.fill(Qt::transparent);

    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(background);
    p.setPen(Qt::NoPen);
    p.drawRoundedRect(pm.rect(), 56, 56);
    p.setPen(glyph);

    QFont iconFont(QStringLiteral("Material Icons Outlined"));
    iconFont.setPixelSize(150);
    p.setFont(iconFont);
    p.drawText(pm.rect(), Qt::AlignCenter, QStringLiteral("headset"));
    p.end();

    return QIcon(pm);
}

} // namespace AppIcon
