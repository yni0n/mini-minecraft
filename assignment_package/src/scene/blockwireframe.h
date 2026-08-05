#pragma once
#include "drawable.h"

class BlockWireframe : public Drawable {
public:
    BlockWireframe(OpenGLContext* context);
    ~BlockWireframe();
    void createVBOdata() override;
    GLenum drawMode() override;
};
