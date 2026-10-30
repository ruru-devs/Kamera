import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import kamera 1.0

ApplicationWindow {
    id: root
    visible: true
    width: 1180
    height: 760
    minimumWidth: 760
    minimumHeight: 480
    title: qsTr("Kamera")
    color: Theme.bg

    // Single shared model instance for the whole app.
    GalleryModel {
        id: gallery
        onError: (msg) => toast.show(msg, true)
        onNotification: (msg) => toast.show(msg, false)
    }

    onClosing: {
        gallery.cancelBackgroundTasks()
        Qt.quit()
    }

    Component.onCompleted: gallery.scan()

    PhotoViewer {
        id: viewer
        anchors.fill: parent
        z: 100
        model: gallery
        visible: false
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        visible: !viewer.visible

        TopBar {
            id: topBar
            Layout.fillWidth: true
            Layout.preferredHeight: 56
            model: gallery
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.hairline
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            Sidebar {
                id: sidebar
                Layout.fillHeight: true
                Layout.preferredWidth: 220
                model: gallery
            }

            Rectangle {
                Layout.fillHeight: true
                Layout.preferredWidth: 1
                color: Theme.hairline
            }

            PhotoGrid {
                id: grid
                Layout.fillWidth: true
                Layout.fillHeight: true
                model: gallery
                onPhotoActivated: function(index) {
                    viewer.openAt(index)
                }
            }
        }
    }

    // Toast notification banner
    Rectangle {
        id: toast
        z: 200
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: 16
        height: 36
        radius: Theme.radiusMedium
        color: isError ? "#E78284" : Theme.surfaceRaised
        border.color: isError ? "#EA999C" : Theme.accent
        border.width: 1
        opacity: visible ? 1 : 0
        visible: false

        property bool isError: false

        Behavior on opacity { NumberAnimation { duration: Theme.durationFast } }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 16
            anchors.rightMargin: 16
            spacing: 8

            Text {
                id: toastText
                color: toast.isError ? "#11111B" : Theme.textPrimary
                font.family: Theme.fontFamily
                font.pixelSize: 12
                font.weight: Font.Medium
            }
        }

        Timer {
            id: toastTimer
            interval: 3500
            onTriggered: toast.visible = false
        }

        function show(msg, error = false) {
            toastText.text = msg
            toast.isError = error
            toast.visible = true
            toast.width = toastText.implicitWidth + 32
            toastTimer.restart()
        }
    }
}
