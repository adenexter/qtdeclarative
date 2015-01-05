/****************************************************************************
**
** Copyright (C) 2013 Digia Plc and/or its subsidiary(-ies).
** Contact: http://www.qt-project.org/legal
**
** This file is part of the QtQuick module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and Digia.  For licensing terms and
** conditions see http://qt.digia.com/licensing.  For further information
** use the contact form at http://qt.digia.com/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU Lesser General Public License version 2.1 requirements
** will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Digia gives you certain additional
** rights.  These rights are described in the Digia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3.0 as published by the Free Software
** Foundation and appearing in the file LICENSE.GPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU General Public License version 3.0 requirements will be
** met: http://www.gnu.org/copyleft/gpl.html.
**
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include "qsgrenderloop_p.h"
#include "qsgthreadedrenderloop_p.h"
#include "qsgwindowsrenderloop_p.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QTime>
#include <QtCore/private/qabstractanimation_p.h>

#include <QtGui/QOpenGLContext>
#include <QtGui/private/qguiapplication_p.h>
#include <qpa/qplatformintegration.h>

#include <QtQml/private/qqmlglobal_p.h>

#include <QtQuick/QQuickWindow>
#include <QtQuick/private/qquickwindow_p.h>
#include <QtQuick/private/qsgcontext_p.h>
#include <private/qquickprofiler_p.h>

QT_BEGIN_NAMESPACE


extern Q_GUI_EXPORT QImage qt_gl_read_framebuffer(const QSize &size, bool alpha_format, bool include_alpha);

/*!
    expectations for this manager to work:
     - one opengl context to render multiple windows
     - OpenGL pipeline will not block for vsync in swap
     - OpenGL pipeline will block based on a full buffer queue.
     - Multiple screens can share the OpenGL context
     - Animations are advanced for all windows once per swap
 */

DEFINE_BOOL_CONFIG_OPTION(qmlNoThreadedRenderer, QML_BAD_GUI_RENDER_LOOP);
DEFINE_BOOL_CONFIG_OPTION(qmlForceThreadedRenderer, QML_FORCE_THREADED_RENDERER); // Might trigger graphics driver threading bugs, use at own risk

QSGRenderLoop *QSGRenderLoop::s_instance = 0;

QSGRenderLoop::~QSGRenderLoop()
{
}

void QSGRenderLoop::cleanup()
{
    if (!s_instance)
        return;
    foreach (QQuickWindow *w, s_instance->windows()) {
        QQuickWindowPrivate *wd = QQuickWindowPrivate::get(w);
        if (wd->windowManager == s_instance) {
           s_instance->windowDestroyed(w);
           wd->windowManager = 0;
        }
    }
    delete s_instance;
    s_instance = 0;
}

class QSGGuiThreadRenderLoop : public QSGRenderLoop
{
    Q_OBJECT
public:
    QSGGuiThreadRenderLoop();
    ~QSGGuiThreadRenderLoop();

    void show(QQuickWindow *window);
    void hide(QQuickWindow *window);

    void windowDestroyed(QQuickWindow *window);

    void renderWindow(QQuickWindow *window);
    void exposureChanged(QQuickWindow *window);
    QImage grab(QQuickWindow *window);

    void maybeUpdate(QQuickWindow *window);
    void update(QQuickWindow *window) { maybeUpdate(window); } // identical for this implementation.

    void releaseResources(QQuickWindow *) { }

    QAnimationDriver *animationDriver() const { return 0; }

    QSGContext *sceneGraphContext() const;
    QSGRenderContext *createRenderContext(QSGContext *) const { return rc; }

    bool event(QEvent *);

    struct WindowData {
        bool updatePending : 1;
        bool grabOnly : 1;
    };

    QHash<QQuickWindow *, WindowData> m_windows;

    QOpenGLContext *gl;
    QSGContext *sg;
    QSGRenderContext *rc;

    QImage grabContent;
    int m_update_timer;

    bool eventPending;
};

