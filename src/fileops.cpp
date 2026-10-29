#include "fileops.h"

#include <QDesktopServices>
#include <QUrl>
#include <QFileDialog>
#include <QStandardPaths>

// KIO OpenWith dialog
#include <KIO/OpenUrlJob>
#include <KIO/ApplicationLauncherJob>
#include <KIO/JobUiDelegateFactory>
#include <KUrlRequesterDialog>
#include <KUrlRequester>
#include <KFile>

FileOps::FileOps(QObject *parent)
    : QObject(parent)
{}

void FileOps::openWith(const QUrl &url)
{
    // Use KIO::OpenUrlJob so KDE handles the SFTP url transparently —
    // it will stream the file if needed, then hand it to the default app.
    auto *job = new KIO::OpenUrlJob(url);
    job->setUiDelegate(KIO::createDefaultJobUiDelegate(KJobUiDelegate::AutoHandlingEnabled, nullptr));
    job->setRunExecutables(false);
    job->start();
}

void FileOps::openWithDialog(const QUrl &url)
{
    // In KF6, ApplicationLauncherJob() without a service prompts the user
    // with the KDE "Open With" dialog.
    auto *job = new KIO::ApplicationLauncherJob();
    job->setUrls({url});
    job->setUiDelegate(KIO::createDefaultJobUiDelegate(KJobUiDelegate::AutoHandlingEnabled, nullptr));
    job->start();
}

QString FileOps::chooseDestinationFolder(const QString &dialogTitle)
{
    // QFileDialog::getExistingDirectory uses the platform-native dialog
    // (KDE's own, under Plasma) — the same approach KIO itself uses for
    // "Copy To.../Browse…" (see KFileCopyToMenu). Destination is always a
    // local path since "Copy to PC" means exactly that.
    const QString startDir = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    const QString title = dialogTitle.isEmpty()
        ? QStringLiteral("Choose destination folder")
        : dialogTitle;
    return QFileDialog::getExistingDirectory(
        nullptr, title, startDir,
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
}

QString FileOps::chooseWatchedFolder(const QString &dialogTitle, const QString &startUrl)
{
    // KUrlRequesterDialog wraps KUrlRequester, which is KIO-aware: it can
    // browse phone:// and kdeconnect:// the same way Dolphin does, unlike
    // QFileDialog which only understands local paths. This is the right
    // tool for picking a *watched* folder that lives on the phone.
    const QString title = dialogTitle.isEmpty()
        ? QStringLiteral("Choose a folder to watch")
        : dialogTitle;
    const QUrl start = startUrl.isEmpty() ? QUrl(QStringLiteral("kdeconnect://")) : QUrl(startUrl);

    KUrlRequesterDialog dlg(start, title, nullptr);
    dlg.setWindowTitle(title);
    if (auto *req = dlg.urlRequester()) {
        req->setMode(KFile::Directory | KFile::ExistingOnly);
    }

    if (dlg.exec() == QDialog::Accepted) {
        QUrl chosen = dlg.selectedUrl();
        if (chosen.isValid())
            return chosen.toString();
    }

    return QString();
}
