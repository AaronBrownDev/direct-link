#include "applogger.hpp"

#include <QtLogging>

QtMessageHandler AppLogger::s_original_handler = nullptr;
std::unique_ptr<AppLogger> AppLogger::s_instance = std::make_unique<AppLogger>();

AppLogger::AppLogger(QObject *parent) : QObject(parent) {}

AppLogger::~AppLogger() {
    if (s_original_handler != nullptr) {
        qInstallMessageHandler(s_original_handler);
    }
}

void AppLogger::installHandler() {
    s_original_handler = qInstallMessageHandler(AppLogger::logToApp);
}

void AppLogger::logToApp(QtMsgType type, const QMessageLogContext &context, const QString &msg) {
    if (s_original_handler != nullptr) {
        s_original_handler(type, context, msg);
    }

    QString type_str;
    switch (type) {
        case QtDebugMsg:
            type_str = QStringLiteral("debug");
            break;
        case QtInfoMsg:
            type_str = QStringLiteral("info");
            break;
        case QtWarningMsg:
            type_str = QStringLiteral("warning");
            break;
        case QtCriticalMsg:
            type_str = QStringLiteral("critical");
            break;
        case QtFatalMsg:
            type_str = QStringLiteral("fatal");
            break;
        default:
            type_str = QStringLiteral("unknown");
    }

    emit s_instance->messageReceived(type_str, msg);
}

AppLogger *AppLogger::logger() {
    return s_instance.get();
}