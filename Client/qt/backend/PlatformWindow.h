#pragma once

#include <QObject>

// Native window appearance, kept away from device control.
//
// Matching the Windows title bar to the app theme has nothing to do with headphones, so it
// does not belong on HeadphonesController. QML talks to this through the `Platform` context
// property; every method is a no-op on platforms that don't have the feature.
class PlatformWindow : public QObject
{
    Q_OBJECT

public:
    explicit PlatformWindow(QObject* parent = nullptr) : QObject(parent) {}

    // Dark/light native window chrome (title bar) for the given QQuickWindow.
    Q_INVOKABLE void applyTheme(QObject* window, bool dark);
};
