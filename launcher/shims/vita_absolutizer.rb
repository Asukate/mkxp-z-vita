# Vita relative-path absolutizer (preloaded before game scripts).
# Measured failure: PIF's folder loader does File.open("Data/Scripts/...",
# "r") with relative paths and dies with Errno::EINVAL -- the Vita libc
# passes relative paths to a kernel that only accepts device-absolute
# ones (engine chdir does not propagate). Any game doing relative Ruby
# IO hits the same wall (Essentials saves resolve through "." too).
# This wraps the path-taking Ruby IO entry points used by game code and
# prefixes the game root when the path is relative. Absolute paths
# (ux0:/, app0:/) and non-String arguments (fds) pass through untouched.
# One line per game: set ROOT to the game's launcher-tree folder.
module VitaAbsolutizer
  ROOT = "ux0:/data/hardrpg/games/infinite-fusion"

  def self.absolutize(path)
    return path unless path.is_a?(String)
    return path if path =~ %r{\A[a-zA-Z][a-zA-Z0-9+.-]*:/}
    return path if path.start_with?("/")
    ROOT + "/" + path
  end
end

class << File
  alias_method :vita_abs_open, :open unless method_defined?(:vita_abs_open)
  def open(path, *args, &block)
    vita_abs_open(VitaAbsolutizer.absolutize(path), *args, &block)
  end

  %i[exist? exists? directory? file? readable? writable? size mtime
     delete unlink read binread write binwrite].each do |name|
    next unless respond_to?(name)
    alias_name = :"vita_abs_#{name}"
    next if method_defined?(alias_name)
    alias_method alias_name, name
    define_method(name) do |path, *args, &block|
      send(alias_name, VitaAbsolutizer.absolutize(path), *args, &block)
    end
  end

  %i[rename link symlink].each do |name|
    next unless respond_to?(name)
    alias_name = :"vita_abs_#{name}"
    next if method_defined?(alias_name)
    alias_method alias_name, name
    define_method(name) do |from, to, *args, &block|
      send(alias_name, VitaAbsolutizer.absolutize(from),
           VitaAbsolutizer.absolutize(to), *args, &block)
    end
  end

  alias_method :vita_abs_expand_path, :expand_path unless method_defined?(:vita_abs_expand_path)
  def expand_path(path, *args, &block)
    if path.is_a?(String) && args.empty? &&
       path !~ %r{\A[a-zA-Z][a-zA-Z0-9+.-]*:/} && !path.start_with?("/")
      return VitaAbsolutizer::ROOT + "/" + path
    end
    vita_abs_expand_path(path, *args, &block)
  end
end

class << Dir
  %i[foreach entries glob mkdir rmdir exist? exists? chdir].each do |name|
    next unless respond_to?(name)
    alias_name = :"vita_abs_#{name}"
    next if method_defined?(alias_name)
    alias_method alias_name, name
    define_method(name) do |path, *args, &block|
      send(alias_name, VitaAbsolutizer.absolutize(path), *args, &block)
    end
  end
end

class << FileTest
  %i[exist? exists? directory? file? readable? writable? size].each do |name|
    next unless respond_to?(name)
    alias_name = :"vita_abs_#{name}"
    next if method_defined?(alias_name)
    alias_method alias_name, name
    define_method(name) do |path, *args, &block|
      send(alias_name, VitaAbsolutizer.absolutize(path), *args, &block)
    end
  end
end

if defined?(FileUtils)
  module FileUtils
    class << self
      %i[mkdir_p makedirs rm_rf remove_entry_securely].each do |name|
        next unless respond_to?(name)
        alias_name = :"vita_abs_#{name}"
        next if method_defined?(alias_name)
        alias_method alias_name, name
        define_method(name) do |path, *args, &block|
          fixed = path.is_a?(Array) ? path.map { |p| VitaAbsolutizer.absolutize(p) } : VitaAbsolutizer.absolutize(path)
          send(alias_name, fixed, *args, &block)
        end
      end

      %i[cp copy mv move ln].each do |name|
        next unless respond_to?(name)
        alias_name = :"vita_abs_#{name}"
        next if method_defined?(alias_name)
        alias_method alias_name, name
        define_method(name) do |from, to, *args, &block|
          send(alias_name, VitaAbsolutizer.absolutize(from),
               VitaAbsolutizer.absolutize(to), *args, &block)
        end
      end
    end
  end
end
