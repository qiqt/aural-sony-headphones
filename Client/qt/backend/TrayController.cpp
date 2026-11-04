#include "TrayController.h"

#include "AppIcon.h"
#include "AppSettings.h"
#include "HeadphonesController.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QEvent>
#include <QWindow>

namespace {

// How a setting's status reads in a menu. The tray never claims a value is on the device
// just because the user picked it.
QString statusSuffix(const QString& status)
{
    if (status == QLatin1String("requested"))    return QObject::tr(" - sending");
    if (status == QLatin1String("acknowledged")) return QObject::tr(" - sent, not confirmed");
    if (status == QLatin1String("failed"))       return QObject::tr(" - failed");
    if (status == QLatin1String("unknown"))      return QObject::tr(" - not read from device");
    return QString(); // confirmed
}

} // namespace

TrayController::TrayController(HeadphonesController* controller, AppSettings* settings, QObject* parent)
    : QObject(parent)
    , _controller(controller)
    , _settings(settings)
{
    _available = QSystemTrayIcon::isSystemTrayAvailable();

    // Owned here rather than in main(): quitting on close is the other half of the
    // close-to-tray behaviour below, and the two must never disagree.
    applyQuitPolicy();
    if (_settings)
        connect(_settings, &AppSettings::minimiseToTrayChanged, this, &TrayController::applyQuitPolicy);

    if (!_available)
        return;

    buildMenu();
    // The window can be shown or hidden without any controller signal firing, so re-read the
    // world every time the menu opens rather than trusting whatever the last refresh left behind.
    connect(&_menu, &QMenu::aboutToShow, this, &TrayController::refresh);
    _tray.setContextMenu(&_menu);
    _tray.setIcon(AppIcon::headset(false));
    _tray.setToolTip(QStringLiteral("Aural"));
    connect(&_tray, &QSystemTrayIcon::activated, this, &TrayController::onActivated);
    _tray.show();

    connect(_controller, &HeadphonesController::connectionChanged, this, &TrayController::refresh);
    connect(_controller, &HeadphonesController::batteryChanged, this, &TrayController::refresh);
    connect(_controller, &HeadphonesController::ambientChanged, this, &TrayController::refresh);
    connect(_controller, &HeadphonesController::systemChanged, this, &TrayController::refresh);
    connect(_controller, &HeadphonesController::s2cChanged, this, &TrayController::refresh);
    connect(_controller, &HeadphonesController::capabilitiesChanged, this, &TrayController::refresh);
    connect(_controller, &HeadphonesController::settingStatusChanged, this, &TrayController::refresh);
    connect(_controller, &HeadphonesController::errorOccurred, this, &TrayController::onError);

    refresh();
}

void TrayController::buildMenu()
{
    _deviceAction = _menu.addAction(tr("No device connected"));
    _deviceAction->setEnabled(false);

    _batteryAction = _menu.addAction(tr("Battery unknown"));
    _batteryAction->setEnabled(false);

    _statusAction = _menu.addAction(QString());
    _statusAction->setEnabled(false);
    _statusAction->setVisible(false);

    _menu.addSeparator();

    _ambientMenu = _menu.addMenu(tr("Ambient sound"));
    _ambientGroup = new QActionGroup(this);
    _ambientGroup->setExclusive(true);

    const char* modeNames[3] = { "Noise Cancelling", "Ambient Sound", "Off" };
    for (int i = 0; i < 3; ++i)
    {
        _ambientActions[i] = _ambientMenu->addAction(tr(modeNames[i]));
        _ambientActions[i]->setCheckable(true);
        _ambientGroup->addAction(_ambientActions[i]);
        connect(_ambientActions[i], &QAction::triggered, this, [this, i](bool checked) {
            if (_updating || !checked)
                return;
            _controller->setAmbientMode(i);
        });
    }

    _s2cAction = _menu.addAction(tr("Speak-to-Chat"));
    _s2cAction->setCheckable(true);
    connect(_s2cAction, &QAction::triggered, this, [this](bool checked) {
        if (_updating) return;
        _controller->setS2cEnabled(checked);
    });

    _dseeAction = _menu.addAction(tr("DSEE Extreme"));
    _dseeAction->setCheckable(true);
    connect(_dseeAction, &QAction::triggered, this, [this](bool checked) {
        if (_updating) return;
        _controller->setDsee(checked);
    });

    _menu.addSeparator();

    _showAction = _menu.addAction(tr("Show Aural"));
    connect(_showAction, &QAction::triggered, this, [this]() {
        if (_window && _window->isVisible())
            hideWindow();
        else
            showWindow();
    });

    _connectionAction = _menu.addAction(tr("Disconnect"));
    connect(_connectionAction, &QAction::triggered, this, [this]() {
        if (_controller->connected())
            _controller->disconnectDevice();
        else
            _controller->refreshDevices();
    });

    _menu.addSeparator();

    _quitAction = _menu.addAction(tr("Quit Aural"));
    connect(_quitAction, &QAction::triggered, this, []() { QCoreApplication::quit(); });
}

void TrayController::applyQuitPolicy()
{
    const bool keepRunningInTray = _available && _settings && _settings->minimiseToTray();
    QApplication::setQuitOnLastWindowClosed(!keepRunningInTray);
}

