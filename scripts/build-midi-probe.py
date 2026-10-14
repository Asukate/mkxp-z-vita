#!/usr/bin/env python3
"""Generate a small original MIDI/input probe game; no commercial assets."""
from pathlib import Path
import struct
import subprocess
import sys
import zipfile

out = Path(sys.argv[1]).resolve() if len(sys.argv) == 2 else None
if out is None:
    raise SystemExit('usage: build-midi-probe.py OUTPUT_DIRECTORY')
game = out / 'HardRPG MIDI Probe'
(game / 'Audio/BGM').mkdir(parents=True, exist_ok=True)
(game / 'Audio/ME').mkdir(parents=True, exist_ok=True)
(game / 'Data').mkdir(exist_ok=True)

def var(n):
    pieces = [n & 127]
    n >>= 7
    while n:
        pieces.insert(0, (n & 127) | 128)
        n >>= 7
    return bytes(pieces)

def smf(tracks):
    return b'MThd' + struct.pack('>IHHH', 6, int(len(tracks) > 1), len(tracks), 480) + b''.join(
        b'MTrk' + struct.pack('>I', len(t)) + t for t in tracks)

def melody(program=0, drum=False, marker=False, bend=False):
    chan = 9 if drum else 0
    t = bytearray(b'\0\xff\x51\x03\x07\xa1\x20')
    t += bytes([0, 0xc0 | chan, program])
    if marker:
        t += bytes([0, 0xb0 | chan, 111, 0])
    for key in ([36, 38, 42, 38] if drum else [60, 64, 67, 72, 67, 64, 60, 55]):
        if bend:
            t += bytes([0, 0xe0 | chan, 0, 72])
        t += bytes([0, 0x90 | chan, key, 90])
        t += var(240) + bytes([0x80 | chan, key, 0])
        t += var(120) + bytes([0xb0 | chan, 7, 100])
    t += b'\0\xff\x2f\0'
    return bytes(t)

songs = {
    'Piano': smf([melody()]),
    'Strings': smf([melody(48, marker=True)]),
    'Drums': smf([melody(drum=True)]),
    'MultiTrack': smf([melody(0), melody(drum=True)]),
    'PitchBend': smf([melody(80, bend=True)]),
}
for name, data in songs.items():
    (game / 'Audio/BGM' / (name + '.mid')).write_bytes(data)
(game / 'Audio/ME/Chime.mid').write_bytes(smf([melody(10)]))
(game / 'Game.ini').write_text('[Game]\nTitle=HardRPG MIDI Probe\nLibrary=RGSS301.dll\nScripts=Data/Scripts.rvdata2\nRTP=\n')
script = r'''
Graphics.resize_screen(544,416)
Audio.setup_midi if Audio.respond_to?(:setup_midi)
screen=Sprite.new
screen.bitmap=Bitmap.new(544,416)
screen.bitmap.font.size=20
names=%w[Piano Strings Drums MultiTrack PitchBend]
selected=0
playing='Stopped'
volume=70
pitch=100
x,y=260,340
begin
  loop do
    Input.update
    if Input.trigger?(:LEFT);selected=(selected-1)%names.size;end
    if Input.trigger?(:RIGHT);selected=(selected+1)%names.size;end
    if Input.trigger?(:C) || Input.trigger?(:MOUSELEFT)
      Audio.bgm_play('Audio/BGM/'+names[selected],volume,pitch)
      playing=names[selected]
    end
    if Input.trigger?(:B);Audio.bgm_stop;Audio.me_stop;playing='Stopped';end
    if Input.trigger?(:X);Audio.me_play('Audio/ME/Chime',volume,pitch);end
    if Input.trigger?(:L);volume=volume==70 ? 35 : 70;end
    if Input.trigger?(:R);pitch=pitch==100 ? 120 : 100;end
    if Input.trigger?(:A);Audio.bgm_fade(1000);playing='Fading';end
    break if Input.trigger?(:Y)
    x=[[x+(Input.press?(:LEFT) ? -2 : Input.press?(:RIGHT) ? 2 : 0),10].max,520].min
    y=[[y+(Input.press?(:UP) ? -2 : Input.press?(:DOWN) ? 2 : 0),310].max,395].min
    b=screen.bitmap
    b.fill_rect(0,0,544,416,Color.new(25,8,14))
    lines=['HardRPG MIDI / input probe',
      'Selected: '+names[selected], 'Playing: '+playing,
      'Left/Right select | Cross plays | Circle stops',
      'Triangle: ME chime over BGM | Square: fade',
      'L: volume '+volume.to_s+' | R: pitch '+pitch.to_s,
      'Apply L/R settings by starting a track again.',
      'Start: exit | D-pad moves the white square',
      'Mouse click plays; sleep/resume and listen.']
    lines.each_with_index {|line,i| b.draw_text(8,8+i*30,528,28,line)}
    b.fill_rect(x,y,12,12,Color.new(255,255,255))
    Graphics.update
  end
ensure
  Audio.bgm_stop
  Audio.me_stop
  screen.bitmap.dispose
  screen.dispose
end
'''
source = game / 'probe.rb'
source.write_text(script)
subprocess.run(['ruby', '-rzlib', '-e',
    'code=File.binread(ARGV[0]);File.binwrite(ARGV[1],Marshal.dump([[1,"MIDI probe",Zlib.deflate(code)]]))',
    str(source), str(game / 'Data/Scripts.rvdata2')], check=True)
with zipfile.ZipFile(out / 'HardRPG-MIDI-Probe.zip', 'w', zipfile.ZIP_DEFLATED) as z:
    for f in sorted(game.rglob('*')):
        if f.is_file():
            z.write(f, f.relative_to(out))
print(out / 'HardRPG-MIDI-Probe.zip')
