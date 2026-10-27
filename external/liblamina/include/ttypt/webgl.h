#ifndef TTYPT_WEBGL_H
#define TTYPT_WEBGL_H

/*
 * webgl.h — Canonical WebGL 1/2 graphics interface for WebAssembly and native C.
 * Implements two-level WASM import namespace "webgl" with native fallback.
 */

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned int GLenum;
typedef unsigned int GLuint;
typedef int GLint;
typedef int GLsizei;
typedef float GLfloat;
typedef float GLclampf;
typedef unsigned char GLboolean;

#define GL_FALSE                  0
#define GL_TRUE                   1

#define GL_COLOR_BUFFER_BIT       0x00004000
#define GL_DEPTH_BUFFER_BIT       0x00000100

#define GL_POINTS                 0x0000
#define GL_LINES                  0x0001
#define GL_TRIANGLES              0x0004
#define GL_TRIANGLE_STRIP         0x0005
#define GL_TRIANGLE_FAN           0x0006

#define GL_BYTE                   0x1400
#define GL_UNSIGNED_BYTE          0x1401
#define GL_SHORT                  0x1402
#define GL_UNSIGNED_SHORT         0x1403
#define GL_INT                    0x1404
#define GL_UNSIGNED_INT           0x1405
#define GL_FLOAT                  0x1406

#define GL_FRAGMENT_SHADER        0x8B30
#define GL_VERTEX_SHADER          0x8B31

#define GL_ARRAY_BUFFER           0x8892
#define GL_ELEMENT_ARRAY_BUFFER   0x8893

#define GL_STREAM_DRAW            0x88E0
#define GL_STATIC_DRAW            0x88E4
#define GL_DYNAMIC_DRAW           0x88E8

#define GL_COMPILE_STATUS         0x8B81
#define GL_LINK_STATUS            0x8B82

#ifdef __wasm__
__attribute__((import_module("webgl"), import_name("glViewport")))
void glViewport(GLint x, GLint y, GLsizei width, GLsizei height);

__attribute__((import_module("webgl"), import_name("glClearColor")))
void glClearColor(GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha);

__attribute__((import_module("webgl"), import_name("glClear")))
void glClear(GLenum mask);

__attribute__((import_module("webgl"), import_name("glCreateShader")))
GLuint glCreateShader(GLenum type);

__attribute__((import_module("webgl"), import_name("glShaderSource")))
void glShaderSource(GLuint shader, const char *src, size_t len);

__attribute__((import_module("webgl"), import_name("glCompileShader")))
void glCompileShader(GLuint shader);

__attribute__((import_module("webgl"), import_name("glGetShaderParameter")))
GLint glGetShaderParameter(GLuint shader, GLenum pname);

__attribute__((import_module("webgl"), import_name("glCreateProgram")))
GLuint glCreateProgram(void);

__attribute__((import_module("webgl"), import_name("glAttachShader")))
void glAttachShader(GLuint program, GLuint shader);

__attribute__((import_module("webgl"), import_name("glLinkProgram")))
void glLinkProgram(GLuint program);

__attribute__((import_module("webgl"), import_name("glGetProgramParameter")))
GLint glGetProgramParameter(GLuint program, GLenum pname);

__attribute__((import_module("webgl"), import_name("glUseProgram")))
void glUseProgram(GLuint program);

__attribute__((import_module("webgl"), import_name("glCreateBuffer")))
GLuint glCreateBuffer(void);

__attribute__((import_module("webgl"), import_name("glBindBuffer")))
void glBindBuffer(GLenum target, GLuint buffer);

__attribute__((import_module("webgl"), import_name("glBufferData")))
void glBufferData(GLenum target, const void *data, size_t size, GLenum usage);

__attribute__((import_module("webgl"), import_name("glEnableVertexAttribArray")))
void glEnableVertexAttribArray(GLuint index);

__attribute__((import_module("webgl"), import_name("glDisableVertexAttribArray")))
void glDisableVertexAttribArray(GLuint index);

__attribute__((import_module("webgl"), import_name("glVertexAttribPointer")))
void glVertexAttribPointer(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, size_t offset);

__attribute__((import_module("webgl"), import_name("glDrawArrays")))
void glDrawArrays(GLenum mode, GLint first, GLsizei count);

