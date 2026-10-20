#pragma once

#include <QIcon>

// The app/tray artwork, painted from the bundled Material icon font so there is no .ico
// asset pipeline to keep in sync.
namespace AppIcon {

// state: true = a device is connected (brand colour), false = idle (muted colour).
QIcon headset(bool connected);

} // namespace AppIcon
