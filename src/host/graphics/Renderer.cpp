#include "Renderer.h"
#include <QOpenGLShader>
#include <QOpenGLBuffer>

namespace chimera::graphics {

static const char *vertexShaderSource = R"(
    #version 410 core
    layout(location = 0) in vec2 aPos;
    layout(location = 1) in vec2 aTexCoord;
    out vec2 vTexCoord;
    void main() {
        gl_Position = vec4(aPos, 0.0, 1.0);
        vTexCoord = aTexCoord;
    }
)";

static const char *fragmentShaderSource = R"(
    #version 410 core
    in vec2 vTexCoord;
    out vec4 fragColor;
    uniform sampler2D uTexture;
    void main() {
        fragColor = texture(uTexture, vTexCoord);
    }
)";

Renderer::Renderer() = default;

Renderer::~Renderer() {
    if (m_vao) glDeleteVertexArrays(1, &m_vao);
    if (m_vbo) glDeleteBuffers(1, &m_vbo);
}

bool Renderer::initialize() {
    initializeOpenGLFunctions();
    m_program = std::make_unique<QOpenGLShaderProgram>();
    m_program->addShaderFromSourceCode(QOpenGLShader::Vertex, vertexShaderSource);
    m_program->addShaderFromSourceCode(QOpenGLShader::Fragment, fragmentShaderSource);
    if (!m_program->link()) return false;

    m_texture = std::make_unique<QOpenGLTexture>(QOpenGLTexture::Target2D);
    m_texture->setMinificationFilter(QOpenGLTexture::Linear);
    m_texture->setMagnificationFilter(QOpenGLTexture::Linear);
    m_texture->setWrapMode(QOpenGLTexture::ClampToEdge);

    GLfloat vertices[] = {
        // pos        // texcoord
        -1.0f, -1.0f, 0.0f, 1.0f,
         1.0f, -1.0f, 1.0f, 1.0f,
        -1.0f,  1.0f, 0.0f, 0.0f,
         1.0f,  1.0f, 1.0f, 0.0f,
    };

    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), reinterpret_cast<void *>(2 * sizeof(float)));
    glBindVertexArray(0);

    return true;
}

void Renderer::resize(int width, int height) {
    m_viewportW = width;
    m_viewportH = height;
}

void Renderer::render(const uint8_t *pixelData, int imgWidth, int imgHeight) {
    if (!m_program || !pixelData) return;
    glViewport(0, 0, m_viewportW, m_viewportH);
    glClear(GL_COLOR_BUFFER_BIT);

    m_texture->bind();
    m_texture->setData(QOpenGLTexture::RGBA, QOpenGLTexture::UInt8, pixelData);

    m_program->bind();
    m_program->setUniformValue("uTexture", 0);
    glBindVertexArray(m_vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);
    m_program->release();
}

} // namespace chimera::graphics
