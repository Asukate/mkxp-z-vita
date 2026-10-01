/*
** shader.h
**
** This file is part of mkxp.
**
** Copyright (C) 2013 - 2021 Amaryllis Kulla <ancurio@mapleshrine.eu>
**
** mkxp is free software: you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation, either version 2 of the License, or
** (at your option) any later version.
**
** mkxp is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with mkxp.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef SHADER_H
#define SHADER_H

#include "etc-internal.h"
#include "gl-util.h"
#include "glstate.h"

class ShaderNoConstructTag
{
public:
	explicit ShaderNoConstructTag(const char *name = 0)
	    : name(name)
	{}

	const char *name;
};

class Shader
{
public:
	void bind();
	static void unbind();

	enum Attribute
	{
		Position = 0,
		TexCoord = 1,
		Color = 2
	};
    
    static std::string &commonHeader();

protected:
	explicit Shader(const char *constructionName = 0);
	~Shader();

    void init(const unsigned char *vert, int vertSize,
              const unsigned char *frag, int fragSize,
	          const char *vertName, const char *fragName,
	          const char *programName);
	void initFromFile(const char *vertFile, const char *fragFile,
	                  const char *programName);

	static void setVec4Uniform(GLint location, const Vec4 &vec);
    static void setVec2Uniform(GLint location, const Vec2 &vec);
	static void setTexUniform(GLint location, unsigned unitIndex, TEX::ID texture);

	GLuint vertShader, fragShader;
	GLuint program;
	bool initialized;
	const char *diagnosticConstructionName;
	const char *diagnosticProgramName;
    
private:
#ifdef MKXPZ_BUILD_XCODE
    static std::string shaderCommon;
#endif
};

class ShaderBase : public Shader
{
public:

	struct GLProjMat : public GLProperty<Vec2i>
	{
	private:
		void apply(const Vec2i &value);
#ifdef __vita__
		GLint u_scale;
#else
		GLint u_mat;
#endif

		friend class ShaderBase;
	};

	/* Stack is not used (only 'set()') */
	GLProjMat projMat;

	/* Retrieves the current glState.viewport size,
	 * calculates the corresponding ortho projection matrix
	 * and loads it into the shaders uniform */
	void applyViewportProj();

	void setTexSize(const Vec2i &value);
	void setTranslation(const Vec2i &value);

protected:
	explicit ShaderBase(const char *constructionName = 0);
	void init();
	virtual bool framebufferScalingAllowed();

	GLint u_texSizeInv, u_translation;
};

class FlatColorShader : public ShaderBase
{
public:
	FlatColorShader();

	void setColor(const Vec4 &value);

private:
	GLint u_color;
};

class SimpleShader : public ShaderBase
{
public:
	SimpleShader();
	SimpleShader(const ShaderNoConstructTag &);

	void setTexOffsetX(int value);

protected:
	GLint u_texOffsetX;
};

class SimpleColorShader : public ShaderBase
{
public:
	SimpleColorShader();
};

class SimpleAlphaShader : public ShaderBase
{
public:
	SimpleAlphaShader();
};

class SimpleSpriteShader : public ShaderBase
{
public:
	SimpleSpriteShader();
	SimpleSpriteShader(const ShaderNoConstructTag &);

	void setSpriteMat(const float value[16]);

protected:
	GLint u_spriteMat;
};

class AlphaSpriteShader : public ShaderBase
{
public:
	AlphaSpriteShader();

	void setSpriteMat(const float value[16]);
	void setAlpha(float value);

private:
	GLint u_spriteMat, u_alpha;
};

class TransShader : public ShaderBase
{
public:
	TransShader();

	void setCurrentScene(TEX::ID tex);
	void setFrozenScene(TEX::ID tex);
	void setTransMap(TEX::ID tex);
	void setProg(float value);
	void setVague(float value);

private:
	GLint u_currentScene, u_frozenScene, u_transMap, u_prog, u_vague;
};

class SimpleTransShader : public ShaderBase
{
public:
	SimpleTransShader();

	void setCurrentScene(TEX::ID tex);
	void setFrozenScene(TEX::ID tex);
	void setProg(float value);

private:
	GLint u_currentScene, u_frozenScene, u_prog;
};

class SpriteShader : public ShaderBase
{
public:
	SpriteShader();

