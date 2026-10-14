require_relative 'tap-to-walk'

module Graphics
  class << self
    attr_accessor :frame_count
  end
end
Graphics.frame_count = 0
class Color
  attr_reader :channels
  def initialize(*channels); @channels = channels; end
end
class Bitmap
  attr_reader :rectangles, :disposals
  @@allocations = 0
  def self.allocations; @@allocations; end
  def initialize(w,h)
    raise 'wrong marker size' unless [w,h] == [32,32]
    @@allocations += 1
    @rectangles, @disposals = [], 0
  end
  def fill_rect(*args); @rectangles << args; end
  def dispose; @disposals += 1; end
end
class Sprite
  attr_accessor :bitmap, :z, :visible, :x, :y, :opacity, :ox, :oy,
                :zoom_x, :zoom_y, :blend_type
  attr_reader :viewport, :disposals
  def initialize(viewport = nil)
    @viewport, @disposals = viewport, 0
  end
  def dispose; @disposals += 1; end
end
class WalkMap
  def adjust_x(x); x-display_x; end
  def adjust_y(y); y-display_y; end
end
map, state = WalkMap.new, HardRPGTapWalk::Driver.new
player = WalkPlayer.new(map)
viewport = Object.new
marker = HardRPGTapWalk::DestinationMarker.new(viewport)
marker.update(map,state)
raise 'idle allocates marker' unless marker.sprite.nil? && Bitmap.allocations == 0
state.request(map,player,[12,4])
marker.update(map,state)
sprite, bitmap = marker.sprite, marker.sprite.bitmap
raise 'wrong destination centre' unless sprite.visible && [sprite.x,sprite.y] == [400,144]
raise 'not centred additive marker' unless [sprite.ox,sprite.oy,sprite.blend_type] == [16,16,1]
raise 'wrong viewport' unless sprite.viewport.equal?(viewport)
raise 'marker not white' unless bitmap.rectangles.size == 1 && bitmap.rectangles[0].last.channels == [255,255,255]
zooms = []
opacities = 20.times.map do |frame|
  Graphics.frame_count = frame
  marker.update(map,state)
  zooms << sprite.zoom_x
  raise 'asymmetric expansion' unless sprite.zoom_x == sprite.zoom_y
  sprite.opacity
end
raise 'marker does not fade' unless opacities.min == 6 && opacities.max == 120
raise 'marker does not expand' unless zooms.min == 1.0 && zooms.max == 1.95
map.display_x, map.display_y = 1.5,2.25
marker.update(map,state)
raise 'marker drift on fractional scroll' unless [sprite.x,sprite.y] == [352,72]
state.request(map,player,[3,8])
marker.update(map,state)
raise 'replacement tap left old marker' unless [sprite.x,sprite.y] == [64,200]
raise 'replacement tap did not restart pulse' unless sprite.zoom_x == 1.05 && sprite.opacity == 114
raise 'repeated taps allocate bitmap' unless Bitmap.allocations == 1
state.cancel(:physical)
marker.update(map,state)
raise 'cancel left marker' if sprite.visible
state.request(map,player,[player.x,player.y])
state.tick(map,player,:now=>0)
marker.update(map,state)
raise 'arrival left marker' unless state.status == :arrived && !sprite.visible
map.walls = [[4,4]]
state.request(map,player,[4,4])
20.times { state.tick(map,player,:now=>0) }
marker.update(map,state)
raise 'unreachable destination left marker' unless state.reason == :unreachable && !sprite.visible
marker.dispose
marker.dispose
raise 'marker resource disposal failed' unless sprite.disposals == 1 && bitmap.disposals == 1
marker.update(map,state)
raise 'disposed marker recreated' unless marker.sprite.nil? && Bitmap.allocations == 1
puts 'PASS: white additive marker expands and fades, follows camera, replaces target and hides on cancel/arrival/failure'
puts 'PASS: one bitmap reused and sprite/bitmap disposed exactly once'

# Stock-style spriteset initialization can call update before the wrapper owns
# its marker. Check that lifecycle and viewport ownership remain intact.
class Spriteset_Map
  attr_reader :normal_updates, :normal_dispose
  def initialize
    @viewport1 = Object.new
    @normal_updates = 0
    update
  end
  def update; @normal_updates += 1; end
  def dispose; @normal_dispose = true; end
end
Spriteset_Map.prepend(HardRPGTapWalk::MapSprites)
$game_map = map
HardRPGTapWalk.driver.request(map,player,[12,4])
sprites = Spriteset_Map.new
sprites.update
owned = sprites.instance_variable_get(:@hardrpg_destination_marker)
raise 'spriteset hook lost original update' unless sprites.normal_updates == 2
raise 'spriteset marker not shown' unless owned.sprite.visible
owned_sprite, owned_bitmap = owned.sprite, owned.sprite.bitmap
sprites.dispose
raise 'spriteset cleanup lost' unless sprites.normal_dispose && owned_sprite.disposals == 1 && owned_bitmap.disposals == 1
puts 'PASS: map spriteset preserves update and disposes its destination marker'
