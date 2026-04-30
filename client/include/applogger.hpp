/*
 * File: applogger.hpp
 * Author: Justin Williams
 * Date: 4/29/26
 * File Description: A Qt message handler that intercepts qDebug,
 * qInfo, qWarning, and qCritical output and emits lines as a 
 * signal. Allows QML components to receive output messages while 
 * preserving terminal output.
 */

#pragma once

#include <QObject>
#include <QDebug>
#include <QQmlEngine>
#include <gsl/pointers>

class AppLogger : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(AppLogger* logger READ logger CONSTANT)

    public:
        explicit AppLogger(QObject *parent = nullptr);
        ~AppLogger() override;

        AppLogger(const AppLogger &) = delete;
        AppLogger &operator=(const AppLogger &) = delete;
        AppLogger(AppLogger &&) = delete;
        AppLogger &operator=(AppLogger &&) = delete;

        static gsl::owner<AppLogger *>create(QQmlEngine *engine, QJSEngine * /*unused*/) {
            return new AppLogger(engine);
        }

        static void installHandler();

        [[nodiscard]] static AppLogger* logger();

    signals:

        void messageReceived(const QString &type, const QString &message);

    private:
        static QtMessageHandler s_original_handler;
        static void logToApp(QtMsgType type, const QMessageLogContext &context, const QString &msg);
        static std::unique_ptr<AppLogger> s_instance;
};