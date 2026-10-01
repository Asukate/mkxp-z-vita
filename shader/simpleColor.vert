
#ifdef MKXPZ_VITA
uniform vec2 projScale;
#else
uniform mat4 projMat;
#endif

uniform vec2 texSizeInv;
uniform vec2 translation;

attribute vec2 position;
attribute vec2 texCoord;
attribute lowp vec4 color;

varying vec2 v_texCoord;
varying lowp vec4 v_color;

void main()
{
#ifdef MKXPZ_VITA
	gl_Position = vitaProject(vec4(position + translation, 0, 1), projScale);
#else
	gl_Position = projMat * vec4(position + translation, 0, 1);
#endif

	v_texCoord = texCoord * texSizeInv;
	v_color = color;
}
