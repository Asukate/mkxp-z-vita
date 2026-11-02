# launcher/native_events.rb keeps what it read from each script section, and a second start reads
# that instead of the scripts. The kept methods must be the ones a fresh read finds.
module Kernel; private def rgss_main; yield if block_given?; end; end
module HardRPGNative; end
require_relative '../launcher/native_events'

def check(label, ok)
  raise "#{label} failed" unless ok
end

C = HardRPGNative::SectionCache
SECTION = <<~RUBY
  class Game_Event < Game_Character
    def near_the_screen?(dx = 12, dy = 8)
      ax = $game_map.adjust_x(@real_x) - Graphics.width / 2 / 32
      ay = $game_map.adjust_y(@real_y) - Graphics.height / 2 / 32
      ax >= -dx && ax <= dx && ay >= -dy && ay <= dy  # in range
    end
  end
  class Window_Other
    def refresh
      draw_text(0, 0, 10, 10, "#")
    end
  end
RUBY
dir = File.expand_path("../build/tests/native-events-cache-#{$$}", __dir__)
[File.dirname(File.dirname(dir)), File.dirname(dir), dir].each { |d| Dir.mkdir(d) unless File.directory?(d) }
begin
  $hardrpg_native_events_cache = File.join(dir, 'cache', 'native-events.dat')
  fresh = HardRPGNative::Norm.methods(SECTION).select { |k, _| k.start_with?('Game_Event#') }.to_a
  check('first read', C.methods(SECTION) == fresh && fresh.size == 1)
  rgss_main { }
  check('kept at rgss_main', File.file?($hardrpg_native_events_cache))
  C.instance_variable_set(:@sections, nil)
  calls = 0
  HardRPGNative::Norm.singleton_class.send(:alias_method, :plain_methods, :methods)
  HardRPGNative::Norm.define_singleton_method(:methods) { |s| calls += 1; plain_methods(s) }
  check('second start reads the kept methods', C.methods(SECTION) == fresh && calls == 0)
  check('a changed section is read again', C.methods(SECTION + "\n") == fresh && calls == 1)
  File.binwrite($hardrpg_native_events_cache, 'garbage')
  C.instance_variable_set(:@sections, nil)
  check('a damaged file is ignored', C.methods(SECTION) == fresh && calls == 2)
ensure
  $hardrpg_native_events_cache = nil
  Dir.glob(File.join(dir, '**', '*')).sort.reverse.each { |f| File.directory?(f) ? Dir.rmdir(f) : File.delete(f) }
  Dir.rmdir(dir)
end
puts 'Native events section cache checks passed'
