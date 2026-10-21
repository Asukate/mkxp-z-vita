// Sharp bilinear: scales pixel art by any factor while every source pixel
// keeps the same width. Inside a pixel the sample stays on its centre; only
// the screen pixel that straddles a boundary is blended. Needs the texture's
// linear filtering. Same idea as libretro's sharp-bilinear-simple (public domain).
#ifdef GLSLES
	precision highp float;
#endif
uniform sampler2D texture;
uniform vec2 sourceSize;
uniform vec2 outputScale;
varying vec2 v_texCoord;

void main()
{
	vec2 texel = v_texCoord * sourceSize;
	vec2 centre = fract(texel) - 0.5;
	vec2 region = 0.5 - 0.5 / max(outputScale, vec2(1.0));
	vec2 f = (centre - clamp(centre, -region, region)) * outputScale + 0.5;
	gl_FragColor = texture2D(texture, (floor(texel) + f) / sourceSize);
}
