#include "v4l2camera.h"
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <errno.h>
#include <QDebug>

// helper ioctl loop
static int xioctl(int fd, unsigned long request, void *arg)
{
    int r;
    do {
        r = ioctl(fd, request, arg);
    } while (r == -1 && errno == EINTR);
    return r;
}

V4L2Camera::V4L2Camera(const QString &device, int width, int height, QObject *parent)
    : QThread(parent), m_device(device), m_width(width), m_height(height)
{
}

V4L2Camera::~V4L2Camera()
{
    stopCapture();
    wait();
}

void V4L2Camera::stopCapture()
{
    m_running = false;
}

void V4L2Camera::run()
{
    if (!openDevice()) {
        emit errorOccurred(QString("Cannot open %1").arg(m_device));
        return;
    }
    if (!queryCaps()) {
        closeDevice();
        return;
    }
    if (!initFormat()) {
        closeDevice();
        return;
    }
    if (!initMmap()) {
        closeDevice();
        return;
    }
    if (!startStreaming()) {
        uninitMmap();
        closeDevice();
        return;
    }

    m_running = true;
    while (m_running) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(m_fd, &fds);
        timeval tv{};
        tv.tv_sec = 2;
        tv.tv_usec = 0;
        int r = select(m_fd + 1, &fds, nullptr, nullptr, &tv);
        if (r == -1) {
            if (errno == EINTR) continue;
            emit errorOccurred(QString("select error: %1").arg(strerror(errno)));
            break;
        } else if (r == 0) {
            // timeout, continue
            continue;
        }

        if (!readOneFrame()) {
            // small sleep to avoid busy looping if EAGAIN
            msleep(2);
        }
    }

    stopStreaming();
    uninitMmap();
    closeDevice();
}

bool V4L2Camera::openDevice()
{
    m_fd = ::open(m_device.toLocal8Bit().constData(), O_RDWR | O_NONBLOCK, 0);
    if (m_fd < 0) {
        emit errorOccurred(QString("open(%1) failed: %2").arg(m_device).arg(strerror(errno)));
        return false;
    }
    return true;
}

void V4L2Camera::closeDevice()
{
    if (m_fd >= 0) {
        ::close(m_fd);
        m_fd = -1;
    }
}

bool V4L2Camera::queryCaps()
{
    v4l2_capability cap;
    if (xioctl(m_fd, VIDIOC_QUERYCAP, &cap) == -1) {
        emit errorOccurred(QString("VIDIOC_QUERYCAP failed: %1").arg(strerror(errno)));
        return false;
    }
    if (cap.capabilities & V4L2_CAP_VIDEO_CAPTURE_MPLANE) {
        m_is_mplane = true;
    } else if (cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) {
        m_is_mplane = false;
    } else {
        emit errorOccurred("Device does not support video capture");
        return false;
    }

    if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
        emit errorOccurred("Device does not support streaming I/O");
        return false;
    }
    return true;
}

bool V4L2Camera::trySetFormatSingle(uint32_t pixfmt)
{
    v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = m_width;
    fmt.fmt.pix.height = m_height;
    fmt.fmt.pix.pixelformat = pixfmt;
    fmt.fmt.pix.field = V4L2_FIELD_ANY;

    if (xioctl(m_fd, VIDIOC_S_FMT, &fmt) == -1) {
        return false;
    }
    m_width = fmt.fmt.pix.width;
    m_height = fmt.fmt.pix.height;
    m_pixfmt = fmt.fmt.pix.pixelformat;
    m_num_planes = 1;
    qDebug() << "Selected single-planar format" << m_pixfmt << " size " << m_width << "x" << m_height;
    return true;
}

bool V4L2Camera::trySetFormatMPlane(uint32_t pixfmt)
{
    v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    fmt.fmt.pix_mp.width = m_width;
    fmt.fmt.pix_mp.height = m_height;
    fmt.fmt.pix_mp.pixelformat = pixfmt;
    fmt.fmt.pix_mp.field = V4L2_FIELD_ANY;

    if (xioctl(m_fd, VIDIOC_S_FMT, &fmt) == -1) {
        return false;
    }
    m_width = fmt.fmt.pix_mp.width;
    m_height = fmt.fmt.pix_mp.height;
    m_pixfmt = fmt.fmt.pix_mp.pixelformat;
    m_num_planes = fmt.fmt.pix_mp.num_planes;
    if (m_num_planes <= 0) m_num_planes = 2; // safe default
    qDebug() << "Selected mplane format" << m_pixfmt << " size " << m_width << "x" << m_height << " planes=" << m_num_planes;
    return true;
}

