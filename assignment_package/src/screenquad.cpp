#include "screenquad.h"

ScreenQuad::ScreenQuad(OpenGLContext* context)
    : Drawable(context)
{}

ScreenQuad::~ScreenQuad()
{}

GLenum ScreenQuad::drawMode() {
    return GL_TRIANGLES;
}

void ScreenQuad::createVBOdata() {
    // 4 个顶点（NDC 坐标），两个三角形拼成全屏矩形
    glm::vec4 pos[4] = {
        glm::vec4(-1, -1, 0, 1),  // 左下
        glm::vec4( 1, -1, 0, 1),  // 右下
        glm::vec4( 1,  1, 0, 1),  // 右上
        glm::vec4(-1,  1, 0, 1),  // 左上
    };

    glm::vec2 uv[4] = {
        glm::vec2(0, 0),  // 左下
        glm::vec2(1, 0),  // 右下
        glm::vec2(1, 1),  // 右上
        glm::vec2(0, 1),  // 左上
    };

    GLuint idx[6] = { 0, 2, 1, 0, 3, 2 };  // 两个三角形
    indexCounts[INDEX] = 6;

    // 索引缓冲
    generateBuffer(INDEX);
    bindBuffer(INDEX);
    mp_context->glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                             6 * sizeof(GLuint), idx, GL_STATIC_DRAW);

    // 位置缓冲
    generateBuffer(POSITION);
    bindBuffer(POSITION);
    mp_context->glBufferData(GL_ARRAY_BUFFER,
                             4 * sizeof(glm::vec4), pos, GL_STATIC_DRAW);

    // UV 缓冲
    generateBuffer(UV);
    bindBuffer(UV);
    mp_context->glBufferData(GL_ARRAY_BUFFER,
                             4 * sizeof(glm::vec2), uv, GL_STATIC_DRAW);
}
