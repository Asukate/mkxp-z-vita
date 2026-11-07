uniform sampler2D sourceA;
uniform sampler2D sourceB;
uniform sampler2D sourceC;
uniform sampler2D sourceD;
uniform highp vec4 weights;
uniform highp vec4 background;
varying highp vec2 uvA;
varying highp vec2 uvB;
varying highp vec2 uvC;
varying highp vec2 uvD;

highp vec3 blendLayer(highp vec3 rgb, highp vec4 color, highp float opacity)
{
    highp vec4 source = floor(color * 255.0 + 0.5);
    highp float a = floor((source.a * opacity + 127.0) * (1.0 / 255.0));
    highp vec3 sum = source.rgb * a + rgb * (255.0 - a);
    // The CPU's reciprocal[255] is 257 in 16.16. The largest numerator
    // is 65025, so the rounded product fits the exact float32 integer range.
    return floor(sum * (257.0 / 65536.0) + 0.5);
}

void main()
{
    highp vec3 rgb = floor(background.rgb * 255.0 + 0.5);
    rgb = blendLayer(rgb, texture2D(sourceA, uvA), weights.x);
    if (weights.y > 0.0)
        rgb = blendLayer(rgb, texture2D(sourceB, uvB), weights.y);
    if (weights.z > 0.0)
        rgb = blendLayer(rgb, texture2D(sourceC, uvC), weights.z);
    if (weights.w > 0.0)
        rgb = blendLayer(rgb, texture2D(sourceD, uvD), weights.w);
    gl_FragColor = vec4(rgb * (1.0 / 255.0), 1.0);
}
