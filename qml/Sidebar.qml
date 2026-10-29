import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import kamera 1.0

// Left sidebar: list of watched folders. Selecting one only matters in
// "By folder" mode — it's what GalleryModel.activeFolder gets set to.
// Folders are added via a KIO-aware picker (fileOps.chooseWatchedFolder)
// since they live on the phone, not on the local filesystem.
Item {
    id: root
    property var model

    Rectangle {
        anchors.fill: parent
        color: Theme.surface
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Text {
            Layout.fillWidth: true
            Layout.margins: 14
            Layout.bottomMargin: 4
            text: qsTr("WATCHED FOLDERS")
            color: Theme.textFaint
            font.family: Theme.fontFamily
            font.pixelSize: 10
            font.letterSpacing: 1
            font.weight: Font.DemiBold
        }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.topMargin: 4
            clip: true
            model: root.model.watchedFolders

            delegate: Item {
                id: delegateRoot
                width: list.width
                height: 36

                readonly property string folderName: {
                    // watchedFolders holds full kio paths; show just the
                    // trailing segment so the list stays scannable.
                    var s = modelData.toString()
                    if (s.endsWith("/")) s = s.slice(0, -1)
                    var parts = s.split("/")
                    return parts.length ? parts[parts.length - 1] : s
                }
                readonly property bool isActive:
                    root.model.viewMode === "folder" &&
                    root.model.activeFolder === folderName

                Rectangle {
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    radius: Theme.radiusSmall
                    color: delegateRoot.isActive ? Theme.surfaceRaised : "transparent"
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 14
                    anchors.rightMargin: 8
                    spacing: 8

                    Text {
                        Layout.fillWidth: true
                        text: delegateRoot.folderName
                        color: delegateRoot.isActive ? Theme.textPrimary : Theme.textMuted
                        font.family: Theme.fontFamily
                        font.pixelSize: 13
                        elide: Text.ElideRight
                    }

                    ToolButton {
                        id: removeBtn
                        text: "×"
                        implicitWidth: 24
                        implicitHeight: 24
                        opacity: hovered ? 1 : 0.5
                        onClicked: root.model.removeWatchedFolder(modelData)
                        ToolTip.text: qsTr("Stop watching this folder")
                        ToolTip.visible: hovered
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    z: -1
                    onClicked: {
                        root.model.viewMode = "folder"
                        root.model.activeFolder = delegateRoot.folderName
                    }
                }
            }

            Text {
                anchors.centerIn: parent
                visible: list.count === 0
                text: qsTr("No folders yet")
                color: Theme.textFaint
                font.family: Theme.fontFamily
                font.pixelSize: 12
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.hairline
        }

        Button {
            Layout.fillWidth: true
            Layout.margins: 10
            text: qsTr("+ Add folder")
            flat: true
            onClicked: {
                var chosen = fileOps.chooseWatchedFolder(
                    qsTr("Choose a folder to watch"))
                if (chosen.length > 0)
                    root.model.addWatchedFolder(chosen)
            }
        }
    }
}
