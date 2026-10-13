#include "opengl_hooks.hpp"
#include "log.hpp"

#include <windows.h>
#include <gl/GL.h>

#include "MinHook.h"

namespace gpucfg::opengl_hooks {

namespace {

using PFN_glGetString    = const GLubyte* (APIENTRY*)(GLenum);
using PFN_wglGetProcAddr = PROC            (WINAPI  *)(LPCSTR);

PFN_glGetString    g_real_glGetString    = nullptr;
PFN_wglGetProcAddr g_real_wglGetProcAddr = nullptr;

const char* name_gl_enum(GLenum e) {
    switch (e) {
        case GL_VENDOR:                   return "GL_VENDOR";
        case GL_RENDERER:                 return "GL_RENDERER";
        case GL_VERSION:                  return "GL_VERSION";
        case GL_EXTENSIONS:               return "GL_EXTENSIONS";
        case 0x8B8C /*GL_SHADING_LANGUAGE_VERSION*/: return "GL_SHADING_LANGUAGE_VERSION";
        default:                          return "<other>";
    }
}

const GLubyte* APIENTRY hk_glGetString(GLenum name) {
    const GLubyte* r = g_real_glGetString ? g_real_glGetString(name) : nullptr;
    if (name == GL_VENDOR || name == GL_RENDERER || name == GL_VERSION) {
        GCLOG("gl: glGetString(%s) -> \"%s\"",
               name_gl_enum(name),
               r ? reinterpret_cast<const char*>(r) : "(null)");
    }
    return r;
}

PROC WINAPI hk_wglGetProcAddress(LPCSTR name) {
    PROC r = g_real_wglGetProcAddr ? g_real_wglGetProcAddr(name) : nullptr;
    if (name && r) {
        // Only log a handful of names to avoid log spam.
        if (lstrcmpA(name, "glGetStringi") == 0 ||
            lstrcmpA(name, "wglGetExtensionsStringARB") == 0) {
            GCLOG("gl: wglGetProcAddress(\"%s\") -> %p", name, r);
        }
    }
    return r;
}

}  // namespace

bool install_hooks() {
    HMODULE gl = LoadLibraryW(L"opengl32.dll");
    if (!gl) {
        GCLOG("gl: LoadLibrary(opengl32.dll) failed GLE=%lu", GetLastError());
        return false;
    }

    bool any = false;

    void* t1 = reinterpret_cast<void*>(GetProcAddress(gl, "glGetString"));
    if (t1) {
        MH_STATUS st = MH_CreateHook(t1, reinterpret_cast<void*>(&hk_glGetString),
                                     reinterpret_cast<void**>(&g_real_glGetString));
        if (st == MH_OK) {
            GCLOG("gl: hooked opengl32.dll!glGetString @ %p", t1);
            any = true;
        } else {
            GCLOG("gl: MH_CreateHook(glGetString) failed status=%d", int(st));
        }
    } else {
        GCLOG("gl: opengl32.dll!glGetString not found");
    }

    void* t2 = reinterpret_cast<void*>(GetProcAddress(gl, "wglGetProcAddress"));
    if (t2) {
        MH_STATUS st = MH_CreateHook(t2, reinterpret_cast<void*>(&hk_wglGetProcAddress),
                                     reinterpret_cast<void**>(&g_real_wglGetProcAddr));
        if (st == MH_OK) {
            GCLOG("gl: hooked opengl32.dll!wglGetProcAddress @ %p", t2);
            any = true;
        } else {
            GCLOG("gl: MH_CreateHook(wglGetProcAddress) failed status=%d", int(st));
        }
    }
    return any;
}

}  // namespace gpucfg::opengl_hooks
