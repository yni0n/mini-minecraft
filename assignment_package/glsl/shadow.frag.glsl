#version 150

out vec4 out_Col;

void main() {
    out_Col = vec4(1.0);   // 深度-only FBO 没有颜色附件，这句写不写都不影响深度
}