QSGRenderLoop *QSGRenderLoop::instance()
{
    if (!s_instance) {

        // For compatibility with 5.3 and earlier's QSG_INFO environment variables
        if (qEnvironmentVariableIsSet("QSG_INFO"))
            ((QLoggingCategory &) QSG_LOG_INFO()).setEnabled(QtDebugMsg, true);

        s_instance = QSGContext::createWindowManager();

        if (!s_instance) {

            enum RenderLoopType {
                BasicRenderLoop,
                ThreadedRenderLoop,
                WindowsRenderLoop
            };

            RenderLoopType loopType = BasicRenderLoop;

#ifdef Q_OS_WIN
            loopType = WindowsRenderLoop;
#else
            if (QGuiApplicationPrivate::platformIntegration()->hasCapability(QPlatformIntegration::ThreadedOpenGL))
                loopType = ThreadedRenderLoop;
#endif
            if (qmlNoThreadedRenderer())
                loopType = BasicRenderLoop;
            else if (qmlForceThreadedRenderer())
                loopType = ThreadedRenderLoop;

            const QByteArray loopName = qgetenv("QSG_RENDER_LOOP");
            if (loopName == QByteArrayLiteral("windows"))
                loopType = WindowsRenderLoop;
            else if (loopName == QByteArrayLiteral("basic"))
                loopType = BasicRenderLoop;
            else if (loopName == QByteArrayLiteral("threaded"))
                loopType = ThreadedRenderLoop;

            switch (loopType) {
            case ThreadedRenderLoop:
                qCDebug(QSG_LOG_INFO, "threaded render loop");
                s_instance = new QSGThreadedRenderLoop();
                break;
            case WindowsRenderLoop:
                qCDebug(QSG_LOG_INFO, "windows render loop");
                s_instance = new QSGWindowsRenderLoop();
                break;
            default:
                qCDebug(QSG_LOG_INFO, "QSG: basic render loop");
                s_instance = new QSGGuiThreadRenderLoop();
                break;
            }
        }

        qAddPostRoutine(QSGRenderLoop::cleanup);
    }
    return s_instance;
}

void QSGRenderLoop::setInstance(QSGRenderLoop *instance)
{
    Q_ASSERT(!s_instance);
    s_instance = instance;
}

QSGGuiThreadRenderLoop::QSGGuiThreadRenderLoop()
    : gl(0)
    , eventPending(false)
{
    sg = QSGContext::createDefaultContext();
    rc = sg->createRenderContext();
}

QSGGuiThreadRenderLoop::~QSGGuiThreadRenderLoop()
{
    delete rc;
    delete sg;
}

void QSGGuiThreadRenderLoop::show(QQuickWindow *window)
{
    WindowData data;
    data.updatePending = false;
    data.grabOnly = false;
    m_windows[window] = data;

    maybeUpdate(window);
}

void QSGGuiThreadRenderLoop::hide(QQuickWindow *window)
{
    if (!m_windows.contains(window))
        return;

    m_windows.remove(window);
    QQuickWindowPrivate *cd = QQuickWindowPrivate::get(window);
    if (gl)
        gl->makeCurrent(window);
    cd->cleanupNodesOnShutdown();

    if (m_windows.size() == 0) {
        if (!cd->persistentSceneGraph) {
            rc->invalidate();
            QCoreApplication::sendPostedEvents(0, QEvent::DeferredDelete);
            if (!cd->persistentGLContext) {
                delete gl;
                gl = 0;
            }
        }
    }
}

void QSGGuiThreadRenderLoop::windowDestroyed(QQuickWindow *window)
{
    hide(window);
    if (m_windows.size() == 0) {
        rc->invalidate();
        QCoreApplication::sendPostedEvents(0, QEvent::DeferredDelete);
        delete gl;
        gl = 0;
    } else if (window == gl->surface()) {
        gl->doneCurrent();
    }
}

