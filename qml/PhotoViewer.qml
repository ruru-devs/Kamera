import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import kamera 1.0

// Full-res viewer. Streams the original from the phone via KIO (handled by
// FileOps/openWith for video, and a plain Image with the KIO url for
// photos — KIO registers its own Qt network/file backends so QML's Image
// can load a phone:// url directly once KIO has mounted it).
// Arrow-key navigation only — no slideshow, no in-app editing; this is a
// viewer, not an editor.
Item {
    id: root
    property var model
    property int currentIndex: -1

    function openAt(index) {
        currentIndex = index
        visible = true
        forceActiveFocus()
        exifPanel.visible = false
        if (root.model && index >= 0) {
            root.model.preloadNeighbors(index, 4)
        }
    }

    function close() {
        visible = false
        currentIndex = -1
    }

    function goNext() {
        if (currentIndex < 0) return
        currentIndex = root.model.nextIndex(currentIndex)
        if (root.model && currentIndex >= 0) {
            root.model.preloadNeighbors(currentIndex, 4)
        }
    }

    function goPrev() {
        if (currentIndex < 0) return
        currentIndex = root.model.prevIndex(currentIndex)
        if (root.model && currentIndex >= 0) {
            root.model.preloadNeighbors(currentIndex, 4)
        }
    }

    // Single source of truth for "is the current item a video", derived
    // from currentEntry. Previously this was declared twice as a local
    // property — once on fullImage, once on the sibling Column — which
    // happened to work for those two bindings individually but is fragile
    // and wrong in spirit: `isVideoCurrent` referenced from any other
    // sibling (or from a binding inside this Item but outside fullImage)
    // resolved to undefined/error since neither local copy is visible
    // outside its own object. Hoisting it onto root makes it a real,
    // single, reusable property.
    readonly property bool isVideoCurrent: root.currentEntry ? root.currentEntry.isVideo : false
    readonly property bool isLocalCurrent: root.currentEntry ? (String(root.currentEntry.url).indexOf("file:") === 0 || String(root.currentEntry.url).indexOf("://") === -1) : false

    focus: visible
    Keys.onLeftPressed: goPrev()
    Keys.onRightPressed: goNext()
    Keys.onEscapePressed: close()
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

        // Cached thumbnail preview shown while full-res loads or as fallback
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

        // Full rendered quality image/first frame.
        // For local files: loaded directly if image, or via image://full/ for video first frame.
        // For remote files: loaded via the async KIO image://full/ provider.
        Image {
            id: fullImage
            anchors.fill: parent
            anchors.margins: 24
            fillMode: Image.PreserveAspectFit
            asynchronous: true
            visible: root.currentIndex >= 0 && (status === Image.Ready)
            source: {
                if (root.currentIndex < 0 || !root.currentEntry || !root.currentEntry.url)
                    return ""
                if (root.isLocalCurrent && !root.isVideoCurrent)
                    return root.currentEntry.url
                return "image://full/" + encodeURIComponent(String(root.currentEntry.url))
            }
        }

        MouseArea {
            anchors.fill: parent
            onDoubleClicked: {
                if (root.currentIndex >= 0)
                    root.model.openFullRes(root.currentIndex)
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
                anchors.horizontalCenterOffset: 3 // visual centering for triangle play glyph
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

        BusyIndicator {
            anchors.centerIn: parent
            running: fullImage.status === Image.Loading
            visible: running
        }
    }

    // Resolve the current row once into a plain object so the rest of the
    // UI doesn't repeat role lookups everywhere.
    //
    // NOTE on the magic numbers below (257/258/262/263/264): these are
    // Qt::UserRole + 1..N matching GalleryModel::Roles exactly:
    //   UrlRole = Qt::UserRole + 1        = 257
    //   FilenameRole                       = 258
    //   DateTakenRole                      = 259
    //   DateStringRole                     = 260
    //   FolderRole                         = 261
    //   IsVideoRole                        = 262
    //   LocalThumbRole                     = 263
    //   HasThumbRole                       = 264
    readonly property var currentEntry: {
        if (currentIndex < 0 || !root.model) return null
        var idx = root.model.index(currentIndex, 0)
        return {
            url:        root.model.data(idx, 257), // UrlRole
            filename:   root.model.data(idx, 258), // FilenameRole
            isVideo:    Boolean(root.model.data(idx, 262)), // IsVideoRole
            localThumb: root.model.data(idx, 263), // LocalThumbRole
            hasThumb:   Boolean(root.model.data(idx, 264))  // HasThumbRole
        }
    }

    // ── Top bar: filename + close ──
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
            root.model.deleteFromPhone(root.currentIndex)
            root.close()
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
        onAccepted: {
            if (renameField.text.length > 0)
                root.model.renameFile(root.currentIndex, renameField.text)
        }
        onOpened: renameField.text = currentEntry ? currentEntry.filename : ""

        TextField {
            id: renameField
            width: 240
        }
    }
}
