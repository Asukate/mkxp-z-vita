# Retain completed Fiber objects and disable GC to expose native resource
# retention independently of Ruby allocation pressure.
GC.disable
retained = []
2000.times do |i|
  fiber = Fiber.new { Fiber.yield(i); i + 1 }
  raise 'yield changed' unless fiber.resume == i
  raise 'resume changed' unless fiber.resume == i + 1
  raise 'completed fiber remains alive' if fiber.alive?
  retained << fiber
end

100.times do
  parent = Fiber.new do
    nested = Fiber.new { Fiber.yield(:nested); :done }
    raise 'nested yield changed' unless nested.resume == :nested
    raise 'nested termination changed' unless nested.resume == :done
    retained << nested
    :parent
  end
  raise 'parent result changed' unless parent.resume == :parent
  retained << parent
  failure = Fiber.new { raise ArgumentError, 'expected fiber exception' }
  begin
    failure.resume
    raise 'exception lost'
  rescue ArgumentError => error
    raise unless error.message == 'expected fiber exception'
  end
  retained << failure
end

retained.each do |fiber|
  begin
    fiber.resume
    raise 'dead fiber accepted'
  rescue FiberError
  end
end
GC.enable
GC.start
retained.clear
GC.start
puts 'Fiber yields, nested resumes, exceptions, dead resumes and GC passed (2300 fibers)'
