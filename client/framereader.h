#ifndef FRAMEREADER_H
#define FRAMEREADER_H

#include <QObject>
#include <QVideoSink>
#include <QVideoFrame>

class FrameReader : public QObject
{
    Q_OBJECT
    // Q_PROPERTY(QVideoSink *videoSink)
public:
    FrameReader();
};

#endif // FRAMEREADER_H