bool V4L2Camera::initFormat()
{
    // try common formats (NV12, NV21, UYVY, YUYV)
    const uint32_t preferred[] = {
        V4L2_PIX_FMT_NV12, V4L2_PIX_FMT_NV21, V4L2_PIX_FMT_UYVY, V4L2_PIX_FMT_YUYV
    };

    if (m_is_mplane) {
        for (uint32_t f : preferred) {
            if (trySetFormatMPlane(f)) return true;
        }
        // fallback: get current format
        v4l2_format fmt;
        memset(&fmt,0,sizeof(fmt));
        fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        if (xioctl(m_fd, VIDIOC_G_FMT, &fmt) == -1) {
            emit errorOccurred("VIDIOC_G_FMT (mplane) failed");
            return false;
        }
        m_width = fmt.fmt.pix_mp.width;
        m_height = fmt.fmt.pix_mp.height;
        m_pixfmt = fmt.fmt.pix_mp.pixelformat;
        m_num_planes = fmt.fmt.pix_mp.num_planes;
        if (m_num_planes <= 0) m_num_planes = 2;
        qDebug() << "Fallback mplane format" << m_pixfmt << " size " << m_width << "x" << m_height << " planes=" << m_num_planes;
        return true;
    } else {
        for (uint32_t f : preferred) {
            if (trySetFormatSingle(f)) return true;
        }
        // fallback: get current single-planar format
        v4l2_format fmt;
        memset(&fmt,0,sizeof(fmt));
        fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (xioctl(m_fd, VIDIOC_G_FMT, &fmt) == -1) {
            emit errorOccurred("VIDIOC_G_FMT failed");
            return false;
        }
        m_width = fmt.fmt.pix.width;
        m_height = fmt.fmt.pix.height;
        m_pixfmt = fmt.fmt.pix.pixelformat;
        m_num_planes = 1;
        qDebug() << "Fallback single-planar format" << m_pixfmt << " size " << m_width << "x" << m_height;
        return true;
    }
}

