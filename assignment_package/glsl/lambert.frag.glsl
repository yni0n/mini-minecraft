#version 150
// ^ Change this to version 130 if you have compatibility issues

// This is a fragment shader. If you've opened this file first, please
// open and read lambert.vert.glsl before reading on.
// Unlike the vertex shader, the fragment shader actually does compute
// the shading of geometry. For every pixel in your program's output
// screen, the fragment shader is run for every bit of geometry that
// particular pixel overlaps. By implicitly interpolating the position
// data passed into the fragment shader by the vertex shader, the fragment shader
// can compute what color to apply to its pixel based on things like vertex
// position, light position, and vertex color.

uniform vec4 u_Color; // The color with which to render this instance of geometry.
uniform sampler2D u_Texture;        // ★ 新增：纹理采样器
uniform float u_Time;           // ★ 时间 (秒)

uniform sampler2D u_NormalMap;   // 纹理单元 1
uniform vec3 u_LightDir;         // 指向光源
uniform vec3 u_LightColor;
uniform vec3 u_AmbientColor;
uniform vec3 u_Eye;
uniform vec3 u_FogSunColor;    // 朝太阳方向的雾色（sunset 色板）
uniform vec3 u_FogDuskColor;   // 背太阳方向的雾色（dusk 色板）
uniform vec3 u_FogSunDir;      // 太阳方向（用于方向雾，不随昼夜翻转）
uniform float u_FogDensity;
uniform int u_NormalMapEnabled;   // 0 = 关, 1 = 开

// These are the interpolated values out of the rasterizer, so you can't know
// their specific values without knowing the vertices that contributed to them
in vec4 fs_Pos;
in vec4 fs_Nor;
//in vec4 fs_LightVec;
in vec4 fs_Col;
in vec2 v_UV;                       // ★ 新增

out vec4 out_Col; // This is the final output color that you will see on your
// screen for the pixel that is currently being processed.

// 根据位置和 UV 的屏幕导数反推切线基
mat3 cotangentFrame(vec3 N, vec3 p, vec2 uv) {
    vec3 dp1 = dFdx(p),  dp2 = dFdy(p);
    vec2 duv1 = dFdx(uv), duv2 = dFdy(uv);
    vec3 dp2perp = cross(dp2, N);
    vec3 dp1perp = cross(N, dp1);
    vec3 T = dp2perp * duv1.x + dp1perp * duv2.x;
    vec3 B = dp2perp * duv1.y + dp1perp * duv2.y;
    float invmax = inversesqrt(max(dot(T,T), dot(B,B)));
    return mat3(T * invmax, B * invmax, N);
}


void main()
{
    vec2 animUV = v_UV;

    // ★ 动画 UV（仅对 WATER/LAVA）
    if(fs_Col.a > 0.5) {
        float tileSize = 1.0 / 16.0;
        float baseU = floor(v_UV.x / tileSize) * tileSize;
        float baseV = floor(v_UV.y / tileSize) * tileSize;
        float lu = (v_UV.x - baseU) / tileSize;
        float lv = (v_UV.y - baseV) / tileSize;

        float phase = fs_Pos.y * 3.0;              // Y 越高相位越靠前
        float wave = sin((phase + u_Time * 2.0)) * 0.03;  // ±0.03 tile 幅度的 sin

        lu = fract(lu + u_Time * 0.12 + wave);     // 基漂移 + sin 波动
        lv = fract(lv + u_Time * 0.05);


        animUV = vec2(baseU + lu * tileSize, baseV + lv * tileSize);
    }

    // Material base color (before shading)
    vec4 diffuseColor = texture(u_Texture, animUV);
    // ---------- 法线贴图 ----------
    vec3 N = normalize(fs_Nor.xyz);
    bool isFluid = fs_Col.a > 0.5;
    if(!isFluid && u_NormalMapEnabled == 1) {
        mat3 TBN = cotangentFrame(N, fs_Pos.xyz, animUV);
        vec3 nMap = texture(u_NormalMap, animUV).rgb * 2.0 - 1.0;
        N = normalize(TBN * nMap);
    }

    // ---------- Blinn-Phong ----------
    vec3 L = normalize(u_LightDir);
    vec3 V = normalize(u_Eye - fs_Pos.xyz);
    vec3 H = normalize(L + V);   // 半程向量：Blinn 与 Phong 的区别就在这

    // Calculate the diffuse term for Lambert shading
    float diffuseTerm = dot(N, L);
    // ★ 液体面双面光照：反面法线取反后，用 abs 确保两侧亮度一致
    if(fs_Col.a > 0.5) {
        diffuseTerm = abs(diffuseTerm);
    }
    // Avoid negative lighting values
    diffuseTerm = clamp(diffuseTerm, 0, 1);

    // 高光：液体/雪更亮更锐，普通方块很弱
    float shininess    = isFluid ? 64.0 : 16.0;
    float specStrength = isFluid ? 0.6  : 0.15;
    float specularTerm = pow(clamp(dot(N, H), 0.0, 1.0), shininess) * specStrength;

    vec3 ambient  = u_AmbientColor;
    vec3 diffuse  = u_LightColor * diffuseTerm;
    vec3 specular = u_LightColor * specularTerm;

    // ---------- 雾：远处淡入背景（天空地平线）色 ----------
    vec3 litColor = diffuseColor.rgb * (ambient + diffuse) + specular;
    float dist = length(fs_Pos.xyz - u_Eye);
    float fogFactor = clamp(smoothstep(220.0, 280.0, dist), 0.0, 1.0);//60可见，200不可见
    // ★ 方向雾：与 sky.frag.glsl:291-303 的 raySunDot 渐变完全一致
    vec3 viewDir = normalize(fs_Pos.xyz - u_Eye);   // 眼睛→片元方向（与天空的 rayDir 同向）
    float raySunDot = dot(viewDir, u_FogSunDir);
    vec3 fogColor;
    if(raySunDot > 0.75) {
        fogColor = u_FogSunColor;                         // 朝太阳：日落黄
    } else if(raySunDot > -0.1) {
        float t = (raySunDot - 0.75) / (-0.1 - 0.75);    // 过渡带
        fogColor = mix(u_FogSunColor, u_FogDuskColor, t);
    } else {
        fogColor = u_FogDuskColor;                        // 背太阳：黄昏紫
    }
    out_Col = vec4(mix(litColor, fogColor, fogFactor), diffuseColor.a);

    //float ambientTerm = 0.2;
    //float lightIntensity = diffuseTerm + ambientTerm;   //Add a small float value to the color multiplier
    //to simulate ambient lighting. This ensures that faces that are not
    //lit by our point light are not completely black.

    // Compute final shaded color
    //out_Col = vec4(diffuseColor.rgb * lightIntensity, diffuseColor.a);
}
