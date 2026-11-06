// CRT-style post-processing effect for the sandbox (see PostProcess in the renderer docs).
//
// Edit this file while the sandbox runs: it is reloaded within half a second. A syntax error is
// logged and the previous version stays active until the file compiles again.
#version 330 core

in vec2 vUv;

uniform sampler2D uTexture;
uniform vec2 uResolution;
uniform float uTime;

out vec4 fragColor;

const float kScanlineStrength = 0.18; // darkening of every other line
const float kAberration = 1.5;        // colour fringe width in pixels at the screen edges
const float kVignette = 0.45;         // darkening towards the corners

void main()
{
    // Colour fringes grow towards the edges, like a lens that does not focus all colours alike.
    vec2 fromCenter = vUv - 0.5;
    vec2 shift = fromCenter * (kAberration / uResolution) * 2.0;
    vec3 color;
    color.r = texture(uTexture, vUv + shift).r;
    color.g = texture(uTexture, vUv).g;
    color.b = texture(uTexture, vUv - shift).b;

    // Scanlines: a gentle sine over the output rows, drifting slowly.
    float scanline = 0.5 + 0.5 * sin((gl_FragCoord.y + uTime * 20.0) * 3.14159);
    color *= 1.0 - kScanlineStrength * scanline;

    // Vignette.
    float vignette = 1.0 - kVignette * dot(fromCenter, fromCenter) * 2.0;
    color *= clamp(vignette, 0.0, 1.0);

    fragColor = vec4(color, 1.0);
}
