import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import kamera 1.0

// The hero of the app. Edge-to-edge thumbnail grid. In "date" mode, a thin
// sticky strip at the top shows the date label for whatever's currently
// scrolled into view — GridView lays out uniform cells, so a real
// Google-Photos-style row-breaking header isn't something it can do
// natively; this gets the orientation benefit without that complexity.
Item {
    id: root
    property var model
    signal photoActivated(int index)

    readonly property int cellSize: 152
    readonly property int cellSpacing: 3
    readonly property int stripHeight: 34

    Rectangle {
        anchors.fill: parent
        color: Theme.bg
    }

    // ── Sticky date strip ──
    Rectangle {
        id: strip
        visible: root.model.viewMode === "date" && root.model.count > 0
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: root.stripHeight
        z: 10
        color: Theme.bg

        Rectangle {
            // hairline under the strip so it reads as a fixed band, not
            // part of the scrolling content
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 1
            color: Theme.hairline
        }

        Text {
            anchors.left: parent.left
            anchors.leftMargin: 14
            anchors.verticalCenter: parent.verticalCenter
            text: {
                if (root.model.count === 0) return ""
                var topIndex = grid.indexAt(grid.contentX + 1, grid.contentY + 1)
                if (topIndex < 0) topIndex = 0
                return root.model.groupLabelForIndex(topIndex)
            }
            color: Theme.textPrimary
            font.family: Theme.fontFamily
            font.pixelSize: 13
            font.weight: Font.DemiBold
        }
    }

    GridView {
        id: grid
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.top: strip.visible ? strip.bottom : parent.top
        anchors.margins: 12
        anchors.topMargin: 12
        clip: true
        cellWidth: root.cellSize + root.cellSpacing
        cellHeight: root.cellSize + root.cellSpacing
        model: root.model
        cacheBuffer: cellHeight * 6 // prefetch a few rows ahead of scroll

        WheelHandler {
            target: null
            orientation: Qt.Vertical
            onWheel: (event) => {
                var dy = event.pixelDelta.y !== 0
                    ? event.pixelDelta.y * 1.5
                    : (event.angleDelta.y / 120 * grid.cellHeight * 1.5)
                var minY = grid.originY
                var maxY = Math.max(minY, grid.contentHeight - grid.height + grid.originY)
                grid.contentY = Math.max(minY, Math.min(grid.contentY - dy, maxY))
                grid.returnToBounds()
            }
        }

        delegate: Item {
            id: cell
            width: grid.cellWidth - root.cellSpacing
            height: grid.cellHeight - root.cellSpacing

            Rectangle {
                id: thumb
                anchors.fill: parent
                radius: Theme.radiusSmall
                color: Theme.surface
                clip: true

                property bool hovered: false

                Image {
                    anchors.fill: parent
                    visible: model.hasThumb
                    source: model.hasThumb
                        ? "image://thumb/" + encodeURIComponent(model.localThumb)
                        : ""
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                    cache: false
                }

                // Placeholder while the background thumb generator catches up.
                Rectangle {
                    anchors.fill: parent
                    visible: !model.hasThumb
                    color: Theme.surfaceRaised

                    BusyIndicator {
                        anchors.centerIn: parent
                        implicitWidth: 22
                        implicitHeight: 22
                        running: root.model.generatingThumbs
                        visible: root.model.generatingThumbs
                    }
                }

                // Video duration/type badge, bottom-right corner.
                Rectangle {
                    visible: model.isVideo
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.margins: 5
                    radius: Theme.radiusSmall
                    color: "#00000099"
                    width: playIcon.implicitWidth + 10
                    height: playIcon.implicitHeight + 6

                    Text {
                        id: playIcon
                        anchors.centerIn: parent
                        text: "▶"
                        color: "#FFFFFF"
                        font.pixelSize: 10
                    }
                }

                // Subtle hover lift — the only chrome the grid gets.
                scale: thumb.hovered ? 1.03 : 1.0
                Behavior on scale { NumberAnimation { duration: Theme.durationFast } }

                border.width: thumb.hovered ? 1 : 0
                border.color: Theme.accent

                MouseArea {
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onEntered: thumb.hovered = true
                    onExited: thumb.hovered = false
                    onClicked: root.photoActivated(index)
                }
            }
        }

        ScrollBar.vertical: ScrollBar {}
    }

    // Empty states
    Column {
        anchors.centerIn: parent
        spacing: 8
        visible: root.model.count === 0 && !root.model.scanning

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: root.model.watchedFolders.length === 0
                  ? qsTr("No folders watched yet")
                  : qsTr("Nothing here")
            color: Theme.textMuted
            font.family: Theme.fontFamily
            font.pixelSize: 14
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            visible: root.model.watchedFolders.length === 0
            text: qsTr("Add a folder from the sidebar to get started")
            color: Theme.textFaint
            font.family: Theme.fontFamily
            font.pixelSize: 12
        }
    }
}
