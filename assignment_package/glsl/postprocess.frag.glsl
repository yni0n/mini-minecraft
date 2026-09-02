#version 150

in vec2 fs_UV;

uniform sampler2D u_ScreenTexture;  // FBO 渲染出的场景纹理
uniform int   u_FluidType;          // 0=无, 1=水, 2=岩浆
uniform float u_Time;              // 运行时间(秒)
out vec4 out_Col;

void main() {
    vec2 uv = fs_UV;

    // ========== 水下：UV 波动扭曲 ==========
    if(u_FluidType == 1) {
        float waveX = sin(uv.y * 30.0 + u_Time * 1.5) * 0.002;
        float waveY = cos(uv.x * 25.0 + u_Time * 0.8) * 0.001;
        uv += vec2(waveX, waveY);
    }

    vec3 color = texture(u_ScreenTexture, uv).rgb;

    if(u_FluidType == 1) {
        // 水下：蓝色调覆盖 + 涟漪亮度变化
        float ripple = sin(u_Time * 3.0 + uv.x * 10.0 + uv.y * 8.0) * 0.1 + 0.9;
        vec3 waterTint = vec3(0.1, 0.35, 0.6) * ripple;
        color = mix(color, color * waterTint * 1.5, 0.45);
        color += vec3(0.0, 0.05, 0.1) * ripple;
    }
    else if(u_FluidType == 2) {
        // 岩浆中：红橙色调 + 闪烁
        float flicker = sin(u_Time * 4.0) * 0.05
                      + sin(u_Time * 6.7) * 0.03 + 0.92;
        vec3 lavaTint = vec3(0.7, 0.2, 0.03) * flicker;
        color = mix(color, color * lavaTint * 1.8, 0.5);
        color += vec3(0.15, 0.03, 0.0) * flicker;
    }

    // ★ 必须输出 alpha=1.0，避免 Alpha 混合错误
    out_Col = vec4(color, 1.0);
}
