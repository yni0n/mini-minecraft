#version 150

in vec2 fs_UV;
in float fs_Seed;
in vec3 fs_WorldPos;

uniform float u_Snow;
uniform float u_Alpha;
uniform vec3 u_Eye;

out vec4 out_Col;

void main() {
    float alpha;
    vec3 col;
    if(u_Snow > 0.5) {
        // 雪花：方形（MC 风格），保留窄软边防闪烁
        vec2 d = abs(fs_UV - 0.5) * 2.0;        // 0=中心, 1=边缘
        float edge = max(d.x, d.y);             // ★ 方形距离度量
        alpha = smoothstep(1.0, 0.88, edge);     // 只留 12% 的软边
        col = vec3(0.95, 0.96, 1.0);
    }
     else {
        // 雨丝：横向中间亮两边淡 + 每根雨丝明暗随机
        float edge = smoothstep(0.0, 0.4, fs_UV.x) * smoothstep(1.0, 0.6, fs_UV.x);
        alpha = edge * (0.55 + 0.45 * fract(fs_Seed * 13.7));
        col = vec3(0.65, 0.72, 0.85);
    }
    // 远处淡出，避免远处半透明面糊成一片
    float dist = length(fs_WorldPos - u_Eye);
    alpha *= 1.0 - smoothstep(32.0, 48.0, dist);
    out_Col = vec4(col, alpha * u_Alpha);
}
