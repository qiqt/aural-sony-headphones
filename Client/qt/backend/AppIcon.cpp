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
        const QColor accent("#cdb8ff");
        const qreal stroke = qMax(1.0, size / 16.0);
        painter.setBrush(accent);
        painter.setPen(Qt::NoPen);
        painter.drawRoundedRect(QRectF(size - 2 * stroke, size * 0.38,
                                       2 * stroke, size * 0.24),
                                stroke * 0.5, stroke * 0.5);
        const QRectF body(stroke * 0.5, size * 0.16,
                          size - 2.5 * stroke, size * 0.68);
        painter.setPen(QPen(accent, stroke));
        painter.setBrush(QColor("#221735"));
        painter.drawRoundedRect(body, stroke, stroke);

        const QRectF textArea = body.adjusted(stroke * 0.5, 0, -stroke * 0.5, 0);

        QFont font(QStringLiteral("Segoe UI"));
        font.setBold(true);
        font.setPixelSize(qRound(size * 0.58));
        while (font.pixelSize() > 1 &&
               QFontMetricsF(font).horizontalAdvance(label) > textArea.width())
            font.setPixelSize(font.pixelSize() - 1);
        painter.setFont(font);
        painter.setPen(accent);
        painter.drawText(textArea, Qt::AlignCenter, label);
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
