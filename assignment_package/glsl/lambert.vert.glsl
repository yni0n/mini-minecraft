#version 150
// ^ Change this to version 130 if you have compatibility issues

//This is a vertex shader. While it is called a "shader" due to outdated conventions, this file
//is used to apply matrix transformations to the arrays of vertex data passed to it.
//Since this code is run on your GPU, each vertex is transformed simultaneously.
//If it were run on your CPU, each vertex would have to be processed in a FOR loop, one at a time.
//This simultaneous transformation allows your program to run much faster, especially when rendering
//geometry with millions of vertices.

uniform mat4 u_Model;       // The matrix that defines the transformation of the
                            // object we're rendering. In this assignment,
                            // this will be the result of traversing your scene graph.

uniform mat4 u_ModelInvTr;  // The inverse transpose of the model matrix.
                            // This allows us to transform the object's normals properly
                            // if the object has been non-uniformly scaled.

uniform mat4 u_ViewProj;    // The matrix that defines the camera's transformation.
                            // We've written a static matrix for you to use for HW2,
                            // but in HW3 you'll have to generate one yourself

uniform vec4 u_Color;       // When drawing the cube instance, we'll set our uniform color to represent different block types.

in vec4 vs_Pos;             // The array of vertex positions passed to the shader

in vec4 vs_Nor;             // The array of vertex normals passed to the shader

in vec4 vs_Col;             // The array of vertex colors passed to the shader.

in vec2 vs_UV;              // ★ 新增纹理坐标
uniform float u_Time;            // ★ 已在 mygl.cpp:586 绑定，CPU 侧零改动
uniform float u_WindStrength;    // ★ 风力 0~1，由天气驱动

out vec4 fs_Pos;
out vec4 fs_Nor;            // The array of normals that has been transformed by u_ModelInvTr. This is implicitly passed to the fragment shader.
//out vec4 fs_LightVec;       // The direction in which our virtual light lies, relative to each vertex. This is implicitly passed to the fragment shader.
out vec4 fs_Col;            // The color of each vertex. This is implicitly passed to the fragment shader.
out vec2 v_UV;              // ★ 新增纹理坐标

const vec4 lightDir = normalize(vec4(0.5, 1, 0.75, 0));  // The direction of our virtual light, which is used to compute the shading of
                                        // the geometry in the fragment shader.
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

void main()
{
    // ★ 风摆：权重来自 normal.w
    float swayK  = vs_Nor.w;
    bool  isLeaf = swayK > 1.5;
    float weight = isLeaf ? 1.0 : swayK;
    vec3  windPos = vs_Pos.xyz + windOffset(vs_Pos.xyz, weight, isLeaf);

    fs_Pos = vec4(windPos, 1.0);   // ★ 用位移后的位置，阴影/雾才对得上
    fs_Col = vs_Col;
    v_UV   = vs_UV;           // ★ 传递给片段着色器

    mat3 invTranspose = mat3(u_ModelInvTr);
    fs_Nor = vec4(invTranspose * vec3(vs_Nor), 0);          // Pass the vertex normals to the fragment shader for interpolation.
                                                            // Transform the geometry's normals by the inverse transpose of the
                                                            // model matrix. This is necessary to ensure the normals remain
                                                            // perpendicular to the surface after the surface is transformed by
                                                            // the model matrix.


    vec4 modelposition = u_Model * vec4(windPos, 1.0);   // ★ 用位移后的位置    Temporarily store the transformed vertex positions for use below

    //fs_LightVec = (lightDir);  // Compute the direction in which the light source lies

    gl_Position = u_ViewProj * modelposition;// gl_Position is a built-in variable of OpenGL which is
                                             // used to render the final positions of the geometry's vertices
}
