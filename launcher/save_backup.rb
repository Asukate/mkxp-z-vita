# HardRPG save backups (preload, after safe_save.rb).
#
# Each time the game replaces a save, the save it replaces is kept outside
# the game folder, in ux0:/data/hardrpg/save-backups/<settings key>/<file>/,
# so an overwritten save can be put back from the launcher and removing the
# game folder does not take its saves with it. safe_save.rb has already moved
# the old file aside to swap the new one in; here it is moved into the backup
# folder instead of being deleted (a rename, so it costs next to nothing; a
# copy when the game is on another memory card).
#
# Per save file (a "slot" folder):
#   path.txt                 where the game keeps the file: relative to the
#                            game folder, or absolute (Essentials, AppData)
#   current.json             what the file in the game holds now (size, time,
#                            map, party leader, level, play time)
#   YYYYMMDD-HHMMSS.<ext>    a kept version, named by the time it was saved
#   YYYYMMDD-HHMMSS.json     its description; "why" says how it was kept
#
# Which versions stay: the newest always; an older one only when it is at
# least GAP seconds newer than the next older one (so autosaves every few
# minutes do not push out everything else); at most "keep" of them. The
# launcher writes game.json with "enabled" and "keep" before the game starts.
# The launcher does the rest (copies from the library, restore, listing).
module HardRPGSaveBackup
  GAP = 600
  KEEP = 5
  MAX_BYTES = 32 * 1024 * 1024
  VERSION = /\A(\d{8})-(\d{6})(?:-(\d+))?\.[^.]+\z/
  DIR = begin
    if $hardrpg_save_backup_dir
      $hardrpg_save_backup_dir.to_s
    elsif $hardrpg_rescue_base
      base = $hardrpg_rescue_base.to_s
      File.join(File.dirname(File.dirname(base)), 'save-backups', File.basename(base))
    end
  end
  STATS = Hash.new(0)

  class << self
    attr_reader :last_error

    # game.json comes from the launcher at each start: the switch and the
    # count as they were then, and the game folder. The game's own settings
    # file has the switch as changed in the in-game menu since.
    def settings
      text = read(File.join(DIR, 'game.json'))
      enabled = text !~ /"enabled"\s*:\s*false/
      own = read(File.join(File.dirname(File.dirname(DIR)), 'config', File.basename(DIR) + '.json'))
      enabled = $1 == 'true' if own =~ /"saveBackups"\s*:\s*(true|false)/
      keep = text[/"keep"\s*:\s*(\d+)/, 1].to_i
      @folder = (m = text.match(/"folder"\s*:\s*"((?:[^"\\]|\\.)*)"/)) ? unjson(m[1]) : nil
      [enabled, keep > 0 ? keep : KEEP]
    end

    def read(path)
      File.exist?(path) ? File.binread(path) : ''
    rescue SystemCallError
      ''
    end

    # The save as the launcher names it: inside the game folder, relative to
    # it ("Save01.rvdata2", not "./Save01.rvdata2" or the whole path).
    def name_of(target)
      t = target.tr('\\', '/').sub(/\A(?:\.\/)+/, '')
      f = @folder.to_s.chomp('/')
      t = t[f.size + 1..-1] if !f.empty? && t.start_with?(f + '/')
      t
    end

    # Game data written at run time (a compiler, a cache) is not a save.
    def save?(target)
      parts = target.tr('\\', '/').split('/')
      parts[0...-1].none? { |p| p.casecmp('data').zero? }
    end

    def stamp(t)
      t = Time.at(t)
      format('%04d%02d%02d-%02d%02d%02d', t.year, t.month, t.day, t.hour, t.min, t.sec)
    end

    def epoch(name)
      m = VERSION.match(name) or return 0
      d, h = m[1], m[2]
      Time.local(d[0, 4].to_i, d[4, 2].to_i, d[6, 2].to_i, h[0, 2].to_i, h[2, 2].to_i, h[4, 2].to_i).to_i
    end

    def ensure_dir(path)
      Dir.mkdir(path) unless File.directory?(path)
    rescue SystemCallError
      raise unless File.directory?(path)
    end

    def fnv(text)
      h = 14_695_981_039_346_656_037
      text.each_byte { |c| h = ((h ^ c) * 1_099_511_628_211) & 0xffffffffffffffff }
      format('%016x', h)
    end

    # The folder for a save file: its name, or its name and a hash of its
    # path when another file of that name (elsewhere) has the plain one.
    def slot_dir(target)
      name = File.basename(target)
      [name, "#{name}~#{fnv(target)[0, 8]}"].each do |slot|
        dir = File.join(DIR, slot)
        path_file = File.join(dir, 'path.txt')
        if File.directory?(dir)
          recorded = File.exist?(path_file) ? File.binread(path_file) : ''
          return dir if recorded.empty? || recorded == target
        else
          ensure_dir(dir)
          File.binwrite(path_file, target)
          return dir
        end
      end
      nil
    end

    def versions(dir)
      Dir.children(dir).select { |n| VERSION.match?(n) && !n.end_with?('.json') }
         .sort_by { |n| [-epoch(n), n] }
    rescue SystemCallError
      []
    end

    def json_of(dir, version)
      File.join(dir, version.sub(/\.[^.]+\z/, '.json'))
    end

    def why(dir, version)
      path = json_of(dir, version)
      File.exist?(path) ? File.binread(path)[/"why"\s*:\s*"([a-z]+)"/, 1].to_s : ''
    end

    def drop(dir, version)
      File.delete(File.join(dir, version)) rescue nil
      File.delete(json_of(dir, version)) rescue nil
    end

    def same?(a, b)
      File.size(a) == File.size(b) && File.binread(a) == File.binread(b)
    end

    # safe_save.rb: the new save is in place; old is the one it replaced.
    def retire(old, target)
      t = clock
      enabled, keep = settings
      unless DIR && enabled && save?(target)
        File.delete(old) if File.exist?(old)
        return
      end
      ensure_dir(File.dirname(DIR))
      ensure_dir(DIR)
      dir = slot_dir(name_of(target))
      if dir.nil?
        File.delete(old) if File.exist?(old)
        return
      end
      keep_old(dir, old, keep) if File.exist?(old)
      File.binwrite(File.join(dir, 'current.json'), json(describe(target)))
      STATS['ms'] += ((clock - t) * 1000).round(1)
      STATS['saves'] += 1
    rescue StandardError => e
      @last_error = e
      File.delete(old) rescue nil
    end

    def keep_old(dir, old, keep)
      size = File.size(old)
      return File.delete(old) if size > MAX_BYTES
      list = versions(dir)
      return File.delete(old) if list.first && same?(old, File.join(dir, list.first))
      saved = File.mtime(old).to_i
      ext = File.extname(old.sub(/\.hardrpg-old\z/, ''))
      name = stamp(saved) + ext
      n = 1
      name = "#{stamp(saved)}-#{n += 1}#{ext}" while File.exist?(File.join(dir, name))
      dest = File.join(dir, name)
      begin
        File.rename(old, dest)
      rescue SystemCallError
        File.binwrite(dest, File.binread(old)) # another memory card
        File.delete(old)
      end
      info = described(dir, size, saved)
      info['saved'] = saved
      info['kept'] = Time.now.to_i
      info['why'] = 'save'
      info['size'] = size
      File.binwrite(json_of(dir, name), json(info))
      thin(dir, keep)
    end

    # current.json describes the old file when it is still the one it was
    # written for.
    def described(dir, size, saved)
      path = File.join(dir, 'current.json')
      return {} unless File.exist?(path)
      text = File.binread(path)
      return {} unless text[/"size":(\d+)/, 1].to_i == size && (text[/"saved":(\d+)/, 1].to_i - saved).abs <= 2
      info = {}
      text.scan(/"(map_name|leader)":"((?:[^"\\]|\\.)*)"/) { |k, v| info[k] = unjson(v) }
      text.scan(/"(level|playtime_s|map_id)":(\d+)/) { |k, v| info[k] = v.to_i }
      info
    end

    def unjson(text)
      text.gsub(/\\u([0-9a-f]{4})/) { [$1.hex].pack('U') }.gsub(/\\(.)/, '\1')
    end

    def thin(dir, keep)
      list = versions(dir)
      if list.size >= 3 && why(dir, list[1]) == 'save' && epoch(list[1]) - epoch(list[2]) < GAP
        drop(dir, list[1])
        list.delete_at(1)
      end
      list.drop(keep).each { |v| drop(dir, v) }
    end

    def describe(target)
      info = { 'size' => File.size(target), 'saved' => File.mtime(target).to_i }
      party = ($game_party.respond_to?(:members) ? $game_party.members : $game_party.actors) rescue nil
      leader = party.first rescue nil
      if leader
        info['leader'] = leader.name.to_s
        info['level'] = leader.level.to_i rescue 0
      end
      map = ($game_map.map_id rescue 0).to_i
      if map > 0
        info['map_id'] = map
        name = ($game_map.display_name.to_s rescue '')
        name = map_names[map].to_s if name.empty?
        info['map_name'] = name unless name.empty?
      end
      frames = (Graphics.frame_count / Graphics.frame_rate rescue 0).to_i
      info['playtime_s'] = frames if frames > 0
      info
    rescue StandardError
      { 'size' => (File.size(target) rescue 0), 'saved' => (File.mtime(target).to_i rescue 0) }
    end

    def map_names
      @map_names ||= begin
        infos = nil
        %w[Data/MapInfos.rxdata Data/MapInfos.rvdata Data/MapInfos.rvdata2].each do |f|
          infos = (load_data(f) rescue nil)
          break if infos
        end
        names = {}
        (infos || {}).each { |id, i| names[id] = (i.name.to_s rescue '') }
        names
      end
    end

    def json(value)
      case value
      when Hash then '{' + value.map { |k, v| json(k.to_s) + ':' + json(v) }.join(',') + '}'
      when String
        s = value.dup.force_encoding(Encoding::UTF_8)
        s = s.encode(Encoding::UTF_8, invalid: :replace, undef: :replace, replace: '?') unless s.valid_encoding?
        '"' + s.gsub(/["\\\x00-\x1f]/) { |c| format('\\u%04x', c.ord) } + '"'
      when nil then 'null'
      else value.to_s
      end
    end

    def clock
      Process.clock_gettime(Process::CLOCK_MONOTONIC)
    rescue StandardError
      Time.now.to_f
    end
  end
end

if HardRPGSaveBackup::DIR && defined?(HardRPGSafeSave)
  class << HardRPGSafeSave
    def retire(old, target)
      HardRPGSaveBackup.retire(old, target)
    end
  end
end