bool V4L2Camera::initMmap()
{
    v4l2_requestbuffers req;
    memset(&req,0,sizeof(req));
    req.count = 4;
    req.memory = V4L2_MEMORY_MMAP;
    req.type = m_is_mplane ? V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE : V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (xioctl(m_fd, VIDIOC_REQBUFS, &req) == -1) {
        emit errorOccurred(QString("VIDIOC_REQBUFS failed: %1").arg(strerror(errno)));
        return false;
    }
    if (req.count < 2) {
        emit errorOccurred("Insufficient buffer memory");
        return false;
    }

    m_buffers.clear();
    m_buffers.resize(req.count);

    for (uint32_t i = 0; i < (uint32_t)req.count; ++i) {
        if (m_is_mplane) {
            // multi-planar: query buffer with plane info
            v4l2_buffer buf;
            memset(&buf, 0, sizeof(buf));
            buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
            buf.memory = V4L2_MEMORY_MMAP;
            buf.index = i;

            // prepare planes array to receive info
            v4l2_plane planes[VIDEO_MAX_PLANES];
            memset(planes, 0, sizeof(planes));
            buf.m.planes = planes;
            buf.length = VIDEO_MAX_PLANES;

            if (xioctl(m_fd, VIDIOC_QUERYBUF, &buf) == -1) {
                emit errorOccurred(QString("VIDIOC_QUERYBUF (mplane) failed: %1").arg(strerror(errno)));
                return false;
            }

            // number of planes filled by driver is in buf.length OR m_num_planes; use min
            int planes_count = buf.length;
            if (planes_count <= 0) planes_count = m_num_planes;
            if (planes_count > VIDEO_MAX_PLANES) planes_count = VIDEO_MAX_PLANES;

            m_buffers[i].starts.resize(planes_count);
            m_buffers[i].lengths.resize(planes_count);

            for (int p = 0; p < planes_count; ++p) {
                size_t len = planes[p].length;
                off_t off = planes[p].m.mem_offset;
                void *start = mmap(nullptr, len, PROT_READ | PROT_WRITE, MAP_SHARED, m_fd, off);
                if (start == MAP_FAILED) {
                    emit errorOccurred(QString("mmap plane %1 failed: %2").arg(p).arg(strerror(errno)));
                    // unmap previously mapped planes for this buffer
                    for (int q = 0; q < p; ++q) {
                        if (m_buffers[i].starts[q]) munmap(m_buffers[i].starts[q], m_buffers[i].lengths[q]);
                        m_buffers[i].starts[q] = nullptr;
                        m_buffers[i].lengths[q] = 0;
                    }
                    return false;
                }
                m_buffers[i].starts[p] = start;
                m_buffers[i].lengths[p] = len;
            }
        } else {
            // single-planar
            v4l2_buffer buf;
            memset(&buf, 0, sizeof(buf));
            buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buf.memory = V4L2_MEMORY_MMAP;
            buf.index = i;

            if (xioctl(m_fd, VIDIOC_QUERYBUF, &buf) == -1) {
                emit errorOccurred(QString("VIDIOC_QUERYBUF failed: %1").arg(strerror(errno)));
                return false;
            }

            m_buffers[i].starts.resize(1);
            m_buffers[i].lengths.resize(1);
            m_buffers[i].lengths[0] = buf.length;
            m_buffers[i].starts[0] = mmap(nullptr, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, m_fd, buf.m.offset);
            if (m_buffers[i].starts[0] == MAP_FAILED) {
                emit errorOccurred(QString("mmap failed: %1").arg(strerror(errno)));
                return false;
            }
        }
    }

    // queue buffers
    for (uint32_t i = 0; i < (uint32_t)m_buffers.size(); ++i) {
        if (m_is_mplane) {
            v4l2_buffer buf;
            memset(&buf, 0, sizeof(buf));
            buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
            buf.memory = V4L2_MEMORY_MMAP;
            buf.index = i;

            // prepare empty planes array; driver doesn't require mem_offset here for mmap requeue
            v4l2_plane planes[VIDEO_MAX_PLANES];
            memset(planes,0,sizeof(planes));
            buf.m.planes = planes;
            buf.length = m_num_planes;

            if (xioctl(m_fd, VIDIOC_QBUF, &buf) == -1) {
                emit errorOccurred(QString("VIDIOC_QBUF (mplane) failed: %1").arg(strerror(errno)));
                return false;
            }
        } else {
            v4l2_buffer buf;
            memset(&buf, 0, sizeof(buf));
            buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buf.memory = V4L2_MEMORY_MMAP;
            buf.index = i;
            if (xioctl(m_fd, VIDIOC_QBUF, &buf) == -1) {
                emit errorOccurred(QString("VIDIOC_QBUF failed: %1").arg(strerror(errno)));
                return false;
            }
        }
    }
    return true;
}

void V4L2Camera::uninitMmap()
{
    for (auto &b : m_buffers) {
        for (size_t p = 0; p < b.starts.size(); ++p) {
            if (b.starts[p] && b.lengths[p]) {
                munmap(b.starts[p], b.lengths[p]);
            }
            b.starts[p] = nullptr;
            b.lengths[p] = 0;
        }
    }
    m_buffers.clear();
}

bool V4L2Camera::startStreaming()
{
    v4l2_buf_type type = m_is_mplane ? V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE : V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (xioctl(m_fd, VIDIOC_STREAMON, &type) == -1) {
        emit errorOccurred(QString("VIDIOC_STREAMON failed: %1").arg(strerror(errno)));
        return false;
    }
    return true;
}

void V4L2Camera::stopStreaming()
{
    if (m_fd < 0) return;
    v4l2_buf_type type = m_is_mplane ? V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE : V4L2_BUF_TYPE_VIDEO_CAPTURE;
    xioctl(m_fd, VIDIOC_STREAMOFF, &type);
}