	void setSpriteMat(const float value[16]);
	void setTone(const Vec4 &value);
	void setColor(const Vec4 &value);
	void setOpacity(float value);
	void setBushDepth(bool bushY, bool bushUnder, float bushSlope, float bushIntercept);
	void setBushOpacity(float value);
    void setPattern(const TEX::ID pattern, const Vec2 &dimensions);
    void setPatternBlendType(int blendType);
    void setPatternTile(bool value);
    void setShouldRenderPattern(bool value);
    void setPatternOpacity(float value);
    void setPatternScroll(const Vec2 &scroll);
    void setPatternZoom(const Vec2 &zoom);
    void setInvert(bool value);

private:
	GLint u_spriteMat, u_tone, u_opacity, u_color,
    u_bushY, u_bushUnder, u_bushSlope, u_bushIntercept, u_bushOpacity, u_pattern, u_renderPattern,
    u_patternBlendType, u_patternSizeInv, u_patternTile, u_patternOpacity, u_patternScroll, u_patternZoom, u_invert;
};

class PlaneShader : public ShaderBase
{
public:
	PlaneShader();

	void setTone(const Vec4 &value);
	void setColor(const Vec4 &value);
	void setFlash(const Vec4 &value);
	void setOpacity(float value);

private:
	GLint u_tone, u_color, u_flash, u_opacity;
};

class GrayShader : public ShaderBase
{
public:
	GrayShader();

	void setGray(float value);

protected:
	virtual bool framebufferScalingAllowed();

private:
	GLint u_gray;
};

class TilemapShader : public ShaderBase
{
public:
	TilemapShader();

	void setAnimation(int value, int frames[7]);

	void setTone(const Vec4 &value);
	void setColor(const Vec4 &value);
	void setOpacity(float value);

private:
	GLint u_cpuAniOffsets, u_aniIndex, u_tone, u_color, u_opacity, u_atFrames;
};

class FlashMapShader : public ShaderBase
{
public:
	FlashMapShader();

	void setAlpha(float value);

private:
	GLint u_alpha;
};

class HueShader : public ShaderBase
{
public:
	HueShader();

	void setHueAdjust(float value);

private:
	GLint u_hueAdjust;
};

class SimpleMatrixShader : public ShaderBase
{
public:
	SimpleMatrixShader();

	void setMatrix(const float value[16]);

private:
	GLint u_matrix;
};

/* Gaussian blur */
struct BlurShader
{
	class HPass : public ShaderBase
	{
	public:
		HPass();
	};

	class VPass : public ShaderBase
	{
	public:
		VPass();
	};

	HPass pass1;
	VPass pass2;
};

class TilemapVXShader : public ShaderBase
{
public:
	TilemapVXShader();

	void setAniOffset(const Vec2 &value);

private:
	GLint u_aniOffset;
};

/* Bitmap blit */
class BltShader : public ShaderBase
{
public:
	BltShader();
	BltShader(const ShaderNoConstructTag &);

	void init();

	void setSource();
	void setDestination(const TEX::ID value);
	void setDestCoorF(const Vec2 &value);
	void setSubRect(const FloatRect &value);
	void setOpacity(float value);

private:
	GLint u_source, u_destination, u_subRect, u_opacity;
};

class KglInvertShader : public ShaderBase
{
public:
	KglInvertShader();
};

class KglCompressAlphaShader : public ShaderBase
{
public:
	KglCompressAlphaShader();
};

class KglSubtractShader : public BltShader
{
public:
	KglSubtractShader();
};

class KglShadowShaderH : public ShaderBase
{
public:
	KglShadowShaderH();

	void setParams(int x1, int x2, int y, bool soft, int w, int h, int x_center, int y_center, double slope1, double slope2);

private:
	GLint u_x1, u_x2, u_y, u_soft, u_w, u_h, u_x_center, u_y_center, u_slope1, u_slope2;
};

class KglShadowShaderV : public ShaderBase
{
public:
	KglShadowShaderV();

	void setParams(int y1, int y2, int x, bool wall, bool soft, int w, int h, int x_center, int y_center, double slope1, double slope2);

private:
	GLint u_y1, u_y2, u_x, u_wall, u_soft, u_w, u_h, u_x_center, u_y_center, u_slope1, u_slope2;
};

class Lanczos3Shader : public SimpleShader
{
public:
	Lanczos3Shader();
	Lanczos3Shader(const ShaderNoConstructTag &);

	void setTexSize(const Vec2i &value);

protected:
	GLint u_sourceSize;
};

class BicubicShader : public Lanczos3Shader
{
public:
	BicubicShader();

