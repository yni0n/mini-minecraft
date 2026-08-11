#pragma once
#include "drawable.h"

// 全屏四边形：4 个顶点覆盖 NDC [-1,1] 范围
// 专供后处理 Pass 使用，只需要 Position + UV
class ScreenQuad : public Drawable {
public:
    ScreenQuad(OpenGLContext* context);
    ~ScreenQuad();
    void createVBOdata() override;
    GLenum drawMode() override;
};