void QSGGuiThreadRenderLoop::renderWindow(QQuickWindow *window)
{
    QQuickWindowPrivate *cd = QQuickWindowPrivate::get(window);
    if (!cd->isRenderable() || !m_windows.contains(window))
        return;

    WindowData &data = const_cast<WindowData &>(m_windows[window]);

    bool current = false;

    if (!gl) {
        gl = new QOpenGLContext();
        gl->setFormat(window->requestedFormat());
        if (!gl->create()) {
            qWarning("QtQuick: failed to create OpenGL context");
            delete gl;
            gl = 0;
        } else {
            current = gl->makeCurrent(window);
        }
        if (current)
            cd->context->initialize(gl);
    } else {
        current = gl->makeCurrent(window);
    }

    bool alsoSwap = data.updatePending;
    data.updatePending = false;

    if (!current)
        return;

    if (!data.grabOnly) {
        cd->flushDelayedTouchEvent();
        // Event delivery/processing triggered the window to be deleted or stop rendering.
        if (!m_windows.contains(window))
            return;
    }
    cd->polishItems();

    QElapsedTimer renderTimer;
    qint64 renderTime = 0, syncTime = 0, polishTime = 0;
    bool profileFrames = QSG_LOG_TIME_RENDERLOOP().isDebugEnabled() || QQuickProfiler::enabled;
    if (profileFrames)
        renderTimer.start();

    cd->polishItems();

    if (profileFrames)
        polishTime = renderTimer.nsecsElapsed();

    cd->syncSceneGraph();

    if (profileFrames)
        syncTime = renderTimer.nsecsElapsed();

    cd->renderSceneGraph(window->size());

    if (profileFrames)
        renderTime = renderTimer.nsecsElapsed();

    if (data.grabOnly) {
        grabContent = qt_gl_read_framebuffer(window->size(), false, false);
        data.grabOnly = false;
    }

    if (alsoSwap && window->isVisible()) {
        if (!cd->customRenderStage || !cd->customRenderStage->swap())
            gl->swapBuffers(window);
        cd->fireFrameSwapped();
    }

    qint64 swapTime = 0;
    if (profileFrames)
        swapTime = renderTimer.nsecsElapsed() - renderTime;

    if (QSG_LOG_TIME_RENDERLOOP().isDebugEnabled()) {
        static QTime lastFrameTime = QTime::currentTime();
        qCDebug(QSG_LOG_TIME_RENDERLOOP,
                "Frame rendered with 'basic' renderloop in %dms, polish=%d, sync=%d, render=%d, swap=%d, frameDelta=%d",
                int(swapTime / 1000000),
                int(polishTime / 1000000),
                int((syncTime - polishTime) / 1000000),
                int((renderTime - syncTime) / 1000000),
                int((swapTime - renderTime) / 10000000),
                int(lastFrameTime.msecsTo(QTime::currentTime())));
        lastFrameTime = QTime::currentTime();
    }

    Q_QUICK_SG_PROFILE1(QQuickProfiler::SceneGraphRenderLoopFrame, (
            syncTime,
            renderTime - syncTime,
            swapTime));

    // Might have been set during syncSceneGraph()
    if (data.updatePending)
        maybeUpdate(window);
}

void QSGGuiThreadRenderLoop::exposureChanged(QQuickWindow *window)
{
    if (window->isExposed()) {
        m_windows[window].updatePending = true;
        renderWindow(window);
    }
}

QImage QSGGuiThreadRenderLoop::grab(QQuickWindow *window)
{
    if (!m_windows.contains(window))
        return QImage();

    m_windows[window].grabOnly = true;

    renderWindow(window);

    QImage grabbed = grabContent;
    grabContent = QImage();
    return grabbed;
}



void QSGGuiThreadRenderLoop::maybeUpdate(QQuickWindow *window)
{
    if (!m_windows.contains(window))
        return;

    m_windows[window].updatePending = true;

    if (!eventPending) {
        const int exhaust_delay = 5;
        m_update_timer = startTimer(exhaust_delay, Qt::PreciseTimer);
        eventPending = true;
    }
}



QSGContext *QSGGuiThreadRenderLoop::sceneGraphContext() const
{
    return sg;
}


bool QSGGuiThreadRenderLoop::event(QEvent *e)
{
    if (e->type() == QEvent::Timer) {
        eventPending = false;
        killTimer(m_update_timer);
        m_update_timer = 0;
        for (QHash<QQuickWindow *, WindowData>::const_iterator it = m_windows.constBegin();
             it != m_windows.constEnd(); ++it) {
            const WindowData &data = it.value();
            if (data.updatePending)
                renderWindow(it.key());
        }
        return true;
    }
    return QObject::event(e);
}

#include "qsgrenderloop.moc"

QT_END_NAMESPACE
