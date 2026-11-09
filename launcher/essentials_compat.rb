# Pokemon Essentials compatibility (preload). Essentials v16-v17 and the fan
# games built on it (Uranium, Insurgence, ...) were written for RGSS1 on
# Windows and replace parts of Graphics with Windows-only code. Each fix
# below checks for the Essentials code it replaces and leaves other games,
# and other Essentials versions, alone. Runs through HardRPGCompat after every script section.
module HardRPGEssentials
  # The engine's own Graphics methods, before a game replaces them.
  NATIVE_SNAP = Graphics.method(:snap_to_bitmap) if Graphics.respond_to?(:snap_to_bitmap)
  NATIVE_RESIZE = Graphics.method(:resize_screen) if Graphics.respond_to?(:resize_screen)
  NATIVE_WIDTH = Graphics.method(:width)
  NATIVE_HEIGHT = Graphics.method(:height)

  def self.essentials?
    Object.const_defined?(:PokemonSystem) && Object.const_defined?(:PokeBattle_Pokemon)
  end

  # A method the game defined in Ruby (a script section), not the engine's.
  # Takes an object and method name, or an UnboundMethod and nil.
  def self.game_defined?(owner, name)
    method = name.nil? ? owner : owner.method(name)
    method.source_location != nil
  rescue NameError
    false
  end



  # v16/v17 read their binary data files (items.dat, moves, encounters...)
  # through StringInput, whose getc returns @string[@pos]: a byte number in
  # Ruby 1.8, a one-character string now, so fgetw/fgetdw fail with "String
  # can't be coerced into Integer" and the game stops on a black screen.
  # FontInstaller copies the game's fonts into the Windows font folder,
  # then asks the player to restart. The engine reads the game's Fonts
  # folder directly, so there is nothing to install: it would only show the
  # warning, write stray files and restart the game at every launch.
  def self.fix_font_installer
    return false unless Object.const_defined?(:FontInstaller) &&
                        FontInstaller.respond_to?(:install) &&
                        game_defined?(FontInstaller, :install)
    return false if FontInstaller.singleton_class.ancestors.include?(HardRPGNoFontInstall)
    FontInstaller.singleton_class.prepend(HardRPGNoFontInstall)
    true
  end

  # getPlayTime (the length of a WAV, OGG or MP3, used by title screens to
  # time their music) and its helpers read single bytes as file.read(1)[0]:
  # a number in Ruby 1.8, a one-character string now ("undefined method
  # `times' for \"\\x01\":String"). They are replaced by HardRPGAudioLength.
  AUDIO_LENGTH = %i[getPlayTime2 oggfiletime getOggPage]

  def self.fix_audio_length
    return false unless AUDIO_LENGTH.all? { |m| Object.private_method_defined?(m) || Object.method_defined?(m) }
    owner = Object.instance_method(:getPlayTime2)
    return false if owner.source_location.nil? || owner.source_location[0].start_with?('app0:')
    Object.class_eval do
      define_method(:getPlayTime2) { |filename| HardRPGAudioLength.play_time(filename) }
      define_method(:oggfiletime) { |file| HardRPGAudioLength.ogg_time(file) }
      define_method(:getOggPage) { |file| HardRPGAudioLength.ogg_page(file) }
      private :getPlayTime2, :oggfiletime, :getOggPage
    end
    true
  end

  # pbResolveBitmap finds an image by trying up to seven file names on each
  # search path; in a packed game every try reads the archive (load_data). It
  # runs every frame (Uranium's player sprite asks for an outfit file that
  # does not exist), which took the gender screen down to about 12 fps. The
  # game's files do not change while it runs, so each answer is kept.
  def self.fix_resolve_cache
    return false unless Object.private_method_defined?(:pbResolveBitmap) || Object.method_defined?(:pbResolveBitmap)
    current = Object.instance_method(:pbResolveBitmap)
    return false if current == @resolve_wrapper || current.arity != 1 || !game_defined?(current, nil)
    @resolve_gen = (@resolve_gen || 0) + 1
    original = :"hardrpg_uncached_pbResolveBitmap_#{@resolve_gen}"
    Object.send(:alias_method, original, :pbResolveBitmap)
    Object.send(:define_method, :pbResolveBitmap) do |x|
      HardRPGResolveCache.fetch(x) { send(original, x) }
    end
    Object.send(:private, :pbResolveBitmap, original)
    HardRPGResolveCache.clear # a new definition may answer differently
    @resolve_wrapper = Object.instance_method(:pbResolveBitmap)
    true
  end

  # pbSameThread(window) asks Windows whether the game's window is in front.
  # Essentials asks before every key check, and builds two Win32API objects
  # each time: a quarter of the script time while walking in Uranium. The
  # engine answers those calls itself (HardRPGWindow): the game's window is
  # always in front, so the answer is fixed.
  def self.fix_same_thread
    return false unless Object.private_method_defined?(:pbSameThread) || Object.method_defined?(:pbSameThread)
    current = Object.instance_method(:pbSameThread)
    return false if current == @same_thread || current.arity != 1 || !game_defined?(current, nil)
    return false unless defined?(HardRPGWindow) && HardRPGWin32.handler('user32', 'GetWindowThreadProcessId')
    Object.send(:define_method, :pbSameThread) { |wnd| wnd == HardRPGWindow::HWND }
    Object.send(:private, :pbSameThread)
    @same_thread = Object.instance_method(:pbSameThread)
    true
  end

  # Text entry (player, Pokemon and box names): Essentials uses a bare text
  # box typed on a PC keyboard unless it sees a controller. The Vita has no
  # keyboard, so it takes the on-screen letter grid, as with a controller.
  def self.fix_text_entry
    changed = false
    if Object.const_defined?(:USEKEYBOARDTEXTENTRY) && Object.const_get(:USEKEYBOARDTEXTENTRY) == true
      Object.send(:remove_const, :USEKEYBOARDTEXTENTRY)
      Object.const_set(:USEKEYBOARDTEXTENTRY, false)
      changed = true
    end
    if Object.const_defined?(:PokemonEntryScene) && PokemonEntryScene.const_defined?(:USEKEYBOARD, false) &&
       PokemonEntryScene.const_get(:USEKEYBOARD) == true
      PokemonEntryScene.send(:remove_const, :USEKEYBOARD)
      PokemonEntryScene.const_set(:USEKEYBOARD, false)
      changed = true
    end
    changed
  end

  def self.fix_string_input
    return false unless Object.const_defined?(:StringInput) &&
                        StringInput.method_defined?(:getc) &&
                        game_defined?(StringInput.instance_method(:getc), nil)
    return false if StringInput.ancestors.include?(HardRPGStringInputBytes)
    StringInput.prepend(HardRPGStringInputBytes)
    true
  end

  # RGSS1 always rendered 640x480. v16/v17 show a 512x384 game by shrinking
  # the player window (found with FindWindowEx("RGSS Player")) so only the
  # top-left 512x384 is visible, and report that size from Graphics.width
  # and Graphics.height. The engine resizes its screen to that size instead.
  def self.fix_screen_size
    return false unless NATIVE_RESIZE && game_defined?(Graphics, :width)
    width = Graphics.width.to_i
    height = Graphics.height.to_i
    return false unless width.between?(160, 1280) && height.between?(120, 720)
    return false if width == NATIVE_WIDTH.call && height == NATIVE_HEIGHT.call
    NATIVE_RESIZE.call(width, height)
    true
  end

  # v16/v17 take screenshots (save previews, battle transitions, menus over
  # the map) with rubyscreen.dll writing a BMP to TEMP and load it back. The
  # engine captures the screen itself.
  def self.fix_snap_to_bitmap
    return false unless NATIVE_SNAP && game_defined?(Graphics, :snap_to_bitmap)
    return false if Graphics.method(:snap_to_bitmap).owner == HardRPGEssentialsSnap
    Graphics.singleton_class.prepend(HardRPGEssentialsSnap)
    true
  end