	void setSharpness(int sharpness);

protected:
	GLint u_bc;
};

#ifdef MKXPZ_SSL
class XbrzShader : public Lanczos3Shader
{
public:
	XbrzShader();

	void setTargetScale(const Vec2 &value);

protected:
	GLint u_targetScale;
};
#endif

class Lanczos3SpriteShader : public SimpleSpriteShader
{
public:
	Lanczos3SpriteShader();
	Lanczos3SpriteShader(const ShaderNoConstructTag &);

	void setTexSize(const Vec2i &value);

protected:
	GLint u_sourceSize;
};

class BicubicSpriteShader : public Lanczos3SpriteShader
{
public:
	BicubicSpriteShader();

	void setSharpness(int sharpness);

protected:
	GLint u_bc;
};

class XbrzSpriteShader : public Lanczos3SpriteShader
{
public:
	XbrzSpriteShader();

	void setTargetScale(const Vec2 &value);

protected:
	GLint u_targetScale;
};

/* Vita's runtime GLSL compiler is slow enough that constructing every shader
 * before the first frame accounts for most of the game's boot time.  Keep the
 * desktop behavior unchanged, but construct Vita shaders on first use. */
#ifdef __vita__
template<class T>
class LazyShader
{
public:
	LazyShader()
	    : instance(0)
	{}

	~LazyShader()
	{
		delete instance;
	}

	operator T&()
	{
		return get();
	}

	T *operator&()
	{
		return &get();
	}

private:
	T &get()
	{
		if (!instance)
			instance = new T();
		return *instance;
	}

	LazyShader(const LazyShader &);
	LazyShader &operator=(const LazyShader &);

	T *instance;
};

#define SHADER_SET_MEMBER(type, name) LazyShader<type> name
#else
#define SHADER_SET_MEMBER(type, name) type name
#endif

/* Global object containing all available shaders */
struct ShaderSet
{
	explicit ShaderSet(const char *constructionName = 0);

	SHADER_SET_MEMBER(FlatColorShader, flatColor);
	SHADER_SET_MEMBER(SimpleShader, simple);
	SHADER_SET_MEMBER(SimpleColorShader, simpleColor);
	SHADER_SET_MEMBER(SimpleAlphaShader, simpleAlpha);
	SHADER_SET_MEMBER(SimpleSpriteShader, simpleSprite);
	SHADER_SET_MEMBER(AlphaSpriteShader, alphaSprite);
	SHADER_SET_MEMBER(SpriteShader, sprite);
	SHADER_SET_MEMBER(PlaneShader, plane);
	SHADER_SET_MEMBER(GrayShader, gray);
	SHADER_SET_MEMBER(TilemapShader, tilemap);
	SHADER_SET_MEMBER(FlashMapShader, flashMap);
	SHADER_SET_MEMBER(TransShader, trans);
	SHADER_SET_MEMBER(SimpleTransShader, simpleTrans);
	SHADER_SET_MEMBER(HueShader, hue);
	SHADER_SET_MEMBER(BltShader, blt);
	SHADER_SET_MEMBER(SimpleMatrixShader, simpleMatrix);
	SHADER_SET_MEMBER(BlurShader, blur);
	SHADER_SET_MEMBER(TilemapVXShader, tilemapVX);
	SHADER_SET_MEMBER(KglInvertShader, kglInvert);
	SHADER_SET_MEMBER(KglCompressAlphaShader, kglCompressAlpha);
	SHADER_SET_MEMBER(KglSubtractShader, kglSubtract);
	SHADER_SET_MEMBER(KglShadowShaderH, kglShadowH);
	SHADER_SET_MEMBER(KglShadowShaderV, kglShadowV);
	SHADER_SET_MEMBER(BicubicShader, bicubic);
	SHADER_SET_MEMBER(Lanczos3Shader, lanczos3);
#ifdef MKXPZ_SSL
	SHADER_SET_MEMBER(XbrzShader, xbrz);
#endif
	SHADER_SET_MEMBER(Lanczos3SpriteShader, lanczos3Sprite);
	SHADER_SET_MEMBER(BicubicSpriteShader, bicubicSprite);
#ifdef MKXPZ_SSL
	SHADER_SET_MEMBER(XbrzSpriteShader, xbrzSprite);
#endif
};

#undef SHADER_SET_MEMBER

void vitaShaderDiagUseProgramBefore(GLuint program);
void vitaShaderDiagUseProgramAfter(GLuint program);

#endif // SHADER_H
