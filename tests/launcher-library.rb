#!/usr/bin/env ruby
# Integration checks against the production Ruby model and native PhysicsFS bridge.
require "fileutils"
require "json"
module HTTPLite; JSON = ::JSON; end
require "tmpdir"
root = File.expand_path("..", __dir__)
work = ARGV[0] or abort "usage: launcher-library.rb WORK_DIR"
FileUtils.mkdir_p(work)
native = File.join(File.expand_path(work), "hardrpg_archive.so")
abort "Host backend build failed" unless system(RbConfig.ruby, File.join(root, "scripts/build-host-archive.rb"), native)
require native
HARDRPG_DATA_ROOT = Dir.mktmpdir("launcher-test-", File.expand_path(work))
HARDRPG_BROWSE_ROOTS = [File.expand_path(work) + "/"]
load File.join(root, "launcher/library.rb")
def check(value, message); raise message unless value; end
def fixture(path, version, title)
  FileUtils.mkdir_p(path)
  File.write(File.join(path, "Game.ini"), "[Game]\nTitle=#{title}\nLibrary=RGSS#{version}01.dll\n")
  File.write(File.join(path, "payload.bin"), "payload" * 40_000)
end
begin
  HardRPG.setup
  check(File.directory?(HardRPG::CONFIG_ROOT), "config directory missing")
  File.write(HardRPG::ROOT + '/launcher-config.json', JSON.generate({'preloadScript' => ['keep.rb'], 'SESourceCount' => 6}))
  HardRPG::DisplaySettings.change('fixedAspectRatio', false)
  HardRPG::DisplaySettings.change('integerScalingActive', true)
  HardRPG::DisplaySettings.change('smoothScaling', 1)
  display = JSON.parse(File.read(HardRPG::ROOT + '/launcher-config.json'))
  check(display['fixedAspectRatio'] == false && display['integerScalingActive'] == true && display['integerScalingLastMile'] == false, "display settings not wired to renderer options")
  check(display['smoothScaling'] == 1 && display['smoothScalingDown'] == 1, "filtering omitted downscaling")
  check(display['preloadScript'] == ['keep.rb'] && display['SESourceCount'] == 6, "display settings erased unrelated config")
  fixture(File.join(HardRPG::GAMES_ROOT, "Collection", "Game"), 3, "Nested Ace")
  fixture(File.join(HardRPG::GAMES_ROOT, "Other", "Game"), 2, "Nested VX")
  fixture(File.join(HardRPG::GAMES_ROOT, "XP"), 1, "XP Game")
  library = HardRPG::Library.new
  check(library.entries.find { |e| e.name == "XP Game" }.version == 1, "XP detection")
  library.enter(library.entries.find { |e| e.name == "Collection/" })
  game = library.entries.first
  check(game.kind == :game && game.version == 3, "nested game detection")
  selection = library.launch(game)
  check(selection['path'] == "Collection/Game", "relative nested handoff")
  check(JSON.parse(File.read(HardRPG::PICK_PATH)) == selection, "handoff JSON")
  key = game.node.config_key
  check(library.back && !library.back, "parent navigation must stop at root")
  library.enter(library.entries.find { |e| e.name == "Other/" })
  check(key != library.entries.first.node.config_key, "duplicate basenames collide")
  check(library.entries.first.version == 2, "VX detection")
  library.back
  # A declared, absent RTP must be caught before extraction/restart. Settings
  # reference existing packs; neither folders nor wrapped ZIPs are copied.
  rtp_game = File.join(HardRPG::GAMES_ROOT, "Needs RTP")
  fixture(rtp_game, 3, "Needs RTP")
  File.write(File.join(rtp_game, "Game.ini"), "[Game]\nTitle=Needs RTP\nLibrary=RGSS301.dll\nRTP=RPGVXAce\n")
  library.refresh
  needs_rtp = library.entries.find { |e| e.name == "Needs RTP" }
  File.delete(HardRPG::PICK_PATH) if File.exist?(HardRPG::PICK_PATH)
  begin
    library.launch(needs_rtp)
    raise "missing RTP silently restarted the engine"
  rescue HardRPG::MissingRtp => e
    check(e.message.include?("VX Ace") && e.message.include?("Settings"), "unclear missing RTP message")
  end
  check(!File.exist?(HardRPG::PICK_PATH), "missing RTP published a launch handoff")
  pack = File.join(HardRPG::RTP_ROOT, "RPGVXAce")
  FileUtils.mkdir_p(File.join(pack, "Graphics", "System"))
  FileUtils.mkdir_p(File.join(pack, "Audio", "SE"))
  File.write(File.join(pack, "Graphics", "System", "Window.png"), "rtp image")
  File.write(File.join(pack, "Audio", "SE", "Cursor.ogg"), "rtp sound")
  selection = library.launch(needs_rtp)
  check(selection['rtp'] == [{'path' => pack, 'root' => ''}], "default RTP folder name mismatch")
  collection = Dir.mktmpdir("existing-rtp-", File.expand_path(work))
  moved_pack = File.join(collection, "RPGVXAce")
  FileUtils.mv(pack, moved_pack)
  HardRPG::Rtp.set("RPGVXAce", collection)
  check(HardRPG::Rtp.paths['RPGVXAce'] == collection, "custom RTP reference not persisted")
  check(library.launch(needs_rtp)['rtp'].first['path'] == moved_pack, "external RTP collection not resolved")
  rtp_zip = File.join(collection, "VXACE.zip")
  check(system("python3", "-c", <<~PY, rtp_zip), "RTP ZIP fixture creation")
    import zipfile, sys
    with zipfile.ZipFile(sys.argv[1], 'w', zipfile.ZIP_DEFLATED) as z:
      z.writestr('RPGVXAce/Graphics/System/Window.png', b'rtp image')
      z.writestr('RPGVXAce/Audio/SE/Cursor.ogg', b'rtp sound')
  PY
  caches_before_rtp = Dir.entries(HardRPG::CACHE_ROOT).sort
  original_rtp_zip = File.binread(rtp_zip)
  HardRPG::Rtp.set("RPGVXAce", rtp_zip)
  mount = library.launch(needs_rtp)['rtp'].first
  check(mount == {'path' => rtp_zip, 'root' => 'RPGVXAce'}, "wrapped RTP ZIP handoff")
  check(File.binread(rtp_zip) == original_rtp_zip && Dir.entries(HardRPG::CACHE_ROOT).sort == caches_before_rtp, "RTP ZIP copied/extracted/changed")
  HardRPGArchive.open(rtp_zip)
  HardRPGArchive.set_root(mount['root'])
  check(HardRPGArchive.read('Graphics/System/Window.png') == 'rtp image', "native RTP wrapper root did not expose assets")
  HardRPGArchive.close
  browser_with_zips = HardRPG::FolderBrowser.new(true)
  browser_with_zips.enter(HardRPG::Entry.new('RTP/', :folder, collection, nil))
  check(browser_with_zips.entries.any? { |e| e.kind == :zip && e.node == rtp_zip }, "RTP picker cannot select ZIP")
  HardRPG::Rtp.set("RPGVXAce", nil)
  check(!HardRPG::Rtp.paths.key?('RPGVXAce') && File.exist?(rtp_zip), "reset RTP reference deleted pack")
  FileUtils.mv(moved_pack, pack)
  FileUtils.remove_entry(collection)
  zip = File.join(HardRPG::GAMES_ROOT, "wrapped.zip")
  python = <<~PY
    import zipfile, sys
    with zipfile.ZipFile(sys.argv[1], 'w', zipfile.ZIP_DEFLATED) as z:
      z.writestr('Wrapper/Game.ini', '[Game]\\nTitle=ZIP Ace\\nLibrary=RGSS301.dll\\nRTP=RPGVXAce\\n')
      z.writestr('Wrapper/Data/sample.bin', b'archive data' * 30000)
  PY
  check(system("python3", "-c", python, zip), "ZIP fixture creation")
  library.refresh
  entry = library.entries.find { |e| e.kind == :zip }
  check(library.enter(entry).nil?, "wrapper ZIP should open a folder")
  zipped = library.entries.find { |e| e.kind == :game }
  check(zipped.name == "ZIP Ace", "ZIP wrapper game detection")
  cache_before_missing = Dir.entries(HardRPG::CACHE_ROOT).sort
  FileUtils.mv(pack, pack + '.unavailable')
  File.delete(HardRPG::PICK_PATH) if File.exist?(HardRPG::PICK_PATH)
  begin
    library.launch(zipped)
    raise "ZIP game with missing RTP was extracted/launched"
  rescue HardRPG::MissingRtp
  end
  check(Dir.entries(HardRPG::CACHE_ROOT).sort == cache_before_missing && !File.exist?(HardRPG::PICK_PATH), "missing RTP created a ZIP cache or handoff")
  FileUtils.mv(pack + '.unavailable', pack)
  progress = []
  selected = library.launch(zipped) { |done, total| progress << [done, total] }
  check(selected['source'] == 'cache', "ZIP cache handoff")
  cache = File.join(HardRPG::CACHE_ROOT, selected['path'])
  check(File.binread(File.join(cache, "Data/sample.bin")) == "archive data" * 30000, "deflated ZIP extraction")
  check(progress.last[0] == progress.last[1], "ZIP progress did not complete")
  File.write(File.join(cache, "Save01.rvdata2"), "keep save")
  library.launch(zipped)
  check(File.read(File.join(cache, "Save01.rvdata2")) == "keep save", "cache relaunch lost save")
  HardRPGArchive.open(zip)
  begin
    HardRPGArchive.read("../escape")
    raise "native ZIP traversal accepted"
  rescue ArgumentError
  end
  check(!HardRPG.safe_relative?("a/../b") && !HardRPG.safe_relative?("/absolute"), "Ruby traversal accepted")
  external_root = Dir.mktmpdir("external-games-", File.expand_path(work))
  external = File.join(external_root, "Existing", "Game")
  fixture(external, 3, "External Ace")
  save = File.join(external, "Save01.rvdata2")
  File.write(save, "original save")
  source_data = File.binread(File.join(external, "payload.bin"))
  search_library = HardRPG::Library.new
  search_library.add_search_folder(external_root)
  fixture(File.join(external_root, 'Later Game'), 2, 'Added later')
  search_library.refresh
  check(search_library.entries.any? { |e| e.name == 'Added later' }, "extra game folder was not searched on refresh")
  check(HardRPG::Library.new.entries.any? { |e| e.name == 'Added later' }, "extra game folder not persisted")
  search_library.remove_search_folder(external_root)
  check(!search_library.entries.any? { |e| e.name == 'Added later' } && File.exist?(save), "removing search path erased data or retained the scan")
  FileUtils.remove_entry(File.join(external_root, 'Later Game'))
  external_zip = File.join(external_root, "Existing Archive.zip")
  FileUtils.cp(zip, external_zip)
  original_zip = File.binread(external_zip)
  caches_before = Dir.entries(HardRPG::CACHE_ROOT).sort
  separate = HardRPG::Library.new
  check(separate.add_folder(external_root) == 2, "external collection and ZIP scan")
  check(separate.add_folder(external_root) == 0, "external duplicate added twice")
  reloaded = HardRPG::Library.new
  linked = reloaded.entries.find { |e| e.name == "External Ace" }
  check(linked && linked.node.path == external, "persisted external reference missing")
  linked_zip = reloaded.entries.find { |e| e.name == "ZIP Ace" }
  check(linked_zip && linked_zip.node.archive && linked_zip.node.path == external_zip, "persisted external ZIP reference missing")
  check(File.binread(external_zip) == original_zip && Dir.entries(HardRPG::CACHE_ROOT).sort == caches_before, "adding an archive extracted or changed it")
  handoff = reloaded.launch(linked)
  check(handoff['source'] == 'external' && handoff['path'] == external, "external native handoff")
  check(File.read(save) == "original save" && File.binread(File.join(external, "payload.bin")) == source_data, "external scan changed game/save files")
  check(!File.exist?(File.join(HardRPG::GAMES_ROOT, "External Ace")), "external game was copied")
  index = File.read(HardRPG::LIBRARY_PATHS)
  begin
    reloaded.add_folder(external_root) { raise HardRPG::PreparationCancelled }
    raise "folder scan cancellation ignored"
  rescue HardRPG::PreparationCancelled
  end
  check(File.read(HardRPG::LIBRARY_PATHS) == index, "cancelled scan changed references")
  check(!HardRPG.safe_external?(external_root + "/../escape"), "external traversal accepted")
  browser = HardRPG::FolderBrowser.new
  check(browser.path == HARDRPG_BROWSE_ROOTS.first, "browser did not start at storage root")
  folder = browser.entries.find { |entry| entry.node == external_root }
  browser.enter(folder)
  check(browser.back && browser.path == HARDRPG_BROWSE_ROOTS.first, "browser parent navigation")
  check(browser.back && browser.path.nil? && !browser.back, "browser escaped storage picker")
  FileUtils.rm_rf(external_root)
  reloaded.refresh
  check(!reloaded.entries.any? { |e| e.name == "External Ace" }, "missing external game should not launch")
  check(File.read(HardRPG::LIBRARY_PATHS) == index, "missing external game erased its reference")
  escaped = "quoted \"name\"\\tab\t\n"
  check(JSON.parse(HardRPG.json(escaped)) == escaped, "handoff escaping")
  File.utime(Time.now + 10, Time.now + 10, zip)
  begin
    library.launch(zipped)
    raise "changed ZIP overwrote cache"
  rescue IOError => e
    check(e.message.include?("ZIP changed"), "unexpected changed ZIP error")
  end
  check(File.read(File.join(cache, "Save01.rvdata2")) == "keep save", "changed ZIP lost save")
  library.remove_tree(cache)
  begin
    library.launch(zipped) { raise HardRPG::PreparationCancelled }
    raise "ZIP cancellation ignored"
  rescue HardRPG::PreparationCancelled
  end
  check(!File.exist?(cache) && !File.exist?(cache + '.partial'), "cancelled ZIP published a partial game")
  File.write(File.join(HardRPG::GAMES_ROOT, "broken.zip"), "not a ZIP")
  library.back
  library.refresh
  begin
    library.enter(library.entries.find { |e| e.name == "broken.zip" })
    raise "corrupt ZIP accepted"
  rescue IOError
  end
  puts "PASS: display settings/preserved config, persistent game search folders, missing/default/custom/wrapped-ZIP RTP/no-copy, nested/external games, ZIP preparation/cache/saves/cancellation/traversal"
ensure
  HardRPGArchive.close
  FileUtils.remove_entry(HARDRPG_DATA_ROOT)
  FileUtils.remove_entry(external_root) if defined?(external_root) && external_root && File.directory?(external_root)
  FileUtils.remove_entry(collection) if defined?(collection) && collection && File.directory?(collection)
end
