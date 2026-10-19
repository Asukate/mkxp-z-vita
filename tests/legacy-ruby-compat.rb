# Check launcher/legacy_ruby_compat.rb against the parser it runs behind.
# Each case is shipped-game syntax that Ruby 1.8/1.9 accepted; the test
# raises the real SyntaxError, repairs with the real message, the way the
# engine does, and checks what the repaired section computes.
require_relative '../launcher/legacy_ruby_compat'

if RUBY_VERSION >= '3.4'
  puts "skipped: Ruby #{RUBY_VERSION} parses with Prism; run under Ruby 3.1 (the engine's) to 3.3"
  exit
end

def check(label, expected, actual)
  raise "#{label}: expected #{expected.inspect}, got #{actual.inspect}" unless expected == actual
end

def repair_and_eval(label, source, binding_ = binding)
  10.times do
    begin
      return eval(source, binding_, label)
    rescue SyntaxError => error
      repaired = HardRPGLegacySyntax.repair(source, error.message)
      raise "#{label}: no repair for #{error.message}" unless repaired
      check("#{label} line count", source.count("\n"), repaired.count("\n"))
      source = repaired
    end
  end
  raise "#{label}: did not converge"
end

# PALETTA: MapNode parent lookup.
check('Hash#index', 6, {4 => :a, 6 => :b}.index(:b))
check('Hash#index missing', nil, {4 => :a}.index(:c))

# The Endless Empty {0134}: begin/else with no rescue.
log = []
result = repair_and_eval('{0134}', <<~RUBY, binding)
  def endless_load(log)
    begin
      log << :body
    else
      log << :else
    end
    :done
  end
  endless_load(log)
RUBY
check('else result', :done, result)
check('else order', [:body, :else], log)
raised = begin
  repair_and_eval('{0135}', "begin\n  raise ArgumentError, 'x'\nelse\n  :else\nend\n")
rescue ArgumentError => error
  error.message
end
check('else re-raises', 'x', raised)

# VorlorN Section104: space between method name and argument list.
Color = Struct.new(:r, :g, :b) unless defined?(Color)
colors = repair_and_eval('Section104', <<~RUBY)
  a = Color.new (189, 225, 242)
  b = Color.new (5, 88, 168)
  c = [Color.new (1, 2, 3), (4)]
  [a, b, c]
RUBY
check('spaced call', [Color.new(189, 225, 242), Color.new(5, 88, 168),
                      [Color.new(1, 2, 3), 4]], colors)
check('grouping untouched', 'x = foo (a), b', HardRPGLegacySyntax.join_spaced_call('x = foo (a), b'))
check('quoted paren', 'foo("(", 2)', HardRPGLegacySyntax.join_spaced_call('foo ("(", 2)'))
check('keyword untouched', 'if (a, b)', HardRPGLegacySyntax.join_spaced_call('if (a, b)'))

# Ruby 1.8 `when x:` clauses.
kinds = repair_and_eval('Section010', <<~RUBY)
  [1, 2, 3].map do |n|
    case n
    when 1: :one
    when 2 : :two
    else :many
    end
  end
RUBY
check('when colon', [:one, :two, :many], kinds)

# A section that fails for another reason gets no rewrite.
check('unrelated error', nil, HardRPGLegacySyntax.repair("x = (\n", "{0001}:1: syntax error, unexpected end-of-input"))
puts 'legacy Ruby compatibility checks passed'