void TrayController::attachWindow(QWindow* window)
{
    _window = window;
    if (_window)
        _window->installEventFilter(this);
    refresh();
}

bool TrayController::eventFilter(QObject* watched, QEvent* event)
{
    // Closing the window keeps Aural running in the tray, unless the user turned that off
    // (or there is no tray to run in).
    if (_available && watched == _window && event->type() == QEvent::Close &&
        _settings && _settings->minimiseToTray())
    {
        event->ignore();
        hideWindow();
        _tray.showMessage(tr("Aural is still running"),
                          tr("Use the tray icon to reopen it or to quit."),
                          AppIcon::headset(_controller->connected()), 4000);
        return true;
    }

    // Keep "Show Aural"/"Hide Aural" honest when the window appears or disappears.
    if (watched == _window && (event->type() == QEvent::Show || event->type() == QEvent::Hide))
        refresh();

    return QObject::eventFilter(watched, event);
}

void TrayController::showWindow()
{
    if (!_window)
        return;
    _window->show();
    _window->raise();
    _window->requestActivate();
}

void TrayController::hideWindow()
{
    if (_window)
        _window->hide();
}

void TrayController::onActivated(QSystemTrayIcon::ActivationReason reason)
{
    if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick)
        showWindow();
}

void TrayController::onError(const QString& message)
{
    if (!_available)
        return;
    // Only interrupt when the window isn't there to show the toast itself.
    if (_window && _window->isVisible())
        return;
    _tray.showMessage(tr("Aural"), message, QSystemTrayIcon::Warning, 5000);
}

QString TrayController::statusLine() const
{
    const QVariantMap status = _controller->settingStatus();
    const QStringList failedKeys = [&] {
        QStringList keys;
        for (auto it = status.begin(); it != status.end(); ++it)
            if (it.value().toString() == QLatin1String("failed"))
                keys << it.key();
        return keys;
    }();

    if (!failedKeys.isEmpty())
        return tr("Last change failed: %1").arg(failedKeys.join(QStringLiteral(", ")));
    if (_controller->syncing())
        return tr("Sending settings...");
    return QString();
}

void TrayController::refresh()
{
    if (!_available)
        return;

    _updating = true;

    const bool connected = _controller->connected();

    _deviceAction->setText(connected ? _controller->deviceModel() : tr("No device connected"));

    const int battery = _controller->batteryLevel();
    if (connected && battery >= 0)
    {
        _batteryAction->setText(_controller->batteryCharging()
            ? tr("Battery %1% (charging)").arg(battery)
            : tr("Battery %1%").arg(battery));
    }
    else
    {
        _batteryAction->setText(tr("Battery unknown"));
    }

    const QString line = statusLine();
    _statusAction->setText(line);
    _statusAction->setVisible(!line.isEmpty());

    // The tray is another view of the same state, so it uses the same availability gates the
    // window does (SON-11). A control the window is withholding must not be reachable here.
    const bool ambient = _controller->ambientAvailable();
    const bool s2c = _controller->supportsSpeakToChat();
    const bool dsee = _controller->dseeAvailable();

    const QVariantMap status = _controller->settingStatus();
    const QString ambientSuffix = connected ? statusSuffix(status.value("ambient").toString()) : QString();
    _ambientMenu->menuAction()->setVisible(ambient);
    _ambientMenu->setEnabled(ambient);
    _ambientMenu->setTitle(tr("Ambient sound") + ambientSuffix);
    const int mode = _controller->ambientMode();
    for (int i = 0; i < 3; ++i)
        _ambientActions[i]->setChecked(ambient && i == mode);

    _s2cAction->setVisible(s2c);
    _s2cAction->setEnabled(s2c);
    _s2cAction->setChecked(_controller->s2cEnabled());
    _s2cAction->setText(tr("Speak-to-Chat") +
        (connected ? statusSuffix(status.value("speakToChat").toString()) : QString()));

    _dseeAction->setVisible(dsee);
    _dseeAction->setEnabled(dsee);
    _dseeAction->setChecked(_controller->dsee());
    _dseeAction->setText(tr("DSEE Extreme") +
        (connected ? statusSuffix(status.value("dsee").toString()) : QString()));

    _showAction->setText(_window && _window->isVisible() ? tr("Hide Aural") : tr("Show Aural"));
    _connectionAction->setText(connected ? tr("Disconnect") : tr("Search for devices"));

    _tray.setIcon(connected && battery >= 0 && battery <= 100
        ? AppIcon::battery(battery) : AppIcon::headset(connected));
    QString tip = connected ? _controller->deviceModel() : tr("Aural - no device connected");
    if (connected && battery >= 0)
        tip += tr(" - %1%").arg(battery);
    if (!line.isEmpty())
        tip += QStringLiteral("\n") + line;
    _tray.setToolTip(tip);

    if (connected != _lastConnected)
    {
        _lastConnected = connected;
        if (_window && !_window->isVisible())
        {
            _tray.showMessage(QStringLiteral("Aural"),
                connected ? tr("Connected to %1").arg(_controller->deviceModel())
                          : tr("Device disconnected"),
                AppIcon::headset(connected), 3000);
        }
    }

    _updating = false;
}
