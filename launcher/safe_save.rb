# Safe game saves (preload). RPG Maker's own save code opens the save file
# for writing and dumps into it, so the old save is gone the moment the
# write starts. If the write stops part way (the app closed, crashed or lost
# power; an error that is not a StandardError) the slot keeps a cut-off file
# that the game can no longer load (issue 14: a BLACK SOULS II save that ends
# inside the map's events).
#
# Here a game file opened for writing with an RPG Maker data extension goes
# to "<name>.hardrpg-tmp" first and replaces the old file only once it is
# closed after a complete write. An error inside the game's write block
# leaves the old file as it was; the stock save code then deletes the slot
# ("rescue; delete_save_file"), which here would delete the intact old
# save, so that one delete is skipped. The Vita's rename does not replace an
# existing file, so the old file is moved to "<name>.hardrpg-old" for the
# moment between the two renames; a launch finds a slot left in that state
# and puts the old file back.
module HardRPGSafeSave
  EXT = /\.r[xv]data2?\z/i
  TEMP = '.hardrpg-tmp'
  OLD = '.hardrpg-old'

  # Writes the temp file and swaps it in on close.
  module Writer
    def close
      return super if closed? || @hardrpg_save_abandoned
      super
      HardRPGSafeSave.promote(path, @hardrpg_save_target)
    end

    # The game's write raised: keep the old file, drop the partial one.
    def hardrpg_abandon
      @hardrpg_save_abandoned = true
      close unless closed?
      File.delete(path) rescue nil
      HardRPGSafeSave.abandoned = @hardrpg_save_target
    end
  end

  class << self
    # The slot whose last write failed; the game's next delete of it is the
    # stock clean-up of a partial file that was never written there.
    attr_accessor :abandoned

    def keep_after_failed_write?(names)
      return false unless @abandoned
      kept = names.map { |n| n.respond_to?(:to_path) ? n.to_path : n }.include?(@abandoned)
      @abandoned = nil
      kept
    end

    # The file name to protect, or nil for every other open.
    def target(name, mode)
      name = name.to_path if name.respond_to?(:to_path)
      return nil unless name.is_a?(String) && mode.is_a?(String)
      return nil unless mode.include?('w') && !mode.include?('+')
      return nil unless EXT.match?(name)
      name
    end

    def promote(temp, target)
      old = target + OLD
      File.delete(old) if File.exist?(old)
      File.rename(target, old) if File.exist?(target)
      begin
        File.rename(temp, target)
      rescue SystemCallError
        File.rename(old, target) if File.exist?(old) && !File.exist?(target)
        raise
      end
      File.delete(old) rescue nil
    end

    # A slot whose new file never arrived gets its old file back; temp files
    # left by a write that stopped are removed.
    def recover(target)
      old = target + OLD
      if File.exist?(old)
        if File.exist?(target)
          File.delete(old)
        else
          File.rename(old, target)
        end
      end
      temp = target + TEMP
      File.delete(temp) if File.exist?(temp)
    rescue SystemCallError
      nil
    end

    def recover_folder(folder = '.')
      Dir.glob(File.join(folder, '*' + OLD)).each { |f| recover(f[0...-OLD.size]) }
      Dir.glob(File.join(folder, '*' + TEMP)).each { |f| recover(f[0...-TEMP.size]) }
    rescue SystemCallError
      nil
    end
  end
end

class << File
  alias_method :hardrpg_safe_save_open, :open

  def open(name, *rest, &block)
    mode = rest[0]
    mode = mode[:mode] if mode.is_a?(Hash)
    target = HardRPGSafeSave.target(name, mode)
    return hardrpg_safe_save_open(name, *rest, &block) unless target
    HardRPGSafeSave.abandoned = nil
    HardRPGSafeSave.recover(target)
    file = hardrpg_safe_save_open(target + HardRPGSafeSave::TEMP, *rest)
    file.instance_variable_set(:@hardrpg_save_target, target)
    file.extend(HardRPGSafeSave::Writer)
    return file unless block
    begin
      result = yield file
    rescue Exception
      file.hardrpg_abandon
      raise
    end
    file.close unless file.closed?
    result
  end
  ruby2_keywords(:open) if respond_to?(:ruby2_keywords, true)

  alias_method :hardrpg_safe_save_delete, :delete

  def delete(*names)
    return 0 if HardRPGSafeSave.keep_after_failed_write?(names)
    hardrpg_safe_save_delete(*names)
  end
end

HardRPGSafeSave.recover_folder
