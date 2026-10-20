#pragma once

#include <QObject>
#include <QSettings>

// Application preferences that outlive a run. Device settings live on the headphones and are
// never stored here - this is only for how Aural itself looks and behaves.
//
// Backed by QSettings, so on Windows it lands under
// HKCU\Software\Aural\Aural (organisation and application names are set in main()).
class AppSettings : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool darkTheme READ darkTheme WRITE setDarkTheme NOTIFY darkThemeChanged)
    Q_PROPERTY(bool minimiseToTray READ minimiseToTray WRITE setMinimiseToTray NOTIFY minimiseToTrayChanged)

public:
    explicit AppSettings(QObject* parent = nullptr);

    bool darkTheme() const { return _darkTheme; }
    void setDarkTheme(bool dark);

    bool minimiseToTray() const { return _minimiseToTray; }
    void setMinimiseToTray(bool enabled);

signals:
    void darkThemeChanged();
    void minimiseToTrayChanged();

private:
    QSettings _settings;
    bool _darkTheme = true;
    bool _minimiseToTray = true;
};
