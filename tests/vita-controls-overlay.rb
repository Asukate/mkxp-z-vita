# Check disabled-path isolation and ownership/resize behavior of the RGSS overlay.
module Graphics
  class << self
    attr_accessor :width, :height, :updates
    def update; self.updates += 1; end
  end
  self.width, self.height, self.updates = 544, 416, 0
end
module Input
  class << self
    attr_accessor :enabled
    def vita_controls; [enabled, 1, 0, true, 'Run', 'Confirm']; end
    def mouse_x; 100; end
    def mouse_y; 200; end
  end
end
class Color
  def initialize(*); end
end
class Bitmap
  attr_reader :font
  @@count = 0
  def self.count; @@count; end
  def initialize(*); @@count += 1; @font = Struct.new(:size).new; end
  def fill_rect(*); end
  def draw_text(*); end
  def dispose; raise 'double bitmap disposal' if @disposed; @disposed = true; end
  def disposed?; @disposed; end
end
class Sprite
  attr_accessor :x, :y, :z
  attr_writer :bitmap
  def bitmap; raise 'disposed sprite read' if @disposed; @bitmap; end
  def dispose; raise 'double sprite disposal' if @disposed; @disposed = true; end
  def disposed?; @disposed; end
end
source = File.expand_path('../launcher/vita_controls.rb', __dir__)
original = Graphics.method(:update)
Input.enabled = false
load source
raise 'disabled overlay changed update' unless Graphics.method(:update) == original
Graphics.update
raise 'disabled overlay allocated' unless Bitmap.count == 0
Input.enabled = true
load source
Graphics.update
raise 'missing overlay' unless Bitmap.count == 3
100.times { Graphics.update }
raise 'steady frame allocations' unless Bitmap.count == 3
sprites = HardRPGControlOverlay.instance_variable_get(:@sprites).dup
bitmaps = HardRPGControlOverlay.instance_variable_get(:@bitmaps).dup
Graphics.width = 640
Graphics.update
raise 'resize leaked objects' unless sprites.all?(&:disposed?) && bitmaps.all?(&:disposed?)
raise 'resize did not rebuild' unless Bitmap.count == 6
HardRPGControlOverlay.instance_variable_get(:@sprites).first.dispose
Graphics.update
raise 'disposed overlay recovery failed' unless Bitmap.count == 9
raise 'pointer coordinates' unless HardRPGControlOverlay.instance_variable_get(:@pointer).x == 94
puts 'PASS: disabled overlay, steady-frame bitmap reuse, resize disposal and cursor'
