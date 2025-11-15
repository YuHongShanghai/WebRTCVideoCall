#ifndef VIDEOITEM_H
#define VIDEOITEM_H

#include <QQuickItem>
#include <QQuickFramebufferObject>
#include <QTimerEvent>
#include "i420render.h"

class VideoItem : public QQuickFramebufferObject
{
    Q_OBJECT
public:
    VideoItem(QQuickItem *parent = nullptr);
    Renderer *createRenderer() const override;

    int m_videoWidth = 0;
    int m_videoHeight = 0;
    YUVData yuvData;
    bool videoSizeChanged;

public slots:
    void onVideoSizeChanged(int w, int h);
    void onReceiveVideoData(const YUVData &data);
};


#endif // VIDEOITEM_H
