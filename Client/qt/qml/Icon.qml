import QtQuick
import Aural

// Material Icons (Outlined) glyph via ligature name, e.g. name: "settings".
Text {
    property string name: ""
    property int size: 20
    font.family: "Material Icons Outlined"
    font.pixelSize: size
    text: name
    color: Theme.textSecondary
    verticalAlignment: Text.AlignVCenter
    horizontalAlignment: Text.AlignHCenter
}
