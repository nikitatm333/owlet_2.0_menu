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

    // QML-callable:
    Q_INVOKABLE void requestInputSwitch();        // toggle 0 <-> 1
    Q_INVOKABLE void requestInputSwitchTo(int idx); // switch to specific input index

signals:
    void frameReady(const QImage &img);
    void errorOccurred(const QString &message);
    void inputChanged(int newInput); // emitted on successful switch

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

    // Called inside camera thread to perform the requested input change.
    void handlePendingInputChange();

    QString m_device;
    int m_width;
    int m_height;
    int m_fd{-1};
    std::atomic<bool> m_running{false};

    // video format & buffers
    bool m_is_mplane{false};
    int m_num_planes{0};
    uint32_t m_pixfmt{0};

    struct Buffer {
        std::vector<void*> starts;
        std::vector<size_t> lengths;
    };
    std::vector<Buffer> m_buffers;

    // --- input switching control ---
    // -1 = no request, -2 = toggle, >=0 specific input index
    std::atomic<int> m_requested_input{-1};
    std::atomic<int> m_current_input{-1}; // last-applied input (-1 = unknown)
    QMutex m_reconf_mutex; // protects the reconfigure sequence
};
