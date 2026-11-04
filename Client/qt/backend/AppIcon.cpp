#include "AppIcon.h"

#include <QColor>
#include <QFont>
#include <QFontMetricsF>
#include <QPainter>
#include <QPixmap>

namespace AppIcon {

QIcon battery(int percentage)
{
    if (percentage < 0 || percentage > 100)
        return headset(true);

    QIcon icon;
    const QString label = QString::number(percentage);
    for (const int size : {16, 20, 24, 32, 40, 48, 64, 256})
    {
        QPixmap pm(size, size);
        pm.fill(Qt::transparent);
        QPainter painter(&pm);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::TextAntialiasing);
        painter.setBrush(QColor("#a078ff"));
        painter.setPen(Qt::NoPen);
        painter.drawRoundedRect(QRectF(0, 0, size, size), size * 0.22, size * 0.22);

        QFont font(QStringLiteral("Segoe UI"));
        font.setBold(true);
        font.setPixelSize(qRound(size * 0.75));
        while (font.pixelSize() > 1 &&
               QFontMetricsF(font).horizontalAdvance(label) > size * 0.9)
            font.setPixelSize(font.pixelSize() - 1);
        painter.setFont(font);
        painter.setPen(QColor("#340080"));
        painter.drawText(pm.rect(), Qt::AlignCenter, label);
        painter.end();
        icon.addPixmap(pm);
    }
    return icon;
}

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