__attribute__((import_module("webgl"), import_name("glGetUniformLocation")))
GLint glGetUniformLocation(GLuint program, const char *name, size_t len);

__attribute__((import_module("webgl"), import_name("glUniform1f")))
void glUniform1f(GLint location, GLfloat v0);

__attribute__((import_module("webgl"), import_name("glUniform2f")))
void glUniform2f(GLint location, GLfloat v0, GLfloat v1);

__attribute__((import_module("webgl"), import_name("glUniform3f")))
void glUniform3f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2);

__attribute__((import_module("webgl"), import_name("glUniform4f")))
void glUniform4f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3);

__attribute__((import_module("webgl"), import_name("glUniformMatrix4fv")))
void glUniformMatrix4fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value);

__attribute__((import_module("webgl"), import_name("glDeleteShader")))
void glDeleteShader(GLuint shader);

__attribute__((import_module("webgl"), import_name("glDeleteProgram")))
void glDeleteProgram(GLuint program);

__attribute__((import_module("webgl"), import_name("glDeleteBuffer")))
void glDeleteBuffer(GLuint buffer);

#else

static inline void glViewport(GLint x, GLint y, GLsizei width, GLsizei height) { (void)x; (void)y; (void)width; (void)height; }
static inline void glClearColor(GLclampf r, GLclampf g, GLclampf b, GLclampf a) { (void)r; (void)g; (void)b; (void)a; }
static inline void glClear(GLenum mask) { (void)mask; }
static inline GLuint glCreateShader(GLenum type) { (void)type; return 0; }
static inline void glShaderSource(GLuint s, const char *src, size_t len) { (void)s; (void)src; (void)len; }
static inline void glCompileShader(GLuint s) { (void)s; }
static inline GLint glGetShaderParameter(GLuint s, GLenum p) { (void)s; (void)p; return 1; }
static inline GLuint glCreateProgram(void) { return 0; }
static inline void glAttachShader(GLuint p, GLuint s) { (void)p; (void)s; }
static inline void glLinkProgram(GLuint p) { (void)p; }
static inline GLint glGetProgramParameter(GLuint p, GLenum param) { (void)p; (void)param; return 1; }
static inline void glUseProgram(GLuint p) { (void)p; }
static inline GLuint glCreateBuffer(void) { return 0; }
static inline void glBindBuffer(GLenum t, GLuint b) { (void)t; (void)b; }
static inline void glBufferData(GLenum t, const void *d, size_t s, GLenum u) { (void)t; (void)d; (void)s; (void)u; }
static inline void glEnableVertexAttribArray(GLuint i) { (void)i; }
static inline void glDisableVertexAttribArray(GLuint i) { (void)i; }
static inline void glVertexAttribPointer(GLuint i, GLint s, GLenum t, GLboolean n, GLsizei st, size_t o) { (void)i; (void)s; (void)t; (void)n; (void)st; (void)o; }
static inline void glDrawArrays(GLenum m, GLint f, GLsizei c) { (void)m; (void)f; (void)c; }
static inline GLint glGetUniformLocation(GLuint p, const char *n, size_t l) { (void)p; (void)n; (void)l; return -1; }
static inline void glUniform1f(GLint l, GLfloat v0) { (void)l; (void)v0; }
static inline void glUniform2f(GLint l, GLfloat v0, GLfloat v1) { (void)l; (void)v0; (void)v1; }
static inline void glUniform3f(GLint l, GLfloat v0, GLfloat v1, GLfloat v2) { (void)l; (void)v0; (void)v1; (void)v2; }
static inline void glUniform4f(GLint l, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3) { (void)l; (void)v0; (void)v1; (void)v2; (void)v3; }
static inline void glUniformMatrix4fv(GLint l, GLsizei c, GLboolean t, const GLfloat *v) { (void)l; (void)c; (void)t; (void)v; }
static inline void glDeleteShader(GLuint s) { (void)s; }
static inline void glDeleteProgram(GLuint p) { (void)p; }
static inline void glDeleteBuffer(GLuint b) { (void)b; }

#endif

#ifdef __cplusplus
}
#endif

#endif /* TTYPT_WEBGL_H */
