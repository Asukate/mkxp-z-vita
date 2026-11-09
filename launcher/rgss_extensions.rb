# Windows DLLs that RGSS games call through Win32API (preload).
#
# The Vita cannot load Windows libraries, so the engine turns every Win32API
# call into a no-op that returns zero. That is right for window, cursor and
# console calls, and wrong where a game checks the answer: Pokemon Essentials
# fan games refuse to start when RGSS Linker reports failure, and read their
# settings from the INI through kernel32. This file answers those calls the
# way the DLL would, by DLL and function name, never by game title.
# Fixes that replace a game's own methods. The engine calls run after every
# script section: a hook does nothing until the code it fixes exists, and
# applies again if a later section redefines it. A hook returns true when it
# changed something; that, and any failure, is logged once per hook.
module HardRPGCompat
  HOOKS = []
  REPORTED = {}

  def self.add(name, &hook)
    HOOKS << [name, hook]
  end

  def self.run
    HOOKS.each do |name, hook|
      begin
        report(name, 'applied') if hook.call == true
      rescue Exception => e # a fix must never stop the game
        report(name, "skipped (#{e.class}: #{e.message})")
      end
    end
    nil
  end

  def self.report(name, what)
    return if REPORTED[[name, what]]
    REPORTED[[name, what]] = true
    line = "Compatibility: #{name} #{what}"
    # The engine's logger reaches runtime.log; Ruby's $stderr does not.
    if defined?(MKXP) && MKXP.respond_to?(:puts) then MKXP.puts(line)
    elsif defined?(System) && System.respond_to?(:puts) then System.puts(line)
    else $stderr.puts(line)
    end
  end
end

# Windows games find a scratch folder in TEMP; Pokemon Essentials builds
# screenshot paths from it.
module HardRPGEnv
  TEMP = 'ux0:/data/hardrpg/temp'

  def self.install
    Dir.mkdir(TEMP) unless File.directory?(TEMP)
    %w[TEMP TMP].each { |name| ENV[name] ||= TEMP }
  rescue SystemCallError
    nil
  end
end
HardRPGEnv.install if RUBY_PLATFORM.to_s =~ /vita|psp2|arm-none/ || File.directory?('ux0:/data')

module HardRPGWin32
  HANDLERS = {}

  # "./RGSS Linker.dll" and "rgss linker" name the same library.
  def self.key(library, function)
    name = library.to_s.tr('\\', '/').split('/').last.to_s.downcase.sub(/\.dll\z/, '')
    "#{name}!#{function.to_s.downcase}"
  end

  def self.define(library, *functions, &handler)
    functions.each { |function| HANDLERS[key(library, function)] = handler }
    LOOKUPS.clear if defined?(LOOKUPS)
  end

  # Essentials makes new Win32API objects on every key check: remember each
  # library/function pair's handler (or its absence).
  LOOKUPS = {}

  def self.handler(library, function)
    pair = "#{library}\0#{function}"
    return LOOKUPS[pair] if LOOKUPS.key?(pair)
    LOOKUPS[pair.freeze] = HANDLERS[key(library, function)]
  end

  # Writes a C string into a buffer the game passed as a String and returns
  # the bytes written, like the Win32 *A functions.
  def self.write_cstring(buffer, value, size)
    return 0 unless buffer.is_a?(String) && size.to_i > 0
    bytes = value.to_s.b[0, [size.to_i, buffer.bytesize].min - 1] || ''.b
    buffer.force_encoding(Encoding::BINARY)[0, bytes.bytesize + 1] = bytes + "\0"
    bytes.bytesize
  end

  def self.ini_value(file, section, key)
    path = file.to_s.tr('\\', '/')
    path = File.basename(path) unless File.exist?(path)
    return nil unless File.exist?(path)
    current = nil
    File.foreach(path, mode: 'rb') do |line|
      line = line.strip
      if line =~ /\A\[(.*)\]\z/
        current = $1.strip.downcase
      elsif current == section.to_s.downcase && line =~ /\A([^=]+)=(.*)\z/ &&
            $1.strip.downcase == key.to_s.downcase
        return $2.strip
      end
    end
    nil
  rescue SystemCallError, IOError
    nil
  end
end

module HardRPGWin32Call
  def initialize(library, function, *rest)
    @hardrpg_handler = HardRPGWin32.handler(library, function)
    super
  end

  def call(*args)
    args = args[0] if args.size == 1 && args[0].is_a?(Array) && @hardrpg_handler
    return @hardrpg_handler.call(*args) if @hardrpg_handler
    super
  end

  def Call(*args)
    call(*args)
  end
end
# The Vita engine looks up HANDLERS itself when a Win32API object is made (and answers the window
# and thread checks in C), so the Ruby layer is only for engines without that dispatch.
MiniFFI.prepend(HardRPGWin32Call) if defined?(MiniFFI) && !MiniFFI.const_defined?(:HARDRPG_NATIVE_DISPATCH)

