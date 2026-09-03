#version 150

uniform sampler2D u_Texture;   // ★ 新增
in vec2 fs_UV;                // ★ 新增

out vec4 out_Col;

void main() {
    // ★ 与主 pass 相同的 cutout 阈值，否则影子是实心方块
    if(texture(u_Texture, fs_UV).a < 0.5) discard;
    out_Col = vec4(1.0);   // 深度-only FBO 没有颜色附件，这句写不写都不影响深度
}