// small helpers for conversion
static inline uchar clamp255(int v) {
    if (v < 0) return 0;
    if (v > 255) return 255;
    return (uchar)v;
}
static inline void yuvToRgbPixel(int y, int u, int v, uchar &r, uchar &g, uchar &b)
{
    int c = y - 16;
    int d = u - 128;
    int e = v - 128;
    int rr = (298 * c + 409 * e + 128) >> 8;
    int gg = (298 * c - 100 * d - 208 * e + 128) >> 8;
    int bb = (298 * c + 516 * d + 128) >> 8;
    r = clamp255(rr);
    g = clamp255(gg);
    b = clamp255(bb);
}

bool V4L2Camera::readOneFrame()
{
    if (m_is_mplane) {
        v4l2_buffer buf;
        memset(&buf,0,sizeof(buf));
        v4l2_plane planes[VIDEO_MAX_PLANES];
        memset(planes,0,sizeof(planes));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.m.planes = planes;
        buf.length = VIDEO_MAX_PLANES;

        if (xioctl(m_fd, VIDIOC_DQBUF, &buf) == -1) {
            if (errno == EAGAIN) return false;
            emit errorOccurred(QString("VIDIOC_DQBUF (mplane) failed: %1").arg(strerror(errno)));
            return false;
        }

        int idx = buf.index;
        int planes_count = buf.length;
        if (planes_count <= 0) planes_count = m_num_planes;
        if ((size_t)idx >= m_buffers.size()) {
            // defensive
            if (xioctl(m_fd, VIDIOC_QBUF, &buf) == -1) {
                emit errorOccurred(QString("VIDIOC_QBUF (requeue invalid idx) failed: %1").arg(strerror(errno)));
            }
            return false;
        }

        QImage out(m_width, m_height, QImage::Format_RGB888);

        // handle NV12 / NV21 common mplane layout: plane0 = Y, plane1 = interleaved UV
        const unsigned char *yPlane = nullptr;
        const unsigned char *uvPlane = nullptr;
        if (m_buffers[idx].starts.size() > 0) yPlane = static_cast<const unsigned char*>(m_buffers[idx].starts[0]);
        if (m_buffers[idx].starts.size() > 1) uvPlane = static_cast<const unsigned char*>(m_buffers[idx].starts[1]);

        bool isNV21 = (m_pixfmt == V4L2_PIX_FMT_NV21);
        if ((m_pixfmt == V4L2_PIX_FMT_NV12 || m_pixfmt == V4L2_PIX_FMT_NV21) && yPlane && uvPlane) {
            for (int row = 0; row < m_height; ++row) {
                uchar *dst = out.scanLine(row);
                int yRow = row * m_width;
                int uvRow = (row / 2) * m_width;
                for (int col = 0; col < m_width; ++col) {
                    int y = yPlane[yRow + col];
                    int uvIndex = uvRow + (col & ~1);
                    int u = uvPlane[uvIndex + (isNV21 ? 1 : 0)];
                    int v = uvPlane[uvIndex + (isNV21 ? 0 : 1)];
                    uchar r,g,b;
                    yuvToRgbPixel(y,u,v,r,g,b);
                    dst[col*3 + 0] = r;
                    dst[col*3 + 1] = g;
                    dst[col*3 + 2] = b;
                }
            }
        } else {
            // fallback -> grayscale from first plane
            if (!yPlane && m_buffers[idx].starts.size() > 0) {
                yPlane = static_cast<const unsigned char*>(m_buffers[idx].starts[0]);
            }
            if (yPlane) {
                for (int row = 0; row < m_height; ++row) {
                    uchar *dst = out.scanLine(row);
                    int yRow = row * m_width;
                    for (int col = 0; col < m_width; ++col) {
                        int y = yPlane[yRow + col];
                        dst[col*3 + 0] = y;
                        dst[col*3 + 1] = y;
                        dst[col*3 + 2] = y;
                    }
                }
            }
        }

        emit frameReady(out);

        // requeue
        buf.m.planes = planes;
        buf.length = planes_count;
        if (xioctl(m_fd, VIDIOC_QBUF, &buf) == -1) {
            emit errorOccurred(QString("VIDIOC_QBUF (requeue mplane) failed: %1").arg(strerror(errno)));
            return false;
        }
        return true;
    } else {
        // single-planar path
        v4l2_buffer buf;
        memset(&buf,0,sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;

        if (xioctl(m_fd, VIDIOC_DQBUF, &buf) == -1) {
            if (errno == EAGAIN) return false;
            emit errorOccurred(QString("VIDIOC_DQBUF failed: %1").arg(strerror(errno)));
            return false;
        }

        int idx = buf.index;
        if ((size_t)idx >= m_buffers.size()) {
            if (xioctl(m_fd, VIDIOC_QBUF, &buf) == -1) {
                emit errorOccurred(QString("VIDIOC_QBUF (requeue invalid idx) failed: %1").arg(strerror(errno)));
            }
            return false;
        }

        const unsigned char *data = static_cast<const unsigned char*>(m_buffers[idx].starts[0]);

        QImage out(m_width, m_height, QImage::Format_RGB888);

        if (m_pixfmt == V4L2_PIX_FMT_NV12 || m_pixfmt == V4L2_PIX_FMT_NV21) {
            const unsigned char *yPlane = data;
            const unsigned char *uvPlane = data + (m_width * m_height);
            bool isNV21 = (m_pixfmt == V4L2_PIX_FMT_NV21);

            for (int row = 0; row < m_height; ++row) {
                uchar *dst = out.scanLine(row);
                const int yRow = row * m_width;
                const int uvRow = (row / 2) * m_width;
                for (int col = 0; col < m_width; ++col) {
                    int y = yPlane[yRow + col];
                    int uvIndex = uvRow + (col & ~1);
                    int u = uvPlane[uvIndex + (isNV21 ? 1 : 0)];
                    int v = uvPlane[uvIndex + (isNV21 ? 0 : 1)];
                    uchar r,g,b;
                    yuvToRgbPixel(y,u,v,r,g,b);
                    dst[col*3 + 0] = r;
                    dst[col*3 + 1] = g;
                    dst[col*3 + 2] = b;
                }
            }
        } else if (m_pixfmt == V4L2_PIX_FMT_UYVY || m_pixfmt == V4L2_PIX_FMT_YUYV) {
            const unsigned char *p = data;
            for (int row = 0; row < m_height; ++row) {
                const unsigned char *rowPtr = p + row * m_width * 2;
                uchar *dst = out.scanLine(row);
                for (int col = 0; col < m_width; col += 2) {
                    int A = rowPtr[(col/2)*4 + 0];
                    int B = rowPtr[(col/2)*4 + 1];
                    int C = rowPtr[(col/2)*4 + 2];
                    int D = rowPtr[(col/2)*4 + 3];
                    int U, Y0, V, Y1;
                    if (m_pixfmt == V4L2_PIX_FMT_UYVY) {
                        U = A; Y0 = B; V = C; Y1 = D;
                    } else { // YUYV
                        Y0 = A; U = B; Y1 = C; V = D;
                    }
                    uchar r,g,b;
                    yuvToRgbPixel(Y0, U, V, r, g, b);
                    dst[col*3 + 0] = r; dst[col*3 + 1] = g; dst[col*3 + 2] = b;
                    yuvToRgbPixel(Y1, U, V, r, g, b);
                    dst[(col+1)*3 + 0] = r; dst[(col+1)*3 + 1] = g; dst[(col+1)*3 + 2] = b;
                }
            }
        } else {
            // fallback grayscale
            const unsigned char *yPlane = data;
            for (int row = 0; row < m_height; ++row) {
                uchar *dst = out.scanLine(row);
                for (int col = 0; col < m_width; ++col) {
                    uchar y = yPlane[row * m_width + col];
                    dst[col*3 + 0] = y;
                    dst[col*3 + 1] = y;
                    dst[col*3 + 2] = y;
                }
            }
        }

        emit frameReady(out);

        if (xioctl(m_fd, VIDIOC_QBUF, &buf) == -1) {
            emit errorOccurred(QString("VIDIOC_QBUF(requeue) failed: %1").arg(strerror(errno)));
            return false;
        }
        return true;
    }
}