end

# The length in seconds of a WAV, OGG Vorbis or MP3 file, as Essentials'
# getPlayTime2 computed it, reading bytes as numbers.
module HardRPGAudioLength
  def self.dword(f) = f.eof? ? 0 : (f.read(4).unpack1('V') || 0)
  def self.word(f) = f.eof? ? 0 : (f.read(2).unpack1('v') || 0)
  def self.byte(f) = f.read(1)&.getbyte(0) || 0

  def self.play_time(filename)
    return -1 unless safe_exist?(filename)
    File.open(filename, 'rb') do |f|
      magic = dword(f)
      return wav_time(f) if magic == 0x46464952 # RIFF
      if magic == 0x5367674F # OggS
        f.pos = 0
        return ogg_time(f)
      end
      f.pos = 0
      mp3_time(f, filename)
    end
  end

  def self.safe_exist?(filename)
    defined?(safeExists?) ? safeExists?(filename) : File.exist?(filename)
  end

  def self.wav_time(f)
    dword(f) # file size
    return -1 unless dword(f) == 0x45564157 && dword(f) == 0x20746d66 # WAVE, fmt
    dword(f); word(f); word(f); dword(f) # fmt size, format, channels, rate
    bytes_per_second = dword(f)
    return -1 if bytes_per_second == 0
    word(f); word(f) # block align, bits
    return -1 unless dword(f) == 0x61746164 # data
    dword(f) * 1.0 / bytes_per_second
  end

  def self.ogg_page(f)
    return nil unless dword(f) == 0x5367674F
    header = f.read(22)
    segments = byte(f)
    body = 0
    segments.times { body += byte(f) }
    [header, f.pos, body, f.pos + body]
  end

  def self.ogg_time(f)
    pages = []
    while (page = ogg_page(f))
      pages << page
      f.pos = page[3]
    end
    return -1 if pages.empty?
    serial = nil
    i = -1
    lengths = []
    rates = []
    pages.each do |header, body_pos, _, _|
      page_serial = header[10, 4].unpack('V')
      frame = header[2, 8].unpack('C*').reverse.inject(0) { |v, b| (v << 8) | b }
      if page_serial != serial
        serial = page_serial
        f.pos = body_pos
        return -1 unless byte(f) == 1 && f.read(6) == 'vorbis'
        i += 1
        return -1 unless dword(f) == 0
        byte(f) # channels
        rates[i] = dword(f)
      end
      lengths[i] = frame
    end
    (0...lengths.length).sum { |k| lengths[k] * 1.0 / rates[k] }
  end

  BITRATES = [32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320].freeze
  FREQUENCIES = [44100, 22050, 11025, 48000].freeze

  def self.mp3_time(f, filename)
    loop do
      header = nil
      until f.eof?
        next unless byte(f) == 0xFF
        header = f.read(3)
        break
      end
      return -1 unless header && header.bytesize == 3
      next unless header.getbyte(0) == 0xFB
      b = header.getbyte(1)
      index = b >> 4
      next if index == 0 || index == 15
      bitrate = BITRATES[index]
      freq = FREQUENCIES[(b >> 2) & 3]
      padding = (b >> 1) & 1
      frame_length = (144000 * bitrate) / freq + padding
      frames = File.size(filename) / (frame_length + 4)
      return frames * 1152.0 / freq
    end
  end
