import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import kamera 1.0

// Top bar: date/folder view toggle on the left, scan + thumbnail
// generation status on the right. No search box, no zoom slider —
// those were deliberately left out of scope.
Item {
    id: root
    property var model

    Rectangle {
        anchors.fill: parent
        color: Theme.surface
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 16
        anchors.rightMargin: 16
        spacing: 16

        Text {
            text: "Kamera"
            color: Theme.textPrimary
            font.family: Theme.fontFamily
            font.pixelSize: 16
            font.weight: Font.DemiBold
        }

        Item { Layout.preferredWidth: 8 }

        // ── View mode toggle: By date / By folder ──
        Rectangle {
            Layout.preferredWidth: 220
            Layout.preferredHeight: 32
            radius: Theme.radiusMedium
            color: Theme.bg
            border.color: Theme.hairline
            border.width: 1

            Row {
                anchors.fill: parent
                anchors.margins: 2

                ModeButton {
                    text: qsTr("By date")
                    width: parent.width / 2
                    height: parent.height
                    active: root.model.viewMode === "date"
                    onClicked: root.model.viewMode = "date"
                }
                ModeButton {
                    text: qsTr("By folder")
                    width: parent.width / 2
                    height: parent.height
                    active: root.model.viewMode === "folder"
                    onClicked: {
                        root.model.viewMode = "folder"
                        if (root.model.activeFolder === "" && root.model.watchedFolders.length > 0) {
                            root.model.activeFolder = root.model.watchedFolders[0]
                        }
                    }
                }
            }
        }

        Item { Layout.fillWidth: true }

        // ── Status ──
        RowLayout {
            spacing: 8
            visible: root.model.scanning || root.model.generatingThumbs

            BusyIndicator {
                implicitWidth: 16
                implicitHeight: 16
                running: true
            }
            Text {
                text: root.model.scanning
                      ? qsTr("Scanning…")
                      : qsTr("Generating thumbnails…")
                color: Theme.textMuted
                font.family: Theme.fontFamily
                font.pixelSize: 12
            }
        }

        Text {
            visible: !root.model.scanning && !root.model.generatingThumbs
            text: qsTr("%1 items").arg(root.model.count)
            color: Theme.textMuted
            font.family: Theme.fontFamily
            font.pixelSize: 12
        }

        ToolButton {
            text: qsTr("Rescan")
            enabled: !root.model.scanning
            onClicked: root.model.scan()
        }
    }

    // Small local component for the toggle pill — kept inline since it's
    // only used here and needs to read `active`/emit `clicked`.
    component ModeButton: Rectangle {
        id: btn
        property string text
        property bool active
        signal clicked()

        radius: Theme.radiusSmall
        color: active ? Theme.accent : "transparent"

        Behavior on color { ColorAnimation { duration: Theme.durationFast } }

        Text {
            anchors.centerIn: parent
            text: btn.text
            color: btn.active ? "#FFFFFF" : Theme.textMuted
            font.family: Theme.fontFamily
            font.pixelSize: 12
            font.weight: btn.active ? Font.DemiBold : Font.Normal
        }

        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: btn.clicked()
        }
    }
}