# kernel32: INI reads. RGSS Linker reads its library name this way, and
# Essentials games read the title and options from Game.ini.
HardRPGWin32.define('kernel32', 'GetPrivateProfileString', 'GetPrivateProfileStringA') do |section, key, default, buffer, size, file|
  value = HardRPGWin32.ini_value(file, section, key)
  value = default.is_a?(String) ? default : '' if value.nil?
  HardRPGWin32.write_cstring(buffer, value, size)
end
HardRPGWin32.define('kernel32', 'GetPrivateProfileInt', 'GetPrivateProfileIntA') do |section, key, default, file|
  value = HardRPGWin32.ini_value(file, section, key)
  value =~ /\A\s*-?\d+/ ? value.to_i : default.to_i
end
HardRPGWin32.define('kernel32', 'GetUserDefaultLangID', 'GetSystemDefaultLangID') { 0x0409 }
# The game's window. RGSS games find it with FindWindow("RGSS Player") and
# check it is theirs and in front before reading the keyboard (Pokemon
# Essentials ignores every key otherwise). There is one window, it is always
# in front, its client area is the game screen and it has no border.
module HardRPGWindow
  HWND = 0x00010010
  THREAD = 1

  def self.size
    [Graphics.width.to_i, Graphics.height.to_i]
  rescue StandardError
    [640, 480]
  end

  def self.write(buffer, packed)
    return 0 unless buffer.is_a?(String) && buffer.bytesize >= packed.bytesize
    buffer.force_encoding(Encoding::BINARY)[0, packed.bytesize] = packed
    1
  end
end
HardRPGWin32.define('user32', 'FindWindow', 'FindWindowA', 'FindWindowW') { |*| HardRPGWindow::HWND }
HardRPGWin32.define('user32', 'FindWindowEx', 'FindWindowExA', 'FindWindowExW') do |_parent, after, *|
  after.to_i == 0 ? HardRPGWindow::HWND : 0
end
HardRPGWin32.define('user32', 'GetForegroundWindow', 'GetActiveWindow', 'GetFocus') { |*| HardRPGWindow::HWND }
HardRPGWin32.define('kernel32', 'GetCurrentThreadId', 'GetCurrentProcessId') { |*| HardRPGWindow::THREAD }
HardRPGWin32.define('user32', 'GetWindowThreadProcessId') do |hwnd, process|
  next 0 unless hwnd.to_i == HardRPGWindow::HWND
  HardRPGWindow.write(process, [HardRPGWindow::THREAD].pack('l'))
  HardRPGWindow::THREAD
end
HardRPGWin32.define('user32', 'GetClientRect', 'GetWindowRect') do |hwnd, rect|
  next 0 unless hwnd.to_i == HardRPGWindow::HWND
  HardRPGWindow.write(rect, [0, 0, *HardRPGWindow.size].pack('l4'))
end

# user32: the "screen" is the Vita's.
HardRPGWin32.define('user32', 'GetSystemMetrics') do |index|
  case index.to_i
  when 0, 16 then 960 # SM_CXSCREEN, SM_CXFULLSCREEN
  when 1, 17 then 544
  else 0
  end
end

# Winsock (ws2_32/wsock32): games' online features (Pokemon Insurgence's
# trades and battles) open raw sockets. A zero from every call looks like
# success and the game waits on a connection that never answers; report the
# network as down instead, so the game shows its own "could not connect".
%w[ws2_32 wsock32].each do |dll|
  HardRPGWin32.define(dll, 'WSAStartup') { |*| 10091 }                     # WSASYSNOTREADY
  HardRPGWin32.define(dll, 'WSAGetLastError') { |*| 10050 }                # WSAENETDOWN
  HardRPGWin32.define(dll, 'socket', 'accept') { |*| -1 }                  # INVALID_SOCKET
  HardRPGWin32.define(dll, 'connect', 'bind', 'listen', 'send', 'recv', 'sendto',
                      'recvfrom', 'select', 'setsockopt', 'getsockopt', 'ioctlsocket',
                      'shutdown') { |*| -1 }                               # SOCKET_ERROR
  HardRPGWin32.define(dll, 'gethostbyname', 'gethostbyaddr') { |*| 0 }     # NULL
  HardRPGWin32.define(dll, 'closesocket', 'WSACleanup') { |*| 0 }
end

# RGSS Linker (Nuri Yuri) loads RGSS extension DLLs into the running Ruby.
# Its initializer must report 1 or the game raises LoadError; each extension
# is then "loaded" by calling its Init_ function, which installs the
# replacement below when one exists.
module HardRPGRGSSExtensions
  INITIALIZERS = {}

  def self.define(library, function, &installer)
    INITIALIZERS[HardRPGWin32.key(library, function)] = installer
    HardRPGWin32.define(library, function) do |*|
      installer.call
      0
    end
  end
