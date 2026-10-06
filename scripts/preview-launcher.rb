#!/usr/bin/env ruby
# SDL desktop adapter: executes the production launcher Ruby, not a UI mockup.
# It checks launcher appearance/navigation, not the Vita renderer or game runtime.
require "fiddle/import"
require "fileutils"
require "optparse"
require "zlib"
require "json"
module HTTPLite; JSON = ::JSON; end

options = {data: nil, work: nil, screenshot: nil, keys: nil, demo: false, browse: []}
OptionParser.new do |parser|
  parser.banner = "usage: preview-launcher.rb --work DIR [--data-root DIR] [--demo] [--keys down,cross,...] [--screenshot PNG]"
  parser.on("--work DIR") { |v| options[:work] = File.expand_path(v) }
  parser.on("--data-root DIR") { |v| options[:data] = File.expand_path(v) }
  parser.on("--demo") { options[:demo] = true }
  parser.on("--browse-root DIR") { |v| options[:browse] << File.expand_path(v).chomp("/") + "/" }
  parser.on("--keys KEYS") { |v| options[:keys] = v.split(",") }
  parser.on("--screenshot PNG") { |v| options[:screenshot] = File.expand_path(v) }
end.parse!
abort "--work DIR is required for local preview output" unless options[:work]
ROOT = File.expand_path("..", __dir__)
FileUtils.mkdir_p(options[:work])
HARDRPG_DATA_ROOT = options[:data] || File.join(options[:work], "data/hardrpg")
HARDRPG_BROWSE_ROOTS = options[:browse].empty? ? [File.expand_path(options[:work]) + "/"] : options[:browse]
FileUtils.mkdir_p(HARDRPG_DATA_ROOT)
HARDRPG_PREVIEW_DRIVER = true
HARDRPG_SPLASH_PATH = File.join(ROOT, "launcher/assets/hardrpg-splash.png")
native = File.join(options[:work], "hardrpg_archive.so")
if !File.exist?(native) || File.mtime(native) < File.mtime(File.join(ROOT, "binding/launcher-filesystem-binding.cpp"))
  abort "Could not compile ZIP backend" unless system(RbConfig.ruby, File.join(__dir__, "build-host-archive.rb"), native)
end
require native
load File.join(ROOT, "launcher/library.rb")
HardRPG.setup
if options[:demo]
  abort "--demo requires the preview's own data directory" if options[:data]
  # Tiny metadata-only fixtures, no game data or artwork.
  names = ["Ao Oni", "BLACK SOULS", "BLACK SOULS II full ver.", "Hylics",
           "The Witch's House ver. 1.09a", "To the Moon", "Pokemon Infinite Fusion", "Red Hoods Woods"]
  names.each_with_index do |name, i|
    dir = File.join(HardRPG::GAMES_ROOT, name)
    FileUtils.mkdir_p(dir)
    File.write(File.join(dir, "Game.ini"), "[Game]\nTitle=#{name}\nLibrary=RGSS#{i == 0 ? '202' : '301'}.dll\n")
  end
end

module SDL
  extend Fiddle::Importer
  dlload "libSDL2-2.0.so.0", "libSDL2_ttf-2.0.so.0", "libSDL2_image-2.0.so.0"
  extern "void* IMG_Load(const char*)"
  extern "int SDL_Init(unsigned int)"
  extern "void SDL_Quit()"
  extern "void* SDL_CreateWindow(const char*, int, int, int, int, unsigned int)"
  extern "void* SDL_CreateRenderer(void*, int, unsigned int)"
  extern "void* SDL_CreateTexture(void*, unsigned int, int, int, int)"
  extern "int SDL_SetTextureBlendMode(void*, int)"
  extern "int SDL_SetRenderTarget(void*, void*)"
  extern "int SDL_SetRenderDrawColor(void*, unsigned char, unsigned char, unsigned char, unsigned char)"
  extern "int SDL_RenderClear(void*)"
  extern "int SDL_RenderFillRect(void*, void*)"
  extern "int SDL_RenderSetClipRect(void*, void*)"
  extern "int SDL_RenderCopy(void*, void*, void*, void*)"
  extern "void SDL_RenderPresent(void*)"
  extern "int SDL_RenderReadPixels(void*, void*, unsigned int, void*, int)"
  extern "int SDL_QueryTexture(void*, void*, void*, void*, void*)"
  extern "void* SDL_CreateTextureFromSurface(void*, void*)"
  extern "void SDL_FreeSurface(void*)"
  extern "void SDL_DestroyTexture(void*)"
  extern "void SDL_DestroyRenderer(void*)"
  extern "void SDL_DestroyWindow(void*)"
  extern "int SDL_PollEvent(void*)"
  extern "void SDL_Delay(unsigned int)"
  extern "const char* SDL_GetError()"
  extern "int TTF_Init()"
  extern "void* TTF_OpenFont(const char*, int)"
  extern "void* TTF_RenderUTF8_Blended(void*, const char*, unsigned int)"
  extern "void TTF_CloseFont(void*)"
  extern "void TTF_Quit()"
  RGBA32 = 0x16762004
