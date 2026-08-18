#version 150

uniform mat4 u_ViewProj;    // We're actually passing the inverse of the viewproj
                            // from our CPU, but it's named u_ViewProj so we don't
                            // have to bother rewriting our ShaderProgram class

uniform ivec2 u_Dimensions; // Screen dimensions

uniform vec3 u_Eye; // Camera pos

uniform float u_Time;
uniform vec3 u_SunDir;   // 太阳方向,由 CPU 每帧计算

out vec4 out_Col;

const float PI = 3.14159265359;
const float TWO_PI = 6.28318530718;

// Sunset palette
const vec3 sunset[5] = vec3[](vec3(255, 229, 119) / 255.0,
                               vec3(254, 192, 81) / 255.0,
                               vec3(255, 137, 103) / 255.0,
                               vec3(253, 96, 81) / 255.0,
                               vec3(57, 32, 51) / 255.0);
// Dusk palette
const vec3 dusk[5] = vec3[](vec3(144, 96, 144) / 255.0,
                            vec3(96, 72, 120) / 255.0,
                            vec3(72, 48, 120) / 255.0,
                            vec3(48, 24, 96) / 255.0,
                            vec3(0, 24, 72) / 255.0);

const vec3 sunColor = vec3(255, 255, 190) / 255.0;
const vec3 cloudColor = sunset[3];

vec2 sphereToUV(vec3 p) {
    float phi = atan(p.z, p.x);
    if(phi < 0) {
        phi += TWO_PI;
    }
    float theta = acos(p.y);
    return vec2(1 - phi / TWO_PI, 1 - theta / PI);
}

vec3 uvToSunset(vec2 uv) {
    if(uv.y < 0.5) {
        return sunset[0];
    }
    else if(uv.y < 0.55) {
        return mix(sunset[0], sunset[1], (uv.y - 0.5) / 0.05);
    }
    else if(uv.y < 0.6) {
        return mix(sunset[1], sunset[2], (uv.y - 0.55) / 0.05);
    }
    else if(uv.y < 0.65) {
        return mix(sunset[2], sunset[3], (uv.y - 0.6) / 0.05);
    }
    else if(uv.y < 0.75) {
        return mix(sunset[3], sunset[4], (uv.y - 0.65) / 0.1);
    }
    return sunset[4];
}

vec3 uvToDusk(vec2 uv) {
    if(uv.y < 0.5) {
        return dusk[0];
    }
    else if(uv.y < 0.55) {
        return mix(dusk[0], dusk[1], (uv.y - 0.5) / 0.05);
    }
    else if(uv.y < 0.6) {
        return mix(dusk[1], dusk[2], (uv.y - 0.55) / 0.05);
    }
    else if(uv.y < 0.65) {
        return mix(dusk[2], dusk[3], (uv.y - 0.6) / 0.05);
    }
    else if(uv.y < 0.75) {
        return mix(dusk[3], dusk[4], (uv.y - 0.65) / 0.1);
    }
    return dusk[4];
}

vec2 random2( vec2 p ) {
    return fract(sin(vec2(dot(p,vec2(127.1,311.7)),dot(p,vec2(269.5,183.3))))*43758.5453);
}

vec3 random3( vec3 p ) {
    return fract(sin(vec3(dot(p,vec3(127.1, 311.7, 191.999)),
                          dot(p,vec3(269.5, 183.3, 765.54)),
                          dot(p, vec3(420.69, 631.2,109.21))))
                 *43758.5453);
}

float WorleyNoise3D(vec3 p)
{
    // Tile the space
    vec3 pointInt = floor(p);
    vec3 pointFract = fract(p);

    float minDist = 1.0; // Minimum distance initialized to max.

    // Search all neighboring cells and this cell for their point
    for(int z = -1; z <= 1; z++)
    {
        for(int y = -1; y <= 1; y++)
        {
            for(int x = -1; x <= 1; x++)
            {
                vec3 neighbor = vec3(float(x), float(y), float(z));

                // Random point inside current neighboring cell
                vec3 point = random3(pointInt + neighbor);

                // Animate the point
                point = 0.5 + 0.5 * sin(u_Time * 0.01 + 6.2831 * point); // 0 to 1 range

                // Compute the distance b/t the point and the fragment
                // Store the min dist thus far
                vec3 diff = neighbor + point - pointFract;
                float dist = length(diff);
                minDist = min(minDist, dist);
            }
        }
    }
    return minDist;
}

