import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Aural

// The confirmation the headphones ask for before a multipoint change (a fixed-message alert).
// The answer quotes the message ID the headphones sent, with the user's choice; nothing else
// ever answers it. Escape counts as Cancel, as closing Sony's own dialog does.
Popup {
    id: dialog

    readonly property var alert: Hp.multipointAlert
    readonly property bool active: alert.messageId !== undefined
    readonly property bool turningOn: alert.turningOn === true
    // true: the headphones offer accept or cancel. false: they only ask for acknowledgement.
    readonly property bool choice: alert.choice === true

    function answer(accept) {
        if (dialog.active)
            Hp.answerMultipointAlert(dialog.alert.messageId, accept)
    }

    // Opening and closing follow the controller: the alert map empties once it is answered,
    // settled, or its session ends. The dialog never closes itself.
    visible: active
    modal: true
    closePolicy: Popup.NoAutoClose
    anchors.centerIn: Overlay.overlay
    width: Math.min(440, (parent ? parent.width : 440) - 2 * Theme.xl)
    padding: Theme.lg

    Overlay.modal: Rectangle { color: Theme.dark ? "#99000000" : "#66000000" }

    background: Rectangle {
        radius: Theme.radiusLg
        color: Theme.surfaceContainer
        border.color: Theme.outlineVariant
        border.width: 1
    }

    Shortcut {
        sequence: "Esc"
        enabled: dialog.active
        onActivated: dialog.answer(false)
    }

    contentItem: ColumnLayout {
        spacing: Theme.md

        Text {
            Layout.fillWidth: true
            text: dialog.choice
                  ? (dialog.turningOn ? "Turn on multipoint?" : "Turn off multipoint?")
                  : (dialog.turningOn ? "Turning on multipoint" : "Turning off multipoint")
            color: Theme.textPrimary
            font.family: Theme.displayFont
            font.pixelSize: Theme.headlineMd
            font.weight: Font.DemiBold
            wrapMode: Text.WordWrap
        }
        Text {
            Layout.fillWidth: true
            // Sony's caution for message 06 is about LDAC; whether LDAC is in use right now is
            // not something the alert says.
            text: dialog.turningOn
                  ? "The headphones will disconnect to turn on multipoint. If they are using LDAC, it will be turned off, because LDAC can't be used with multipoint. Aural will reconnect and check the setting."
                  : "The headphones will disconnect to turn off multipoint. Aural will reconnect and check the setting."
            color: Theme.textSecondary
            font.pixelSize: Theme.bodyMd
            wrapMode: Text.WordWrap
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: Theme.base
            spacing: Theme.sm
            Item { Layout.fillWidth: true }
            AuralButton {
                visible: dialog.choice
                text: "Cancel"
                onClicked: dialog.answer(false)
            }
            AuralButton {
                primary: true
                text: dialog.choice ? "Continue" : "OK"
                onClicked: dialog.answer(true)
            }
        }
    }
}
