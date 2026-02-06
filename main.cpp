#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickImageProvider>
#include <QImage>
#include <QMutex>
#include <QAtomicInt>
#include <QDebug>

#include "v4l2camera.h"

// Image provider that holds the latest frame (thread-safe)
class CameraImageProvider : public QQuickImageProvider
{
public:
    CameraImageProvider()
        : QQuickImageProvider(QQuickImageProvider::Image)
    {}

    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override
    {
        Q_UNUSED(id)
        Q_UNUSED(requestedSize)
        QMutexLocker locker(&m_mutex);
        if (m_image.isNull()) {
            return QImage();
        }
        if (size) *size = m_image.size();
        return m_image;
    }

    void setImage(const QImage &img)
    {
        QMutexLocker locker(&m_mutex);
        m_image = img;
    }

private:
    QImage m_image;
    QMutex m_mutex;
};

// small helper object exposed to QML to force image reloads (token changes with every frame)
class CameraTokenObject : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString token READ token NOTIFY tokenChanged)
public:
    CameraTokenObject(QObject *parent = nullptr) : QObject(parent) {}

    QString token() const { return m_token; }

public slots:
    void updateToken()
    {
        int t = m_counter.fetchAndAddRelaxed(1) + 1;
        m_token = QString::number(t);
        emit tokenChanged();
    }

signals:
    void tokenChanged();

private:
    QString m_token;
    QAtomicInt m_counter{0};
};

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);

    QQmlApplicationEngine engine;

    // create camera and image provider
    V4L2Camera *cam = new V4L2Camera("/dev/video0", 1280, 720);
    CameraImageProvider *provider = new CameraImageProvider();
    CameraTokenObject *tokenObj = new CameraTokenObject();

    // add provider under "camera"
    engine.addImageProvider(QLatin1String("camera"), provider);

    // expose token object and camera as context properties
    engine.rootContext()->setContextProperty("cameraObj", tokenObj);
    engine.rootContext()->setContextProperty("v4l2Camera", cam);

    // connect camera frames -> provider storage + token update
    // IMPORTANT: use tokenObj (QObject*) as context for the lambda (provider is NOT a QObject)
    QObject::connect(cam, &V4L2Camera::frameReady, tokenObj,
                     [provider, tokenObj](const QImage &img){
                         provider->setImage(img);
                         tokenObj->updateToken();
                     });

    QObject::connect(cam, &V4L2Camera::errorOccurred, [](const QString &msg){
        qWarning() << "Camera error:" << msg;
    });

    // start camera thread
    cam->start();

    // try loading from resource first (if you use qml.qrc), else load local file
    engine.load(QUrl(QStringLiteral("qrc:/main.qml")));
    if (engine.rootObjects().isEmpty()) {
        // fallback to local file (when running from build directory)
        engine.load(QUrl::fromLocalFile(QStringLiteral("main.qml")));
    }

    int ret = app.exec();

    cam->stopCapture();
    cam->wait();
    delete cam;
    return ret;
}

#include "main.moc"
