#version 430
layout(std430, binding = 1) readonly buffer field { vec2 cells[]; };
layout(std140, binding = 4) readonly buffer palette { uint n; vec4 colors[]; };
layout(location = 0) uniform int worldSize;
layout(location = 1) uniform int fieldSize;
layout(location = 2) uniform vec2 sourceCenter;
layout(location = 3) uniform vec2 position;
layout(location = 4) uniform float angle;
layout(location = 5) uniform float fit;
layout(location = 6) uniform bool background;
out vec4 fragColor;

float sampleState(ivec2 p) {
    p = (p % fieldSize + fieldSize) % fieldSize;
    return cells[p.x + p.y * fieldSize].x;
}

void main() {
    vec2 point = vec2(gl_FragCoord.x, float(worldSize) - gl_FragCoord.y);
    float radius = (float(worldSize) * 0.5 - 8.0) * 0.9;
    float distance = length(point - vec2(float(worldSize) * 0.5));
    if (background) {
        fragColor = distance <= radius && distance >= radius - 2.0
            ? vec4(vec3(100.0, 150.0, 190.0) / 255.0, 1.0) : vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }
    if (distance > radius - 2.0) discard;
    vec2 delta = (point - position) / fit;
    float c = cos(angle), s = sin(angle);
    vec2 source = vec2(c * delta.x + s * delta.y, -s * delta.x + c * delta.y) + sourceCenter;
    ivec2 base = ivec2(floor(source));
    vec2 fraction = fract(source);
    float state = mix(mix(sampleState(base), sampleState(base + ivec2(1, 0)), fraction.x),
                      mix(sampleState(base + ivec2(0, 1)), sampleState(base + ivec2(1, 1)), fraction.x), fraction.y);
    if (state <= 0.02) discard;
    float t = clamp(state, 0.0, 1.0) * float(n - 1u);
    uint index = uint(floor(t));
    fragColor = vec4(mix(colors[index].rgb, colors[min(index + 1u, n - 1u)].rgb, fract(t)), 1.0);
}
