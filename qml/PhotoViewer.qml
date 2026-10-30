import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import kamera 1.0

// Full-res viewer with zoom/pan, navigation, caching, and management
Item {
    id: root
    property var model
    property int currentIndex: -1
    property string currentPhotoId: ""

    // Zoom and pan properties
    property real zoomScale: 1.0
    readonly property real minZoom: 1.0
    readonly property real maxZoom: 5.0

    function resetZoom() {
        zoomScale = 1.0
        contentItem.x = 0
        contentItem.y = 0
    }

    function openAt(index) {
        if (!root.model || index < 0 || index >= root.model.count) return
        currentIndex = index
        currentPhotoId = root.model.idForIndex(index)
        resetZoom()
        visible = true
        forceActiveFocus()
        exifPanel.visible = false
        root.model.preloadNeighbors(index, 4)
    }

    function close() {
        visible = false
        currentIndex = -1
        currentPhotoId = ""
        resetZoom()
    }

    function goNext() {
        if (currentIndex < 0 || !root.model || root.model.count === 0) return
        currentIndex = root.model.nextIndex(currentIndex)
        currentPhotoId = root.model.idForIndex(currentIndex)
        resetZoom()
        root.model.preloadNeighbors(currentIndex, 4)
    }

    function goPrev() {
        if (currentIndex < 0 || !root.model || root.model.count === 0) return
        currentIndex = root.model.prevIndex(currentIndex)
        currentPhotoId = root.model.idForIndex(currentIndex)
        resetZoom()
        root.model.preloadNeighbors(currentIndex, 4)
    }

    // Keep viewer on the selected photo across model resets
    Connections {
        target: root.model
        function onModelReset() {
            if (!root.visible || root.currentPhotoId === "") return
            var newIdx = root.model.indexOfId(root.currentPhotoId)
            if (newIdx >= 0) {
                root.currentIndex = newIdx
            } else if (root.model.count > 0) {
                var fallbackIdx = Math.min(root.currentIndex, root.model.count - 1)
                if (fallbackIdx < 0) fallbackIdx = 0
                root.currentIndex = fallbackIdx
                root.currentPhotoId = root.model.idForIndex(fallbackIdx)
            } else {
                root.close()
            }
        }
    }

    readonly property bool isVideoCurrent: root.currentEntry ? root.currentEntry.isVideo : false
    readonly property bool isLocalCurrent: root.currentEntry ? (String(root.currentEntry.url).indexOf("file:") === 0 || String(root.currentEntry.url).indexOf("://") === -1) : false

    focus: visible
    Keys.onLeftPressed: goPrev()
    Keys.onRightPressed: goNext()
    Keys.onEscapePressed: {
        if (zoomScale > 1.05) {
            resetZoom()
        } else {
            close()
        }
    }
    Keys.onReturnPressed: if (currentIndex >= 0) root.model.openFullRes(currentIndex)
    Keys.onEnterPressed: if (currentIndex >= 0) root.model.openFullRes(currentIndex)

    Rectangle {
        anchors.fill: parent
        color: "#000000"
    }

    // ── Image / video surface ──
    Item {
        id: stage
        anchors.fill: parent
        anchors.bottomMargin: bottomBar.height
        clip: true

        Item {
            id: contentItem
            anchors.fill: parent
            scale: root.zoomScale
            transformOrigin: Item.Center

            Behavior on scale { NumberAnimation { duration: Theme.durationFast } }

            // Cached thumbnail preview shown while full-res loads
            Image {
                id: thumbPreview
                anchors.fill: parent
                anchors.margins: 24
                fillMode: Image.PreserveAspectFit
                asynchronous: true
                visible: root.currentIndex >= 0 && Boolean(root.currentEntry && root.currentEntry.hasThumb) && (fullImage.status !== Image.Ready)
                source: (root.currentEntry && root.currentEntry.hasThumb && root.currentEntry.localThumb)
                    ? "image://thumb/" + encodeURIComponent(root.currentEntry.localThumb)
                    : ""
            }

            // Full rendered quality image/first frame
            Image {
                id: fullImage
                anchors.fill: parent
                anchors.margins: 24
                fillMode: Image.PreserveAspectFit
                asynchronous: true
                sourceSize: Qt.size(3840, 3840) // Limit max texture size for massive phone sensors
                visible: root.currentIndex >= 0 && (status === Image.Ready)
                source: {
                    if (root.currentIndex < 0 || !root.currentEntry || !root.currentEntry.url)
                        return ""
                    if (root.isLocalCurrent && !root.isVideoCurrent)
                        return root.currentEntry.url
                    return "image://full/" + encodeURIComponent(String(root.currentEntry.url))
                }
            }

            // Video overlay: Large central Play button over the rendered first frame
            Rectangle {
                anchors.centerIn: parent
                visible: root.currentIndex >= 0 && root.isVideoCurrent
                width: 88
                height: 88
                radius: 44
                color: playArea.containsMouse ? "#DD1E1E2E" : "#AA11111B"
                border.color: playArea.containsMouse ? Theme.accent : "#FFFFFF55"
                border.width: 2

                Behavior on color { ColorAnimation { duration: Theme.durationFast } }
                Behavior on scale { NumberAnimation { duration: Theme.durationFast } }
                scale: playArea.pressed ? 0.95 : (playArea.containsMouse ? 1.08 : 1.0)

                Text {
                    anchors.centerIn: parent
                    anchors.horizontalCenterOffset: 3
                    text: "▶"
                    color: "#FFFFFF"
                    font.pixelSize: 34
                }

                MouseArea {
                    id: playArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        if (root.currentEntry) {
                            fileOps.openWith(root.currentEntry.url)
                        }
                    }
                }
            }
        }

        // Zoom and Pan interaction
        MouseArea {
            anchors.fill: parent
            hoverEnabled: true
            drag.target: root.zoomScale > 1.0 ? contentItem : null
            drag.axis: Drag.XAndYAxis

            onDoubleClicked: {
                if (root.isVideoCurrent) {
                    if (root.currentIndex >= 0) root.model.openFullRes(root.currentIndex)
                } else {
                    if (root.zoomScale > 1.05) {
                        root.resetZoom()
                    } else {
                        root.zoomScale = 2.5
                    }
                }
            }

            onWheel: (wheel) => {
                if (root.isVideoCurrent) return
                var factor = wheel.angleDelta.y > 0 ? 1.25 : 0.8
                var targetZoom = Math.max(root.minZoom, Math.min(root.maxZoom, root.zoomScale * factor))
                root.zoomScale = targetZoom
                if (root.zoomScale <= 1.05) {
                    root.resetZoom()
                }
            }
        }

        BusyIndicator {
            anchors.centerIn: parent
            running: fullImage.status === Image.Loading
            visible: running
        }
    }

    readonly property var currentEntry: {
        if (currentIndex < 0 || !root.model) return null
        return root.model.get(currentIndex)
    }

    // ── Top bar: filename + zoom indicator + close ──
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 48
        color: "#00000088"

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 16
            anchors.rightMargin: 16

            Text {
                Layout.fillWidth: true
                text: currentEntry ? currentEntry.filename : ""
                color: Theme.textPrimary
                font.family: Theme.fontFamily
                font.pixelSize: 13
                elide: Text.ElideMiddle
            }

            Text {
                visible: root.zoomScale > 1.05
                text: Math.round(root.zoomScale * 100) + "%"
                color: Theme.accent
                font.family: Theme.fontFamily
                font.pixelSize: 12
            }

            ToolButton {
                text: qsTr("✕")
                onClicked: root.close()
            }
        }
    }

    // ── Side nav arrows ──
    ToolButton {
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
        anchors.leftMargin: 8
        text: "‹"
        font.pixelSize: 24
        onClicked: root.goPrev()
    }
    ToolButton {
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.rightMargin: 8
        text: "›"
        font.pixelSize: 24
        onClicked: root.goNext()
    }

    // ── Bottom action bar ──
    Rectangle {
        id: bottomBar
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 52
        color: "#00000088"

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 16
            anchors.rightMargin: 16
            spacing: 8

            Button {
                text: qsTr("Info")
                flat: true
                onClicked: exifPanel.visible = !exifPanel.visible
            }
            Button {
                text: qsTr("Open in Gwenview")
                flat: true
                onClicked: root.model.openFullRes(root.currentIndex)
            }
            Button {
                text: qsTr("Open with…")
                flat: true
                onClicked: currentEntry && fileOps.openWithDialog(currentEntry.url)
            }
            Button {
                text: qsTr("Copy to PC")
                flat: true
                onClicked: {
                    var dest = fileOps.chooseDestinationFolder(qsTr("Copy to…"))
                    if (dest.length > 0)
                        root.model.copyToPC(root.currentIndex, dest)
                }
            }
            Item { Layout.fillWidth: true }
            Button {
                text: qsTr("Rename")
                flat: true
                onClicked: renameDialog.open()
            }
            Button {
                text: qsTr("Delete")
                flat: true
                onClicked: deleteDialog.open()
            }
        }
    }

    // ── EXIF info panel ──
    Rectangle {
        id: exifPanel
        visible: false
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: bottomBar.top
        anchors.topMargin: 48
        width: 260
        color: Theme.surface

        readonly property var info: root.currentIndex >= 0
            ? root.model.exifForIndex(root.currentIndex) : ({})

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 16
            spacing: 10

            Text {
                text: qsTr("Details")
                color: Theme.textPrimary
                font.family: Theme.fontFamily
                font.pixelSize: 14
                font.weight: Font.DemiBold
            }

            Repeater {
                model: [
                    { label: qsTr("Filename"), key: "filename" },
                    { label: qsTr("Date taken"), key: "dateTaken" },
                    { label: qsTr("Folder"), key: "folder" },
                    { label: qsTr("Size"), key: "fileSize" },
                ]
                delegate: ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 1
                    Text {
                        text: modelData.label
                        color: Theme.textFaint
                        font.family: Theme.fontFamily
                        font.pixelSize: 10
                    }
                    Text {
                        Layout.fillWidth: true
                        text: exifPanel.info[modelData.key] !== undefined
                              ? String(exifPanel.info[modelData.key]) : ""
                        color: Theme.textMuted
                        font.family: Theme.fontFamily
                        font.pixelSize: 12
                        wrapMode: Text.Wrap
                    }
                }
            }
        }
    }

    // ── Confirm dialogs ──
    Dialog {
        id: deleteDialog
        anchors.centerIn: parent
        modal: true
        title: qsTr("Delete from phone?")
        standardButtons: Dialog.Yes | Dialog.No
        onAccepted: {
            var deletingIdx = root.currentIndex
            root.model.deleteFromPhone(deletingIdx)
            // Model reset handler will advance to the next photo automatically
        }
        Text {
            text: qsTr("This removes the file from your phone. It can't be undone here.")
            color: Theme.textMuted
            font.family: Theme.fontFamily
            wrapMode: Text.Wrap
        }
    }

    Dialog {
        id: renameDialog
        anchors.centerIn: parent
        modal: true
        title: qsTr("Rename file")
        standardButtons: Dialog.Ok | Dialog.Cancel

        function performRename() {
            var newName = renameField.text.trim()
            if (newName.length > 0 && newName !== currentEntry.filename && newName.indexOf("/") === -1) {
                root.model.renameFile(root.currentIndex, newName)
                renameDialog.close()
            }
        }

        onAccepted: performRename()
        onOpened: {
            renameField.text = currentEntry ? currentEntry.filename : ""
            renameField.forceActiveFocus()
            renameField.selectAll()
        }

        ColumnLayout {
            spacing: 8

            TextField {
                id: renameField
                implicitWidth: 260
                onAccepted: renameDialog.performRename()
            }

            Text {
                visible: renameField.text.indexOf("/") !== -1
                text: qsTr("Filename cannot contain '/'")
                color: "#E78284"
                font.pixelSize: 11
            }
        }
    }
}
