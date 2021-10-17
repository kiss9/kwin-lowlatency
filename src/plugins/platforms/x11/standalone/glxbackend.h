/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2012 Martin Gräßlin <mgraesslin@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#ifndef KWIN_GLX_BACKEND_H
#define KWIN_GLX_BACKEND_H
#include "openglbackend.h"
#include "texture.h"
#include "x11eventfilter.h"

#include <xcb/glx.h>
#include <epoxy/glx.h>
#include <fixx11h.h>

namespace KWin
{

class VsyncMonitor;
class X11StandalonePlatform;

// GLX_MESA_swap_interval
using glXSwapIntervalMESA_func = int (*)(unsigned int interval);
extern glXSwapIntervalMESA_func glXSwapIntervalMESA;

class FBConfigInfo
{
public:
    GLXFBConfig fbconfig;
    int bind_texture_format;
    int texture_targets;
    int y_inverted;
    int mipmap;
};


// ------------------------------------------------------------------


class SwapEventFilter : public X11EventFilter
{
public:
    SwapEventFilter(xcb_drawable_t drawable, xcb_glx_drawable_t glxDrawable);
    bool event(xcb_generic_event_t *event) override;

private:
    xcb_drawable_t m_drawable;
    xcb_glx_drawable_t m_glxDrawable;
};


/**
 * @brief OpenGL Backend using GLX over an X overlay window.
 */
class GlxBackend : public QObject, public OpenGLBackend
{
    Q_OBJECT

public:
    GlxBackend(Display *display, X11StandalonePlatform *backend);
    ~GlxBackend() override;
    void screenGeometryChanged(const QSize &size) override;
    SceneOpenGLTexturePrivate *createBackendTexture(SceneOpenGLTexture *texture) override;
    QRegion beginFrame(int screenId) override;
    void endFrame(int screenId, const QRegion &damage, const QRegion &damagedRegion) override;
    bool scanout(int screenId, SurfaceItem *surfaceItem) override;
    bool makeCurrent() override;
    void doneCurrent() override;
    OverlayWindow* overlayWindow() const override;
    void init() override;
    bool directScanoutAllowed(int screen) const override;

private:
    void vblank(std::chrono::nanoseconds timestamp);
    void present(const QRegion &damage);
    bool initBuffer();
    bool checkVersion();
    void initExtensions();
    bool initRenderingContext();
    bool initFbConfig();
    void initVisualDepthHashTable();
    void setSwapInterval(int interval);
    Display *display() const {
        return m_x11Display;
    }

    int visualDepth(xcb_visualid_t visual) const;
    FBConfigInfo *infoForVisual(xcb_visualid_t visual);

    /**
     * @brief The OverlayWindow used by this Backend.
     */
    OverlayWindow *m_overlayWindow;
    Window window;
    GLXFBConfig fbconfig;
    GLXWindow glxWindow;
    GLXContext ctx;
    QHash<xcb_visualid_t, FBConfigInfo *> m_fbconfigHash;
    QHash<xcb_visualid_t, int> m_visualDepthHash;
    std::unique_ptr<SwapEventFilter> m_swapEventFilter;
    int m_bufferAge;
    bool m_haveMESACopySubBuffer = false;
    bool m_haveMESASwapControl = false;
    bool m_haveEXTSwapControl = false;
    bool m_haveSGISwapControl = false;
    long long m_lastUnredirectedWindow;
    Display *m_x11Display;
    X11StandalonePlatform *m_backend;
    VsyncMonitor *m_vsyncMonitor = nullptr;
    friend class GlxTexture;
};

/**
 * @brief Texture using an GLXPixmap.
 */
class GlxTexture : public SceneOpenGLTexturePrivate
{
public:
    ~GlxTexture() override;
    void onDamage() override;
    bool loadTexture(WindowPixmap *pixmap) override;
    OpenGLBackend *backend() override;

private:
    friend class GlxBackend;
    GlxTexture(SceneOpenGLTexture *texture, GlxBackend *backend);
    bool loadTexture(xcb_pixmap_t pix, const QSize &size, xcb_visualid_t visual);
    Display *display() const {
        return m_backend->m_x11Display;
    }
    SceneOpenGLTexture *q;
    GlxBackend *m_backend;
    GLXPixmap m_glxpixmap; // the glx pixmap the texture is bound to
};

} // namespace
#endif // KWIN_GLX_BACKEND_H
