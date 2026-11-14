# Run as the Main section of a synthetic RGSS3 game on Vita or Vita3K.
def win32_api(library, name, args)
  Win32API.new(library, name, args, "i")
end

def check(label, expected, actual)
  raise "#{label}: expected #{expected.inspect}, got #{actual.inspect}" unless actual == expected
end

def wait_for_fullscreen(expected)
  60.times do
    return if Graphics.fullscreen == expected
    Graphics.update
  end
  check("fullscreen mode", expected, Graphics.fullscreen)
end

def check_rect(api, hwnd, width, height)
  rect = "\0" * 16
  check("rect result", 1, api.call(hwnd, rect))
  check("rect coordinates", [0, 0, width, height], rect.unpack("l<4"))
end

send_input = win32_api("user32", "SendInput", %w(i p i))
last_error = win32_api("kernel32", "GetLastError", "")
window_rect = win32_api("user32", "GetWindowRect", "ip")
client_rect = win32_api("user32", "GetClientRect", "ip")
hwnd = win32_api("user32", "GetForegroundWindow", "").call
style = win32_api("user32", "GetWindowLongA", "ii")
metrics = win32_api("user32", "GetSystemMetrics", "i")
map_key = win32_api("user32", "MapVirtualKeyA", "ii")

Graphics.resize_screen(640, 480)
Graphics.fullscreen = true
wait_for_fullscreen(true)

# INPUT_KEYBOARD records in the 32-bit layout used by RGSS.
keys = [[0x12, 0], [0x0d, 0], [0x0d, 2], [0x12, 2]]
records = keys.map { |vk, flags| [1, vk, 0, flags, 0, 0, 0, 0].pack("LS2L5") }
check("full batch", 4, send_input.call(4, records.join, 28))
wait_for_fullscreen(false)
check_rect(window_rect, hwnd, 646, 509)
check_rect(client_rect, hwnd, 640, 480)
check("dialog frame", 0x00400000, style.call(hwnd, -16) & 0x00400000)
check("Alt scan code", 0x38, map_key.call(0x12, 0))
check("truncated batch", 0, send_input.call(5, records.join, 28))
check("invalid parameter", 87, last_error.call)
check("unsupported event", 0, send_input.call(1, "\0" * 28, 28))
check("unsupported error", 120, last_error.call)

records.each { |record| check("split batch", 1, send_input.call(1, record, 28)) }
wait_for_fullscreen(true)
check_rect(window_rect, hwnd, metrics.call(78), metrics.call(79))
check("borderless", 0, style.call(hwnd, -16) & 0x00400000)

# Fullscreen++ packs the same shortcut with explicit padding.
fullscreen_plus_plus = keys.flat_map { |vk, flags| [1, vk, flags] }.pack("LSx2Lx16" * 4)
check("Fullscreen++ batch", 4, send_input.call(4, fullscreen_plus_plus, 28))
wait_for_fullscreen(false)

MKXP.puts("Win32 SendInput regression passed")
loop { Graphics.update }
