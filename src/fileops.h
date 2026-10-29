#pragma once

#include <QObject>
#include <QUrl>

// Thin wrapper around KIO / Qt file operations exposed to QML.
// Heavy operations (copy, delete, rename) live in GalleryModel since they
// interact with the model data. This class handles the actions that don't:
//   • open with an external app
//   • show a native file picker for the copy destination
class FileOps : public QObject
{
    Q_OBJECT
public:
    explicit FileOps(QObject *parent = nullptr);

    // Open a KIO url with the system's default handler (e.g. mpv for video)
    Q_INVOKABLE void openWith(const QUrl &url);

    // Open a KDE "Open With" dialog for the given url
    Q_INVOKABLE void openWithDialog(const QUrl &url);

    // Show a native "choose a folder" dialog for picking a Copy to PC
    // destination. Uses Qt's platform-integrated picker (KDE's, when running
    // under Plasma), which understands KIO URLs as well as local paths.
    // Returns a local filesystem path, or an empty string if cancelled.
    Q_INVOKABLE QString chooseDestinationFolder(const QString &dialogTitle = QString());

    // Show a KIO-aware folder picker for choosing a *watched* folder to add
    // (e.g. a path under phone:// or kdeconnect://). A plain QFileDialog
    // can't browse those schemes, so this uses KIO::KUrlRequesterDialog,
    // the same building block Dolphin itself relies on for KIO-aware
    // browsing. Returns the chosen KIO url as a string, or an empty string
    // if cancelled.
    Q_INVOKABLE QString chooseWatchedFolder(const QString &dialogTitle = QString(),
                                             const QString &startUrl = QString());
};
