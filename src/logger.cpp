#include "logger.h"

#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include <QTextStream>
#include <QMutex>
#include <QMutexLocker>
#include <QSysInfo>
#include <iostream>

namespace Logger
{
    static QMutex s_logMutex;
    static const qint64 MAX_LOG_SIZE = 5 * 1024 * 1024; // 5 MB
    static const int MAX_RETENTION_DAYS = 30;            // 1 month
    static bool s_verbose = false;

    QString logFilePath()
    {
        return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
               + QStringLiteral("/kamera/kamera.log");
    }

    static QString rotatedLogFilePath()
    {
        return logFilePath() + QStringLiteral(".old");
    }

    static void pruneOldEntries()
    {
        const QString activePath = logFilePath();
        const QString oldPath    = rotatedLogFilePath();

        // 1. Clean up rotated archive if older than 30 days
        QFileInfo oldInfo(oldPath);
        if (oldInfo.exists()) {
            if (oldInfo.lastModified().date() < QDate::currentDate().addDays(-MAX_RETENTION_DAYS)) {
                QFile::remove(oldPath);
            }
        }

        // 2. Prune entries older than 30 days and enforce size cap on active log
        QFile file(activePath);
        if (!file.exists() || !file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            return;
        }

        const QDate cutoffDate = QDate::currentDate().addDays(-MAX_RETENTION_DAYS);
        QStringList keptLines;
        bool modified = false;

        QTextStream in(&file);
        while (!in.atEnd()) {
            const QString line = in.readLine();
            if (line.length() >= 10 && line[4] == QLatin1Char('-') && line[7] == QLatin1Char('-')) {
                const QDate lineDate = QDate::fromString(line.left(10), QStringLiteral("yyyy-MM-dd"));
                if (lineDate.isValid() && lineDate < cutoffDate) {
                    modified = true;
                    continue; // Skip lines older than 30 days
                }
            }
            keptLines.append(line);
        }
        file.close();

        // Check if kept lines exceed size limit; keep the most recent entries
        qint64 totalBytes = 0;
        for (const QString &l : keptLines) {
            totalBytes += l.toUtf8().size() + 1;
        }
        while (totalBytes > MAX_LOG_SIZE && !keptLines.isEmpty()) {
            totalBytes -= (keptLines.first().toUtf8().size() + 1);
            keptLines.removeFirst();
            modified = true;
        }

        if (modified) {
            if (file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
                QTextStream out(&file);
                for (const QString &l : keptLines) {
                    out << l << QLatin1Char('\n');
                }
                file.close();
            }
        }
    }

    static void rotateLogIfNeeded()
    {
        const QString activePath = logFilePath();
        QFileInfo info(activePath);
        if (info.exists() && info.size() >= MAX_LOG_SIZE) {
            const QString oldPath = rotatedLogFilePath();
            if (QFile::exists(oldPath)) {
                QFile::remove(oldPath);
            }
            QFile::rename(activePath, oldPath);
        }
    }

    void messageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg)
    {
        QMutexLocker locker(&s_logMutex);

        rotateLogIfNeeded();

        const char *level = "INFO ";
        switch (type) {
        case QtDebugMsg:    level = "DEBUG"; break;
        case QtInfoMsg:     level = "INFO "; break;
        case QtWarningMsg:  level = "WARN "; break;
        case QtCriticalMsg: level = "ERROR"; break;
        case QtFatalMsg:    level = "FATAL"; break;
        }

        const QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"));
        
        QString contextInfo;
        if (context.category && qstrcmp(context.category, "default") != 0) {
            contextInfo = QStringLiteral("[%1] ").arg(QLatin1String(context.category));
        }

        const QString logEntry = QStringLiteral("%1 [%2] %3%4\n").arg(
            timestamp,
            QLatin1String(level),
            contextInfo,
            msg
        );

        if (s_verbose) {
            std::cerr << logEntry.toStdString();
        }

        const QString activePath = logFilePath();
        QFile file(activePath);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
            QDir().mkpath(QFileInfo(activePath).absolutePath());
            if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
                return;
            }
        }
        file.write(logEntry.toUtf8());
        file.flush();

        if (type == QtFatalMsg) {
            if (!s_verbose) {
                std::cerr << logEntry.toStdString();
            }
            std::abort();
        }
    }

    void init(bool verbose)
    {
        s_verbose = verbose || qEnvironmentVariableIsSet("KAMERA_VERBOSE");

        const QString logPath = logFilePath();
        QDir().mkpath(QFileInfo(logPath).absolutePath());

        pruneOldEntries();

        qInstallMessageHandler(messageHandler);

        qInfo().noquote() << QStringLiteral("=== Kamera session started at %1 (Qt %2, OS: %3) ===")
            .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")),
                 QString::fromLatin1(qVersion()),
                 QSysInfo::prettyProductName());
        qInfo().noquote() << QStringLiteral("Log file: %1 (Max size: 5MB, Retention: 30 days)").arg(logPath);
    }
}
