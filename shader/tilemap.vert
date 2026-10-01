#ifdef MKXPZ_VITA
uniform vec2 projScale;
#else
uniform mat4 projMat;
#endif

uniform vec2 texSizeInv;
uniform vec2 translation;

attribute vec2 position;
attribute vec2 texCoord;

varying vec2 v_texCoord;

const int nAutotiles = 7;
const float tileW = 32.0;
const float tileH = 32.0;
const float autotileW = 3.0*tileW;
const float autotileH = 4.0*tileW;
const float atAreaW = autotileW;
const float atAreaH = autotileH*float(nAutotiles);
const float atAniOffsetX = 3.0*tileW;
const float atAniOffsetY = tileH;

#ifdef GLSLES
uniform lowp vec2 cpuAniOffsets[nAutotiles];
#else
uniform highp int aniIndex;
uniform lowp int atFrames[nAutotiles];
#endif

void main()
{
    vec2 tex = texCoord;
    lowp int atIndex = int(tex.y / autotileH);

    /*
     * Normal tiles live below the autotile area and can therefore derive
     * indices well above the 7-element animation uniform arrays. Multiplying
     * the fetched value by pred=0 does not make an out-of-bounds array access
     * defined. Clamp invalid indices to a safe slot before either shader path
     * indexes cpuAniOffsets/atFrames; valid autotiles are unchanged.
     */
    if (atIndex < 0 || atIndex >= nAutotiles)
        atIndex = 0;

    lowp int pred = int(tex.x <= atAreaW && tex.y <= atAreaH);
#ifdef GLSLES
    tex += cpuAniOffsets[atIndex] * float(pred);
#else
    lowp int frame = int(aniIndex - atFrames[atIndex] * (aniIndex / atFrames[atIndex]));
    lowp int tileRow = frame / 8;
    lowp int col = frame - 8 * tileRow;
    tex.x += atAniOffsetX * float(col * pred);
    tex.y += atAniOffsetY * float(tileRow * pred);
#endif

#ifdef MKXPZ_VITA
    gl_Position = vitaProject(vec4(position + translation, 0, 1), projScale);
#else
    gl_Position = projMat * vec4(position + translation, 0, 1);
#endif

    v_texCoord = tex * texSizeInv;
}
