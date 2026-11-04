#pragma once

#include <QActionGroup>
#include <QMenu>
#include <QObject>
#include <QSystemTrayIcon>

class QAction;
class QWindow;

class AppSettings;
class HeadphonesController;

// Tray icon, quick controls and window actions. Settings and their status come from
// HeadphonesController, just as they do in the main window.
class TrayController : public QObject
{
    Q_OBJECT

public:
    TrayController(HeadphonesController* controller, AppSettings* settings, QObject* parent = nullptr);

    // Called once the QML window exists. Also installs the close-to-tray filter.
    void attachWindow(QWindow* window);

    bool available() const { return _available; }

private slots:
    void refresh();
    // Closing the window only keeps Aural alive when there is a tray to keep it in *and* the
    // user asked for that. Re-applied whenever the preference changes, so turning "Keep
    // running in the system tray" off takes effect on the next close instead of leaving the
    // app running invisibly.
    void applyQuitPolicy();
    void onActivated(QSystemTrayIcon::ActivationReason reason);
    void onError(const QString& message);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void buildMenu();
    void showWindow();
    void hideWindow();
    QString statusLine() const;

    HeadphonesController* _controller;
    AppSettings* _settings;
    QWindow* _window = nullptr;
    bool _available = false;

    QSystemTrayIcon _tray;
    QMenu _menu;

    QAction* _deviceAction = nullptr;
    QAction* _batteryAction = nullptr;
    QAction* _statusAction = nullptr;
    QMenu* _ambientMenu = nullptr;
    QActionGroup* _ambientGroup = nullptr;
    QAction* _ambientActions[3] = { nullptr, nullptr, nullptr };
    QAction* _s2cAction = nullptr;
    QAction* _dseeAction = nullptr;
    QAction* _showAction = nullptr;
    QAction* _connectionAction = nullptr;
    QAction* _quitAction = nullptr;

    bool _updating = false;   // guards against user-signal loops while we refresh check states
    bool _lastConnected = false;
};
