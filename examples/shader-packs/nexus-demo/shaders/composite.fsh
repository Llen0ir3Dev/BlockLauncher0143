#version 120

uniform sampler2D colortex0;
uniform vec4 fogColor;

varying vec2 texcoord;

void main() {
    vec3 color = texture2D(colortex0, texcoord).rgb;
    color = mix(color, fogColor.rgb, 0.05);
    gl_FragColor = vec4(color, 1.0);
}
