# Check launcher/rgss_extensions.rb against stand-ins for the engine's
# MiniFFI (Win32API) and Audio. Usage: ruby tests/rgss-extensions.rb [scripts-dir]
# With a directory of extracted Uranium sections, it also runs the game's own
# RGSS Linker and FmodEx sections the way the game does at startup.
require 'tmpdir'

PLAYED = []
class MiniFFI # the engine's dummy: every call returns 0
  def initialize(library, function, *) = nil
  def call(*) = 0
end
Win32API = MiniFFI
module Graphics
  @size = [640, 480]
  def self.snap_to_bitmap = :native_snap
  def self.width = @size[0]
  def self.height = @size[1]
  def self.resize_screen(w, h) = @size = [w, h]
end
module Audio
  %i[bgm_play bgm_stop bgm_fade bgs_play bgs_stop bgs_fade me_play me_stop me_fade se_play se_stop].each do |name|
    define_singleton_method(name) { |*args| PLAYED << [name, *args]; nil }
  end
end
require_relative '../launcher/rgss_extensions'
require_relative '../launcher/essentials_compat'

def check(label, expected, actual)
  raise "#{label}: expected #{expected.inspect}, got #{actual.inspect}" unless expected == actual
end

Dir.mktmpdir do |dir|
  Dir.chdir(dir) do
    File.write('Game.ini', "[Game]\r\nLibrary=RGSS102E.dll\r\nTitle=Test Title\r\n[Other]\r\nN=42\r\n")
    buffer = "\0" * 32
    n = Win32API.new('kernel32', 'GetPrivateProfileString', 'ppppip', 'i').call('Game', 'Title', '', buffer, 32, './/Game.ini')
    check('ini string', ['Test Title', 10, 32], [buffer[/\A[^\0]*/], n, buffer.bytesize])
    n = Win32API.new('kernel32', 'GetPrivateProfileString', 'ppppip', 'i').call('Game', 'Missing', 'dflt', buffer, 32, 'Game.ini')
    check('ini default', ['dflt', 4], [buffer[/\A[^\0]*/], n])
    check('ini int', 42, Win32API.new('KERNEL32.DLL', 'GetPrivateProfileIntA', 'ppip', 'i').call('Other', 'n', 0, 'Game.ini'))
    check('untouched call', 0, Win32API.new('user32', 'ShowWindow', 'll', 'l').call(0, 0))
    check('array arguments', 960, Win32API.new('user32', 'GetSystemMetrics', 'l', 'l').call([0]))
  end
end

# Essentials replaces Graphics.snap_to_bitmap with a rubyscreen.dll version;
# the compatibility hook restores the engine's, and only for Essentials.
eval("module Graphics; def self.snap_to_bitmap; :rubyscreen; end; end", TOPLEVEL_BINDING, 'Section010')
HardRPGCompat.run
check('not Essentials: untouched', :rubyscreen, Graphics.snap_to_bitmap)
class PokemonSystem; end
class PokeBattle_Pokemon; end
eval("module Graphics; def self.width; 512; end; def self.height; 384; end; end", TOPLEVEL_BINDING, 'Section010')
HardRPGCompat.run
check('Essentials: engine snap', :native_snap, Graphics.snap_to_bitmap)
check('Essentials: screen size', [512, 384], HardRPGEssentials::NATIVE_WIDTH.call.then { |w| [w, HardRPGEssentials::NATIVE_HEIGHT.call] })

# Essentials' own focus check (PSystem_Controls): keys count only when the
# foreground window belongs to the game's thread.
fg = Win32API.new('user32', 'GetForegroundWindow', '', 'i').call
pid = [0].pack('l')
wt = Win32API.new('user32', 'GetWindowThreadProcessId', '%w(l p)', 'l').call(fg, pid)
check('same thread', true, fg != 0 && wt == Win32API.new('kernel32', 'GetCurrentThreadId', '%w()', 'l').call)
check('find rgss window', [fg, 0], [Win32API.new('user32', 'FindWindowEx', '%w(l l p p)', 'l').call(0, 0, 'RGSS Player', 0),
                                    Win32API.new('user32', 'FindWindowEx', '%w(l l p p)', 'l').call(0, fg, 'RGSS Player', 0)])
rect = [0, 0, 0, 0].pack('l4')
Win32API.new('user32', 'GetClientRect', %w(l p), 'i').call(fg, rect)
check('client rect', [0, 0, Graphics.width, Graphics.height], rect.unpack('l4'))

# H-Mode7 maps fall back to top-down.
module HM7; end
class Game_System; attr_accessor :hm7; end
HardRPGCompat.run
gs = Game_System.new; gs.hm7 = true
check('H-Mode7 off', false, gs.hm7)

# Winsock reports the network as down instead of a fake success.
check('socket', -1, Win32API.new('ws2_32', 'socket', 'lll', 'l').call(2, 1, 6))
check('WSAStartup', 10091, Win32API.new('ws2_32.dll', 'WSAStartup', 'ip', 'i').call(0x202, "\0" * 400))

