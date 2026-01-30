#include "cameracontroller.h"
#include <QGuiApplication>
#include <QDebug>

CameraController::CameraController(QObject *parent)
    : QObject(parent)
    , m_camera(new V4L2Camera(this))
{
    connect(m_camera, &V4L2Camera::frameAvailable,
            this, &CameraController::onFrameAvailable);
    connect(m_camera, &V4L2Camera::errorOccurred,
            this, &CameraController::onErrorOccurred);
    connect(m_camera, &V4L2Camera::frameRateChanged,
            this, &CameraController::onFrameRateChanged);
    connect(m_camera, &V4L2Camera::debugMessage,
            this, &CameraController::onDebugMessage);

    // Устанавливаем устройство по умолчанию
    m_camera->setDevicePath("/dev/video0");
}

CameraController::~CameraController()
{
    stopCamera();
}

void CameraController::startCamera(int cameraIndex)
{
    qDebug() << "CameraController: startCamera called with index" << cameraIndex;

    if (m_camera->isRunning()) {
        qDebug() << "Camera is already running, stopping first...";
        stopCamera();
    }

    selectCamera(cameraIndex);

    m_camera->setDevicePath("/dev/video0");
    m_camera->setInputIndex(cameraIndex);

    if (m_camera->startCapture()) {
        qDebug() << "Camera capture started successfully";
        emit runningChanged(true);
    } else {
        m_errorString = "Не удалось запустить захват камеры";
        emit errorOccurred(m_errorString);
    }
}

void CameraController::stopCamera()
{
    qDebug() << "CameraController: stopCamera called";

    if (m_camera && m_camera->isRunning()) {
        m_camera->stopCapture();
        emit runningChanged(false);
        m_frameRate = 0;
        emit frameRateChanged(m_frameRate);
    }
}

void CameraController::selectCamera(int index)
{
    if (index < 0 || index > 1) {
        index = 0;
    }

    m_currentCameraIndex = index;
    qDebug() << "Selected camera:" << index;
    emit cameraChanged(index);
}

void CameraController::switchCamera()
{
    int newIndex = (m_currentCameraIndex == 0) ? 1 : 0;
    qDebug() << "Switching camera from" << m_currentCameraIndex << "to" << newIndex;
    startCamera(newIndex);
}

void CameraController::setPixelFormat(int format)
{
    m_camera->setPixelFormat(format);
    qDebug() << "Set pixel format to:" << QString::number(format, 16);
}

void CameraController::onFrameAvailable(const QImage &frame)
{
    m_currentFrame = frame;
    emit frameChanged(frame);
}

void CameraController::onErrorOccurred(const QString &error)
{
    m_errorString = error;
    qWarning() << "Camera error:" << error;
    emit errorOccurred(error);
}

void CameraController::onFrameRateChanged(int frameRate)
{
    m_frameRate = frameRate;
    emit frameRateChanged(frameRate);
}

void CameraController::onDebugMessage(const QString &message)
{
    m_debugMessage = message;
    qDebug() << "Camera debug:" << message;
    emit debugMessageChanged(message);
}
