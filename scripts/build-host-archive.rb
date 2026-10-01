#!/usr/bin/env ruby
# Build the same PhysicsFS/Ruby ZIP bridge used by the Vita launcher.
require "rbconfig"
require "fileutils"
require "shellwords"
out = ARGV[0] or abort "usage: build-host-archive.rb OUTPUT.so"
root = File.expand_path("..", __dir__)
config = RbConfig::CONFIG
FileUtils.mkdir_p(File.dirname(out))
flags = IO.popen(%w[pkg-config --cflags --libs physfs], &:read)
abort "Install PhysicsFS development headers/pkg-config" unless $?.success?
args = [ENV.fetch("CXX", "c++"), "-shared", "-fPIC", "-std=c++17",
        "-I#{config['rubyhdrdir']}", "-I#{config['rubyarchhdrdir']}",
        File.join(root, "binding/launcher-filesystem-binding.cpp"), "-o", out]
args.concat(Shellwords.split(flags + " " + config["LIBRUBYARG_SHARED"]))
abort "Host ZIP bridge compilation failed" unless system(*args)
