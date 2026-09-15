#version 150

uniform mat4 u_ViewProj;       // 太阳的正交 ViewProj
uniform float u_Time;            // ★ 已在 mygl.cpp:586 绑定，CPU 侧零改动
uniform float u_WindStrength;    // ★ 风力 0~1，由天气驱动

in vec4 vs_Pos;                // ★ 以下 4 个 in 一个都不能少，原因见下方警告
in vec4 vs_Nor;
in vec4 vs_Col;
in vec2 vs_UV;

out vec2 fs_UV;     // ★ 新增：传给 frag 做 alpha test
// ==================== 风 ====================
// 铁律：位移只能是 (世界坐标, 时间) 的纯函数。
// 相邻方块共享的角顶点世界坐标相同 → 位移完全相同 → 结构上不可能裂缝。
const vec2 WIND_DIR = vec2(0.849, 0.529);   // 全局风向（已归一化），想换方向只改这里

vec3 windOffset(vec3 wp, float weight, bool isLeaf) {
    if(weight <= 0.001) return vec3(0.0);

    // ① 阵风包络：一条 ~140 格宽的"风带"以 ~13 格/秒扫过地面
    float band = 0.5 + 0.5 * sin(dot(wp.xz, WIND_DIR * 0.045) - u_Time * 0.6);
    float gust = 0.45 + 0.55 * band;

    // ② 低频行波：整树 / 整片林一起摇（波长 ≈70 格、≈27 格）
    float p1 = dot(wp.xz, WIND_DIR * 0.09) - u_Time * 1.2;
    float p2 = dot(wp.xz, WIND_DIR * 0.23) - u_Time * 3.1;
    float sway = 0.85 * sin(p1) + 0.15 * sin(p2 + 1.7);

    vec3 dir = vec3(WIND_DIR.x, 0.0, WIND_DIR.y);

    // ==================== 树叶：低频 + 中频 + 高频 + 高度分层 ====================
    if(isLeaf) {
        // ③ 中频：树冠级错落（波长≈14格）——同一棵树两端会反向摆
        float q1 = dot(wp.xz, vec2(-0.31, 0.45)) + wp.y * 0.35 - u_Time * 2.2;
        // ④ 高频 + 高度分层：叶片级细碎（波长≈6格，且逐层错开）
        float q2 = dot(wp.xz, vec2(0.62, -0.51)) + wp.y * 0.90 - u_Time * 4.3;
        float leafy = (0.60 * sin(q1) + 0.40 * sin(q2 + 0.9)) * gust;

        const float ampLeafLow  = 0.10;   // 整树摇曳
        const float ampLeafFine = 0.07;   // 树冠错落 + 叶片细碎
        return dir * (sway * gust * ampLeafLow + leafy * ampLeafFine) * u_WindStrength;
    }

    // ==================== 草花：保持原有单频 + 细颤 ====================
    vec3 off = dir * (sway * gust * 0.20 * weight);
    off.x += sin(dot(wp.xz, WIND_DIR * 1.05) - u_Time * 2.6) * 0.02 * weight;
    return off * u_WindStrength;
}

void main() {
    fs_UV = vs_UV;

    // ★ 与主 pass 完全相同的风场，否则草影/树影静止不动
    float swayK  = vs_Nor.w;
    bool  isLeaf = swayK > 1.5;
    float weight = isLeaf ? 1.0 : swayK;
    vec3 windPos = vs_Pos.xyz + windOffset(vs_Pos.xyz, weight, isLeaf);

    gl_Position = u_ViewProj * vec4(windPos, 1.0);
}


//ShaderProgram::drawInterleaved（shaderprogram.cpp:292-313）直接用 m_attribs["vs_Nor"] 访问 map。
//如果 shader 里没声明 vs_Nor，这个 operator[] 访问会插入默认值 0，而 0 != -1 判定通过，
//接着会把 0 号属性（正是 vs_Pos 的位置）重新指向法线数据——深度 pass 直接画错。所以必须把 4
//个 in 全声明：被优化掉的会存 -1（安全跳过），保留的会正确绑定，两种情况都对。