#include "AppSettings.h"

namespace {
constexpr auto KEY_DARK_THEME = "ui/darkTheme";
constexpr auto KEY_MINIMISE_TO_TRAY = "ui/minimiseToTray";
}

AppSettings::AppSettings(QObject* parent)
    : QObject(parent)
{
    // Defaults match the shipped look: dark theme, closing the window keeps Aural in the tray.
    _darkTheme = _settings.value(KEY_DARK_THEME, true).toBool();
    _minimiseToTray = _settings.value(KEY_MINIMISE_TO_TRAY, true).toBool();
}

void AppSettings::setDarkTheme(bool dark)
{
    if (_darkTheme == dark)
        return;
    _darkTheme = dark;
    _settings.setValue(KEY_DARK_THEME, dark);
    // Flush now: preferences should survive even if the app is killed rather than closed.
    _settings.sync();
    emit darkThemeChanged();
}

void AppSettings::setMinimiseToTray(bool enabled)
{
    if (_minimiseToTray == enabled)
        return;
    _minimiseToTray = enabled;
    _settings.setValue(KEY_MINIMISE_TO_TRAY, enabled);
    _settings.sync();
    emit minimiseToTrayChanged();
}
