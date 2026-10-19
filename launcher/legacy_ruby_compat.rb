# Legacy Ruby compatibility (preload). RGSS1 shipped Ruby 1.8 and RGSS2/3
# shipped Ruby 1.9; HardRPG runs every game on Ruby 3.1. This file restores
# what shipped games use that Ruby has since removed.

# Hash#index was removed in Ruby 3.0 (it is Hash#key). PALETTA's pathfinding
# script raised NoMethodError when an event's move route searched a path.
class Hash
  def index(value)
    key(value)
  end unless method_defined?(:index)
end

# Syntax that Ruby 1.8/1.9 accepted with a warning and Ruby 3.1 rejects.
# The engine calls repair only after a script section raises SyntaxError,
# before any of that section has run, and evaluates the result in its place,
# so a section that parses is never touched. Each rewrite keeps the line
# count and the 1.8/1.9 meaning, and only touches the lines Ruby reported.
module HardRPGLegacySyntax
  NOT_CALLS = %w[and begin case defined? do else elsif ensure for if in not or
                 rescue return then unless until when while].freeze

  # Returns the repaired source, or nil when nothing reported can be fixed.
  def self.repair(source, message)
    lines = source.split("\n", -1)
    changed = false
    message.scan(/:(\d+): (.*)$/) do |number, text|
      index = number.to_i - 1
      line = lines[index]
      next unless line
      fixed = repair_line(line, text)
      next if fixed.nil? || fixed == line
      lines[index] = fixed
      changed = true
    end
    changed ? lines.join("\n") : nil
  end

  def self.repair_line(line, text)
    if text.include?('else without rescue is useless')
      # 1.9 ran the else body after the begin body; a rescue that re-raises
      # keeps that meaning and makes the clause legal (The Endless Empty).
      line.sub(/(?<![\w.:])else\b/, 'rescue ::Exception; raise; else')
    elsif text.include?("unexpected ':'") && line =~ /\A\s*when\b/
      line.sub(/\A(\s*when\s+[^:'"]*?[^:\s'"])\s*:(\s|\z)/, '\1 then\2') # `when 1: foo`
    elsif text.include?('syntax error')
      join_spaced_call(line) # `Color.new (189, 225, 242)` (VorlorN)
    end
  end

  # Ruby 1.8 read `name (a, b)` as `name(a, b)`. Join only a group holding a
  # top-level comma: `(a, b)` is not an expression, so there is no other reading.
  def self.join_spaced_call(line)
    result = line.dup
    offset = 0
    while (match = /([A-Za-z_]\w*[!?]?)([ \t]+)\(/.match(result, offset))
      open = match.end(0) - 1
      close = matching_paren(result, open)
      break unless close
      if !NOT_CALLS.include?(match[1]) && top_level_comma?(result, open, close)
        result[match.begin(2), match[2].length] = ''
        offset = match.begin(2)
      else
        offset = match.end(0)
      end
    end
    result
  end

  def self.matching_paren(line, open)
    depth = 0
    quote = nil
    index = open
    while index < line.length
      char = line[index]
      if quote
        if char == '\\' then index += 1
        elsif char == quote then quote = nil
        end
      elsif char == '"' || char == "'" then quote = char
      elsif char == '(' then depth += 1
      elsif char == ')'
        depth -= 1
        return index if depth.zero?
      end
      index += 1
    end
    nil
  end

  def self.top_level_comma?(line, open, close)
    depth = 0
    quote = nil
    index = open + 1
    while index < close
      char = line[index]
      if quote
        if char == '\\' then index += 1
        elsif char == quote then quote = nil
        end
      elsif char == '"' || char == "'" then quote = char
      elsif '([{'.include?(char) then depth += 1
      elsif ')]}'.include?(char) then depth -= 1
      elsif char == ',' && depth.zero? then return true
      end
      index += 1
    end
    false
  end
end
