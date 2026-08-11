#version 150

in vec4 vs_Pos;   // NDC 坐标 (-1 ~ 1)，无需矩阵变换
in vec2 vs_UV;   // 纹理坐标 (0 ~ 1)

out vec2 fs_UV;

void main() {
    fs_UV = vs_UV;
    gl_Position = vs_Pos;   // 直通裁剪空间
}
