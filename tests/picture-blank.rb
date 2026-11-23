# Check HardRPGPictureBlank (launcher/vxace_map_speedups.rb): with the first release's default
# Sprite_Picture and Cache, an erased picture keeps one blank bitmap instead of a new one each frame;
# a shown picture still gets its cached bitmap; another update_bitmap keeps it off.
src = File.read(File.expand_path('../launcher/native_events.rb', __dir__), encoding: 'UTF-8')
eval("module HardRPGNative\n#{src[/  module Norm.*?\n  end\n/m]}\nend")
module Graphics; def self.update; end; end
module Kernel; private def rgss_main; end; end
$hardrpg_vxace_scan_cache = nil
$RGSS_SCRIPTS = []
require_relative '../launcher/vxace_map_speedups'

def check(label, ok)
  raise "#{label} failed" unless ok
end

$made = 0
class Bitmap
  attr_reader :width, :height, :path
  def initialize(a, b = nil)
    $made += 1
    @path, @width, @height = b ? [nil, a, b] : [a, 1, 1]
  end
  def disposed? = false
  def hue_change(*) = nil
end
class Sprite
  attr_accessor :bitmap, :ox, :oy, :x, :y, :z, :zoom_x, :zoom_y, :opacity, :blend_type, :angle, :tone, :viewport
  def initialize(vp = nil)
    @viewport = vp
    @tone = Tone.new
  end
  def update; end
  def dispose; end
end
class Tone; def set(*) = self; end
Game_Picture = Struct.new(:name, :origin, :x, :y, :z, :zoom_x, :zoom_y, :opacity, :blend_type, :angle, :tone, :number)
CACHE = <<~'RUBY'
#==============================================================================
# ■ Cache
#------------------------------------------------------------------------------
# 　各種グラフィックを読み込み、Bitmap オブジェクトを作成、保持するモジュール
# です。読み込みの高速化とメモリ節約のため、作成した Bitmap オブジェクトを内部
# のハッシュに保存し、同じビットマップが再度要求されたときに既存のオブジェクト
# を返すようになっています。
#==============================================================================

module Cache
  #--------------------------------------------------------------------------
  # ● アニメーション グラフィックの取得
  #--------------------------------------------------------------------------
  def self.animation(filename, hue)
    load_bitmap("Graphics/Animations/", filename, hue)
  end
  #--------------------------------------------------------------------------
  # ● 戦闘背景（床）グラフィックの取得
  #--------------------------------------------------------------------------
  def self.battleback1(filename)
    load_bitmap("Graphics/Battlebacks1/", filename)
  end
  #--------------------------------------------------------------------------
  # ● 戦闘背景（壁）グラフィックの取得
  #--------------------------------------------------------------------------
  def self.battleback2(filename)
    load_bitmap("Graphics/Battlebacks2/", filename)
  end
  #--------------------------------------------------------------------------
  # ● 戦闘グラフィックの取得
  #--------------------------------------------------------------------------
  def self.battler(filename, hue)
    load_bitmap("Graphics/Battlers/", filename, hue)
  end
  #--------------------------------------------------------------------------
  # ● 歩行グラフィックの取得
  #--------------------------------------------------------------------------
  def self.character(filename)
    load_bitmap("Graphics/Characters/", filename)
  end
  #--------------------------------------------------------------------------
  # ● 顔グラフィックの取得
  #--------------------------------------------------------------------------
  def self.face(filename)
    load_bitmap("Graphics/Faces/", filename)
  end
  #--------------------------------------------------------------------------
  # ● 遠景グラフィックの取得
  #--------------------------------------------------------------------------
  def self.parallax(filename)
    load_bitmap("Graphics/Parallaxes/", filename)
  end
  #--------------------------------------------------------------------------
  # ● ピクチャ グラフィックの取得
  #--------------------------------------------------------------------------
  def self.picture(filename)
    load_bitmap("Graphics/Pictures/", filename)
  end
  #--------------------------------------------------------------------------
  # ● システム グラフィックの取得
  #--------------------------------------------------------------------------
  def self.system(filename)
    load_bitmap("Graphics/System/", filename)
  end
  #--------------------------------------------------------------------------
  # ● タイルセット グラフィックの取得
  #--------------------------------------------------------------------------
  def self.tileset(filename)
    load_bitmap("Graphics/Tilesets/", filename)
  end
  #--------------------------------------------------------------------------
  # ● タイトル（背景）グラフィックの取得
  #--------------------------------------------------------------------------
  def self.title1(filename)
    load_bitmap("Graphics/Titles1/", filename)
  end
  #--------------------------------------------------------------------------
  # ● タイトル（枠）グラフィックの取得
  #--------------------------------------------------------------------------
  def self.title2(filename)
    load_bitmap("Graphics/Titles2/", filename)
  end
  #--------------------------------------------------------------------------
  # ● ビットマップの読み込み
  #--------------------------------------------------------------------------
  def self.load_bitmap(folder_name, filename, hue = 0)
    @cache ||= {}
    if filename.empty?
      empty_bitmap
    elsif hue == 0
      normal_bitmap(folder_name + filename)
    else
      hue_changed_bitmap(folder_name + filename, hue)
    end
  end
  #--------------------------------------------------------------------------
  # ● 空のビットマップを作成
  #--------------------------------------------------------------------------
  def self.empty_bitmap
    Bitmap.new(32, 32)
  end
  #--------------------------------------------------------------------------
  # ● 通常のビットマップを作成／取得
  #--------------------------------------------------------------------------
  def self.normal_bitmap(path)
    @cache[path] = Bitmap.new(path) unless include?(path)
    @cache[path]
  end
  #--------------------------------------------------------------------------
  # ● 色相変化済みビットマップを作成／取得
  #--------------------------------------------------------------------------
  def self.hue_changed_bitmap(path, hue)
    key = [path, hue]
    unless include?(key)
      @cache[key] = normal_bitmap(path).clone
      @cache[key].hue_change(hue)
    end
    @cache[key]
  end
  #--------------------------------------------------------------------------
  # ● キャッシュ存在チェック
  #--------------------------------------------------------------------------
  def self.include?(key)
    @cache[key] && !@cache[key].disposed?
  end
  #--------------------------------------------------------------------------
  # ● キャッシュのクリア
  #--------------------------------------------------------------------------
  def self.clear
    @cache ||= {}
    @cache.clear
    GC.start
  end
