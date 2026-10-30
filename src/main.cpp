#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QIcon>
#include <QCommandLineParser>
#include <cstdlib>

#include "gallerymodel.h"
#include "thumbnailprovider.h"
#include "fileops.h"
#include "imagediskcache.h"
#include "logger.h"

int main(int argc, char *argv[])
{
    // Check for verbose flag
    bool verbose = false;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--verbose") == 0 || strcmp(argv[i], "-v") == 0) {
            verbose = true;
            break;
        }
    }

    // Initialize file logging (captures all Qt/QML logs to ~/.local/share/kamera/kamera.log)
    Logger::init(verbose);

    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("Kamera"));
    app.setOrganizationName(QStringLiteral("kamera"));
    app.setDesktopFileName(QStringLiteral("kamera"));
    app.setWindowIcon(QIcon::fromTheme(QStringLiteral("camera-photo")));

    qmlRegisterType<GalleryModel>("kamera", 1, 0, "GalleryModel");
    qmlRegisterSingletonType(QUrl(QStringLiteral("qrc:/kamera/qml/Theme.qml")), "kamera", 1, 0, "Theme");

    QQmlApplicationEngine engine;

    // Image provider for local thumbnail files
    engine.addImageProvider(QStringLiteral("thumb"), new ThumbnailProvider);
    // Image provider for original full-resolution images (local & KIO)
    engine.addImageProvider(QStringLiteral("full"), new FullImageProvider);

    // FileOps context property
    FileOps fileOps;
    engine.rootContext()->setContextProperty(QStringLiteral("fileOps"), &fileOps);

    const QUrl url(QStringLiteral("qrc:/kamera/qml/main.qml"));
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated,
                     &app, [url](QObject *obj, const QUrl &objUrl) {
        if (!obj && url == objUrl)
            QCoreApplication::exit(-1);
    }, Qt::QueuedConnection);

    // On quit, clean up disk preview cache and terminate immediately
    QObject::connect(&app, &QCoreApplication::aboutToQuit, [&]() {
        qInfo() << "=== Kamera session ending, cleaning up temporary resources ===";
        ImageDiskCache::clear();
    });

    engine.load(url);
    int res = app.exec();

    // Clean up cache and immediately exit so terminal process releases promptly
    ImageDiskCache::clear();
    std::quick_exit(res);
    return res;
}
