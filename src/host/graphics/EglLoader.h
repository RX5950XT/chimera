#pragma once

// Dynamic EGL function pointer loader for ANGLE on Windows.
// We only have libEGL.dll (no .lib import library), so we use QLibrary
// to load the DLL and resolve core EGL functions at runtime.

#ifdef CHIMERA_HAS_ANGLE

#include <QLibrary>
#include <QDebug>
#include <EGL/egl.h>

namespace chimera::graphics {

struct EglFunctions {
    bool loaded = false;
    QLibrary lib;

    // Core EGL function pointers
    EGLDisplay (EGLAPIENTRY *eglGetDisplay)(EGLNativeDisplayType display_id) = nullptr;
    EGLBoolean (EGLAPIENTRY *eglInitialize)(EGLDisplay dpy, EGLint *major, EGLint *minor) = nullptr;
    EGLBoolean (EGLAPIENTRY *eglTerminate)(EGLDisplay dpy) = nullptr;
    EGLBoolean (EGLAPIENTRY *eglChooseConfig)(EGLDisplay dpy, const EGLint *attrib_list, EGLConfig *configs, EGLint config_size, EGLint *num_config) = nullptr;
    EGLContext (EGLAPIENTRY *eglCreateContext)(EGLDisplay dpy, EGLConfig config, EGLContext share_context, const EGLint *attrib_list) = nullptr;
    EGLBoolean (EGLAPIENTRY *eglDestroyContext)(EGLDisplay dpy, EGLContext ctx) = nullptr;
    EGLSurface (EGLAPIENTRY *eglCreateWindowSurface)(EGLDisplay dpy, EGLConfig config, EGLNativeWindowType win, const EGLint *attrib_list) = nullptr;
    EGLBoolean (EGLAPIENTRY *eglDestroySurface)(EGLDisplay dpy, EGLSurface surface) = nullptr;
    EGLBoolean (EGLAPIENTRY *eglMakeCurrent)(EGLDisplay dpy, EGLSurface draw, EGLSurface read, EGLContext ctx) = nullptr;
    EGLBoolean (EGLAPIENTRY *eglSwapBuffers)(EGLDisplay dpy, EGLSurface surface) = nullptr;
    __eglMustCastToProperFunctionPointerType (EGLAPIENTRY *eglGetProcAddress)(const char *procname) = nullptr;

    bool load() {
        if (loaded) return true;

        // Try executable directory first, then PATH
        lib.setFileName("libEGL");
        lib.setLoadHints(QLibrary::ResolveAllSymbolsHint);
        if (!lib.load()) {
            qWarning() << "Failed to load libEGL.dll:" << lib.errorString();
            return false;
        }

        eglGetDisplay = reinterpret_cast<decltype(eglGetDisplay)>(lib.resolve("eglGetDisplay"));
        eglInitialize = reinterpret_cast<decltype(eglInitialize)>(lib.resolve("eglInitialize"));
        eglTerminate = reinterpret_cast<decltype(eglTerminate)>(lib.resolve("eglTerminate"));
        eglChooseConfig = reinterpret_cast<decltype(eglChooseConfig)>(lib.resolve("eglChooseConfig"));
        eglCreateContext = reinterpret_cast<decltype(eglCreateContext)>(lib.resolve("eglCreateContext"));
        eglDestroyContext = reinterpret_cast<decltype(eglDestroyContext)>(lib.resolve("eglDestroyContext"));
        eglCreateWindowSurface = reinterpret_cast<decltype(eglCreateWindowSurface)>(lib.resolve("eglCreateWindowSurface"));
        eglDestroySurface = reinterpret_cast<decltype(eglDestroySurface)>(lib.resolve("eglDestroySurface"));
        eglMakeCurrent = reinterpret_cast<decltype(eglMakeCurrent)>(lib.resolve("eglMakeCurrent"));
        eglSwapBuffers = reinterpret_cast<decltype(eglSwapBuffers)>(lib.resolve("eglSwapBuffers"));
        eglGetProcAddress = reinterpret_cast<decltype(eglGetProcAddress)>(lib.resolve("eglGetProcAddress"));

        if (!eglGetDisplay || !eglInitialize || !eglTerminate || !eglChooseConfig ||
            !eglCreateContext || !eglDestroyContext || !eglCreateWindowSurface ||
            !eglDestroySurface || !eglMakeCurrent || !eglSwapBuffers || !eglGetProcAddress) {
            qWarning() << "Failed to resolve all EGL functions from libEGL.dll";
            lib.unload();
            return false;
        }

        loaded = true;
        qDebug() << "libEGL.dll loaded successfully";
        return true;
    }
};

inline EglFunctions &egl() {
    static EglFunctions inst;
    return inst;
}

} // namespace chimera::graphics

#endif // CHIMERA_HAS_ANGLE
