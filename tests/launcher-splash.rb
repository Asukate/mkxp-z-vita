require "tmpdir"
require "fileutils"
HARDRPG_DATA_ROOT = Dir.mktmpdir("hardrpg-splash-")
HARDRPG_PREVIEW_DRIVER = true
load File.expand_path("../launcher/library.rb", __dir__)
module Graphics
  def self.resize_screen(*); end
  def self.fixed_aspect_ratio=(_); end
end
class Font
  def self.default_name=(_); end
end
class Bitmap
  def initialize(*); end
end
class Sprite
  attr_accessor :bitmap, :z
end
load File.expand_path("../launcher/picker.rb", __dir__)
class SplashProbe < HardRPG::Picker
  attr_reader :splashes
  def paint_background; end
  def redraw; end
  def show_splash; @splashes = (@splashes || 0) + 1; end
end
original_args = ARGV.dup
begin
  ARGV.clear
  raise "Cold launch lost its splash" unless SplashProbe.new.splashes == 1
  ARGV.replace(["--hardrpg-return"])
  raise "Returning to picker replayed splash" unless SplashProbe.new.splashes.nil?
  File.binwrite(HardRPG::ROOT + "/last-error.txt", "Script error\nBacktrace:\nMain:1\n")
  raise "Error return replayed splash" unless SplashProbe.new.splashes.nil?
  ARGV.clear
  raise "A later fresh app launch lost its splash" unless SplashProbe.new.splashes == 1
  picker = SplashProbe.new
  library = Object.new
  def library.entries; [:game]; end
  def library.enter(entry); entry; end
  def library.launch(entry); end
  picker.instance_variable_set(:@library, library)
  picker.instance_variable_set(:@focus, :games)
  begin
    picker.select
    raise "Game selection did not hand off"
  rescue SystemExit
    raise "Game launch replayed splash" unless picker.splashes == 1
  end
ensure
  ARGV.replace(original_args)
  FileUtils.remove_entry(HARDRPG_DATA_ROOT)
end
puts "PASS: splash on cold app launch only; menu and error returns skip it"
