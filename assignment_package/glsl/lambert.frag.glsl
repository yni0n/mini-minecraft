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


// These are the interpolated values out of the rasterizer, so you can't know
// their specific values without knowing the vertices that contributed to them
in vec4 fs_Pos;
in vec4 fs_Nor;
in vec4 fs_LightVec;
in vec4 fs_Col;
in vec2 v_UV;                       // ★ 新增

out vec4 out_Col; // This is the final output color that you will see on your
// screen for the pixel that is currently being processed.

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

    // Add black lines between blocks (REMOVE WHEN YOU APPLY TEXTURES)
    // bool xBound = fract(fs_Pos.x) < 0.0125 || fract(fs_Pos.x) > 0.9875;
    // bool yBound = fract(fs_Pos.y) < 0.0125 || fract(fs_Pos.y) > 0.9875;
    // bool zBound = fract(fs_Pos.z) < 0.0125 || fract(fs_Pos.z) > 0.9875;
    // if((xBound && yBound) || (xBound && zBound) || (yBound && zBound)) {
    //     diffuseColor.rgb = vec3(0,0,0);
    // }

    // Calculate the diffuse term for Lambert shading
    float diffuseTerm = dot(normalize(fs_Nor), normalize(fs_LightVec));
    // ★ 液体面双面光照：反面法线取反后，用 abs 确保两侧亮度一致
    if(fs_Col.a > 0.5) {
        diffuseTerm = abs(diffuseTerm);
    }
    // Avoid negative lighting values
    diffuseTerm = clamp(diffuseTerm, 0, 1);

    float ambientTerm = 0.2;
    float lightIntensity = diffuseTerm + ambientTerm;   //Add a small float value to the color multiplier
    //to simulate ambient lighting. This ensures that faces that are not
    //lit by our point light are not completely black.

    // Compute final shaded color
    out_Col = vec4(diffuseColor.rgb * lightIntensity, diffuseColor.a);
}