end
HardRPGWin32.define('RGSS Linker', 'RGSSLinker_Initialize') { |*| 1 }

# RGSS FmodEx (Nuri Yuri): an FMOD Ex audio backend with the RGSS Audio
# signature. Each call goes to the engine's own Audio function, captured
# here before a game rewrites Audio to call FmodEx.
module HardRPGFmodEx
  CHANNELS = %i[bgm_play bgm_stop bgm_fade bgs_play bgs_stop bgs_fade
                me_play me_stop me_fade se_play se_stop].freeze
  AUDIO = CHANNELS.each_with_object({}) do |name, map|
    map[name] = Audio.method(name) if defined?(Audio) && Audio.respond_to?(name)
  end

  # What FmodEx returns for a playing sound. The engine loops whole tracks,
  # so loop points are accepted and ignored.
  class Sound
    attr_reader :filename

    def initialize(filename)
      @filename = filename
    end

    def set_loop_points(*) = self
    def playing? = true
  end

  def self.forward(name, *args)
    AUDIO[name]&.call(*args)
  end

  def self.install
    return if Object.const_defined?(:FmodEx)
    mod = Module.new
    mod.define_singleton_method(:init) { |*| true }
    %w[bgm bgs].each do |channel|
      mod.define_singleton_method(:"#{channel}_play") do |file, volume = 100, pitch = 100, *|
        HardRPGFmodEx.forward(:"#{channel}_play", file, volume, pitch)
        mod.instance_variable_set(:"@#{channel}", Sound.new(file))
      end
      mod.define_singleton_method(:"#{channel}_sound") { mod.instance_variable_get(:"@#{channel}") }
    end
    mod.define_singleton_method(:me_play) { |file, volume = 100, pitch = 100, *| HardRPGFmodEx.forward(:me_play, file, volume, pitch); Sound.new(file) }
    mod.define_singleton_method(:se_play) { |file, volume = 100, pitch = 100, *| HardRPGFmodEx.forward(:se_play, file, volume, pitch); Sound.new(file) }
    %w[bgm bgs me].each do |channel|
      mod.define_singleton_method(:"#{channel}_fade") { |time, *| HardRPGFmodEx.forward(:"#{channel}_fade", time) }
      mod.define_singleton_method(:"#{channel}_stop") { |*| HardRPGFmodEx.forward(:"#{channel}_stop") }
    end
    mod.define_singleton_method(:se_stop) { |*| HardRPGFmodEx.forward(:se_stop) }
    Object.const_set(:FmodEx, mod)
  end
end
HardRPGRGSSExtensions.define('RGSS FmodEx', 'Init_FmodEx') { HardRPGFmodEx.install }

module HardRPGFlatMode7
  def hm7
    false
  end
end

# MGC H-Mode7 (MGC_Hmode7.dll) renders a map in perspective with height maps
# in native code (Pokemon Insurgence: soaring and one story map). Until the
# engine has its own renderer, maps that ask for it are drawn as ordinary
# top-down maps: the game sees H-Mode7 as off, which its scripts support.
HardRPGCompat.add('MGC H-Mode7') do
  next false unless Object.const_defined?(:HM7) && Object.const_defined?(:Game_System) &&
                    Game_System.method_defined?(:hm7)
  next false if Game_System.instance_method(:hm7).owner == HardRPGFlatMode7
  Game_System.prepend(HardRPGFlatMode7)
  true
end

# In Ruby 1.8 (RGSS1) map, collect, select and friends called without a block
# returned an Array; now they return an Enumerator, which has no [] or
# length. Insurgence reads `str.split('').map[0]` while it creates the player.
# Enumerator never had these readers, so only code that would otherwise
# stop with NoMethodError reaches them.
class Enumerator
  def [](*args) = to_a[*args]
  def at(index) = to_a.at(index)
  def last(*args) = to_a.last(*args)
  def length = to_a.length
end

# Ruby 1.8 had no String#getbyte, #setbyte or #bytesize, and scripts of that
# era add them as aliases of [], []= and size (Zeus81's Bitmap Export, in
# Pokemon Uranium). In 1.8 those returned bytes; in Ruby 3 they return
# characters, so the aliases break every byte read in the game. The engine's
# own methods are restored whenever a section replaces them.
module HardRPGNativeMethods
  STRING = %i[getbyte setbyte bytesize byteslice].each_with_object({}) do |name, map|
    map[name] = String.instance_method(name) if String.method_defined?(name)
  end
end
HardRPGCompat.add('Ruby 1.8 String backports') do
  restored = false
  HardRPGNativeMethods::STRING.each do |name, native|
    # An alias of another built-in has no source location either: compare
    # with the method captured before the game's scripts ran.
    next if String.instance_method(name) == native
    String.send(:define_method, name, native)
    restored = true
  end
  restored
end
