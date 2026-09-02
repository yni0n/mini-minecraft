#version 150

uniform mat4 u_ViewProj;       // 太阳的正交 ViewProj

in vec4 vs_Pos;                // ★ 以下 4 个 in 一个都不能少，原因见下方警告
in vec4 vs_Nor;
in vec4 vs_Col;
in vec2 vs_UV;

void main() {
    gl_Position = u_ViewProj * vs_Pos;
}


//ShaderProgram::drawInterleaved（shaderprogram.cpp:292-313）直接用 m_attribs["vs_Nor"] 访问 map。
//如果 shader 里没声明 vs_Nor，这个 operator[] 访问会插入默认值 0，而 0 != -1 判定通过，
//接着会把 0 号属性（正是 vs_Pos 的位置）重新指向法线数据——深度 pass 直接画错。所以必须把 4
//个 in 全声明：被优化掉的会存 -1（安全跳过），保留的会正确绑定，两种情况都对。