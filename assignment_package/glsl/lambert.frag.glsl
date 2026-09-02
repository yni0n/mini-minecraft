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
uniform sampler2D u_Texture;        //纹理采样器
uniform float u_Time;           //时间 (秒)
uniform sampler2DShadow u_ShadowMap;   // 纹理单元 2
uniform sampler2DShadow u_PrevShadowMap1;
uniform sampler2DShadow u_PrevShadowMap2;
uniform sampler2DShadow u_PrevShadowMap3;
uniform mat4 u_PrevLightVP1;
uniform mat4 u_PrevLightVP2;
uniform mat4 u_PrevLightVP3;
uniform mat4 u_LightVP;                // 太阳的 ViewProj(与深度 pass 相同)
uniform int u_ShadowEnabled;           // 调试用:0=强制无阴影

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

// 对指定 LightVP/贴图做一次 5x5 PCF 采样,返回 [0,1]
float sampleShadowPCF(sampler2DShadow shadowMap, mat4 lightVP,
                      vec4 worldPos, float bias) {
    vec4 lightSpace = lightVP * worldPos;
    vec3 shadowCoord = lightSpace.xyz / lightSpace.w * 0.5 + 0.5;
    if(shadowCoord.x < 0.0 || shadowCoord.x > 1.0 ||
       shadowCoord.y < 0.0 || shadowCoord.y > 1.0 ||
       shadowCoord.z > 1.0) {
        return 1.0;    // 贴图范围外:视为不遮挡
    }
    float pcfSum = 0.0;
    float texel = 1.0 / 4096.0;      // ★ float,别写 vec2(上次的教训)
    float stride = 1.5;
    for(int x = -2; x <= 2; ++x) {
        for(int y = -2; y <= 2; ++y) {
            pcfSum += texture(shadowMap,
                              vec3(shadowCoord.xy + vec2(x, y) * texel * stride,
                                   shadowCoord.z - bias));
        }
    }
    return pcfSum / 25.0;
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

    // ---------- 阴影贴图采样 ----------
    float shadowVis = 1.0;
    if(u_ShadowEnabled == 1) {
        float bias = max(0.004 * (1.0 - diffuseTerm), 0.0015);
        // ★ 指数权重:0.5 / 0.25 / 0.125 / 0.125
        //   越旧权重越低 → 快速走动时旧贴图失配只造成轻微变浅,不拖影
        float v0 = sampleShadowPCF(u_ShadowMap,     u_LightVP,     fs_Pos, bias);
        float v1 = sampleShadowPCF(u_PrevShadowMap1, u_PrevLightVP1, fs_Pos, bias);
        float v2 = sampleShadowPCF(u_PrevShadowMap2, u_PrevLightVP2, fs_Pos, bias);
        float v3 = sampleShadowPCF(u_PrevShadowMap3, u_PrevLightVP3, fs_Pos, bias);
        shadowVis = 0.5f*v0 + 0.25f*v1 + 0.125f*v2 + 0.125f*v3;
        shadowVis = 0.35 + 0.65 * shadowVis;
    }

    diffuseTerm *= shadowVis;

    // 高光：液体/雪更亮更锐，普通方块很弱
    float shininess    = isFluid ? 64.0 : 16.0;
    float specStrength = isFluid ? 0.6  : 0.15;
    float specularTerm = pow(clamp(dot(N, H), 0.0, 1.0), shininess) * specStrength;
    specularTerm *= shadowVis;

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
