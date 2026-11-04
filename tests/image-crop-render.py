#!/usr/bin/env python3
"""Mesa comparison of sparse storage using the production Sprite geometry and shaders."""
import os, runpy, subprocess, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
base = runpy.run_path(str(ROOT / 'tests/sprite-mega.py'))
preamble = '#include "display/image-crop.h"\n' + base['PREAMBLE']
preamble = preamble.replace('GLuint tex=0;', 'GLuint tex=0;ImageCrop crop;bool enableCrop=false;')
preamble = preamble.replace('glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());',
    'auto packed=pixels;if(enableCrop)crop=findImageCrop(packed.data(),w,h,w*4);\n'
    'if(crop.active())packImageCrop(packed.data(),w*4,crop);\n'
    'glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,crop.active()?crop.width:w,crop.active()?crop.height:h,0,GL_RGBA,GL_UNSIGNED_BYTE,packed.data());')
source = (ROOT / 'src/display/sprite.cpp').read_text()
private = source[source.index('struct SpritePrivate\n{'):source.index('\n};', source.index('struct SpritePrivate\n{'))+3]
helpers = base['CHECKS'].split('int main(int argc,char **argv) try {')[0]
helpers = helpers.replace('u2(shader,"texSizeInv",1.f/b->w,1.f/b->h);',
    'u2(shader,"texSizeInv",1.f/(b->crop.active()?b->crop.width:b->w),1.f/(b->crop.active()?b->crop.height:b->h));'
    'u2(shader,"texOrigin",b->crop.active()?b->crop.x:0,b->crop.active()?b->crop.y:0);')
helpers = helpers.replace('GLuint program(const std::string &root){', 'GLuint program(const std::string &root,bool simple=false){')
helpers = helpers.replace('read(root+"/shader/sprite.vert")', 'read(root+(simple?"/shader/simpleMatrix.vert":"/shader/sprite.vert"))')
helpers = helpers.replace('read(root+"/shader/sprite.frag")', 'read(root+(simple?"/shader/simple.frag":"/shader/sprite.frag"))')
helpers = helpers.replace('glUniformMatrix4fv(glGetUniformLocation(shader,"spriteMat"),1,GL_FALSE,p.trans.getMatrix());',
    'glUniformMatrix4fv(glGetUniformLocation(shader,"spriteMat"),1,GL_FALSE,p.trans.getMatrix());'
    'glUniformMatrix4fv(glGetUniformLocation(shader,"matrix"),1,GL_FALSE,p.trans.getMatrix());')
main = r'''int main(int argc,char **argv) try {
 SDL_Init(SDL_INIT_VIDEO);SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,2);SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,1);
 auto *window=SDL_CreateWindow("sparse image",0,0,128,128,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);
 expect(window,"window");auto context=SDL_GL_CreateContext(window);expect(context,"context");glewInit();glGetError();glViewport(0,0,128,128);
 GLuint programs[2]={program(argv[1]),program(argv[1],true)};
 {
 Bitmap full(2048,2048),small(2048,2048),pattern(16,16);seed(pattern);
 auto *data=reinterpret_cast<uint32_t *>(full.pixels.data());std::fill(data,data+2048*2048,0x00ffffff);
 for(int y=310;y<375;++y)for(int x=240;x<306;++x)
  data[y*2048+x]=(uint32_t((x+y)%251+1)<<24)|uint32_t(x*3%256)|(uint32_t(y*7%256)<<8)|0x00340000;
 small.pixels=full.pixels;small.enableCrop=true;
 Sprite a,b;a.setBitmap(&full);b.setBitmap(&small);
 for(GLuint shader:programs)for(int scenario=0;scenario<30;++scenario){
  for(Sprite *s:{&a,&b}){
   select(*s,215+scenario%3*9,290+scenario%4*11,96,96);
   auto &p=*s->p;p.trans.setPosition(Vec2(62,61));p.trans.setOrigin(Vec2(48,48));
   p.trans.setScale(Vec2(1.19f,1.07f));p.trans.setRotation(scenario%3?0:21);
   p.mirrored=scenario%2;p.onSrcRectChange();p.bushDepth=scenario%3==0?19:0;p.bushDirty=true;
   p.tone->norm=Vec4(.06f,-.03f,.02f,.17f);p.color->norm=Vec4(.8f,.1f,.2f,.12f);
   p.invert=scenario%5==0;p.wave.amp=scenario>=15?4:0;p.wave.dirty=true;p.prepare();
  }
  Effect effect;effect.pattern=scenario%3;effect.smooth=scenario%2;effect.opacity=.73f;
  compare(render(a,shader,pattern,effect),render(b,shader,pattern,effect),"cropped storage, filtering and sprite effects");
  expect(small.crop.active()&&small.crop.width<100&&small.width()==2048,"storage/logical dimensions");
 }
 }
 for(auto shader:programs)glDeleteProgram(shader);SDL_GL_DeleteContext(context);SDL_DestroyWindow(window);SDL_Quit();
 puts("PASS Mesa: production Sprite geometry and plain/effect shaders, 60 crop-boundary/mirror/zoom/rotation/wave/scissor/bilinear/pattern/bush/tone/alpha combinations");
} catch(const Exception &e){std::cerr<<e.msg.c_str()<<'\n';return 1;}
catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
'''
cpp = preamble + private + base['SPRITE'] + base['function'](source, 'void Sprite::setBitmap(Bitmap *bitmap)\n{') + helpers + main
with tempfile.TemporaryDirectory(prefix='hardrpg-image-crop-') as name:
    path=Path(name);(path/'check.cpp').write_text(cpp)
    flags=subprocess.check_output(['pkg-config','--cflags','--libs','sdl2','glew'],text=True).split()
    includes=['-I'+str(ROOT/'src')]+['-I'+str(ROOT/'src'/p) for p in ('etc','util','display/gl')]
    subprocess.run(['c++','-std=c++17','-O1','-g','-fsanitize=address,undefined',*includes,str(path/'check.cpp'),*flags,'-o',str(path/'check')],check=True)
    env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0',UBSAN_OPTIONS='halt_on_error=1',LIBGL_ALWAYS_SOFTWARE='1')
    subprocess.run(['xvfb-run','-a',str(path/'check'),str(ROOT)],env=env,check=True)