end

Color = Struct.new(:red, :green, :blue, :alpha) do
  def initialize(red, green, blue, alpha = 255); super; end
  def bytes; [red, green, blue, alpha].map(&:to_i); end
end

class Font
  class << self; attr_accessor :default_name; end
  attr_accessor :size, :bold, :shadow, :outline, :color
  def initialize; @size, @bold, @shadow, @color = 23, false, true, Color.new(255, 255, 255); end
end

module Graphics
  class << self
    attr_accessor :fixed_aspect_ratio, :renderer
    def width; 960; end
    def height; 544; end
    def resize_screen(*); end
    def update
      @frame ||= SDL.SDL_CreateTexture(@renderer, SDL::RGBA32, 2, width, height)
      SDL.SDL_SetRenderTarget(@renderer, @frame)
      SDL.SDL_SetRenderDrawColor(@renderer, 0, 0, 0, 255)
      SDL.SDL_RenderClear(@renderer)
      Sprite.all.sort_by(&:z).each do |sprite|
        next unless sprite.visible && sprite.bitmap
        rect = [sprite.x, sprite.y, sprite.bitmap.width, sprite.bitmap.height].pack("i4")
        SDL.SDL_RenderCopy(@renderer, sprite.bitmap.texture, nil, rect)
      end
      SDL.SDL_SetRenderTarget(@renderer, nil)
      SDL.SDL_RenderCopy(@renderer, @frame, nil, nil)
      SDL.SDL_RenderPresent(@renderer)
      SDL.SDL_Delay(16)
    end
    def screenshot(path)
      # Read the rendered UI; this is a screenshot, not a generated art asset.
      pixels = "\0" * (width * height * 4)
      SDL.SDL_SetRenderTarget(@renderer, @frame)
      raise SDL.SDL_GetError.to_s unless SDL.SDL_RenderReadPixels(@renderer, nil, SDL::RGBA32, pixels, width * 4).zero?
      SDL.SDL_SetRenderTarget(@renderer, nil)
      raw = height.times.map { |y| "\0" + pixels.byteslice(y * width * 4, width * 4) }.join
      chunk = ->(name, data) { [data.bytesize].pack("N") + name + data + [Zlib.crc32(name + data)].pack("N") }
      png = "\x89PNG\r\n\x1a\n".b + chunk.call("IHDR", [width, height, 8, 6, 0, 0, 0].pack("N2C5")) + chunk.call("IDAT", Zlib.deflate(raw)) + chunk.call("IEND", "")
      FileUtils.mkdir_p(File.dirname(path))
      File.binwrite(path, png)
    end
  end
end

