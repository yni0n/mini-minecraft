#include "blockwireframe.h"

BlockWireframe::BlockWireframe(OpenGLContext* context)
    : Drawable(context)
{}

BlockWireframe::~BlockWireframe()
{}

GLenum BlockWireframe::drawMode() {
    return GL_LINES;
}

void BlockWireframe::createVBOdata() {
    // 单位立方体 12 条边，24 个顶点（GL_LINES 每对顶点一条线）
    glm::vec4 pos[24] = {
        // 底面 (y=0)
        glm::vec4(0,0,0,1), glm::vec4(1,0,0,1),
        glm::vec4(1,0,0,1), glm::vec4(1,0,1,1),
        glm::vec4(1,0,1,1), glm::vec4(0,0,1,1),
        glm::vec4(0,0,1,1), glm::vec4(0,0,0,1),
        // 顶面 (y=1)
        glm::vec4(0,1,0,1), glm::vec4(1,1,0,1),
        glm::vec4(1,1,0,1), glm::vec4(1,1,1,1),
        glm::vec4(1,1,1,1), glm::vec4(0,1,1,1),
        glm::vec4(0,1,1,1), glm::vec4(0,1,0,1),
        // 竖边
        glm::vec4(0,0,0,1), glm::vec4(0,1,0,1),
        glm::vec4(1,0,0,1), glm::vec4(1,1,0,1),
        glm::vec4(1,0,1,1), glm::vec4(1,1,1,1),
        glm::vec4(0,0,1,1), glm::vec4(0,1,1,1),
    };

    // 全部浅白色
    glm::vec4 col[24];
    for(int i = 0; i < 24; ++i) {
        col[i] = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
    }

    // 索引 0~23
    GLuint idx[24];
    for(int i = 0; i < 24; ++i) idx[i] = i;

    indexCounts[INDEX] = 24;

    generateBuffer(INDEX);
    bindBuffer(INDEX);
    mp_context->glBufferData(GL_ELEMENT_ARRAY_BUFFER, 24 * sizeof(GLuint), idx, GL_STATIC_DRAW);

    generateBuffer(POSITION);
    bindBuffer(POSITION);
    mp_context->glBufferData(GL_ARRAY_BUFFER, 24 * sizeof(glm::vec4), pos, GL_STATIC_DRAW);

    generateBuffer(COLOR);
    bindBuffer(COLOR);
    mp_context->glBufferData(GL_ARRAY_BUFFER, 24 * sizeof(glm::vec4), col, GL_STATIC_DRAW);
}