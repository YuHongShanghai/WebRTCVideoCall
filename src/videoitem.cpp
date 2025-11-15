#include "videoitem.h"
#include "i420render.h"
#include <QOpenGLFramebufferObject>
#include <QQuickWindow>
#include <QThread>
#include <iostream>

// update->synchronize->render

class VideoFboItem : public QQuickFramebufferObject::Renderer
{
public:
    VideoFboItem() {
        m_render.init();
    }

    void render() override {
        m_render.paint();
    }

    QOpenGLFramebufferObject *createFramebufferObject(const QSize &size) override {
        QOpenGLFramebufferObjectFormat format;
        format.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
        format.setSamples(4);
        m_render.resize(size.width(), size.height());
        return new QOpenGLFramebufferObject(size, format);
    }

    void synchronize(QQuickFramebufferObject *item) override {
        VideoItem *pItem = qobject_cast<VideoItem *>(item);
        if (pItem) {
            if (pItem->m_videoWidth == 0 || pItem->m_videoHeight == 0) {
                return;
            }
            if(pItem->yuvData.Y.size()<=0 || pItem->yuvData.U.size()<=0 || pItem->yuvData.V.size()<=0) {
                return;
            }
            if (pItem->videoSizeChanged) {
                m_render.updateTextureInfo(pItem->m_videoWidth, pItem->m_videoHeight);
                pItem->videoSizeChanged = false;
            }

            m_render.updateTextureData(pItem->yuvData);
        }
    }
private:
    I420Render m_render;
    YUVData ba;
};

VideoItem::VideoItem(QQuickItem *parent) : QQuickFramebufferObject(parent)
{


}

void VideoItem::onVideoSizeChanged(int w, int h)
{
    qDebug() << "VideoItem::onVideoSizeChanged" << w << h;
    m_videoWidth = w;
    m_videoHeight = h;
    videoSizeChanged = true;
}

void VideoItem::onReceiveVideoData(const YUVData &data)
{
    yuvData = data;
    update();
}

QQuickFramebufferObject::Renderer *VideoItem::createRenderer() const
{
    return new VideoFboItem;
}
