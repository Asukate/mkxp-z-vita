
#ifdef MKXPZ_VITA
uniform vec2 projScale;
#else
uniform mat4 projMat;
#endif

uniform mat4 spriteMat;

uniform vec2 texSizeInv;
uniform vec2 sourceOffset;
uniform vec2 sourceSizeInv;
uniform vec2 patternSizeInv;
uniform vec2 patternScroll;
uniform vec2 patternZoom;
uniform bool renderPattern;
uniform bool patternTile;

attribute vec2 position;
attribute vec2 texCoord;

varying vec2 v_texCoord;
varying vec2 v_sourceCoord;
varying vec2 v_patCoord;

void main()
{
#ifdef MKXPZ_VITA
	gl_Position = vitaProject(spriteMat * vec4(position, 0, 1), projScale);
#else
	gl_Position = projMat * spriteMat * vec4(position, 0, 1);
#endif
    
    v_texCoord = texCoord * texSizeInv;
    vec2 sourceCoord = texCoord + sourceOffset;
    v_sourceCoord = sourceCoord * sourceSizeInv;
    
    if (renderPattern) {
        if (patternTile) {
            vec2 scroll = patternScroll * (patternSizeInv / sourceSizeInv);
            v_patCoord = (sourceCoord * (patternSizeInv / patternZoom)) - (scroll * patternSizeInv);
        }
        else {
            vec2 scroll = patternScroll * (patternSizeInv / sourceSizeInv);
            v_patCoord = (sourceCoord * (sourceSizeInv / patternZoom)) - (scroll * sourceSizeInv);
        }
    }
}
