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
}
