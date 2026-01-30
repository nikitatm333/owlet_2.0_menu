#pragma once

#include <QThread>
#include <QImage>
#include <QString>
#include <QMutex>
#include <vector>
#include <atomic>

class V4L2Camera : public QThread
{
    Q_OBJECT
public:
    explicit V4L2Camera(const QString &device = "/dev/video0", int width = 1280, int height = 720, QObject *parent = nullptr);
    ~V4L2Camera() override;

    void stopCapture();

    int width() const { return m_width; }
    int height() const { return m_height; }
    QString deviceName() const { return m_device; }

signals:
    void frameReady(const QImage &img);
    void errorOccurred(const QString &message);

protected:
    void run() override;

private:
    bool openDevice();
    void closeDevice();
    bool queryCaps();
    bool trySetFormatSingle(uint32_t pixfmt);
    bool trySetFormatMPlane(uint32_t pixfmt);
    bool initFormat();
    bool initMmap();
    void uninitMmap();
    bool startStreaming();
    void stopStreaming();
    bool readOneFrame();

    QString m_device;
    int m_width;
    int m_height;
    int m_fd{-1};
    std::atomic<bool> m_running{false};

    // If the device is multplane, we set this true and use VIDEO_CAPTURE_MPLANE ioctls
    bool m_is_mplane{false};
    int m_num_planes{0};
    uint32_t m_pixfmt{0};

    struct Buffer {
        // for single-planar: starts.size()==1, lengths[0] valid
        // for multplane: starts.size()==m_num_planes
        std::vector<void*> starts;
        std::vector<size_t> lengths;
    };
    std::vector<Buffer> m_buffers;
};
