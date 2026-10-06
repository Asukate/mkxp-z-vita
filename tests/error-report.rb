require "tmpdir"
HARDRPG_DATA_ROOT = Dir.mktmpdir("hardrpg-report-")
load File.expand_path("../launcher/library.rb", __dir__)
def check(value); raise "Error report check failed" unless value; end
begin
  HardRPG.setup
  path = HardRPG::ROOT + "/last-error.txt"
  check(HardRPG.take_error_report.nil?)
  original = "NameError\n\nMissing API\n\nBacktrace:\n" + "Main:12:" + "long entry " * 50 + "\n"
  File.binwrite(path, original)
  lines = HardRPG.take_error_report
  check(lines.first == "NameError" && lines.include?("Backtrace:"))
  check(lines.all? { |line| line.length <= 65 })
  check(!File.exist?(path) && File.binread(path + ".prev") == original)
  check(HardRPG.take_error_report.nil?)
  File.binwrite(path, "Broken\xff\x00text".b)
  lines = HardRPG.take_error_report
  check(lines.join.valid_encoding? && !lines.join.include?("\x00"))
  check(File.binread(path + ".prev") == "Broken\xff\x00text".b)
ensure
  require "fileutils"
  FileUtils.remove_entry(HARDRPG_DATA_ROOT)
end
puts "PASS: error report consumption, retained original, scroll wrapping, malformed UTF-8"

module Graphics
  def self.resize_screen(*); end
  def self.fixed_aspect_ratio=(_); end
end
class Font
  def self.default_name=(_); end
end
module Input
  class << self
    attr_accessor :held, :trigger, :repeat
    def press?(key); (@held || []).include?(key); end
    def trigger?(key); @trigger == key; end
    def repeat?(key); @repeat == key; end
  end
end
HARDRPG_PREVIEW_DRIVER = true
load File.expand_path("../launcher/picker.rb", __dir__)
picker = HardRPG::Picker.allocate
def picker.redraw; @redraws = (@redraws || 0) + 1; end
picker.instance_variable_set(:@error_report, Array.new(40) { |i| "Frame #{i}" })
picker.instance_variable_set(:@error_scroll, 0)
picker.instance_variable_set(:@error_armed, false)
Input.held, Input.trigger = [:C], :C
picker.update
check(picker.instance_variable_get(:@error_report))
Input.held, Input.trigger = [], nil
picker.update
Input.repeat = :DOWN
50.times { picker.update }
check(picker.instance_variable_get(:@error_scroll) == 28)
Input.repeat = :UP
50.times { picker.update }
check(picker.instance_variable_get(:@error_scroll) == 0)
Input.repeat, Input.trigger = nil, :C
picker.update
check(picker.instance_variable_get(:@error_report).nil?)
puts "PASS: held launch button cannot dismiss report, bounded scrolling, dismiss to games"