float WorleyNoise(vec2 uv)
{
    // Tile the space
    vec2 uvInt = floor(uv);
    vec2 uvFract = fract(uv);

    float minDist = 1.0; // Minimum distance initialized to max.

    // Search all neighboring cells and this cell for their point
    for(int y = -1; y <= 1; y++)
    {
        for(int x = -1; x <= 1; x++)
        {
            vec2 neighbor = vec2(float(x), float(y));

            // Random point inside current neighboring cell
            vec2 point = random2(uvInt + neighbor);

            // Animate the point
            point = 0.5 + 0.5 * sin(u_Time * 0.01 + 6.2831 * point); // 0 to 1 range

            // Compute the distance b/t the point and the fragment
            // Store the min dist thus far
            vec2 diff = neighbor + point - uvFract;
            float dist = length(diff);
            minDist = min(minDist, dist);
        }
    }
    return minDist;
}

float worleyFBM(vec3 uv) {
    float sum = 0;
    float freq = 4;
    float amp = 0.5;
    for(int i = 0; i < 8; i++) {
        sum += WorleyNoise3D(uv * freq) * amp;
        freq *= 2;
        amp *= 0.5;
    }
    return sum;
}

//#define RAY_AS_COLOR
//#define SPHERE_UV_AS_COLOR
//#define WORLEY_OFFSET