class Bitmap
  attr_reader :width, :height, :font, :texture
  @@fonts = {}
  def initialize(width, height = nil)
    if width.is_a?(String)
      surface = SDL.IMG_Load(width)
      raise "Image load failed: #{SDL.SDL_GetError}" if surface.null?
      @texture = SDL.SDL_CreateTextureFromSurface(Graphics.renderer, surface)
      SDL.SDL_FreeSurface(surface)
      raise SDL.SDL_GetError.to_s if @texture.null?
      tw, th = "\0" * 4, "\0" * 4
      SDL.SDL_QueryTexture(@texture, nil, nil, tw, th)
      @width, @height, @font = tw.unpack1("i"), th.unpack1("i"), Font.new
      return
    end
    @width, @height, @font = width, height, Font.new
    @texture = SDL.SDL_CreateTexture(Graphics.renderer, SDL::RGBA32, 2, width, height)
    raise SDL.SDL_GetError.to_s if @texture.null?
    SDL.SDL_SetTextureBlendMode(@texture, 1)
    clear
  end
  def target; SDL.SDL_SetRenderTarget(Graphics.renderer, @texture); end
  def clear
    target
    SDL.SDL_SetRenderDrawColor(Graphics.renderer, 0, 0, 0, 0)
    SDL.SDL_RenderClear(Graphics.renderer)
  end
  def fill_rect(x, y, width, height, color)
    target
    SDL.SDL_SetRenderDrawColor(Graphics.renderer, *color.bytes)
    SDL.SDL_RenderFillRect(Graphics.renderer, [x, y, width, height].pack("i4"))
  end
  def draw_text(x, y, width, height, string, align = 0)
    target
    font = @@fonts[@font.size] ||= SDL.TTF_OpenFont(File.join(ROOT, "launcher/font.ttf"), @font.size)
    raise "Font load failed" if font.null?
    color = @font.color.bytes.pack("C4").unpack1("L")
    surface = SDL.TTF_RenderUTF8_Blended(font, string.to_s, color)
    return if surface.null?
    texture = SDL.SDL_CreateTextureFromSurface(Graphics.renderer, surface)
    SDL.SDL_FreeSurface(surface)
    tw, th = "\0" * 4, "\0" * 4
    SDL.SDL_QueryTexture(texture, nil, nil, tw, th)
    tw, th = tw.unpack1("i"), th.unpack1("i")
    shown = [tw, width].min
    tx = x + (align == 1 ? (width - shown) / 2 : align == 2 ? width - shown : 0)
    rect = [tx, y + (height - th) / 2, shown, th].pack("i4")
    SDL.SDL_RenderSetClipRect(Graphics.renderer, [x, y, width, height].pack("i4"))
    SDL.SDL_RenderCopy(Graphics.renderer, texture, nil, rect)
    SDL.SDL_RenderSetClipRect(Graphics.renderer, nil)
    SDL.SDL_DestroyTexture(texture)
  end
  def dispose; SDL.SDL_DestroyTexture(@texture); end
end

class Sprite
  attr_accessor :bitmap, :x, :y, :z, :visible
  @all = []
  class << self; attr_reader :all; end
  def initialize
    @x, @y, @z, @visible = 0, 0, 0, true
    self.class.all << self
  end
  def dispose; self.class.all.delete(self); end
end

module Input
  KEYS = {1_073_741_906 => :UP, 1_073_741_905 => :DOWN, 1_073_741_904 => :LEFT,
          1_073_741_903 => :RIGHT, 13 => :C, 32 => :C, 27 => :B, 8 => :B, 116 => :X, 115 => :Y, 113 => :A}
  class << self
    attr_accessor :injected
    def update
      @pressed = @injected ? [@injected] : []
      @injected = nil
      event = "\0" * 64
      while SDL.SDL_PollEvent(event) != 0
        type = event.unpack1("L")
        exit if type == 0x100
        @pressed << KEYS[event.byteslice(20, 4).unpack1("i")] if type == 0x300
      end
    end
    def trigger?(key); (@pressed || []).include?(key); end
    def press?(key); trigger?(key); end
    def repeat?(key); trigger?(key); end
  end
end

abort "SDL initialization failed" unless SDL.SDL_Init(0x20).zero? && SDL.TTF_Init.zero?
window = SDL.SDL_CreateWindow("HardRPG launcher preview - arrows, Enter, Esc, T", 0x2fff0000, 0x2fff0000, 960, 544, 4)
Graphics.renderer = SDL.SDL_CreateRenderer(window, -1, 1 | 8)
abort "SDL renderer failed: #{SDL.SDL_GetError}" if Graphics.renderer.null?
load File.join(ROOT, "launcher/picker.rb")
picker = HardRPG::Picker.new
begin
  if options[:keys] || options[:screenshot]
    mapping = {"up" => :UP, "down" => :DOWN, "left" => :LEFT, "right" => :RIGHT, "cross" => :C, "circle" => :B, "triangle" => :X, "start" => :Y, "square" => :A}
    (options[:keys] || []).each do |key|
      Input.injected = mapping.fetch(key) { abort "Unknown scripted key: #{key}" }
      Input.update
      picker.update
      Graphics.update
    end
    Graphics.update
    Graphics.screenshot(options[:screenshot]) if options[:screenshot]
    puts "Launcher preview rendered; #{HardRPG::GAMES_ROOT}"
  else
    picker.run
  end
ensure
  HardRPGArchive.close
  SDL.SDL_DestroyRenderer(Graphics.renderer)
  SDL.SDL_DestroyWindow(window)
  SDL.TTF_Quit
  SDL.SDL_Quit
end
