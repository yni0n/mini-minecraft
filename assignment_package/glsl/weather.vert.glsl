#version 150

in vec2 vs_Corner;
in vec4 vs_InstancePos;

uniform mat4 u_ViewProj;
uniform vec3 u_CameraRight;
uniform vec3 u_CameraUp;
uniform float u_Snow;

out vec2 fs_UV;
out float fs_Seed;
out vec3 fs_WorldPos;

void main() {
    // 雨：细长竖条(0.06 x 0.8)；雪：小方块(0.14)
    vec2 size = mix(vec2(0.06, 0.8), vec2(0.14, 0.14), u_Snow);
    // 雨永远竖直(用世界up)，雪是完整billboard(用相机up)
    vec3 up = mix(vec3(0.0, 1.0, 0.0), u_CameraUp, u_Snow);
    vec3 pos = vs_InstancePos.xyz
             + u_CameraRight * vs_Corner.x * size.x
             + up           * vs_Corner.y * size.y;
    fs_UV = vs_Corner + 0.5;
    fs_Seed = vs_InstancePos.w;
    fs_WorldPos = pos;
    gl_Position = u_ViewProj * vec4(pos, 1.0);
}
