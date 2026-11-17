# Check launcher/save_backup.rb on top of launcher/safe_save.rb: a replaced
# save is kept in the backup folder, with its description, and old versions
# are thinned and capped. Like the Vita, File.rename here refuses to replace
# an existing file.
require 'tmpdir'
require 'fileutils'

class << File
  alias_method :host_rename, :rename
  def rename(from, to)
    raise Errno::EEXIST, to if File.exist?(to)
    host_rename(from, to)
  end
end

def check(ok, what)
  raise what unless ok
end

Dir.mktmpdir do |root|
  game = File.join(root, 'game')
  backups = File.join(root, 'save-backups', '0123456789abcdef')
  Dir.mkdir(game)
  $hardrpg_save_backup_dir = backups
  Dir.chdir(game) do
    load File.expand_path('../launcher/safe_save.rb', __dir__)
    load File.expand_path('../launcher/save_backup.rb', __dir__)

    actor = Struct.new(:name, :level)
    party = Struct.new(:members)
    $game_party = party.new([actor.new('Grim', 12)])
    map = Struct.new(:map_id, :display_name)
    $game_map = map.new(7, 'Forest of Sorrows')

    save = lambda do |name, contents, at|
      File.open(name, 'wb') { |f| Marshal.dump(contents, f) }
      File.utime(at, at, name)
      # The description is taken when the save is written; give it the time
      # the test pretends it was written at.
      cur = File.join(backups, File.basename(name), 'current.json')
      File.binwrite(cur, File.binread(cur).sub(/"saved":\d+/, "\"saved\":#{at}")) if File.exist?(cur)
    end
    slot = File.join(backups, 'Save03.rvdata2')
    t = Time.local(2026, 11, 16, 20, 0, 0).to_i

    # A first save: nothing to keep yet, but the slot and its description are.
    save.call('Save03.rvdata2', 'one', t)
    check File.binread(File.join(slot, 'path.txt')) == 'Save03.rvdata2', 'path not recorded'
    check File.binread(File.join(slot, 'current.json')).include?('"leader":"Grim"'), 'no description'
    check HardRPGSaveBackup.versions(slot).empty?, 'kept a version of nothing'

    # The second save keeps the first as a version named by its save time,
    # with the description of the first.
    $game_party = party.new([actor.new('Grim', 13)])
    save.call('Save03.rvdata2', 'two', t + 3600)
    list = HardRPGSaveBackup.versions(slot)
    check list == ['20261116-200000.rvdata2'], "versions #{list}"
    check Marshal.load(File.binread(File.join(slot, list[0]))) == 'one', 'kept the wrong file'
    info = File.binread(File.join(slot, '20261116-200000.json'))
    check info.include?('"why":"save"') && info.include?('"level":12') && info.include?('"map_name":"Forest of Sorrows"'),
          "description #{info}"
    check Marshal.load(File.binread('Save03.rvdata2')) == 'two', 'game save not replaced'
    check Dir.glob('*.hardrpg-*').empty?, 'temp files left'

    # Saves a few minutes apart: the newest is always kept, older ones only
    # ten minutes or more apart.
    base = t + 3600
    (1..6).each { |i| save.call('Save03.rvdata2', "auto #{i}", base + i * 180) }
    times = HardRPGSaveBackup.versions(slot).map { |v| HardRPGSaveBackup.epoch(v) - t }
    check times.first == 3600 + 5 * 180, "newest not kept: #{times}"
    times.each_cons(2).drop(1).each { |a, b| check a - b >= 600, "too close: #{times}" }

    # At most "keep" versions.
    File.binwrite(File.join(backups, 'game.json'), '{"enabled":true,"keep":3}')
    (1..6).each { |i| save.call('Save03.rvdata2', "late #{i}", base + 10_000 + i * 900) }
    check HardRPGSaveBackup.versions(slot).size == 3, "not capped: #{HardRPGSaveBackup.versions(slot)}"

    # The same contents twice are not kept twice.
    newest = HardRPGSaveBackup.versions(slot).first
    File.binwrite('Save03.rvdata2', File.binread(File.join(slot, newest)))
    File.utime(base + 20_000, base + 20_000, 'Save03.rvdata2')
    before = HardRPGSaveBackup.versions(slot)
    save.call('Save03.rvdata2', 'after copy', base + 20_100)
    check HardRPGSaveBackup.versions(slot) == before, 'identical version kept'

    # A restored older version that the game then replaces is not kept again.
    oldest = HardRPGSaveBackup.versions(slot).last
    File.binwrite('Save03.rvdata2', File.binread(File.join(slot, oldest)))
    File.utime(base + 25_000, base + 25_000, 'Save03.rvdata2')
    before = HardRPGSaveBackup.versions(slot)
    save.call('Save03.rvdata2', 'after restore', base + 25_100)
    check HardRPGSaveBackup.versions(slot) == before, 'restored version kept twice'

    # Turned off for the game: the replaced save just goes.
    File.binwrite(File.join(backups, 'game.json'), '{"enabled":false}')
    before = HardRPGSaveBackup.versions(slot)
    save.call('Save03.rvdata2', 'off', base + 30_000)
    check HardRPGSaveBackup.versions(slot) == before, 'kept while off'
    File.binwrite(File.join(backups, 'game.json'), '{"enabled":true}')
    # Switched off in the in-game menu since the launch: the game's settings win.
    Dir.mkdir(File.join(root, 'config'))
    File.binwrite(File.join(root, 'config', '0123456789abcdef.json'), '{"saveBackups":false}')
    save.call('Save03.rvdata2', 'off in game', base + 31_000)
    check HardRPGSaveBackup.versions(slot) == before, 'kept while off in game'
    File.binwrite(File.join(root, 'config', '0123456789abcdef.json'), '{"saveBackups":true}')

    # A save the game names by its whole path inside the game folder (To the
    # Moon's MKXP.data_directory) is the same slot as its plain name.
    File.binwrite(File.join(backups, 'game.json'), %({"enabled":true,"folder":"#{game}"}))
    save.call(File.join(game, 'Save03.rvdata2'), 'by whole path', base + 32_000)
    check HardRPGSaveBackup.versions(slot).size == before.size + 1, 'whole path went elsewhere'
    save.call('./Save09.rvdata2', 'dot slash', base + 33_000)
    check File.binread(File.join(backups, 'Save09.rvdata2', 'path.txt')) == 'Save09.rvdata2', 'dot slash name'

    # Game data written at run time is not a save; a save elsewhere is, under
    # its absolute path; another file of the same name gets its own folder.
    Dir.mkdir('Data')
    File.open('Data/Map001.rxdata', 'wb') { |f| f.write('map') }
    File.open('Data/Map001.rxdata', 'wb') { |f| f.write('map 2') }
    check !File.exist?(File.join(backups, 'Map001.rxdata')), 'game data backed up'
    other = File.join(root, 'appdata')
    Dir.mkdir(other)
    File.open(File.join(other, 'Save03.rvdata2'), 'wb') { |f| f.write('elsewhere') }
    dirs = Dir.children(backups).select { |d| d.start_with?('Save03.rvdata2~') }
    check dirs.size == 1, "no own folder for another Save03: #{Dir.children(backups)}"
    check File.binread(File.join(backups, dirs[0], 'path.txt')) == File.join(other, 'Save03.rvdata2'), 'absolute path'

    # Nothing breaks a save: an unwritable backup folder only drops the copy.
    FileUtils.rm_rf(backups)
    File.binwrite(File.join(root, 'save-backups', 'blocker'), 'x')
    FileUtils.rm_rf(File.join(root, 'save-backups'))
    File.binwrite(File.join(root, 'save-backups'), 'not a folder')
    save.call('Save03.rvdata2', 'still saves', base + 40_000)
    check Marshal.load(File.binread('Save03.rvdata2')) == 'still saves', 'save failed with backups broken'
    check Dir.glob('*.hardrpg-*').empty?, 'old file left when backups failed'
  end
end
puts 'save-backup: ok'
