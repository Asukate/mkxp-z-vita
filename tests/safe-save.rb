# Check launcher/safe_save.rb with the stock VX Ace save code (issue 14).
# Like the Vita, File.rename here refuses to replace an existing file.
require 'tmpdir'
require 'fileutils'

class << File
  alias_method :host_rename, :rename
  def rename(from, to)
    raise Errno::EEXIST, to if File.exist?(to)
    host_rename(from, to)
  end
end

Dir.mktmpdir do |dir|
  Dir.chdir(dir) do
    # A launch after a stop between the two renames: the old file comes back,
    # and a partial temp file is removed.
    File.binwrite('Save01.rvdata2.hardrpg-old', 'old one')
    File.binwrite('Save02.rvdata2.hardrpg-tmp', 'partial')
    File.binwrite('Save02.rvdata2', 'kept')
    load File.expand_path('../launcher/safe_save.rb', __dir__)
    raise 'old file not restored' unless File.binread('Save01.rvdata2') == 'old one'
    raise 'old file left behind' if File.exist?('Save01.rvdata2.hardrpg-old')
    raise 'partial temp left behind' if File.exist?('Save02.rvdata2.hardrpg-tmp')
    raise 'good save touched' unless File.binread('Save02.rvdata2') == 'kept'

    # Stock VX Ace DataManager.save_game / save_game_without_rescue.
    save = lambda do |name, contents|
      begin
        File.open(name, 'wb') do |file|
          Marshal.dump({ header: 1 }, file)
          Marshal.dump(contents, file)
        end
        true
      rescue
        File.delete(name) rescue nil
        false
      end
    end
    big = { map: Array.new(20_000) { |i| "event #{i}" } }

    raise 'save failed' unless save.call('Save03.rvdata2', big)
    first = File.binread('Save03.rvdata2')
    File.open('Save03.rvdata2', 'rb') { |f| Marshal.load(f); raise 'bad contents' unless Marshal.load(f) == big }

    # A write that stops part way with an error the game does not rescue
    # (here NoMemoryError from inside the dump) keeps the previous save.
    bomb = Object.new
    def bomb.marshal_dump
      raise NoMemoryError, 'failed to allocate memory'
    end
    begin
      save.call('Save03.rvdata2', { map: Array.new(20_000) { |i| "new #{i}" } + [bomb] })
      raise 'no error'
    rescue NoMemoryError
    end
    raise 'previous save lost' unless File.binread('Save03.rvdata2') == first
    raise 'partial file left' if File.exist?('Save03.rvdata2.hardrpg-tmp')

    # An ordinary error part way (disk full, a write error): the stock rescue
    # deletes the slot, which here is the intact previous save. It stays.
    broken = Object.new
    def broken.marshal_dump
      raise Errno::ENOSPC, 'Save03.rvdata2'
    end
    raise 'failed save reported ok' if save.call('Save03.rvdata2', { map: Array.new(20_000) { |i| "new #{i}" } + [broken] })
    raise 'previous save deleted after a failed write' unless File.binread('Save03.rvdata2') == first
    # Only that one clean-up delete is skipped.
    File.binwrite('Other.rvdata2', 'x')
    File.delete('Other.rvdata2')
    raise 'unrelated delete skipped' if File.exist?('Other.rvdata2')

    # Replacing works although rename cannot overwrite.
    changed = { map: ['changed'] }
    raise 'second save failed' unless save.call('Save03.rvdata2', changed)
    File.open('Save03.rvdata2', 'rb') { |f| Marshal.load(f); raise 'not replaced' unless Marshal.load(f) == changed }
    raise 'old copy left' if File.exist?('Save03.rvdata2.hardrpg-old')

    # RPG Maker XP style: open, write, close by hand.
    file = File.open('Save4.rxdata', 'wb')
    Marshal.dump([1, 2], file)
    raise 'written before close' if File.exist?('Save4.rxdata')
    file.close
    raise 'XP save missing' unless File.open('Save4.rxdata', 'rb') { |f| Marshal.load(f) } == [1, 2]

    # Keyword mode, other files and other modes are left alone.
    File.open('Save05.rvdata', mode: 'wb') { |f| f.write('kw') }
    raise 'keyword mode' unless File.binread('Save05.rvdata') == 'kw'
    File.open('notes.txt', 'w') { |f| f.write('x') }
    File.open('Save05.rvdata', 'ab') { |f| f.write('+') }
    raise 'append changed' unless File.binread('Save05.rvdata') == 'kw+'
    # Saves are collected and written in large chunks, not piece by piece,
    # and every write form keeps its bytes in order.
    File.open('Save06.rvdata2', 'wb') do |f|
      Marshal.dump({ small: 1 }, f)
      raise 'small save written piece by piece' unless File.size(f.path) == 0
      Marshal.dump(Array.new(60_000) { |i| "piece #{i}" }, f)
      raise 'big save not written in chunks' unless File.size(f.path) > 0
    end
    File.open('Save06.rvdata2', 'rb') do |f|
      Marshal.load(f)
      raise 'chunked contents' unless Marshal.load(f) == Array.new(60_000) { |i| "piece #{i}" }
    end
    File.open('Save07.rxdata', 'wb') { |f| f.print('a'); f.puts('b'); f << 'c'; f.write('d', 'e'); f.putc('f') }
    raise 'write forms' unless File.binread('Save07.rxdata') == "ab\ncdef"
    raise 'temp files left' unless Dir.glob('*.hardrpg-*').empty?
  end
end
puts 'safe-save: ok'
