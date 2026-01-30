#ifndef CAMERACONTROLLER_H
#define CAMERACONTROLLER_H

#include <QObject>
#include <QImage>
#include "v4l2camera.h"

class CameraController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QImage frame READ frame NOTIFY frameChanged)
    Q_PROPERTY(bool isRunning READ isRunning NOTIFY runningChanged)
    Q_PROPERTY(QString errorString READ errorString NOTIFY errorOccurred)
    Q_PROPERTY(int currentCamera READ currentCamera NOTIFY cameraChanged)
    Q_PROPERTY(int frameRate READ frameRate NOTIFY frameRateChanged)
    Q_PROPERTY(QString debugMessage READ debugMessage NOTIFY debugMessageChanged)

public:
    explicit CameraController(QObject *parent = nullptr);
    ~CameraController();

    Q_INVOKABLE void startCamera(int cameraIndex = 0);
    Q_INVOKABLE void stopCamera();
    Q_INVOKABLE void selectCamera(int index);
    Q_INVOKABLE void switchCamera();
    Q_INVOKABLE void setPixelFormat(int format);

    QImage frame() const { return m_currentFrame; }
    bool isRunning() const { return m_camera && m_camera->isRunning(); }
    QString errorString() const { return m_errorString; }
    int currentCamera() const { return m_currentCameraIndex; }
    int frameRate() const { return m_frameRate; }
    QString debugMessage() const { return m_debugMessage; }

signals:
    void frameChanged(const QImage &frame);
    void runningChanged(bool running);
    void errorOccurred(const QString &error);
    void cameraChanged(int camera);
    void frameRateChanged(int frameRate);
    void debugMessageChanged(const QString &message);

private slots:
    void onFrameAvailable(const QImage &frame);
    void onErrorOccurred(const QString &error);
    void onFrameRateChanged(int frameRate);
    void onDebugMessage(const QString &message);

private:
    V4L2Camera *m_camera = nullptr;
    QImage m_currentFrame;
    QString m_errorString;
    QString m_debugMessage;
    int m_currentCameraIndex = 0;
    int m_frameRate = 0;
};

#endif // CAMERACONTROLLER_H
