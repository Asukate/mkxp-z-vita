# Physical-Vita regression: an upload must not leave the destination-copy
# target with odd dimensions when later text blends reuse it.
Graphics.resize_screen(640, 480)
Font.default_size = 23
bitmap = Bitmap.new(456, 456)
bitmap.font.size = 23
bitmap.font.outline = true
sprite = Sprite.new
sprite.bitmap = bitmap
sprite.x = 172
sprite.y = 12

def draw_test_gauges(bitmap)
  [[36, "HP", "538", Color.new(180, 32, 32), Color.new(255, 32, 32)],
   [60, "MP", "100", Color.new(32, 100, 180), Color.new(32, 160, 255)]].each do |y, label, value, first, last|
    bitmap.fill_rect(228, y + 16, 124, 6, Color.new(0, 0, 0))
    bitmap.gradient_fill_rect(228, y + 16, 124, 6, first, last)
    bitmap.font.color.set(Color.new(32, 160, 255))
    bitmap.draw_text(228, y, 30, 24, label)
    bitmap.font.color.set(Color.new(255, 255, 255))
    bitmap.draw_text(260, y, 42, 24, value, 2)
    bitmap.draw_text(300, y, 12, 24, "/", 2)
    bitmap.draw_text(310, y, 42, 24, value, 2)
  end
end

def gauge_pixels(bitmap)
  pixels = []
  (36...84).each do |y|
    (228...354).each do |x|
      color = bitmap.get_pixel(x, y)
      pixels << [color.red, color.green, color.blue, color.alpha]
    end
  end
  pixels
end

draw_test_gauges(bitmap)
expected = gauge_pixels(bitmap)
# The untainted label uses the upload FBO and replaces its exact dimensions.
bitmap.draw_text(10, 390, 436, 30, "WIDEN SHARED COPY TARGET")
20.times { Graphics.update; Input.update }
bitmap.clear
draw_test_gauges(bitmap)
actual = gauge_pixels(bitmap)
differences = expected.zip(actual).count do |before, after|
  before.zip(after).any? { |a, b| (a - b).abs > 2 }
end
result = differences.zero? ? "PASS" : "FAIL"
bitmap.draw_text(10, 360, 436, 30, "#{result}: #{differences} changed pixels")
puts "Bitmap copy resize: #{result}, changed_pixels=#{differences}"
loop { Graphics.update; Input.update }
