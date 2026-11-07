# Exercise the renderer policy without Vita or game data. The device test
# checks the matched game scripts and actual output bytes.
require_relative '../launcher/native_events'

Rect = Struct.new(:x, :y, :width, :height)
Ambient = Struct.new(:alpha)
Surface = Struct.new(:bitmap)
Lantern = Struct.new(:visible)
Effect = Struct.new(:color)
TestMap = Struct.new(:light_surface, :lantern, :light_sources, :effect_surface)

module Graphics
  def self.width; 544; end
  def self.height; 416; end
end

class Bitmap
  attr_reader :width, :height, :drawn
  attr_accessor :reject

  def initialize(width, height)
    @width, @height = width, height
    @drawn = []
  end

  def __hardrpg_compose_opaque(_ambient, _rows)
    !reject
  end

  def clear; end
  def fill_rect(*); end

  def blt(*args)
    @drawn << args
  end

  def dispose
    @disposed = true
  end

  def disposed?
    @disposed
  end
end

class Light_SSource
  attr_reader :draws, :bitmap

  def initialize
    @draws = 0
    @bitmap = Bitmap.new(32, 32)
  end

  def sx; -2.5; end
  def sy; 16; end
  def w; 32; end
  def h; 32; end

  def opacity
    @draws += 1
    100 + @draws
  end
end

class Game_Event
  def initialize(light)
    @light = light
  end
end

class StockLights
  attr_reader :original_calls, :disposed_bitmap

  def update_lights
    @original_calls = (@original_calls || 0) + 1
  end

  def dispose_lights
    @disposed_bitmap = $game_map.light_surface.bitmap
    @disposed_bitmap.dispose
  end
end

def assert(condition, message)
  raise message unless condition
end

class AcceleratedLights < StockLights
  prepend HardRPGNative::KhasLights::Updates
end

light = Light_SSource.new
cpu = Bitmap.new(544, 416)
surface = Surface.new(cpu)
$game_map = TestMap.new(surface, Lantern.new(false), [Game_Event.new(light)], Effect.new(Ambient.new(255)))
renderer = AcceleratedLights.new
renderer.update_lights
gpu = surface.bitmap
assert(!gpu.equal?(cpu) && light.draws == 1 && !renderer.original_calls, 'GPU should consume one draw')

gpu.reject = true
renderer.update_lights
assert(surface.bitmap.equal?(cpu) && light.draws == 2 && !renderer.original_calls,
       'GPU rejection should restore the CPU target and reuse the sampled opacity')
assert(cpu.drawn == [[-2, 16, light.bitmap, Rect.new(0, 0, 32, 32), 102]],
       'CPU replay lost the RGSS coordinate, rect, or opacity')

gpu.reject = false
renderer.update_lights
$game_map.lantern.visible = true
renderer.update_lights
assert(surface.bitmap.equal?(cpu) && renderer.original_calls == 1 && light.draws == 3,
       'visible lantern should go through the original update')

$game_map.lantern.visible = false
renderer.update_lights
renderer.dispose_lights
assert(renderer.disposed_bitmap.equal?(cpu) && cpu.disposed? && gpu.disposed?,
       'normal disposal should release both targets through their owners')
puts 'Khas light renderer fallback and disposal checks passed'
