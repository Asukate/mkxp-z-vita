#!/usr/bin/env ruby
# Packs launcher/picker.rb into a stub VX Ace project:
#   <out>/Game.ini + <out>/Data/Scripts.rvdata2
# Verifies the marshal+zlib round-trip on host (embedded Ruby 3.1 reads
# marshal 4.8 identically, and the engine inflates entry[2] with zlib).
require "zlib"
require "fileutils"

out = ARGV[0] or abort "usage: build-picker.rb <out-dir>"
ROOT = File.expand_path("..", __dir__)
sources = %w[library.rb picker.rb]
codes = sources.map { |name| File.binread(File.join(ROOT, "launcher", name)) }
entries = sources.each_with_index.map { |name, i| [i + 1, name, Zlib.deflate(codes[i], Zlib::BEST_COMPRESSION)] }
blob = Marshal.dump(entries)

back = Marshal.load(blob)
raise "marshal mismatch" unless back.map { |entry| entry[1] } == sources
raise "zlib mismatch" unless back.map { |entry| Zlib.inflate(entry[2]) } == codes

FileUtils.mkdir_p(File.join(out, "Data"))
File.binwrite(File.join(out, "Data/Scripts.rvdata2"), blob)
File.write(File.join(out, "Game.ini"),
           "[Game]\nTitle=HardRPG\nScripts=Data/Scripts.rvdata2\n" \
           "Library=RGSS301.dll\nRTP=\n")
puts "stub ok: #{blob.size} bytes"