void main()
{
    vec2 ndc = (gl_FragCoord.xy / vec2(u_Dimensions)) * 2.0 - 1.0; // -1 to 1 NDC

//    outColor = vec3(ndc * 0.5 + 0.5, 1);

    vec4 p = vec4(ndc.xy, 1, 1); // Pixel at the far clip plane
    p *= 1000.0; // Times far clip plane value
    p = /*Inverse of*/ u_ViewProj * p; // Convert from unhomogenized screen to world

    vec3 rayDir = normalize(p.xyz - u_Eye);

#ifdef RAY_AS_COLOR
    //outColor = 0.5 * (rayDir + vec3(1,1,1));
    out_Col = vec4(0.5 * (rayDir + vec3(1,1,1)) , 1.0);
    return;
#endif

    vec2 uv = sphereToUV(rayDir);
#ifdef SPHERE_UV_AS_COLOR
    //outColor = vec3(uv, 0);
    out_Col = vec4(uv, 0.0, 1.0);
    return;
#endif


    vec2 offset = vec2(0.0);
#ifdef WORLEY_OFFSET
    // Get a noise value in the range [-1, 1]
    // by using Worley noise as the noise basis of FBM
    offset = vec2(worleyFBM(rayDir));
    offset *= 2.0;
    offset -= vec2(1.0);
#endif

    // Compute a gradient from the bottom of the sky-sphere to the top
    vec3 sunsetColor = uvToSunset(uv + offset * 0.1);
    vec3 duskColor = uvToDusk(uv + offset * 0.1);

    // ★ 天空底色随太阳高度变色(白昼偏蓝、夜晚压暗)
    float sunElev = u_SunDir.y;
    vec3 daySky = vec3(0.45, 0.68, 1.0); //白天蓝色
    vec3 nightSky = vec3(0.13, 0.10, 0.30);   // 夜晚蓝紫
    float dayBlend = smoothstep(0.0, 0.6, sunElev); //当太阳y大于0.6时蓝色，0-0.6渐变晚霞
    float nightBlend = 1.0 - smoothstep(-0.35, -0.05, sunElev);  // 1→0 夜晚(沉到 -0.35 以下为全夜)
    sunsetColor = mix(sunsetColor, daySky,   dayBlend   * 0.85);
    sunsetColor = mix(sunsetColor, nightSky, nightBlend * 0.9);
    duskColor    = mix(duskColor,    daySky,   dayBlend   * 0.85);
    duskColor    = mix(duskColor,    nightSky, nightBlend * 0.9);

    vec3 outColor = sunsetColor;

    // Add a glowing sun in the sky
    //vec3 sunDir = normalize(vec3(0, 0.1, 1.0));
    vec3 sunDir = u_SunDir;   // 已在 CPU 端归一化
    // ★ 方形太阳(Minecraft 风格):实心方块 + 75% 同心外框 + 轻微光晕
    vec3 sunCol = mix(vec3(1.0, 0.88, 0.68), vec3(1.0, 1.0, 0.88),
                          smoothstep(0.0, 0.45, sunElev));
    vec3 sFwd   = normalize(cross(vec3(0, 0, 1), sunDir));
    vec3 sRight = cross(sunDir, sFwd);
    // 视线相对太阳的角偏移(度):dx 沿基的"右",dy 沿基的"前"
    float sdx = atan(dot(rayDir, sRight), dot(rayDir, sunDir)) * 180.0 / PI;
    float sdy = atan(dot(rayDir, sFwd),   dot(rayDir, sunDir)) * 180.0 / PI;
    float sCheb = max(abs(sdx), abs(sdy));   // 切比雪夫距离:正方形判定
    float sRound = pow(pow(abs(sdx), 4.0) + pow(abs(sdy), 4.0), 0.25);  // 超椭圆距离:光晕用
    float sunCore  = 5.0;                   // 中心方块半边长(度)
    float sunOuter = sunCore * 1.25;         // 外框:比中心大 25%,同心
    if(sCheb < sunCore) {
        outColor = sunCol;                          // 中心:不透明度 100%
    }
    else if(sCheb < sunOuter) {
        outColor = mix(outColor, sunCol, 0.75);     // 外框:75%,透出背景
    }
    else if(sRound  < sunOuter * 1.7) {
        float halo = 1.0 - smoothstep(sunOuter, sunOuter * 1.8, sRound);  // smoothstep:柔和渐隐
        outColor = mix(outColor, sunCol, halo * 0.25);
    }
    else {
        // 原"日落/黄昏"渐变逻辑,原样保留在最后一个 else 里
        float raySunDot = dot(rayDir, sunDir);
#define SUNSET_THRESHOLD 0.75
#define DUSK_THRESHOLD -0.1
        if(raySunDot > SUNSET_THRESHOLD) {
            // Do nothing, sky is already correct color
        }
        else if(raySunDot > DUSK_THRESHOLD) {
            float t = (raySunDot - SUNSET_THRESHOLD) / (DUSK_THRESHOLD - SUNSET_THRESHOLD);
            outColor = mix(outColor, duskColor, t);
        }
        else {
            outColor = duskColor;
        }
    }

    // ★ 星星:以月亮为中心的立体投影星空,整片星空随月亮一起移动
    //    基绑定 moonDir → 星星与月亮同步转动;保角投影 → 星点恒为圆形
    vec3 moonDirS = -u_SunDir;                                  // 月亮方向(与太阳共轨反向)
    vec3 mFwd   = normalize(cross(vec3(0, 0, 1), moonDirS));     // 轨道面内方向(月亮轨道在 XY 平面附近,与 Z 轴不平行,基永不退化)
    vec3 mRight = cross(moonDirS, mFwd);
    // 立体投影:天球 → 月亮切平面,月亮在原点;θ=90° 处投影半径为 1
    float pd = 1.0 + dot(rayDir, moonDirS);                     // 1=月亮中心,0=太阳方向
    vec2 sp = vec2(dot(rayDir, mRight), dot(rayDir, mFwd)) / max(pd, 0.15);
    vec2 starCell = sp * 40.0;                                  // 格子密度(调大=更密)
    vec2 starId = floor(starCell);
    vec2 starF = fract(starCell);
    vec2 starHash = random2(starId);
    vec2 starC = 0.5 + (starHash - 0.5) * 0.7;                  // 偏移±0.3,星点永不跨格被切断
    float starD = distance(starF, starC);
    float starCore = 1.0 - smoothstep(0.0, 0.05, starD);        // 星点半径≈0.5°,圆形
    float starProb = step(0.88, starHash.x);                    // 约 12% 格子有星
    float twinkle = 0.5 + 0.5 * sin(u_Time * 2.0 + (starHash.x + starHash.y) * 6.2831);
    float starHeight = smoothstep(0.6, 0.68, uv.y);             // 地平线以上渐显
    float farFade = smoothstep(-0.5, -0.2, dot(rayDir, moonDirS)); // 距月亮>~120°(接近太阳方向)淡出
    float starMask = starCore * starProb * starHeight * nightBlend * twinkle * farFade;
    vec3 starCol = mix(vec3(0.9, 0.92, 1.0), vec3(1.0, 0.96, 0.9), starHash.y);
    outColor += starMask * starCol * 1.8;

    // ★ 月亮:与太阳同一条轨道、相差 180°(太阳落山 → 月亮升起)
    vec3 moonDir = -sunDir;
    float moonAngle = acos(dot(rayDir, moonDir)) * 360.0 / PI;
    float moonSize = 16.0;                       // 月盘角半径(度),比太阳(30)小
    vec3 moonColor = vec3(0.85, 0.88, 0.96);     // 冷白月光

    // ★ 方形月亮:实心方块 + 75% 同心外框 + 轻微光晕,夜晚可见(复用星星段的 mFwd/mRight 基)
    float mdx = atan(dot(rayDir, mRight), dot(rayDir, moonDir)) * 180.0 / PI;
    float mdy = atan(dot(rayDir, mFwd),   dot(rayDir, moonDir)) * 180.0 / PI;
    float mCheb = max(abs(mdx), abs(mdy));
    float moonCore  = 4.0;                    // 月亮比太阳小一号
    float moonOuter = moonCore * 1.25;
    float mRound = pow(pow(abs(mdx), 4.0) + pow(abs(mdy), 4.0), 0.25);
    if(mCheb < moonCore) {
        outColor = mix(outColor, moonColor, nightBlend);
    }
    else if(mCheb < moonOuter) {
        outColor = mix(outColor, moonColor, 0.75 * nightBlend);
    }
    else if(mRound < moonOuter * 1.8) {
        float mHalo = 1.0 - smoothstep(moonOuter, moonOuter * 1.8, mRound);
        outColor = mix(outColor, moonColor, mHalo * 0.2 * nightBlend);
    }

    // ★ 云层:头顶分布,移动+翻涌+厚度随机;白天白、夜晚比背景浅
    // 噪声函数与原方案相同(worleyFBM):时间偏移让云整体漂移,内部 sin(第114行)实现翻涌
    vec3 cloudInput = rayDir + vec3(u_Time * 0.02, 0.0, u_Time * 0.01);
    float cloudRaw  = worleyFBM(cloudInput);
    // 仅地平线以上显示,头顶更密(uv.y: 0=脚下, 0.5=地平线, 1=头顶)
    float heightFade = smoothstep(0.45, 0.67, uv.y);
    // 厚度:FBM 值经阈值映射成云遮罩(值越大云越厚)
    float cloudMask  = smoothstep(0.42, 0.68, cloudRaw) * heightFade;
    // 云色:白天灰白(不刺眼),夜晚比背景浅
    vec3 cloudCol = mix(outColor * 1.45, vec3(0.9, 0.9, 0.88), dayBlend);
    // 半透明:0.5 让云透光,飘过太阳时半遮挡(看得见太阳变暗)
    outColor = mix(outColor, cloudCol, cloudMask * 0.5);

    out_Col = vec4(outColor, 1.0);

}
