// opaqueCompose.frag for regions where 5 to 12 lights overlap: the same blend per layer.
uniform sampler2D source0;
uniform sampler2D source1;
uniform sampler2D source2;
uniform sampler2D source3;
uniform sampler2D source4;
uniform sampler2D source5;
uniform sampler2D source6;
uniform sampler2D source7;
uniform sampler2D source8;
uniform sampler2D source9;
uniform sampler2D source10;
uniform sampler2D source11;
uniform highp vec4 mapping0;
uniform highp vec4 mapping1;
uniform highp vec4 mapping2;
uniform highp vec4 mapping3;
uniform highp vec4 mapping4;
uniform highp vec4 mapping5;
uniform highp vec4 mapping6;
uniform highp vec4 mapping7;
uniform highp vec4 mapping8;
uniform highp vec4 mapping9;
uniform highp vec4 mapping10;
uniform highp vec4 mapping11;
uniform highp vec4 weightsA;
uniform highp vec4 weightsB;
uniform highp vec4 weightsC;
uniform highp vec4 background;
varying highp vec2 pos;

highp vec3 blendLayer(highp vec3 rgb, highp vec4 color, highp float opacity)
{
    highp vec4 source = floor(color * 255.0 + 0.5);
    // (x + 127) / 255 in integers: x + 127.5 keeps every quotient at least 1/510 from an integer,
    // so a reciprocal of 255 that is an ulp low on the GPU still floors the same.
    highp float a = floor((source.a * opacity + 127.5) * (1.0 / 255.0));
    highp vec3 sum = source.rgb * a + rgb * (255.0 - a);
    return floor(sum * (257.0 / 65536.0) + 0.5);
}

void main()
{
    highp vec3 rgb = floor(background.rgb * 255.0 + 0.5);
    rgb = blendLayer(rgb, texture2D(source0, pos * mapping0.xy + mapping0.zw), weightsA.x);
    if (weightsA.y > 0.0)
        rgb = blendLayer(rgb, texture2D(source1, pos * mapping1.xy + mapping1.zw), weightsA.y);
    if (weightsA.z > 0.0)
        rgb = blendLayer(rgb, texture2D(source2, pos * mapping2.xy + mapping2.zw), weightsA.z);
    if (weightsA.w > 0.0)
        rgb = blendLayer(rgb, texture2D(source3, pos * mapping3.xy + mapping3.zw), weightsA.w);
    if (weightsB.x > 0.0)
        rgb = blendLayer(rgb, texture2D(source4, pos * mapping4.xy + mapping4.zw), weightsB.x);
    if (weightsB.y > 0.0)
        rgb = blendLayer(rgb, texture2D(source5, pos * mapping5.xy + mapping5.zw), weightsB.y);
    if (weightsB.z > 0.0)
        rgb = blendLayer(rgb, texture2D(source6, pos * mapping6.xy + mapping6.zw), weightsB.z);
    if (weightsB.w > 0.0)
        rgb = blendLayer(rgb, texture2D(source7, pos * mapping7.xy + mapping7.zw), weightsB.w);
    if (weightsC.x > 0.0)
        rgb = blendLayer(rgb, texture2D(source8, pos * mapping8.xy + mapping8.zw), weightsC.x);
    if (weightsC.y > 0.0)
        rgb = blendLayer(rgb, texture2D(source9, pos * mapping9.xy + mapping9.zw), weightsC.y);
    if (weightsC.z > 0.0)
        rgb = blendLayer(rgb, texture2D(source10, pos * mapping10.xy + mapping10.zw), weightsC.z);
    if (weightsC.w > 0.0)
        rgb = blendLayer(rgb, texture2D(source11, pos * mapping11.xy + mapping11.zw), weightsC.w);
    gl_FragColor = vec4(rgb * (1.0 / 255.0), 1.0);
}
