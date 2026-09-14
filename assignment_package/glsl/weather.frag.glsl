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
        // 雪花：×形（MC 风格），保留窄软边防闪烁
        vec2 q = fs_UV * 3.0;
        ivec2 cell = ivec2(floor(q));
        int id = cell.y * 3 + cell.x;      // 0..8
        if(id % 2 != 0) discard;           // 只留 0/2/4/6/8 → 九宫格 7 9 5 1 3 的 ×
        vec2 d = abs(fract(q) - 0.5) * 2.0;
        float edge = max(d.x, d.y);
        alpha = smoothstep(1.0, 0.85, edge);   // 每小格各自保留软边
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
