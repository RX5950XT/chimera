#pragma once

#include <QOpenGLFunctions_4_1_Core>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <memory>

namespace chimera::graphics {

/**
 * @brief Host-side OpenGL renderer that draws the guest framebuffer to a quad.
 */
class Renderer : protected QOpenGLFunctions_4_1_Core {
public:
    Renderer();
    ~Renderer();

    bool initialize();
    void resize(int width, int height);
    void render(const uint8_t *pixelData, int imgWidth, int imgHeight);

private:
    std::unique_ptr<QOpenGLShaderProgram> m_program;
    std::unique_ptr<QOpenGLTexture> m_texture;
    GLuint m_vao = 0, m_vbo = 0;
    int m_viewportW = 0, m_viewportH = 0;
};

} // namespace chimera::graphics
