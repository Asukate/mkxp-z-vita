
#ifdef MKXPZ_VITA
uniform vec2 projScale;
#else
uniform mat4 projMat;
#endif
attribute vec2 position;

void main()
{
#ifdef MKXPZ_VITA
	gl_Position = vitaProject(vec4(position, 0, 1), projScale);
#else
	gl_Position = projMat * vec4(position, 0, 1);
#endif
}
