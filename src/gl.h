/* The GL API three.c renders with: OpenGL ES 3.0. Native builds link libGLESv2;
 * wasmcart builds (T3_WASMCART) import the same entry points from the cart
 * ABI's "gl" module. */
#ifndef T3_GL_H
#define T3_GL_H

#if defined(T3_WASMCART)
#ifndef WC_USE_GL
#define WC_USE_GL
#endif
#include "wasmcart.h"
/* constants wasmcart.h does not define */
#ifndef GL_UNPACK_ALIGNMENT
#define GL_UNPACK_ALIGNMENT 0x0CF5
#endif
/* GLES 3.0 entry points the host implements but wasmcart.h does not declare */
__attribute__((import_module("gl"), import_name("glRenderbufferStorageMultisample")))
extern void glRenderbufferStorageMultisample(GLenum target, GLsizei samples, GLenum internalformat, GLsizei width,
                                             GLsizei height);
__attribute__((import_module("gl"), import_name("glGetFloatv"))) extern void glGetFloatv(GLenum pname, GLfloat *data);
__attribute__((import_module("gl"), import_name("glGetFramebufferAttachmentParameteriv")))
extern void glGetFramebufferAttachmentParameteriv(GLenum target, GLenum attachment, GLenum pname, GLint *params);
#ifndef GL_FRAMEBUFFER_ATTACHMENT_COMPONENT_TYPE
#define GL_FRAMEBUFFER_ATTACHMENT_COMPONENT_TYPE 0x8211
#endif
#ifndef GL_FRAMEBUFFER_ATTACHMENT_RED_SIZE
#define GL_FRAMEBUFFER_ATTACHMENT_RED_SIZE 0x8212
#endif
#ifndef GL_DEPTH24_STENCIL8
#define GL_DEPTH24_STENCIL8 0x88F0
#endif
#ifndef GL_SAMPLES
#define GL_SAMPLES 0x80A9
#endif
#ifndef GL_DRAW_FRAMEBUFFER
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#endif
#ifndef GL_ACTIVE_UNIFORMS
#define GL_ACTIVE_UNIFORMS 0x8B86
#endif
#ifndef GL_ACTIVE_ATTRIBUTES
#define GL_ACTIVE_ATTRIBUTES 0x8B89
#endif
#ifndef GL_FLOAT_VEC2
#define GL_FLOAT_VEC2 0x8B50
#define GL_FLOAT_VEC3 0x8B51
#define GL_FLOAT_VEC4 0x8B52
#define GL_INT_VEC2 0x8B53
#define GL_INT_VEC3 0x8B54
#define GL_INT_VEC4 0x8B55
#define GL_BOOL 0x8B56
#define GL_BOOL_VEC2 0x8B57
#define GL_BOOL_VEC3 0x8B58
#define GL_BOOL_VEC4 0x8B59
#define GL_FLOAT_MAT2 0x8B5A
#define GL_FLOAT_MAT3 0x8B5B
#define GL_FLOAT_MAT4 0x8B5C
#endif
#ifndef GL_SRC_ALPHA_SATURATE
#define GL_SRC_ALPHA_SATURATE 0x0308
#endif
#ifndef GL_EXTENSIONS
#define GL_EXTENSIONS 0x1F03
#endif
#ifndef GL_QUERY_RESULT
#define GL_QUERY_RESULT 0x8866
#define GL_QUERY_RESULT_AVAILABLE 0x8867
#endif
__attribute__((import_module("gl"), import_name("glVertexAttrib4f")))
extern void glVertexAttrib4f(GLuint index, GLfloat x, GLfloat y, GLfloat z, GLfloat w);
__attribute__((import_module("gl"), import_name("glGenQueries"))) extern void glGenQueries(GLsizei n, GLuint *ids);
__attribute__((import_module("gl"), import_name("glDeleteQueries"))) extern void glDeleteQueries(GLsizei n, const GLuint *ids);
__attribute__((import_module("gl"), import_name("glBeginQuery"))) extern void glBeginQuery(GLenum target, GLuint id);
__attribute__((import_module("gl"), import_name("glEndQuery"))) extern void glEndQuery(GLenum target);
__attribute__((import_module("gl"), import_name("glGetQueryObjectuiv")))
extern void glGetQueryObjectuiv(GLuint id, GLenum pname, GLuint *params);
__attribute__((import_module("gl"), import_name("glUniform1uiv")))
extern void glUniform1uiv(GLint location, GLsizei count, const GLuint *value);
#ifndef GL_MIN
#define GL_MIN 0x8007
#endif
#ifndef GL_MAX
#define GL_MAX 0x8008
#endif
#elif defined(T3_GL_HEADER)
#include T3_GL_HEADER
#else
#include <GLES3/gl3.h>
#endif

#endif
