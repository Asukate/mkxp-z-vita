# Shared browser/model for the RGSS launcher and the desktop preview.
module HardRPG
  VERSION = "0.1"
  RELEASE_STAGE = "Alpha"
  ROOT = defined?(HARDRPG_DATA_ROOT) ? HARDRPG_DATA_ROOT : "ux0:/data/hardrpg"
  GAMES_ROOT = ROOT + "/games"
  RTP_ROOT = ROOT + "/rtp"
  CONFIG_ROOT = ROOT + "/config"
  CACHE_ROOT = ROOT + "/cache"
  PICK_PATH = ROOT + "/selection.json"
  LIBRARY_PATHS = ROOT + "/library-paths.txt"
  RTP_PATHS = ROOT + "/rtp-paths.txt"
  GAME_FOLDERS = ROOT + "/game-folders.txt"
  BROWSE_ROOTS = defined?(HARDRPG_BROWSE_ROOTS) ? HARDRPG_BROWSE_ROOTS : %w[ux0:/ uma0:/ imc0:/]
  REPOSITORY = "https://github.com/Asukate/mkxp-z-vita"
  Entry = Struct.new(:name, :kind, :node, :version)

  # The Vita Ruby build deliberately omits the JSON extension. Encode only
  # our small handoff/receipt types; native code validates the parsed schema.
  def self.json(value)
    case value
    when String
      '"' + value.gsub(/["\\\x00-\x1f]/) { |c| "\\u%04x" % c.ord } + '"'
    when Integer; value.to_s
    when Float; raise ArgumentError, "Non-finite JSON number" unless value.finite?; value == value.to_i ? value.to_i.to_s : value.to_s
    when TrueClass; "true"
    when FalseClass; "false"
    when NilClass; "null"
    when Array; "[" + value.map { |item| json(item) }.join(",") + "]"
    when Hash; "{" + value.map { |key, item| json(key) + ":" + json(item) }.join(",") + "}"
    else; raise ArgumentError, "Unsupported handoff value"
    end
  end

  def self.mkdir(path)
    Dir.mkdir(path) unless File.directory?(path)
  end

  def self.setup
    mkdir(File.dirname(ROOT))
    [ROOT, GAMES_ROOT, RTP_ROOT, CONFIG_ROOT, CACHE_ROOT].each { |p| mkdir(p) }
    %w[Standard RPGVX RPGVXAce].each { |p| mkdir(RTP_ROOT + "/" + p) }
  end

  def self.safe_relative?(path, empty = false)
    return empty if path.empty?
    return false if path.match?(/[\x00-\x1f\\:]/)
    path.split("/", -1).all? { |part| !part.empty? && part != "." && part != ".." }
  end

  def self.safe_external?(path)
    BROWSE_ROOTS.any? do |root|
      path.start_with?(root) && safe_relative?(path[root.length..-1])
    end
  end

  def self.key(path)
    # Stable across launches and ZIP edits; never uses Ruby's randomized hash.
    value = 14_695_981_039_346_656_037
    path.each_byte { |b| value = ((value ^ b) * 1_099_511_628_211) & 0xffffffffffffffff }
    value.to_s(16).rjust(16, "0")
  end

  class MissingRtp < IOError
    attr_reader :pack
    def initialize(pack)
      @pack = pack
      super("Missing #{Rtp.label(pack)} RTP. Choose its folder or ZIP in Settings.")
    end
  end

  module DisplaySettings
    OPTIONS = [
      ['fixedAspectRatio', 'Aspect ratio', [true, false], ['Original', 'Fill screen']],
      ['integerScalingActive', 'Scaling', [false, true], ['Fit screen', 'Whole pixels']],
      ['smoothScaling', 'Filtering', [0, 1], ['Nearest', 'Bilinear']]
    ]
    DEFAULTS = {'fixedAspectRatio' => true, 'integerScalingActive' => false, 'smoothScaling' => 0}
    PATH = ROOT + '/launcher-config.json'
    def self.read
      return {} unless File.file?(PATH)
      raise IOError, "Settings file is too large" if File.size(PATH) > 262_144
      begin
        data = HTTPLite::JSON.parse(File.read(PATH))
        raise ArgumentError, "Expected settings object" unless data.is_a?(Hash)
        data
      rescue StandardError => e
        raise IOError, "Cannot read settings: #{e.message}"
      end
    end
    def self.values; DEFAULTS.merge(read); end
    def self.change(key, value)
      option = OPTIONS.find { |item| item.first == key }
      raise ArgumentError, "Invalid display setting" unless option && option[2].include?(value)
      data = read
      data[key] = value
      data['integerScalingLastMile'] = !value if key == 'integerScalingActive'
      data['smoothScalingDown'] = value if key == 'smoothScaling'
      File.open(PATH + '.tmp', 'wb') { |f| f.write(HardRPG.json(data)) }
      File.rename(PATH + '.tmp', PATH)
      values
    end
  end

  module Rtp
    PACKS = %w[Standard RPGVX RPGVXAce]
    ALIASES = {"Standard" => %w[Standard XP], "RPGVX" => %w[RPGVX VX], "RPGVXAce" => %w[RPGVXAce VXACE]}
    def self.pack(name)
      raise ArgumentError, "Invalid RTP pack name" if !HardRPG.safe_relative?(name) || name.include?("/")
      ALIASES.each { |canonical, aliases| return canonical if aliases.any? { |item| item.casecmp(name).zero? } }
      name
    end
    def self.label(name)
      {"Standard" => "XP", "RPGVX" => "VX", "RPGVXAce" => "VX Ace"}.fetch(name, name)
    end
    def self.paths
      return {} unless File.file?(RTP_PATHS)
      File.readlines(RTP_PATHS).each_with_object({}) do |line, result|
        name, path = line.chomp.split("\t", 2)
        result[name] = path if PACKS.include?(name) && path && HardRPG.safe_external?(path)
      end
    end
    def self.set(name, path)
      name = pack(name)
      raise ArgumentError, "Unknown RTP setting" unless PACKS.include?(name)
      if path
        raise ArgumentError, "Choose an RTP folder or ZIP on Vita storage" unless HardRPG.safe_external?(path)
        raise IOError, "Choose a pack containing Graphics and Audio folders." unless inspect(path, name)
      end
      choices = paths
      path ? choices[name] = path : choices.delete(name)
      File.open(RTP_PATHS + ".tmp", "wb") { |f| choices.each { |key, value| f.write(key + "\t" + value + "\n") } }
      File.rename(RTP_PATHS + ".tmp", RTP_PATHS)
    end
    # RTP is mounted read-only in place. A single wrapper in a ZIP is handled
    # with PhysicsFS's archive root, without extracting a second copy.
    def self.inspect(path, name, depth = 0)
      return nil if depth > 8
      zip = File.file?(path) && path.downcase.end_with?(".zip")
      return nil unless zip || File.directory?(path)
      HardRPGArchive.open(path) if zip
      inner = ""
      (9 - depth).times do
        location = inner.empty? ? path : path + "/" + inner
        entries = if zip
                    HardRPGArchive.entries(inner)
                  else
                    Dir.entries(location).filter_map do |item|
                      next unless HardRPG.safe_relative?(item) && !File.symlink?(location + "/" + item)
                      [item, File.directory?(location + "/" + item), 0]
                    end
                  end
        folders = entries.select { |_, directory, _| directory }.map(&:first)
        asset_folders = %w[Graphics Audio].map { |item| folders.find { |folder| folder.casecmp(item).zero? } }
        if asset_folders.all?
          populated = asset_folders.all? do |item|
            child = inner.empty? ? item : inner + "/" + item
            zip ? !HardRPGArchive.entries(child).empty? : (Dir.entries(path + "/" + child) - %w[. ..]).any?
          end
          return zip ? {"path" => path, "root" => inner} : {"path" => location, "root" => ""} if populated
        end
        aliases = ALIASES.fetch(name, [name])
        if !zip && inner.empty?
          candidates = entries.select do |item, directory, _|
            aliases.any? { |alias_name| item.casecmp(directory ? alias_name : alias_name + '.zip').zero? }
          end
          candidates.each do |item, _directory, _|
            mounted = inspect(location + '/' + item, name, depth + 1)
            return mounted if mounted
          end
          return nil unless candidates.empty?
        end
        chosen = folders.find { |folder| aliases.any? { |alias_name| folder.casecmp(alias_name).zero? } }
        chosen ||= folders.first if folders.size == 1
        unless chosen
          # A chosen collection can contain separately zipped packs.
          candidate = entries.find { |item, directory, _| !directory && aliases.any? { |a| item.casecmp(a + ".zip").zero? } }
          return inspect(location + "/" + candidate.first, name) if candidate && !zip
          return nil
        end
        inner = inner.empty? ? chosen : inner + "/" + chosen
      end
      nil
    rescue IOError, SystemCallError, ArgumentError
      nil
    ensure
      HardRPGArchive.close if zip
    end
    def self.resolve(node)
      choices = paths
      node.rtp_names.map { |name| pack(name) }.uniq.map do |name|
        candidates = if choices[name]
                       [choices[name]]
                     else
                       ALIASES.fetch(name, [name]).flat_map { |item| [RTP_ROOT + "/" + item, RTP_ROOT + "/" + item + ".zip"] }
                     end
        mount = candidates.lazy.map { |path| inspect(path, name) }.find { |item| item }
        raise MissingRtp, name unless mount
        mount
      end
    end
    def self.status(name)
      candidates = paths[name] ? [paths[name]] : ALIASES.fetch(name).flat_map { |item| [RTP_ROOT + '/' + item, RTP_ROOT + '/' + item + '.zip'] }
      mount = candidates.lazy.map { |path| inspect(path, name) }.find { |item| item }
      mount ? 'Detected' : 'Missing'
    end
  end

  class Node
    attr_reader :relative, :archive, :inner, :root
    def initialize(relative = "", archive = false, inner = "", root = GAMES_ROOT)
      raise ArgumentError, "Invalid library path" unless HardRPG.safe_relative?(relative, true)
      raise ArgumentError, "Invalid ZIP path" unless HardRPG.safe_relative?(inner, true)
      raise ArgumentError, "Invalid external folder" unless root == GAMES_ROOT || HardRPG.safe_external?(root)
      @relative, @archive, @inner, @root = relative, archive, inner, root
    end

    def self.from_path(path, archive = false, inner = "")
      raise ArgumentError, "Invalid external path" unless HardRPG.safe_external?(path)
      if path.start_with?(GAMES_ROOT + "/")
        new(path[(GAMES_ROOT.length + 1)..-1], archive, inner)
      elsif archive
        new(File.basename(path), true, inner, File.dirname(path))
      else
        new("", false, "", path)
      end
    end
    def external?; @root != GAMES_ROOT; end
    def path; @root + (@relative.empty? ? "" : "/" + @relative); end
    def label; (external? ? path : "games/" + @relative) + (@archive ? "!/" + @inner : ""); end
    def identity; (external? ? path : @relative) + (@archive ? "!/" + @inner : ""); end
    def config_key; HardRPG.key(identity); end
    def join(name); @inner.empty? ? name : @inner + "/" + name; end

    def files
      if @archive
        HardRPGArchive.open(path)
        HardRPGArchive.entries(@inner)
      else
        Dir.entries(path).reject { |n| n == "." || n == ".." }.filter_map do |name|
          next unless HardRPG.safe_relative?(name)
          file = path + "/" + name
          next if File.symlink?(file)
          [name, File.directory?(file), File.size(file)]
        end
      end
    end

    def read(name)
      if @archive
        HardRPGArchive.open(path)
        HardRPGArchive.read(join(name))
      else
        File.open(path + "/" + name, "rb") { |f| f.read(262_144) }
      end
    end

    def child(name, zip = false)
      if @archive
        Node.new(@relative, true, join(name), @root)
      else
        Node.new(@relative.empty? ? name : @relative + "/" + name, zip, "", @root)
      end
    end

    def game_values
      names = files.map(&:first)
      ini = names.find { |n| n.downcase == "game.ini" }
      return {} unless ini
      values, section = {}, ""
      read(ini).each_line do |line|
        text = line.strip
        next if text.start_with?(";", "#")
        if text =~ /^\[(.+)\]$/
          section = $1.downcase
        elsif section == "game" && text.include?("=")
          key, value = text.split("=", 2)
          values[key.strip.downcase] = value.strip
        end
      end
      values
    end

    def rtp_names
      values = game_values
      %w[rtp rtp1 rtp2 rtp3].filter_map { |key| value = values[key]; value unless !value || value.empty? }
    end

    def game
      names = files.map(&:first)
      return nil unless names.any? { |n| n.downcase == "game.ini" }
      values = game_values
      library = values.fetch("library", "").upcase
      scripts = values.fetch("scripts", "").downcase.tr("\\", "/")
      version = if library.include?("RGSS30") || scripts.end_with?(".rvdata2")
                  3
                elsif library.include?("RGSS20") || scripts.end_with?(".rvdata")
                  2
                elsif library.include?("RGSS10") || scripts.end_with?(".rxdata")
                  1
                end
      version ||= 3 if names.any? { |n| n.downcase.end_with?(".rgss3a") }
      version ||= 2 if names.any? { |n| n.downcase.end_with?(".rgss2a") }
      version ||= 1 if names.any? { |n| n.downcase.end_with?(".rgssad") }
      return nil unless version
      title = values.fetch("title", "").dup.force_encoding("UTF-8").scrub("?")
      title = File.basename(@inner.empty? ? path : @inner) if title.empty?
      Entry.new(title, :game, self, version)
    end

    def entries
      files.filter_map do |name, directory, _size|
        next if name.start_with?(".") || name == "__MACOSX"
        if directory
          node = child(name)
          node.game || Entry.new(name + "/", :folder, node, nil)
        elsif !@archive && name.downcase.end_with?(".zip")
          # Open on selection, rather than parsing every ZIP during list refresh.
          Entry.new(name, :zip, child(name, true), nil)
        end
      end.sort_by { |e| [e.kind == :folder ? 0 : 1, e.name.downcase] }
    end
  end

  class PreparationCancelled < StandardError; end

  # A directory picker only reads folders. Adding a library entry saves a
  # reference; it never copies, moves or rewrites the chosen game's files.
  class FolderBrowser
    attr_reader :path, :entries, :error
    def initialize(include_zips = false)
      @include_zips = include_zips
      @path = BROWSE_ROOTS.find { |root| File.directory?(root) }
      refresh
    end
    def refresh
      @error = nil
      @entries = if @path
        Dir.entries(@path).filter_map do |name|
          next if name.start_with?(".") || !HardRPG.safe_relative?(name)
          path = @path + (@path.end_with?("/") ? "" : "/") + name
          next if File.symlink?(path)
          if File.directory?(path)
            Entry.new(name + "/", :folder, path, nil)
          elsif @include_zips && File.file?(path) && name.downcase.end_with?(".zip")
            Entry.new(name, :zip, path, nil)
          end
        end.sort_by { |entry| entry.name.downcase }
      else
        BROWSE_ROOTS.select { |root| File.directory?(root) }.map { |root| Entry.new(root, :folder, root, nil) }
      end
    rescue SystemCallError => e
      @entries, @error = [], e.message
    end
    def enter(entry); @path = entry.node; refresh; end
    def back
      if @path
        @path = BROWSE_ROOTS.include?(@path) ? nil : File.dirname(@path)
        # Ruby's dirname treats a Vita mount like a normal path.
        @path += "/" if @path && BROWSE_ROOTS.include?(@path + "/")
        refresh
        true
      else
        false
      end
    end
  end

  class Library
    attr_reader :stack, :entries, :error
    def initialize
      @stack = [Node.new]
      refresh
    end
    def current; @stack.last; end
    def refresh
      @error = nil
      @entries = current.entries
      if @stack.size == 1
        known = @entries.map { |entry| entry.node.identity }
        sources = references
        search_folders.each do |path|
          begin
            sources.concat(discover(path))
          rescue IOError, SystemCallError, ArgumentError
            # Keep an unavailable storage path for its next refresh.
          end
        end
        sources.each do |node|
          next if known.include?(node.identity)
          begin
            game = node.game
            @entries << game if game
            known << node.identity
          rescue IOError, SystemCallError, ArgumentError
            # An unavailable memory card keeps its saved reference.
          end
        end
        @entries.sort_by! { |entry| [entry.kind == :folder ? 0 : 1, entry.name.downcase] }
      end
    rescue IOError, SystemCallError, ArgumentError => e
      @error = e.message
      @entries = []
    end

    def references
      return [] unless File.file?(LIBRARY_PATHS)
      raise IOError, "Library index is too large" if File.size(LIBRARY_PATHS) > 524_288
      File.readlines(LIBRARY_PATHS).first(2048).filter_map do |line|
        path, kind, inner = line.chomp.split("\t", -1)
        next unless %w[dir zip].include?(kind) && inner && HardRPG.safe_external?(path)
        begin
          Node.from_path(path, kind == "zip", inner)
        rescue ArgumentError
          nil
        end
      end
    end

    def add_folder(path)
      found = discover(path) { |count, label| yield(count, label) if block_given? }
      raise IOError, "No RPG Maker XP, VX or VX Ace games found" if found.empty?
      existing = references
      keys = existing.map(&:identity)
      added = found.reject { |node| keys.include?(node.identity) }.uniq(&:identity)
      nodes = existing + added
      raise IOError, "Library has more than 2048 added games" if nodes.size > 2048
      data = nodes.map { |node| [node.path, node.archive ? "zip" : "dir", node.inner].join("\t") }.join("\n") + "\n"
      File.open(LIBRARY_PATHS + ".tmp", "wb") { |file| file.write(data) }
      File.rename(LIBRARY_PATHS + ".tmp", LIBRARY_PATHS)
      @stack = [Node.new]
      refresh
      added.size
    end

    def search_folders
      return [] unless File.file?(GAME_FOLDERS)
      raise IOError, "Game folder list is too large" if File.size(GAME_FOLDERS) > 65_536
      File.readlines(GAME_FOLDERS).first(32).map(&:chomp).select { |path| HardRPG.safe_external?(path) }.uniq
    end

    def save_search_folders(paths)
      File.open(GAME_FOLDERS + '.tmp', 'wb') { |f| paths.each { |path| f.write(path + "\n") } }
      File.rename(GAME_FOLDERS + '.tmp', GAME_FOLDERS)
      @stack = [Node.new]
      refresh
    end

    def add_search_folder(path)
      raise IOError, "Choose an existing game folder" unless File.directory?(path) && !File.symlink?(path)
      found = discover(path) { |count, label| yield(count, label) if block_given? }
      paths = (search_folders + [path]).uniq
      raise IOError, "Choose at most 32 extra game folders" if paths.size > 32
      save_search_folders(paths)
      found.size
    end

    def remove_search_folder(path)
      save_search_folders(search_folders - [path])
    end

    def discover(path)
      raise ArgumentError, "Choose a game folder or collection" unless HardRPG.safe_external?(path)
      found, visited, pending = [], 0, [[Node.from_path(path), 0]]
      until pending.empty?
        node, depth = pending.pop
        raise IOError, "Folder scan exceeds 16 levels; choose a closer folder" if depth > 16
        visited += 1
        raise IOError, "Folder scan exceeds 2048 folders; choose a closer folder" if visited > 2048
        yield(visited, node.label) if block_given?
        begin
          game = node.game
          if game
            found << game.node
            next # Never walk a game's graphics/audio/save directories.
          end
          node.entries.reverse_each do |entry|
            if entry.kind == :game
              found << entry.node
            else
              pending << [entry.node, depth + 1]
            end
          end
        rescue IOError, SystemCallError, ArgumentError
          next # A corrupt ZIP or unreadable child does not hide other games.
        end
      end
      found
    ensure
      HardRPGArchive.close if defined?(HardRPGArchive)
    end
    def back
      return false if @stack.size == 1
      @stack.pop
      refresh
      true
    end
    def enter(entry)
      return entry if entry.kind == :game
      # ZIPs can contain a game directly, a wrapper folder, or multiple games.
      direct = entry.node.game if entry.kind == :zip
      return direct if direct
      @stack << entry.node
      refresh
      nil
    end

    def launch(entry, &progress)
      node = entry.node
      rtps = Rtp.resolve(node)
      folder = node.archive ? prepare_zip(node, &progress) : node.external? ? node.path : node.relative
      selection = {"source" => node.archive ? "cache" : node.external? ? "external" : "games", "path" => folder,
                   "config" => node.config_key, "rgss" => entry.version, "rtp" => rtps}
      encoded = HardRPG.json(selection)
      raise IOError, "Launch paths are too long; choose a closer game or RTP folder." if encoded.bytesize > 4096
      File.open(PICK_PATH + ".tmp", "wb") { |f| f.write(encoded) }
      File.rename(PICK_PATH + ".tmp", PICK_PATH)
      selection
    ensure
      HardRPGArchive.close if defined?(HardRPGArchive)
    end

    def collect(node, prefix = "", output = [], depth = 0)
      raise IOError, "ZIP folder nesting exceeds 64 levels" if depth > 64
      node.files.each do |name, directory, size|
        raise IOError, "Unsafe ZIP filename" unless HardRPG.safe_relative?(name)
        next if name == "__MACOSX"
        relative = prefix.empty? ? name : prefix + "/" + name
        if directory
          collect(node.child(name), relative, output, depth + 1)
        else
          raise IOError, "Invalid ZIP file size" if size < 0
          output << [node.join(name), relative, size]
          raise IOError, "ZIP has more than 100000 files" if output.size > 100_000
        end
      end
      output
    end

    def remove_tree(path)
      return unless File.exist?(path) || File.symlink?(path)
      raise IOError, "Refusing to remove a linked or non-directory cache" if File.symlink?(path) || !File.directory?(path)
      Dir.entries(path).each do |name|
        next if name == "." || name == ".."
        item = path + "/" + name
        File.directory?(item) && !File.symlink?(item) ? remove_tree(item) : File.delete(item)
      end
      Dir.rmdir(path)
    end

    def prepare_zip(node)
      key = "zip-" + node.config_key
      destination = CACHE_ROOT + "/" + key
      # Saves live in this persistent working copy. A changed archive must not
      # silently overwrite them; keep the existing copy and report the change.
      fingerprint = HardRPG.json([node.identity, File.size(node.path), File.mtime(node.path).to_i])
      marker = destination + "/.hardrpg-source"
      if File.file?(marker)
        raise IOError, "ZIP changed. Back up saves and remove its cache folder before preparing it again." unless File.read(marker) == fingerprint
        return key
      end
      raise IOError, "Existing ZIP cache is incomplete; move it aside first." if File.exist?(destination)
      stage = destination + ".partial"
      remove_tree(stage)
      HardRPG.mkdir(stage)
      begin
        list = collect(node)
        total = list.sum { |_, _, size| size }
        copied = 0
        yield(copied, total) if block_given?
        list.each do |inner, relative, expected|
          parent = stage
          relative.split("/")[0...-1].each { |part| parent += "/" + part; HardRPG.mkdir(parent) }
          HardRPGArchive.open(node.path)
          actual = HardRPGArchive.stream_open(inner)
          raise IOError, "ZIP file size changed" unless actual == expected
          written = 0
          begin
            File.open(stage + "/" + relative, "wb") do |out|
              loop do
                chunk = HardRPGArchive.stream_read(65_536)
                break if chunk.empty?
                out.write(chunk)
                written += chunk.bytesize
                copied += chunk.bytesize
                yield(copied, total) if block_given?
              end
            end
          ensure
            HardRPGArchive.stream_close
          end
          raise IOError, "ZIP file was truncated" unless written == expected
        end
        # The marker is reserved and never trusted from the ZIP itself.
        File.open(stage + "/.hardrpg-source", "wb") { |f| f.write(fingerprint) }
        File.rename(stage, destination)
      rescue StandardError
        remove_tree(stage)
        raise
      end
      key
    end
  end
end