# Essentials StringInput (FileTests v17) returns 1.8 bytes from getc.
eval(<<~'RUBY', TOPLEVEL_BINDING, 'Section051')
  class StringInput
    def initialize(str); @string = str; @pos = 0; @closed = false; end
    def eof?; @pos >= @string.size; end
    def getc
      raise IOError, 'closed stream' if @closed
      ch = @string[@pos]
      @pos += 1
      ch
    end
    def read(len)
      str = @string[@pos, len]
      @pos += len
      str
    end
  end
RUBY
HardRPGCompat.run
io = StringInput.new([0x12345678].pack('V'))
v = 0; x = 0
4.times { v |= (io.getc << x); x += 8 }
check('StringInput dword', 0x12345678, v)
check('Enumerator 1.8 readers', ['1', '2', 3], ['12'.split('').map[0], '12'.split('').map.last, [1, 2, 3].map.length])
data = Marshal.dump([[0, nil, 'Pokemon Trainer', 60], [1, nil, 'Ace', 80]])
check('StringInput Marshal.load', Marshal.load(data), Marshal.load(StringInput.new(data)))

# A 1.8-era backport (alias getbyte []) is undone after the section.
eval("class String; alias getbyte []; alias bytesize size; end", TOPLEVEL_BINDING, 'Section256')
HardRPGCompat.run
check('getbyte restored', 0xC3, "\u00e9".getbyte(0))
check('bytesize restored', 2, "\u00e9".bytesize)

# FontInstaller.install does nothing (the engine reads the Fonts folder).
eval("module FontInstaller; def self.install; raise 'would install'; end; end", TOPLEVEL_BINDING, 'Section114')
HardRPGCompat.run
check('font installer', nil, FontInstaller.install)

# Essentials' audio length (getPlayTime2): a 2-second 8 kHz mono WAV.
Dir.mktmpdir do |dir|
  path = File.join(dir, 'two.wav')
  data = "\0" * 16000
  File.binwrite(path, ['RIFF', 36 + data.bytesize, 'WAVE', 'fmt ', 16, 1, 1, 8000, 8000, 1, 8, 'data', data.bytesize].pack('a4Va4a4VvvVVvva4V') + data)
  check('wav length', 2.0, HardRPGAudioLength.play_time(path))
end

# Text entry uses the letter grid (no keyboard on the Vita).
eval("USEKEYBOARDTEXTENTRY = true; class PokemonEntryScene; USEKEYBOARD = true; end", TOPLEVEL_BINDING, 'Section060')
HardRPGCompat.run
check('text entry grid', [false, false], [USEKEYBOARDTEXTENTRY, PokemonEntryScene::USEKEYBOARD])

# pbResolveBitmap answers are kept (one archive search per name), and a later redefinition is wrapped too.
eval("$tries = 0; def pbResolveBitmap(x); $tries += 1; x == 'here' ? 'Graphics/here' : nil; end", TOPLEVEL_BINDING, 'Section051')
HardRPGCompat.run
3.times { pbResolveBitmap('missing'); pbResolveBitmap('here') }
check('resolve cache', [2, nil, 'Graphics/here'], [$tries, pbResolveBitmap('missing'), pbResolveBitmap('here')])
eval("def pbResolveBitmap(x); $tries += 10; 'v2'; end", TOPLEVEL_BINDING, 'Section200')
HardRPGCompat.run
check('resolve cache after redefinition', ['v2', 'v2', 12], [pbResolveBitmap('missing'), pbResolveBitmap('missing'), $tries])

# pbSameThread: the game's window (HardRPGWindow) is always in front; no Win32API objects per key check.
eval("def pbSameThread(wnd); raise 'Win32 path'; end", TOPLEVEL_BINDING, 'Section165')
HardRPGCompat.run
check('same thread', [true, false, false], [pbSameThread(HardRPGWindow::HWND), pbSameThread(0), pbSameThread(5)])

scripts = ARGV[0]
if scripts
  # Uranium: RGSS Linker initializes, FmodEx loads, BGM reaches the engine.
  Dir.mktmpdir do |dir|
    Dir.chdir(dir) do
      File.write('Uranium.ini', "[Game]\r\nLibrary=RGSS102E.dll\r\n")
      $PokemonSystem = nil
      %w[RGSS_Linker F-mod_main_script].each do |name|
        file = Dir[File.join(scripts, "*_#{name}.rb")].first or raise "no #{name} section"
        eval(File.binread(file).force_encoding('UTF-8'), TOPLEVEL_BINDING, file)
      end
      check('FmodEx installed', true, Object.const_defined?(:FmodEx))
      PLAYED.clear
      Audio.bgm_play('Audio/BGM/Route 1', 80, 100)
      check('bgm forwarded', [[:bgm_play, 'Audio/BGM/Route 1', 80, 100]], PLAYED)
      Audio.bgm_fade(500)
      check('fade forwarded', [:bgm_fade, 500], PLAYED.last)
    end
  end
end
puts 'rgss-extensions: ok'
