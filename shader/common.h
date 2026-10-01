#ifdef GLSLES

#ifdef FRAGMENT_SHADER
/* Only the fragment shader has no default float precision */
precision mediump float;
#endif

#ifdef MKXPZ_VITA
vec4 vitaProject(vec4 position, vec2 scale)
{
	return vec4(position.x * scale.x - position.w,
	            position.y * scale.y - position.w,
	            -2.0 * position.z - position.w,
	            position.w);
}
#endif

#else

/* Desktop GLSL doesn't know about these */
#define highp
#define mediump
#define lowp

#endif
