uniform vec2 projScale;
uniform vec2 translation;
uniform vec4 mappingA;
uniform vec4 mappingB;
uniform vec4 mappingC;
uniform vec4 mappingD;
attribute vec2 position;
varying vec2 uvA;
varying vec2 uvB;
varying vec2 uvC;
varying vec2 uvD;

void main()
{
    gl_Position = vitaProject(vec4(position + translation, 0, 1), projScale);
    uvA = position * mappingA.xy + mappingA.zw;
    uvB = position * mappingB.xy + mappingB.zw;
    uvC = position * mappingC.xy + mappingC.zw;
    uvD = position * mappingD.xy + mappingD.zw;
}