end
RUBY
PICTURE = <<~'RUBY'
#==============================================================================
# ■ Sprite_Picture
#------------------------------------------------------------------------------
# 　ピクチャ表示用のスプライトです。Game_Picture クラスのインスタンスを監視し、
# スプライトの状態を自動的に変化させます。
#==============================================================================

class Sprite_Picture < Sprite
  #--------------------------------------------------------------------------
  # ● オブジェクト初期化
  #     picture : Game_Picture
  #--------------------------------------------------------------------------
  def initialize(viewport, picture)
    super(viewport)
    @picture = picture
    update
  end
  #--------------------------------------------------------------------------
  # ● 解放
  #--------------------------------------------------------------------------
  def dispose
    bitmap.dispose if bitmap
    super
  end
  #--------------------------------------------------------------------------
  # ● フレーム更新
  #--------------------------------------------------------------------------
  def update
    super
    update_bitmap
    update_origin
    update_position
    update_zoom
    update_other
  end
  #--------------------------------------------------------------------------
  # ● 転送元ビットマップの更新
  #--------------------------------------------------------------------------
  def update_bitmap
    self.bitmap = Cache.picture(@picture.name)
  end
  #--------------------------------------------------------------------------
  # ● 原点の更新
  #--------------------------------------------------------------------------
  def update_origin
    if @picture.origin == 0
      self.ox = 0
      self.oy = 0
    else
      self.ox = bitmap.width / 2
      self.oy = bitmap.height / 2
    end
  end
  #--------------------------------------------------------------------------
  # ● 位置の更新
  #--------------------------------------------------------------------------
  def update_position
    self.x = @picture.x
    self.y = @picture.y
    self.z = @picture.number
  end
  #--------------------------------------------------------------------------
  # ● 拡大率の更新
  #--------------------------------------------------------------------------
  def update_zoom
    self.zoom_x = @picture.zoom_x / 100.0
    self.zoom_y = @picture.zoom_y / 100.0
  end
  #--------------------------------------------------------------------------
  # ● その他の更新
  #--------------------------------------------------------------------------
  def update_other
    self.opacity = @picture.opacity
    self.blend_type = @picture.blend_type
    self.angle = @picture.angle
    self.tone.set(@picture.tone)
  end
end
RUBY

[CACHE, PICTURE].each_with_index do |text, i|
  $RGSS_SCRIPTS << [i, "s#{i}", '', text]
  eval(text, TOPLEVEL_BINDING, "{#{i}}")
end

def picture(name) = Game_Picture.new(name, 0, 0, 0, 0, 100, 100, 255, 0, 0, Tone.new, 1)

# The default text alone: a new bitmap every frame.
pic = picture('')
spr = Sprite_Picture.new(nil, pic)
before = $made
5.times { spr.update }
check('default makes one a frame', $made - before == 5)

check('installs', HardRPGPictureBlank.install == 'on')
spr2 = Sprite_Picture.new(nil, pic)
first = spr2.bitmap
before = $made
60.times { spr2.update }
check('kept blank', $made == before && spr2.bitmap.equal?(first) && first.width == 32 && first.height == 32)
pic.name = 'Title'
spr2.update
check('shown picture', spr2.bitmap.path == 'Graphics/Pictures/Title')
spr2.update
check('cached', spr2.bitmap.equal?(Cache.picture('Title')))
pic.name = ''
before = $made
spr2.update
blank = spr2.bitmap
3.times { spr2.update }
check('erased again', $made == before + 1 && spr2.bitmap.equal?(blank) && blank.width == 32)

# Another update_bitmap: off.
HardRPGPictureBlank.instance_variable_set(:@status, nil)
$RGSS_SCRIPTS[1] = [1, 's1', '', PICTURE.sub('self.bitmap = Cache.picture(@picture.name)', 'self.bitmap = Cache.picture(@picture.name + "")')]
HardRPGVXAceScanCache.instance_variable_set(:@data, {})
check('other text off', HardRPGPictureBlank.install == 'off (other script)')
puts 'Picture blank checks passed'
