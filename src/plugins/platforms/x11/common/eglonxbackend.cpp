/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2010, 2012 Martin Gräßlin <mgraesslin@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "eglonxbackend.h"
// kwin
#include "main.h"
#include "options.h"
#include "overlaywindow.h"
#include "platform.h"
#include "xcbutils.h"
// Qt
#include <QLoggingCategory>
#include <QDebug>

Q_LOGGING_CATEGORY(KWIN_CORE, "kwin_core", QtWarningMsg)

namespace KWin
{

EglOnXBackend::EglOnXBackend(Display *display)
    : AbstractEglBackend()
    , m_overlayWindow(kwinApp()->platform()->createOverlayWindow())
    , surfaceHasSubPost(0)
    , m_connection(connection())
    , m_x11Display(display)
    , m_rootWindow(rootWindow())
    , m_x11ScreenNumber(kwinApp()->x11ScreenNumber())
{
    // Egl is always direct rendering
    setIsDirectRendering(true);
}

EglOnXBackend::EglOnXBackend(xcb_connection_t *connection, Display *display, xcb_window_t rootWindow, int screenNumber, xcb_window_t renderingWindow)
    : AbstractEglBackend()
    , m_overlayWindow(nullptr)
    , surfaceHasSubPost(0)
    , m_connection(connection)
    , m_x11Display(display)
    , m_rootWindow(rootWindow)
    , m_x11ScreenNumber(screenNumber)
    , m_renderingWindow(renderingWindow)
{
    // Egl is always direct rendering
    setIsDirectRendering(true);
}

EglOnXBackend::~EglOnXBackend()
{
    if (isFailed() && m_overlayWindow) {
        m_overlayWindow->destroy();
    }
    cleanup();

    if (m_overlayWindow) {
        if (overlayWindow()->window()) {
            overlayWindow()->destroy();
        }
        delete m_overlayWindow;
    }
}

void EglOnXBackend::init()
{
    qputenv("EGL_PLATFORM", "x11");
    if (!initRenderingContext()) {
        setFailed(QStringLiteral("Could not initialize rendering context"));
        return;
    }

    initKWinGL();
    if (!hasExtension(QByteArrayLiteral("EGL_KHR_image")) &&
        (!hasExtension(QByteArrayLiteral("EGL_KHR_image_base")) ||
         !hasExtension(QByteArrayLiteral("EGL_KHR_image_pixmap")))) {
        setFailed(QStringLiteral("Required support for binding pixmaps to EGLImages not found, disabling compositing"));
        return;
    }
    if (!hasGLExtension(QByteArrayLiteral("GL_OES_EGL_image"))) {
        setFailed(QStringLiteral("Required extension GL_OES_EGL_image not found, disabling compositing"));
        return;
    }

    // check for EGL_NV_post_sub_buffer and whether it can be used on the surface
    if (hasExtension(QByteArrayLiteral("EGL_NV_post_sub_buffer"))) {
        if (eglQuerySurface(eglDisplay(), surface(), EGL_POST_SUB_BUFFER_SUPPORTED_NV, &surfaceHasSubPost) == EGL_FALSE) {
            EGLint error = eglGetError();
            if (error != EGL_SUCCESS && error != EGL_BAD_ATTRIBUTE) {
                setFailed(QStringLiteral("query surface failed"));
                return;
            } else {
                surfaceHasSubPost = EGL_FALSE;
            }
        }
    }

    if (surfaceHasSubPost) {
        qCDebug(KWIN_CORE) << "EGL implementation and surface support eglPostSubBufferNV, let's use it";

        // check if swap interval 1 is supported
        EGLint val;
        eglGetConfigAttrib(eglDisplay(), config(), EGL_MAX_SWAP_INTERVAL, &val);
        if (val >= 1) {
            if (eglSwapInterval(eglDisplay(), 1)) {
                qCDebug(KWIN_CORE) << "Enabled v-sync";
            }
        } else {
            qCWarning(KWIN_CORE) << "Cannot enable v-sync as max. swap interval is" << val;
        }
    } else {
        /* In the GLX backend, we fall back to using glCopyPixels if we have no extension providing support for partial screen updates.
         * However, that does not work in EGL - glCopyPixels with glDrawBuffer(GL_FRONT); does nothing.
         * Hence we need EGL to preserve the backbuffer for us, so that we can draw the partial updates on it and call
         * eglSwapBuffers() for each frame. eglSwapBuffers() then does the copy (no page flip possible in this mode),
         * which means it is slow and not synced to the v-blank. */
        qCWarning(KWIN_CORE) << "eglPostSubBufferNV not supported, have to enable buffer preservation - which breaks v-sync and performance";
        eglSurfaceAttrib(eglDisplay(), surface(), EGL_SWAP_BEHAVIOR, EGL_BUFFER_PRESERVED);
    }
}

bool EglOnXBackend::initRenderingContext()
{
    initClientExtensions();
    EGLDisplay dpy = kwinApp()->platform()->sceneEglDisplay();

    // Use eglGetPlatformDisplayEXT() to get the display pointer
    // if the implementation supports it.
    if (dpy == EGL_NO_DISPLAY) {
        const bool havePlatformBase = hasClientExtension(QByteArrayLiteral("EGL_EXT_platform_base"));
        setHavePlatformBase(havePlatformBase);
        if (havePlatformBase) {
            // Make sure that the X11 platform is supported
            if (!hasClientExtension(QByteArrayLiteral("EGL_EXT_platform_x11")) &&
                !hasClientExtension(QByteArrayLiteral("EGL_KHR_platform_x11"))) {
                qCWarning(KWIN_CORE) << "EGL_EXT_platform_base is supported, but neither EGL_EXT_platform_x11 nor EGL_KHR_platform_x11 is supported."
                                     << "Cannot create EGLDisplay on X11";
                return false;
            }

            const int attribs[] = {
                EGL_PLATFORM_X11_SCREEN_EXT, m_x11ScreenNumber,
                EGL_NONE
            };

            dpy = eglGetPlatformDisplayEXT(EGL_PLATFORM_X11_EXT, m_x11Display, attribs);
        } else {
            dpy = eglGetDisplay(m_x11Display);
        }
    }

    if (dpy == EGL_NO_DISPLAY) {
        qCWarning(KWIN_CORE) << "Failed to get the EGLDisplay";
        return false;
    }
    setEglDisplay(dpy);
    initEglAPI();

    initBufferConfigs();

    if (overlayWindow()) {
        if (!overlayWindow()->create()) {
            qCCritical(KWIN_CORE) << "Could not get overlay window";
            return false;
        } else {
            overlayWindow()->setup(None);
        }
    }
    if (!createSurfaces()) {
        qCCritical(KWIN_CORE) << "Creating egl surface failed";
        return false;
    }

    if (!createContext()) {
        qCCritical(KWIN_CORE) << "Create OpenGL context failed";
        return false;
    }

    if (!makeContextCurrent(surface())) {
        qCCritical(KWIN_CORE) << "Make Context Current failed";
        return false;
    }

    EGLint error = eglGetError();
    if (error != EGL_SUCCESS) {
        qCWarning(KWIN_CORE) << "Error occurred while creating context " << error;
        return false;
    }

    return true;
}

bool EglOnXBackend::createSurfaces()
{
    xcb_window_t window = XCB_WINDOW_NONE;
    if (m_overlayWindow) {
        window = m_overlayWindow->window();
    } else if (m_renderingWindow) {
        window = m_renderingWindow;
    }

    EGLSurface surface = createSurface(window);

    if (surface == EGL_NO_SURFACE) {
        return false;
    }
    setSurface(surface);
    return true;
}

EGLSurface EglOnXBackend::createSurface(xcb_window_t window)
{
    if (window == XCB_WINDOW_NONE) {
        return EGL_NO_SURFACE;
    }

    EGLSurface surface = EGL_NO_SURFACE;
    if (havePlatformBase()) {
        // Note: Window is 64 bits on a 64-bit architecture whereas xcb_window_t is
        //       always 32 bits. eglCreatePlatformWindowSurfaceEXT() expects the
        //       native_window parameter to be pointer to a Window, so this variable
        //       cannot be an xcb_window_t.
        surface = eglCreatePlatformWindowSurfaceEXT(eglDisplay(), config(), (void *) &window, nullptr);
    } else {
        //surface = eglCreateWindowSurface(eglDisplay(), config(), window, nullptr);
    }

    return surface;
}

bool EglOnXBackend::initBufferConfigs()
{
    initBufferAge();
    const EGLint config_attribs[] = {
        EGL_SURFACE_TYPE,         EGL_WINDOW_BIT | (supportsBufferAge() ? 0 : EGL_SWAP_BEHAVIOR_PRESERVED_BIT),
        EGL_RED_SIZE,             1,
        EGL_GREEN_SIZE,           1,
        EGL_BLUE_SIZE,            1,
        EGL_ALPHA_SIZE,           0,
        EGL_RENDERABLE_TYPE,      isOpenGLES() ? EGL_OPENGL_ES2_BIT : EGL_OPENGL_BIT,
        EGL_CONFIG_CAVEAT,        EGL_NONE,
        EGL_NONE,
    };

    EGLint count;
    EGLConfig configs[1024];
    if (eglChooseConfig(eglDisplay(), config_attribs, configs, 1024, &count) == EGL_FALSE) {
        qCCritical(KWIN_CORE) << "choose config failed";
        return false;
    }

    ScopedCPointer<xcb_get_window_attributes_reply_t> attribs(xcb_get_window_attributes_reply(m_connection,
                                                                                              xcb_get_window_attributes_unchecked(m_connection, m_rootWindow),
                                                                                              nullptr));
    if (!attribs) {
        qCCritical(KWIN_CORE) << "Failed to get window attributes of root window";
        return false;
    }

    setConfig(configs[0]);
    for (int i = 0; i < count; i++) {
        EGLint val;
        if (eglGetConfigAttrib(eglDisplay(), configs[i], EGL_NATIVE_VISUAL_ID, &val) == EGL_FALSE) {
            qCCritical(KWIN_CORE) << "egl get config attrib failed";
        }
        if (uint32_t(val) == attribs->visual) {
            setConfig(configs[i]);
            break;
        }
    }
    return true;
}

OverlayWindow* EglOnXBackend::overlayWindow() const
{
    return m_overlayWindow;
}

bool EglOnXBackend::makeContextCurrent(const EGLSurface &surface)
{
    return eglMakeCurrent(eglDisplay(), surface, surface, context()) == EGL_TRUE;
}

} // namespace