end

module HardRPGNoFontInstall
  def install(*)
    nil
  end
end

# Answers of pbResolveBitmap by name (nil included), at most LIMIT names.
module HardRPGResolveCache
  LIMIT = 4096
  @answers = {}

  def self.fetch(name)
    return yield unless name.is_a?(String)
    return @answers[name] if @answers.key?(name)
    answer = yield
    @answers.clear if @answers.size >= LIMIT
    @answers[name.dup.freeze] = answer
  end

  def self.clear
    @answers.clear
  end
end

# StringInput#getc as Ruby 1.8 answered it: the byte at the position.
module HardRPGStringInputBytes
  def getc
    raise IOError, 'closed stream' if @closed
    string = @string
    ch = if string.encoding == Encoding::BINARY || string.ascii_only?
           string.getbyte(@pos)
         else
           string[@pos]&.ord
         end
    @pos += 1
    @pos += 1 if @pos == string.size
    ch
  end

  # Ruby 3's Marshal.load reads an IO-like object through getbyte and read;
  # Ruby 1.8 used getc. Without it, Marshal.load(stringinput) fails, e.g.
  # Insurgence's trainertypes.dat right after the player names themself.
  def getbyte
    raise IOError, 'closed stream' if @closed
    b = @string.getbyte(@pos)
    return nil unless b
    @pos += 1
    @pos += 1 if @pos == @string.size
    b
  end
end

# Prepended, so it stays in front when a later section redefines the method.
module HardRPGEssentialsSnap
  def snap_to_bitmap(*)
    HardRPGEssentials::NATIVE_SNAP.call
  end
end

if defined?(HardRPGCompat)
  HardRPGCompat.add('Essentials screenshots') { HardRPGEssentials.essentials? && HardRPGEssentials.fix_snap_to_bitmap }
  HardRPGCompat.add('Essentials screen size') { HardRPGEssentials.essentials? && HardRPGEssentials.fix_screen_size }
  HardRPGCompat.add('Essentials binary reads') { HardRPGEssentials.essentials? && HardRPGEssentials.fix_string_input }
  HardRPGCompat.add('Essentials audio length') { HardRPGEssentials.essentials? && HardRPGEssentials.fix_audio_length }
  HardRPGCompat.add('Essentials text entry') { HardRPGEssentials.essentials? && HardRPGEssentials.fix_text_entry }
  HardRPGCompat.add('Essentials image lookup cache') { HardRPGEssentials.essentials? && HardRPGEssentials.fix_resolve_cache }
  HardRPGCompat.add('Essentials window check') { HardRPGEssentials.essentials? && HardRPGEssentials.fix_same_thread }
  HardRPGCompat.add('Essentials font installer') { HardRPGEssentials.essentials? && HardRPGEssentials.fix_font_installer }
end
