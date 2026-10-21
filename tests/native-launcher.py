#!/usr/bin/env python3
"""Integration tests of the production native launcher model and input state."""
import hashlib
import json
import os
import re
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
import unittest
import zipfile

ROOT = Path(__file__).resolve().parent.parent
BINARY = Path(sys.argv.pop(1)).resolve() if len(sys.argv) > 1 else None
WORK = Path(sys.argv.pop(1)).resolve() if len(sys.argv) > 1 else None
if not BINARY or not WORK:
    raise SystemExit('usage: native-launcher.py HOST_BINARY WORK_DIR')
WORK.mkdir(parents=True, exist_ok=True)


def fnv(value):
    v = 14695981039346656037
    for c in value.encode():
        v = ((v ^ c) * 1099511628211) & ((1 << 64) - 1)
    return f'{v:016x}'


# The release this build calls itself, as release.h says.
_release = (ROOT/'src/native_launcher/release.h').read_text()
VERSION = re.search(r'#define HARDRPG_VERSION "([^"]+)"', _release).group(1)
VERSION_TAG = re.search(r'#define HARDRPG_VERSION_TAG "([^"]+)"', _release).group(1)


def next_tag(tag):
    """The tag one patch release after tag (v0.3.1-alpha -> v0.3.2-alpha)."""
    major, minor, patch, rest = re.match(r'v(\d+)\.(\d+)\.(\d+)(.*)', tag).groups()
    return f'v{major}.{minor}.{int(patch) + 1}{rest}'


# Key routes on the home screen, which opens on the first game.
SETTINGS = 'up,up,right,cross'          # toolbar, top bar, the gear
ABOUT = 'up,up,cross'                   # toolbar, top bar, About
GAME = 'triangle,down,down,down,cross'  # options menu, Game settings
RENAME = 'triangle,up,cross'            # options menu, Rename (its last row)
PLAY = 'cross,cross'                    # first Cross shows details, the second plays


class NativeLauncher(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.rtp_probe = WORK / 'native-rtp-cache'
        import shlex
        flags = shlex.split(subprocess.check_output(
            ['pkg-config', '--cflags', '--libs', 'physfs'], text=True))
        subprocess.check_call([os.environ.get('CXX', 'g++'), '-std=c++14', '-O2',
            '-I'+str(ROOT/'src'), str(ROOT/'tests/native-rtp-cache.cpp'),
            str(ROOT/'src/native_launcher/model.cpp'), str(ROOT/'src/native_launcher/i18n.cpp'), '-Wl,--wrap=PHYSFS_mount',
            *flags, '-o', str(cls.rtp_probe)])

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='native-test-', dir=WORK)
        self.storage = Path(self.temp.name)
        self.root = self.storage / 'hardrpg'
        self.call([])
        self.games = self.root / 'games'

    def tearDown(self):
        self.temp.cleanup()

    def run_binary(self, *args):
        output = subprocess.check_output([str(BINARY), '--root', str(self.root),
            '--storage', str(self.storage) + '/', '--font', str(ROOT / 'assets/liberation.ttf'),
            *map(str, args)], text=True)
        return json.loads(output)

    def call(self, commands):
        script = self.storage / 'commands.json'
        script.write_text(json.dumps(commands))
        return self.run_binary('--commands', script)

    def cmd(self, op, path=None, **kwargs):
        c = {'op': op, **kwargs}
        if path is not None:
            c['path'] = str(path)
        return self.call([c])[0]

    def game(self, path, version=3, title='Fixture', rtp=''):
        path.mkdir(parents=True, exist_ok=True)
        (path / 'Game.ini').write_text(f'[Game]\nTitle={title}\nLibrary=RGSS{version}01.dll\nRTP={rtp}\n')
        (path / 'payload.bin').write_bytes(b'data' * 70000)
        return path

    def zip(self, path, inner='Wrapper', rtp='', version=3):
        path.parent.mkdir(parents=True, exist_ok=True)
        prefix = inner + '/' if inner else ''
        with zipfile.ZipFile(path, 'w', zipfile.ZIP_DEFLATED) as z:
            z.writestr(prefix + 'Game.ini', f'[Game]\nTitle=ZIP game\nLibrary=RGSS{version}01.dll\nRTP={rtp}\n')
            z.writestr(prefix + 'Data/test.bin', b'archive data' * 30000)
        return path

    def pack(self, path):
        (path / 'Graphics/System').mkdir(parents=True)
        (path / 'Audio/SE').mkdir(parents=True)
        (path / 'Graphics/System/Window.png').write_bytes(b'image')
        (path / 'Audio/SE/Cursor.ogg').write_bytes(b'sound')
        return path

    def ui(self, actions='', snapshot=None):
        args = ['--actions', actions]
        if snapshot:
            args.extend(['--snapshot', snapshot])
        return self.run_binary(*args)

    def to(self, name):
        """Keys moving home's focus to the named game."""
        return 'right,' * [e['name'] for e in self.ui()['homeEntries']].index(name)

    def lcf_game(self, path, title='LCF game'):
        path.mkdir(parents=True, exist_ok=True)
        (path/'RPG_RT.ldb').write_bytes(b'\x0bLcfDataBase\x00')
        (path/'RPG_RT.lmt').write_bytes(b'\x0aLcfMapTree\x00')
        (path/'RPG_RT.ini').write_text('[RPG_RT]\nGameTitle='+title+'\n')
        return path

    def test_lcf_games_share_library_and_launch_directly(self):
        self.game(self.games/'Ace',3,'Ace')
        old=self.lcf_game(self.games/'2003','Older game')
        refused=self.cmd('launch',old,runtime='missing')
        self.assertIn('runtime is missing',refused['error'])
        self.assertFalse((self.root/'selection.json').exists())
        listed=self.cmd('list')
        self.assertEqual([x['name'] for x in listed],['Ace','Older game'])
        self.assertEqual(listed[1]['backend'],'easyrpg')
        # From the carousel and from its game settings: the same EasyRPG handoff.
        # (Playing moves the game to the front, so find it again each time.)
        for route in [PLAY, GAME+',scroll:7,cross']:
            with self.subTest(route=route):
                state=self.ui(self.to('Older game')+route)
                self.assertTrue(state['launchRequested'])
                self.assertEqual(state['launchFormat'],4)
                selection=json.loads((self.root/'selection.json').read_text())
                self.assertEqual(selection['backend'],'easyrpg')
                self.assertNotIn('execName',selection)
                self.assertEqual(selection['project'],str(old))
                self.assertEqual(selection['save'],str(self.root/'saves/easyrpg'/fnv('2003')))
                self.assertEqual(selection['rtp'],[])
                self.assertNotIn('rgss',selection)
        # The fifth RTP row (2003) in its game settings, tapped once: focused, not opened.
        state=self.ui(self.to('Older game')+GAME+',scroll:4,cross,touch:220:380')
        self.assertEqual(state['rtpSelected'],4)
        self.assertFalse(state['browserOpen'])
        # Engine tabs: All, VX Ace, 2000/2003, then back to All.
        state=self.ui('r,r')
        self.assertEqual(state['engineFilter'],4)
        self.assertEqual([x['name'] for x in state['homeEntries']],['Older game'])
        self.assertEqual(len(self.ui('r,r,r')['homeEntries']),2)
        (old/'RPG_RT.lmt').unlink()
        self.assertIn('error',self.cmd('game',old))
    def test_loadexec_failure_clears_opening_overlay_and_allows_new_launch(self):
        self.lcf_game(self.games/'A', 'First')
        self.game(self.games/'B', 3, 'Second')
        failed = self.ui(PLAY+',loadexec-fail')
        self.assertFalse(failed['launchRequested'])
        self.assertEqual(failed['launchFormat'], 0)
        self.assertEqual(failed['launchingName'], '')
        self.assertEqual(failed['preparationLabel'], '')
        self.assertIn('0x8080201e', failed['message'])
        self.assertIn('app0:/easyrpg.self', failed['message'])
        self.assertTrue(failed['homeActive'])
        moved = self.ui(PLAY+',loadexec-fail,right')
        self.assertFalse(moved['launchRequested'])
        self.assertEqual(moved['launchingName'], '')
        again = self.ui(PLAY+',loadexec-fail,right,'+PLAY)
        self.assertTrue(again['launchRequested'])
        self.assertEqual(again['launchFormat'], 3)
        self.assertEqual(again['launchingName'], 'Second')
    def test_lcf_zip_external_save_identity_and_rtp(self):
        external=self.lcf_game(self.storage/'Outside game','External LCF')
        (external/'Save01.lsd').write_bytes(b'existing save')
        selection=self.cmd('launch',external)
        self.assertEqual(selection['source'],'external')
        self.assertEqual(selection['project'],str(external))
        self.assertEqual((external/'Save01.lsd').read_bytes(),b'existing save')
        saved=Path(selection['save'])/'Save01.lsd'
        self.assertEqual(saved.read_bytes(),b'existing save')
        saved.write_bytes(b'new progress')
        self.cmd('launch',external)
        self.assertEqual(saved.read_bytes(),b'new progress')
        archive=self.games/'Old.zip'
        with zipfile.ZipFile(archive,'w') as z:
            z.writestr('Wrapper/rPg_Rt.LDB',b'database')
            z.writestr('Wrapper/RPG_RT.lmt',b'tree')
            z.writestr('Wrapper/Save01.lsd',b'original save')
        self.assertEqual(self.cmd('list')[0]['backend'],'easyrpg')
        selection=self.cmd('launch',archive,kind='zip',inner='Wrapper')
        self.assertNotIn('execName',selection)
        cached=Path(selection['project'])
        self.assertEqual((cached/'Save01.lsd').read_bytes(),b'original save')
        self.assertEqual(selection['save'],str(self.root/'saves/easyrpg'/fnv('Old.zip!/Wrapper')))
        self.assertEqual(self.cmd('launch',archive,kind='zip',inner='Wrapper')['save'],selection['save'])
        pack=self.storage/'RTP2003'
        (pack/'System').mkdir(parents=True);(pack/'Music').mkdir()
        (pack/'System/Window.png').write_bytes(b'image')
        (pack/'Music/Title.mid').write_bytes(b'midi')
        self.assertTrue(self.cmd('set-rtp',pack,pack='2003'))
        self.assertEqual(self.cmd('launch',external)['rtp'],[dict(engine='2003',path=str(pack))])
        rtpzip=self.storage/'rtp2000.zip'
        with zipfile.ZipFile(rtpzip,'w') as z:
            z.writestr('Pack/System/Window.png',b'image')
            z.writestr('Pack/Music/Title.mid',b'midi')
        self.assertTrue(self.cmd('set-rtp',rtpzip,pack='2000'))
        picks=self.cmd('launch',external)['rtp']
        self.assertEqual(len(picks),2)
        self.assertTrue((Path(picks[0]['path'])/'System/Window.png').exists())

    def test_easyrpg_handoff_validates_and_preserves_paths(self):
        probe=WORK/'easyrpg-handoff'
        subprocess.check_call([os.environ.get('CXX','g++'),'-std=c++14',
            '-I'+str(ROOT/'src'),str(ROOT/'tests/easyrpg-handoff.cpp'),'-o',str(probe)])
        game=self.lcf_game(self.games/'LCF space & Unicode ñ')
        pick=self.cmd('launch',game)
        request=self.root/'selection.json'
        def check(value):
            request.write_text(json.dumps(value))
            return subprocess.run([str(probe),str(request),str(self.root),str(self.storage)+'/'],
                capture_output=True,text=True)
        result=check(pick)
        self.assertEqual(result.returncode,0,result.stderr)
        args=json.loads(result.stdout)
        self.assertEqual(args[2],str(game));self.assertEqual(args[4],pick['save'])
        custom = str(self.storage/'RTP; space ñ')
        both = [dict(engine='2000',path=custom), dict(engine='2003',path=str(self.storage/'2003'))]
        result=check({**pick,'rtp':both})
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertEqual(json.loads(result.stdout)[5:],[
            '--hardrpg-rtp-2000',custom,'--hardrpg-rtp-2003',str(self.storage/'2003')])
        for invalid in [[dict(engine='XP',path=custom)],
                [dict(engine='2000',path='app0:/bad')], [dict(engine='2000')],
                [both[0],both[0]]]:
            self.assertNotEqual(check({**pick,'rtp':invalid}).returncode,0)
        for update in [dict(backend='mkxp-z'),dict(schema=2),dict(config='../bad'),
                dict(project=str(self.storage/'../escape')),dict(save=str(game)),
                dict(source='unknown'),dict(path='../escape'),dict(rtp=['app0:/data']),
                dict(rtp=['a','b','c'])]:
            self.assertNotEqual(check({**pick,**update}).returncode,0)
        request.write_text('x'*4097)
        self.assertNotEqual(subprocess.run([str(probe),str(request),str(self.root),
            str(self.storage)+'/'],capture_output=True).returncode,0)

    def test_touch_scroll_selection_and_filter_boundaries(self):
        for i in range(24): self.game(self.games/f'{i:02}',title=f'Game {i:02}')
        # Swiping the carousel moves the focus without launching.
        state=self.ui('drag:704:360:284')
        self.assertGreater(state['homeTarget'],0)
        self.assertFalse(state['launchRequested'])
        # The card right of the focused one: a tap focuses it, the next opens
        # its details, and Play in the details launches it.
        state=self.ui('touch:704:284')
        self.assertEqual(state['homeTarget'],1)
        self.assertFalse(state['homeDetails'])
        state=self.ui('touch:704:284,touch:704:284')
        self.assertTrue(state['homeDetails'])
        self.assertFalse(state['launchRequested'])
        state=self.ui('touch:704:284,touch:704:284,touch:400:460')
        self.assertTrue(state['launchRequested'])
        self.assertEqual(state['launchingName'],'Game 01')
        # The Add games card ends the shelf; Left stops at the first game.
        self.assertEqual(self.ui(','.join(['right']*30))['homeTarget'],24)
        self.assertEqual(self.ui(','.join(['right']*30+['left']*30))['homeTarget'],0)
        state=self.ui(','.join(['right']*30)+',cross')
        self.assertFalse(state['launchRequested'])
        self.assertTrue(state['browserOpen'])
        # The grid from the toolbar's view button: Down moves a whole row.
        state=self.ui('up,left,left,left,cross,down,down,down,right')
        self.assertTrue(state['homeGrid'])
        self.assertEqual(state['homeTarget'],11)
        self.assertTrue(self.ui('touch:39:91')['homeGrid'])
        # Filter panel: favourites only, with no favourites, shows nothing.
        state=self.ui('up,cross,down,down,down,down,cross')
        self.assertEqual(state['homePanel'],1)
        self.assertEqual(state['homeEntries'],[])
        state=self.ui('up,cross,down,down,down,down,cross,circle,down,cross')
        self.assertEqual(state['homePanel'],0)
        self.assertFalse(state['launchRequested'])
        # Unplayed only keeps every game; the last row resets the filters.
        self.assertEqual(len(self.ui('up,cross,up,up,up,up,cross')['homeEntries']),24)
        state=self.ui('up,cross,down,down,down,down,cross,up,up,up,up,up,cross')
        self.assertEqual(len(state['homeEntries']),24)
    def test_play_time_favourites_and_sorting(self):
        for name in ['Alpha','Bravo','Charlie']:
            self.game(self.games/name,title=name)
        keys={e['name']:e['config'] for e in self.cmd('list')}
        activity=self.root/'activity.txt'
        activity.write_text(f"{keys['Alpha']} 100 50\n{keys['Bravo']} 300 10\n")
        names=lambda state: [e['name'] for e in state['homeEntries']]
        # Last played first, then the filter panel's sorts (rows 6 to 8); the
        # same sort again turns the order around.
        self.assertEqual(names(self.ui()),['Bravo','Alpha','Charlie'])
        sort=lambda row: 'up,cross'+',down'*row+',cross'
        self.assertEqual(names(self.ui(sort(7))),['Alpha','Bravo','Charlie'])
        self.assertEqual(names(self.ui(sort(7)+',cross')),['Charlie','Bravo','Alpha'])
        self.assertEqual(names(self.ui(sort(8))),['Alpha','Bravo','Charlie'])
        self.assertEqual(names(self.ui(sort(8)+',cross')),['Charlie','Bravo','Alpha'])
        # The options menu's first row stars the focused game; the filter keeps only stars.
        self.ui('triangle,cross')
        self.assertEqual((self.root/'favourite-games.txt').read_text().split(),[keys['Bravo']])
        self.assertEqual(names(self.ui('up,cross,down,down,down,down,cross')),['Bravo'])
        self.ui('triangle,cross')
        self.assertEqual((self.root/'favourite-games.txt').read_text().split(),[])
        # Playing records the start; returning from the game adds the time
        # played, a fresh boot (the game closed from the Vita's home) does not.
        state=self.ui(PLAY)
        self.assertTrue(state['launchRequested'])
        self.assertIn('running '+keys['Bravo'],activity.read_text())
        played=lambda: int(next(l for l in activity.read_text().splitlines() if l.startswith(keys['Bravo'])).split()[2])
        started=int(time.time())-120
        activity.write_text(f"{keys['Bravo']} {started} 10\nrunning {keys['Bravo']} {started}\n")
        self.run_binary('--returning','yes','--actions','')
        self.assertGreaterEqual(played(),130)
        self.assertNotIn('running',activity.read_text())
        activity.write_text(f"{keys['Bravo']} {started} 10\nrunning {keys['Bravo']} {started}\n")
        self.run_binary('--returning','no','--actions','')
        self.assertEqual(played(),10)
        self.assertNotIn('running',activity.read_text())

    def test_language_setting_switches_at_once_and_is_kept(self):
        self.game(self.games/'Ace', 3, 'Ace')
        prefs = self.root/'launcher-preferences.json'
        language = SETTINGS+',down'*7    # Settings: Language is row 7
        self.assertEqual(self.ui(language)['language'], 'en')
        # Right (or Cross) moves to the next language and saves it at once.
        state = self.ui(language+',right')
        self.assertEqual(state['language'], 'es')
        self.assertEqual(json.loads(prefs.read_text())['language'], 'es')
        # The next start is in Spanish, and messages are made in it.
        self.assertEqual(self.ui()['language'], 'es')
        state = self.ui(SETTINGS+',cross,cross')
        self.assertEqual(state['message'], 'Ajustes de pantalla guardados para el próximo juego.')
        self.assertEqual(self.ui(language+',cross')['language'], 'pt-BR')
        state = self.ui(SETTINGS+',cross,cross')
        self.assertEqual(state['message'], 'Ajustes de tela salvos para o próximo jogo.')
        # Left goes back; Portuguese wraps round to English.
        self.assertEqual(self.ui(language+',left')['language'], 'es')
        self.assertEqual(self.ui(language+',right,right')['language'], 'en')
        self.assertEqual(json.loads(prefs.read_text())['language'], 'en')
        state = self.ui(SETTINGS+',cross,cross')
        self.assertEqual(state['message'], 'Display settings saved for the next game launch.')
        # A damaged or unknown language falls back to English.
        prefs.write_text('{"language": "xx"}')
        self.assertEqual(self.ui()['language'], 'en')

    def test_english_fallback_and_complete_translations(self):
        tr = lambda lang, text, **kw: self.cmd('tr', language=lang, text=text, **kw)
        self.assertEqual(tr('es', 'Settings'), 'Ajustes')
        self.assertEqual(tr('pt-BR', 'Settings'), 'Ajustes')
        self.assertEqual(tr('pt-BR', 'Add games'), 'Adicionar jogos')
        self.assertEqual(tr('es', 'Starting {}', args=['Ace']), 'Iniciando Ace')
        # Text with no translation (game names, paths, anything new) stays English.
        self.assertEqual(tr('es', 'A text nobody translated'), 'A text nobody translated')
        self.assertEqual(tr('pt-BR', 'Fixture game'), 'Fixture game')
        self.assertEqual(tr('en', 'Settings'), 'Settings')
        # Every row has both translations, keeps its placeholders, and every
        # character exists in the fonts the launcher ships.
        fonts = [ROOT/'assets/liberation.ttf', ROOT/'launcher/font-display.ttf']
        out = json.loads(subprocess.check_output([str(BINARY), *[a for f in fonts for a in ('--strings', str(f))]], text=True))
        self.assertEqual(out['missingGlyphs'], [])
        self.assertEqual(len(out['en']), out['rows'], 'an English text is in the table twice')
        for code in ['es', 'pt-BR']:
            for english, text in out[code].items():
                with self.subTest(language=code, text=english):
                    self.assertTrue(text)
                    self.assertEqual(text.count('{}'), english.count('{}'))

    def test_welcome_guide_shows_once_and_again_from_settings(self):
        self.game(self.games/'Ace', 3, 'Ace')
        prefs = self.root/'launcher-preferences.json'
        guide = lambda actions='': self.run_binary('--welcome', 'auto', '--actions', actions)
        # First start: the guide is up, on the language slide, in English.
        state = guide()
        self.assertTrue(state['welcomeActive'])
        self.assertEqual(state['welcomeSlide'], 0)
        self.assertFalse(state['homeActive'])
        # Cross goes on, Circle back; it is not marked seen until it ends.
        self.assertEqual(guide('cross,cross,cross')['welcomeSlide'], 3)
        self.assertEqual(guide('cross,cross,circle')['welcomeSlide'], 1)
        self.assertEqual(guide('circle')['welcomeSlide'], 0)
        self.assertFalse(guide('cross,cross')['welcomeSeen'])
        self.assertTrue(guide()['welcomeActive'])
        # Down on the language slide switches the language at once.
        state = guide('down')
        self.assertEqual((state['language'], state['welcomeSlide']), ('es', 0))
        self.assertEqual(json.loads(prefs.read_text())['language'], 'es')
        # Cross on the last slide ends it: home, and it is remembered.
        state = guide('cross,'*5)
        self.assertFalse(state['welcomeActive'])
        self.assertTrue(state['homeActive'])
        self.assertTrue(state['welcomeSeen'])
        saved = json.loads(prefs.read_text())
        self.assertIs(saved['welcomeSeen'], True)
        self.assertEqual(saved['language'], 'es')
        self.assertFalse(guide()['welcomeActive'])
        # Settings, Welcome guide (row 8) shows it again; Start skips it.
        state = guide(SETTINGS+',down'*8+',cross')
        self.assertTrue(state['welcomeActive'])
        self.assertEqual(state['welcomeSlide'], 0)
        state = guide(SETTINGS+',down'*8+',cross,start')
        self.assertFalse(state['welcomeActive'])
        self.assertEqual(state['focus'], 1)   # back on Settings
        # Start on the first start skips it too, and that counts as seen.
        prefs.unlink()
        self.assertTrue(guide('start')['welcomeSeen'])
        self.assertFalse(guide()['welcomeActive'])
        # Its slides draw in every language.
        prefs.unlink()
        for lang in ['en', 'es', 'pt-BR']:
            for slide in range(5):
                shot = self.storage/f'welcome-{lang}-{slide}.png'
                self.run_binary('--welcome', 'auto', '--language', lang, '--actions', ','.join(['cross']*slide), '--snapshot', shot)
                self.assertGreater(shot.stat().st_size, 10000)

    def test_launcher_preferences_and_artwork_keys(self):
        prefs=self.root/'launcher-preferences.json'
        # Settings: Game info and artwork is row 6; Exit follows it. On that
        # page the adult artwork blur is first, then the source keys:
        # SteamGridDB, IGDB's client ID and secret, RAWG.
        info=SETTINGS+',down'*6+',cross'
        self.assertEqual(self.ui(info)['page'],12)
        self.assertTrue(self.ui(SETTINGS+',down'*9+',cross')['exitRequested'])
        state=self.ui(info+',cross')
        self.assertFalse(state['blurAdult'])
        self.assertIs(json.loads(prefs.read_text())['blurAdult'],False)
        self.assertFalse(self.ui()['blurAdult'])
        prefs.write_text('{broken')
        self.assertTrue(self.ui()['blurAdult'])            # a damaged file falls back to blurring
        self.ui(info+',square')                            # Square on the blur row removes nothing
        keys=info+',down'
        grid,rawg=self.root/'steamgriddb-key.txt',self.root/'rawg-key.txt'
        # Typed keys keep only letters and digits.
        self.run_binary('--text','ab12-CD 34','--actions',keys+',cross')
        self.assertEqual(grid.read_text(),'ab12CD34\n')
        self.run_binary('--text','rawg99','--actions',keys+',down,down,down,cross')
        self.assertEqual(rawg.read_text(),'rawg99\n')
        self.run_binary('--text','client1','--actions',keys+',down,cross')
        self.run_binary('--text','secret2','--actions',keys+',down,down,cross')
        self.assertEqual((self.root/'igdb-client-id.txt').read_text(),'client1\n')
        self.assertEqual((self.root/'igdb-client-secret.txt').read_text(),'secret2\n')
        self.run_binary('--text','--cancel','--actions',keys+',cross')
        self.assertEqual(grid.read_text(),'ab12CD34\n')
        self.ui(keys+',square')
        self.assertFalse(grid.exists())
        self.assertTrue(rawg.exists())

    def test_search_filters_keep_launch_identity_and_names(self):
        self.game(self.games/'AO',1,'Ao Oni')
        self.game(self.games/'Moon',1,'To the Moon')
        self.game(self.games/'Ace',3,'BLACK SOULS')
        state=self.run_binary('--actions','','--search','ao')
        self.assertEqual([x['name'] for x in state['homeEntries']],['Ao Oni'])
        self.assertEqual(state['query'],'ao')
        state=self.run_binary('--actions',PLAY,'--search','ao')
        self.assertTrue(state['launchRequested'])
        self.assertEqual(json.loads((self.root/'selection.json').read_text())['path'],'AO')
        # Circle clears the search.
        self.assertEqual(len(self.run_binary('--actions','circle','--search','ao')['homeEntries']),3)
        state=self.run_binary('--actions','r','--search','')
        self.assertEqual(state['engineFilter'],1)
        self.assertEqual(len(state['homeEntries']),2)
        state=self.run_binary('--actions','r,r')
        self.assertEqual(state['engineFilter'],3)
        self.assertEqual([x['name'] for x in state['homeEntries']],['BLACK SOULS'])
        self.assertFalse(state['launchRequested'])
        self.assertEqual(self.ui('l')['engineFilter'],3)
    def test_error_history_keeps_every_report_once_and_exports_one(self):
        # Two identical crashes in the same second stay two reports; reading the
        # retained copy at the next boot adds no third.
        for name,trace in [('Ao Oni','same.rb:1'),('Ao Oni','same.rb:1'),('Hylics','other.rb:2')]:
            (self.root/'active-game.json').write_text(json.dumps({'name':name}))
            report=self.root/'last-error.txt'
            report.write_text(trace+'\n')
            os.utime(report,(1700000000,1700000000))
            self.ui('circle')
            self.ui()
        state=self.ui(SETTINGS+',down,down,down,cross')
        self.assertEqual(state['page'],4)
        self.assertEqual(sorted(x['game'] for x in state['errors']),['Ao Oni','Ao Oni','Hylics'])
        self.assertTrue(all(x['timestamp'] for x in state['errors']))
        state=self.ui(SETTINGS+',down,down,down,cross,cross,square')
        self.assertGreater(state['errorLines'],0)
        self.assertIn('Saved:',state['message'])
        self.assertEqual(len(list((self.root/'errors').glob('*.txt'))),3)
    def test_error_from_another_app_is_kept_but_not_shown(self):
        # Every HardRPG app shares the data folder. A report written by another
        # app's game goes to the history; the launcher does not open it.
        (self.root/'active-game.json').write_text(json.dumps({'name':'Infinite Fusion'}))
        (self.root/'last-error.txt').write_text('elsewhere.rb:1\n')
        (self.root/'last-error.txt.owner').write_text('OTHER0001')
        state=self.ui('')
        self.assertEqual(state['errorLines'],0)
        self.assertTrue(state['homeActive'])
        self.assertFalse((self.root/'last-error.txt').exists())
        self.assertFalse((self.root/'last-error.txt.owner').exists())
        state=self.ui(SETTINGS+',down,down,down,cross')
        self.assertEqual([x['game'] for x in state['errors']],['Infinite Fusion'])
        # A report from this app (the host has no title ID) still opens.
        (self.root/'last-error.txt').write_text('here.rb:2\n')
        self.assertGreater(self.ui('')['errorLines'],0)
    def test_nested_games_engine_detection_and_keys(self):
        a = self.game(self.games / 'Collection/Game', 3, 'Ace')
        b = self.game(self.games / 'Other/Game', 2, 'VX')
        self.game(self.games / 'XP', 1, 'XP')
        listing = self.cmd('list')
        self.assertEqual([x['kind'] for x in listing], ['folder', 'folder', 'game'])
        self.assertEqual(self.cmd('game', a)[0]['rgss'], 3)
        self.assertEqual(self.cmd('game', b)[0]['rgss'], 2)
        x = self.cmd('launch', a)
        self.assertEqual(x, dict(source='games', path='Collection/Game',
            config=fnv('Collection/Game'), rgss=3, rtp=[]))
        self.assertNotEqual(x['config'], self.cmd('launch', b)['config'])
        self.assertEqual(json.loads((self.root/'selection.json').read_text()), self.cmd('launch', b))
        self.assertEqual(self.ui('cross')['entries'][0]['name'], 'Ace')
        self.assertEqual(self.ui('cross,circle')['entries'], listing)
        # A collection opens on its first game, wherever the focus was.
        state = self.ui('right,cross')
        self.assertEqual([e['name'] for e in state['homeEntries']], ['VX'])
        self.assertEqual(state['homeTarget'], 0)
    def test_case_script_and_archive_detection(self):
        p = self.games / 'Case'; p.mkdir()
        (p/'gAmE.InI').write_text('[gAmE]\nTITLE=Pokémon\nScripts=Data\\Scripts.rvdata2\n')
        self.assertEqual(self.cmd('game', p)[0]['name'], 'Pokémon')
        (p/'gAmE.InI').write_text('[Game]\n')
        (p/'Game.rgss2a').write_bytes(b'archive')
        self.assertEqual(self.cmd('game', p)[0]['rgss'], 2)
        (p/'gAmE.InI').write_bytes(b'[Game]\nTitle=bad\xff title\nLibrary=RGSS301.dll\n')
        self.assertEqual(self.cmd('game', p)[0]['name'], 'bad? title')

    def test_launcher_config_switches_persist_validate_and_keep_other_options(self):
        file = self.root / 'launcher-config.json'
        config = lambda: json.loads(file.read_text())
        # Display, by command: unknown keys stay, invalid values change nothing.
        file.write_text(json.dumps(dict(preloadScript=['keep.rb'], SESourceCount=6)))
        self.call([{'op':'set-display','key':'fixedAspectRatio','value':False},
            {'op':'set-display','key':'integerScalingActive','value':True},
            {'op':'set-display','key':'smoothScaling','value':1}])
        self.assertEqual(config(), dict(preloadScript=['keep.rb'], SESourceCount=6,
            fixedAspectRatio=False, integerScalingActive=True, integerScalingLastMile=False,
            smoothScaling=1, smoothScalingDown=1))
        self.ui(SETTINGS+',cross,square')
        self.assertTrue(config()['fixedAspectRatio'])
        # Filtering: Sharp (5) is the default; Cross goes Sharp, Bilinear,
        # Nearest, Sharp. Sharp shrinks with Bilinear; other numbers are refused.
        file.write_text(json.dumps(dict(preloadScript=['keep.rb'])))
        filtering = SETTINGS+',cross,down,down,cross'
        for expected, down in ((1, 1), (0, 0), (5, 1)):
            self.ui(filtering)
            self.assertEqual((config()['smoothScaling'], config()['smoothScalingDown']), (expected, down))
        self.call([{'op':'set-display','key':'smoothScaling','value':2}])
        self.assertEqual(config()['smoothScaling'], 5)
        # Performance and speed: the overlay cycles, fast forward switches.
        file.write_text(json.dumps(dict(preloadScript=['keep.rb'])))
        speed = SETTINGS + ',down,down,down,down,cross'
        self.assertEqual(self.ui(speed)['page'], 5)
        for keys, expected in [(',cross', 1), (',right', 2), (',left', 1)]:
            self.ui(speed + keys)
            self.assertEqual(config()['performanceMetrics'], expected)
        self.ui(speed + ',square')
        self.assertEqual(config()['performanceMetrics'], 0)
        self.ui(speed + ',down,cross')
        self.assertIs(config()['fastForward'], False)
        self.ui(speed + ',down,left,square')
        self.assertEqual(config(), dict(preloadScript=['keep.rb'], performanceMetrics=0, fastForward=True))
        before = file.read_bytes()
        for key, invalid in [('SESourceCount', [7]), ('performanceMetrics', [-1, 3, True, 'FPS']),
                             ('fastForward', [0, 1, 'on', None])]:
            for value in invalid:
                with self.subTest(key=key, value=value):
                    self.assertIn('error', self.cmd('set-display', key=key, value=value))
                    self.assertEqual(file.read_bytes(), before)

    def test_stale_game_ini_yields_to_the_ini_that_has_the_archive(self):
        # The Windows To the Moon: Game.ini is left over, the data is "To the Moon.rgssad".
        ini = '[Game]\nTitle=Renamed XP\nLibrary=RGSS102E.dll\nScripts=Data\\Scripts.rxdata\n'
        p = self.games / 'Renamed'; p.mkdir()
        (p/'Game.ini').write_text(ini)
        (p/'To the Moon.ini').write_text(ini)
        (p/'To the Moon.rgssad').write_bytes(b'archive')
        self.assertEqual(self.cmd('game', p)[0]['rgss'], 1)
        self.assertEqual(self.cmd('launch', p)['execName'], 'To the Moon')
        # Game.ini keeps winning whenever its own data is there.
        for own in ['Game.rgssad', 'Data']:
            q = self.games / ('Kept' + own.replace('.', '')); q.mkdir()
            (q/'Game.ini').write_text(ini)
            (q/'Other.ini').write_text(ini)
            (q/'Other.rgssad').write_bytes(b'archive')
            (q/own).mkdir() if own == 'Data' else (q/own).write_bytes(b'archive')
            self.assertNotIn('execName', self.cmd('launch', q))

    def test_named_game_ini_handoff_and_zip(self):
        p = self.games / 'Named'; p.mkdir()
        ini = '[Game]\nTitle=Named XP\nLibrary=RGSS102E.dll\nScripts=Data\\Scripts.rxdata\n'
        (p/'Updater.ini').write_text('[Settings]\nVersion=1\n')
        (p/'Uranium.ini').write_text(ini)
        (p/'Uranium.rgssad').write_bytes(b'archive')
        self.assertEqual(self.cmd('game', p)[0]['rgss'], 1)
        self.assertEqual(self.cmd('launch', p)['execName'], 'Uranium')
        archive = self.games / 'Named.zip'
        with zipfile.ZipFile(archive, 'w') as z:
            z.writestr('Wrapper/Uranium.ini', ini)
            z.writestr('Wrapper/Uranium.rgssad', b'archive')
        self.assertEqual(next(x for x in self.cmd('list') if x['kind']=='zip')['rgss'], 1)
        self.assertEqual(self.call([{'op':'launch','path':str(archive),
                                    'kind':'zip','inner':'Wrapper'}])[0]['execName'], 'Uranium')

    def test_named_ini_ambiguity_and_game_ini_precedence(self):
        p = self.games / 'Ambiguous'; p.mkdir()
        ini = '[Game]\nLibrary=RGSS102E.dll\nScripts=Data\\Scripts.rxdata\n'
        (p/'First.ini').write_text(ini)
        (p/'Second.ini').write_text(ini)
        self.assertIn('error', self.cmd('game', p))
        (p/'Game.ini').write_text(ini)
        selected = self.cmd('launch', p)
        self.assertEqual(selected['rgss'], 1)
        self.assertNotIn('execName', selected)


    def test_external_references_preserve_games_and_saves(self):
        external = self.game(self.storage / 'Existing/Game')
        save = external/'Save01.rvdata2'; save.write_bytes(b'keep save')
        data = hashlib.sha256((external/'payload.bin').read_bytes()).digest()
        self.assertEqual(self.cmd('add', external), 1)
        self.assertEqual(self.cmd('add', external), 0)
        x = self.cmd('launch', external)
        self.assertEqual(x['source'], 'external')
        self.assertEqual(x['config'], fnv(str(external)))
        self.assertTrue(any(e['path'] == str(external) for e in self.cmd('list')))
        self.assertEqual(save.read_bytes(), b'keep save')
        self.assertEqual(hashlib.sha256((external/'payload.bin').read_bytes()).digest(), data)
        external.rename(external.with_name('offline'))
        self.assertEqual(self.cmd('list'), [])
        self.assertIn(str(external), (self.root/'library-paths.txt').read_text())

    def test_search_paths_refresh_persist_remove_and_cancel(self):
        collection = self.storage/'Collection'; collection.mkdir()
        self.assertEqual(self.cmd('add-search', collection), 0)
        game = self.game(collection/'Later')
        self.assertEqual(self.cmd('list')[0]['path'], str(game))
        self.assertEqual(self.cmd('search-folders'), [str(collection)])
        self.cmd('remove-search', collection)
        self.assertEqual(self.cmd('list'), [])
        self.assertTrue((game/'payload.bin').exists())
        self.assertIn('cancelled', self.cmd('add-search', collection, cancel='yes')['error'])
        self.assertEqual(self.cmd('search-folders'), [])

    def test_zip_cache_and_save_identity_match_ruby(self):
        zip_path = self.zip(self.games/'wrapped.zip')
        self.assertEqual(self.cmd('list')[0]['kind'], 'zip')
        nested = self.cmd('list', zip_path, kind='zip')
        self.assertEqual(nested[0]['inner'], 'Wrapper')
        x = self.cmd('launch', zip_path, kind='zip', inner='Wrapper')
        key = fnv('wrapped.zip!/Wrapper')
        self.assertEqual(x['path'], 'zip-'+key)
        dest = self.root/'cache'/x['path']
        self.assertEqual((dest/'Data/test.bin').read_bytes(), b'archive data'*30000)
        marker = ['wrapped.zip!/Wrapper',zip_path.stat().st_size,int(zip_path.stat().st_mtime)]
        self.assertEqual((dest/'.hardrpg-source').read_text(), json.dumps(marker, separators=(',',':'),ensure_ascii=False))
        (dest/'Save01.rvdata2').write_bytes(b'save')
        self.assertEqual(self.cmd('launch', zip_path, kind='zip', inner='Wrapper'), x)
        self.assertEqual((dest/'Save01.rvdata2').read_bytes(), b'save')
        # A copy already unpacked (by this or an older HardRPG, which wrote the
        # same marker) is reused as it is: nothing is unpacked again.
        (dest/'Data/test.bin').unlink()
        self.assertEqual(self.cmd('launch', zip_path, kind='zip', inner='Wrapper'), x)
        self.assertFalse((dest/'Data/test.bin').exists())
        with zipfile.ZipFile(zip_path,'a') as z: z.writestr('extra.txt',b'changed')
        self.assertIn('ZIP changed', self.cmd('launch',zip_path,kind='zip',inner='Wrapper')['error'])
        self.assertEqual((dest/'Save01.rvdata2').read_bytes(),b'save')

    def test_unpacked_zip_game_stays_listed_after_its_zip_is_deleted(self):
        zip_path = self.zip(self.games/'wrapped.zip')
        x = self.cmd('launch', zip_path, kind='zip', inner='Wrapper')
        dest = self.root/'cache'/x['path']
        (dest/'Save01.rvdata2').write_bytes(b'save')
        zip_path.unlink()
        listed = self.cmd('refresh')
        self.assertEqual([e['name'] for e in listed], ['ZIP game'])
        # Same identity as the ZIP game: settings, controls, names and saves follow.
        state = self.ui(PLAY)
        self.assertTrue(state['launchRequested'])
        selection = json.loads((self.root/'selection.json').read_text())
        self.assertEqual((selection['source'], selection['path'], selection['config']),
                         ('cache', x['path'], x['config']))
        self.assertEqual((dest/'Save01.rvdata2').read_bytes(), b'save')
        # Its game files are now the unpacked copy: one question, about that copy.
        state = self.ui(GAME+',scroll:8,cross')
        self.assertEqual(state['removeQuestion'], [str(dest)])
        self.ui(GAME+',scroll:8,cross,down,cross')
        self.assertFalse(dest.exists())
        self.assertEqual(self.cmd('refresh'), [])
    def test_remove_from_library_asks_about_files_one_question_at_a_time(self):
        plain = self.game(self.games/'Plain', title='Plain game')
        zip_path = self.zip(self.games/'packed.zip')
        x = self.cmd('launch', zip_path, kind='zip', inner='Wrapper')
        dest = self.root/'cache'/x['path']
        names = lambda: sorted(e['name'] for e in self.cmd('refresh'))
        def remove(name, keys=''):
            return self.ui(self.to(name) + GAME + ',scroll:8,cross' + keys)
        # First the game files (the ZIP), then the unpacked copy; No by default.
        self.assertEqual(remove('packed.zip')['removeQuestion'], [str(zip_path)])
        self.assertEqual(remove('packed.zip', ',down,cross')['removeQuestion'], [str(dest)])
        # Circle at the second question cancels everything.
        state = remove('packed.zip', ',down,cross,circle')
        self.assertTrue(zip_path.exists() and dest.exists())
        self.assertEqual(names(), ['Plain game', 'packed.zip'])
        # Delete the ZIP, keep the unpacked copy: off the list, copy kept.
        state = remove('packed.zip', ',down,cross,cross')
        self.assertFalse(zip_path.exists())
        self.assertTrue(dest.exists())
        self.assertEqual(names(), ['Plain game'])
        self.assertTrue(state['homeActive'])
        self.assertEqual(state['homeTarget'], 0)
        # Keeping the files only takes the game off the list; adding the folder shows it.
        self.assertIn('removed from the list', remove('Plain game', ',cross')['message'])
        self.assertTrue((plain/'Game.ini').exists())
        self.assertEqual(names(), [])
        self.cmd('add', self.games)
        self.assertEqual(names(), ['Plain game'])
        # Yes deletes the folder, and nothing of it stays hidden.
        self.assertEqual(remove('Plain game')['removeQuestion'], [str(plain)])
        remove('Plain game', ',down,cross')
        self.assertFalse(plain.exists())
        self.assertTrue(self.games.exists())
        self.game(self.games/'Plain', title='Plain game')
        self.assertEqual(names(), ['Plain game'])
    def test_delete_the_zip_keeps_an_unpacked_game_playable(self):
        zip_path = self.zip(self.games/'freed.zip')
        # Not unpacked yet: no such row, the menu ends at Remove from library.
        self.assertEqual(self.ui(GAME+',scroll:9')['setting'], 8)
        x = self.cmd('launch', zip_path, kind='zip', inner='Wrapper')
        dest = self.root/'cache'/x['path']
        state = self.ui(GAME+',scroll:9,cross')
        self.assertEqual(state['removeQuestion'], [str(zip_path)])
        # No goes back to the menu and keeps the ZIP.
        self.assertEqual(self.ui(GAME+',scroll:9,cross,cross')['page'], self.ui(GAME)['page'])
        self.assertTrue(zip_path.exists())
        state = self.ui(GAME+',scroll:9,cross,down,cross')
        self.assertIn('plays from its unpacked copy', state['message'])
        self.assertTrue(state['homeActive'])
        self.assertFalse(zip_path.exists())
        self.assertTrue(dest.exists())
        self.assertEqual([e['name'] for e in self.cmd('refresh')], ['ZIP game'])
    def test_remove_never_offers_to_delete_a_library_folder(self):
        # A game folder that is itself an extra search folder is only taken off the list.
        outside = self.game(self.storage/'elsewhere'/'Solo', title='Solo game')
        self.cmd('add-search', outside)
        state = self.ui(GAME+',scroll:8,cross')
        self.assertIn('removed from the list', state['message'])
        self.assertTrue((outside/'Game.ini').exists())
    def test_zip_cancel_incomplete_cache_and_symlinks(self):
        zip_path = self.zip(self.games/'cancel.zip', inner='')
        key = fnv('cancel.zip!/')
        self.assertIn('cancelled', self.cmd('launch',zip_path,kind='zip',cancel='yes')['error'])
        self.assertEqual(list((self.root/'cache').iterdir()), [])
        self.assertFalse((self.root/'selection.json').exists())
        dest = self.root/'cache'/('zip-'+key); dest.mkdir()
        (dest/'Save01.rvdata2').write_bytes(b'keep')
        self.assertIn('incomplete', self.cmd('launch',zip_path,kind='zip')['error'])
        self.assertEqual((dest/'Save01.rvdata2').read_bytes(),b'keep')
        shutil.rmtree(dest)
        target = self.storage/'protected'; target.mkdir(); (target/'save').write_text('keep')
        dest.with_name(dest.name+'.partial').symlink_to(target, target_is_directory=True)
        self.assertIn('Refusing', self.cmd('launch',zip_path,kind='zip')['error'])
        self.assertEqual((target/'save').read_text(),'keep')

    def test_zip_multiple_games_external_archive_and_unsafe_paths(self):
        collection = self.storage/'zips';collection.mkdir()
        zip_path = self.zip(collection/'multi.zip', 'A',version=1)
        with zipfile.ZipFile(zip_path,'a') as z:
            z.writestr('B/Game.ini','[Game]\nTitle=Second\nLibrary=RGSS201.dll\n')
        self.assertEqual(self.cmd('add',collection),2)
        self.assertEqual({e['rgss'] for e in self.cmd('list')},{1,2})
        self.assertIn('error',self.cmd('launch',zip_path,kind='zip',inner='../A'))
        self.assertIn('error',self.cmd('add',self.storage/'../escape'))
        self.assertFalse((self.storage.parent/'escape').exists())

    def test_missing_rtp_prevents_zip_extraction_and_handoff(self):
        zip_path = self.zip(self.games/'rtp.zip', '', rtp='VXACE')
        x = self.cmd('launch',zip_path,kind='zip')
        self.assertEqual(x['pack'],'RPGVXAce')
        self.assertEqual(list((self.root/'cache').iterdir()),[])
        self.assertFalse((self.root/'selection.json').exists())
        state = self.ui(PLAY)
        self.assertEqual(state['missingRtp'],'RPGVXAce')
        state = self.ui(PLAY+',cross')
        self.assertEqual(state['focus'],1)
        self.assertEqual(state['page'],2)
        self.assertFalse(state['missingRtp'])
        # The sheet's second row goes back to the games.
        state = self.ui(PLAY+',touch:400:203')
        self.assertFalse(state['missingRtp'])
        self.assertTrue(state['homeActive'])
    def test_rtp_default_custom_collection_zip_and_reset(self):
        game = self.game(self.games/'RTP',rtp='RPGVXAce')
        pack = self.pack(self.root/'rtp/RPGVXAce')
        self.assertEqual(self.cmd('rtp-status',pack='RPGVXAce'),'Detected')
        self.assertEqual(self.cmd('launch',game)['rtp'],[dict(path=str(pack),root='')])
        collection = self.storage/'rtps';collection.mkdir()
        moved = collection/'VXACE';pack.rename(moved)
        self.cmd('set-rtp',collection,pack='RPGVXAce')
        self.assertEqual(self.cmd('launch',game)['rtp'],[dict(path=str(moved),root='')])
        zip_path = collection/'RPGVXAce.zip'
        with zipfile.ZipFile(zip_path,'w',zipfile.ZIP_DEFLATED) as z:
            z.writestr('Wrapped/RPGVXAce/Graphics/System/Window.png',b'image')
            z.writestr('Wrapped/RPGVXAce/Audio/SE/Cursor.ogg',b'sound')
        original = zip_path.read_bytes()
        self.cmd('set-rtp',zip_path,pack='RPGVXAce')
        self.assertEqual(self.cmd('launch',game)['rtp'],[dict(path=str(zip_path),root='Wrapped/RPGVXAce')])
        self.assertEqual(list((self.root/'cache').iterdir()),[])
        self.assertEqual(zip_path.read_bytes(),original)
        self.cmd('set-rtp',pack='RPGVXAce')
        self.assertEqual(self.cmd('rtp-status',pack='RPGVXAce'),'Missing')
        self.assertTrue(zip_path.exists())
        self.assertIn('error',self.cmd('set-rtp',collection,pack='Bad'))

    def test_rtp_zip_inspection_mounts_once_and_caches_until_refresh(self):
        pack = self.storage / 'RPGVXAce.zip'
        with zipfile.ZipFile(pack, 'w') as z:
            for i in range(2500):
                z.writestr(f'Wrapped/RPGVXAce/Graphics/System/{i}.png', b'image')
                z.writestr(f'Wrapped/RPGVXAce/Audio/SE/{i}.ogg', b'sound')
        subprocess.check_call([str(self.rtp_probe), str(self.root), str(pack)])

    def test_rename_under_search_does_not_leave_stale_cursor(self):
        for i in range(12): self.game(self.games/f'{i:02}', title=f'Game {i:02}')
        state = self.run_binary('--search', 'Game', '--actions',
            ','.join(['right']*11) + ',' + RENAME + ',' + PLAY, '--rename', 'Other')
        # The renamed game leaves the search; its neighbour takes the focus.
        self.assertTrue(state['launchRequested'])
        self.assertEqual(state['launchingName'], 'Game 10')
        self.assertEqual(len(state['homeEntries']),11)
    def test_error_report_scroll_large_prefix_and_complete_retention(self):
        report=self.root/'last-error.txt'
        report.write_bytes(('error line\n'*4000).encode()+b'bad\xff\0')
        size=report.stat().st_size
        state=self.ui(','.join(['down']*15))
        self.assertEqual(state['errorScroll'],15)
        self.assertGreater(state['errorLines'],12)
        self.assertFalse(report.exists())
        self.assertEqual((self.root/'last-error.txt.prev').stat().st_size,size)
        report.write_text('Ruby backtrace\nscript.rb:15\n')
        self.assertEqual(self.ui('cross')['errorLines'],0)
        self.assertEqual(self.cmd('error'),[])

    def test_export_error_report_preserves_complete_trace_and_previous_exports(self):
        body = ('Errno::ENOENT\nGraphics/System/Balloon\n' + 'script.rb:10\n' * 4000).encode()
        (self.root/'last-error.txt').write_bytes(body)
        state = self.ui('square,square')
        reports = sorted((self.root/'reports').glob('error-*.txt'))
        self.assertEqual(len(reports),2)
        for report in reports:
            exported = report.read_bytes()
            self.assertIn(b'HardRPG '+VERSION.encode(),exported)
            self.assertTrue(exported.endswith(body))
        self.assertEqual((self.root/'last-error.txt.prev').read_bytes(),body)
        self.assertGreater(state['errorLines'],12)
        self.assertIn(str(reports[-1]),state['message'])
        # The report remains accessible after dismissal or another app launch.
        state = self.ui(SETTINGS+',down,down,down,cross,cross')
        self.assertGreater(state['errorLines'],12)
        self.assertEqual(len(list((self.root/'reports').glob('error-*.txt'))),2)
    def test_error_export_failure_keeps_error_screen_and_reports_reason(self):
        (self.root/'last-error.txt').write_text('Backtrace: script.rb:10\n')
        (self.root/'reports').write_text('occupied')
        state = self.ui('square')
        self.assertGreater(state['errorLines'],0)
        self.assertIn('Cannot create folder',state['message'])
        self.assertEqual((self.root/'reports').read_text(),'occupied')
        state = self.ui(SETTINGS+',down,down,down,cross,cross,circle')
        self.assertEqual(state['errorLines'],0)
        self.assertEqual(state['page'],4)
    def test_corrupt_settings_leave_navigation_usable(self):
        (self.root/'rtp-paths.txt').write_bytes(b'x'*524289)
        state=self.ui(SETTINGS+',down,cross',self.storage/'bad-rtp.png')
        self.assertEqual(state['page'],0)
        self.assertIn('large',state['message'])
        state=self.ui(SETTINGS+',down,cross,circle,right')
        self.assertTrue(state['homeActive'])
        self.assertEqual(state['focus'],0)
        (self.root/'game-folders.txt').write_bytes(b'x'*65537)
        state=self.ui(SETTINGS+',down,down,cross',self.storage/'bad-folders.png')
        self.assertEqual(state['page'],0)
        self.assertIn('large',state['message'])
        (self.root/'launcher-config.json').write_text('{broken')
        state=self.ui(SETTINGS+',cross',self.storage/'bad-display.png')
        self.assertEqual(state['page'],0)
        self.assertTrue(state['message'])
    def test_production_gles_presenter_on_host(self):
        boot=self.storage/'gles';boot.mkdir()
        app=boot/'app0:';app.mkdir()
        shutil.copy2(ROOT/'assets/liberation.ttf',app/'font.ttf')
        (boot/'ux0:/data').mkdir(parents=True)
        env={**os.environ,'SDL_VIDEODRIVER':'offscreen','LIBGL_ALWAYS_SOFTWARE':'1'}
        subprocess.run([str(BINARY),'--runtime-smoke'],cwd=boot,env=env,check=True,
            stdout=subprocess.DEVNULL,stderr=subprocess.PIPE,timeout=15)

    def test_native_boot_gate_and_pinned_game_behavior(self):
        boot=self.storage/'boot';boot.mkdir()
        app=boot/'app0:';app.mkdir();conf=app/'mkxp.json';conf.write_text('{}')
        pick=boot/'ux0:/data/hardrpg/selection.json';pick.parent.mkdir(parents=True)
        def check(*args):
            return json.loads(subprocess.check_output([str(BINARY),'--boot-check',*args],cwd=boot,text=True))
        self.assertTrue(check())
        self.assertTrue(check('--hardrpg-return'))
        self.assertTrue(check('--hardrpg-play'))
        selection=dict(source='games',path='Game',config=fnv('Game'),rgss=3,rtp=[])
        pick.write_text(json.dumps(selection))
        self.assertTrue(check())
        self.assertTrue(check('--hardrpg-return'))
        self.assertFalse(check('--hardrpg-play'))
        self.assertEqual(json.loads(pick.read_text()),selection)
        for update in ({'source':'invalid'},{'path':'../escape'},{'config':'bad'},
                       {'rgss':4},{'rtp':[{'path':'ux0:/../bad','root':''}]},
                       {'source':'cache','path':'bad'}):
            pick.write_text(json.dumps({**selection,**update}))
            self.assertTrue(check('--hardrpg-play'))
        pick.write_text('invalid')
        self.assertTrue(check('--hardrpg-play'))
        conf.write_text('{gameFolder:"ux0:/some/game"}')
        self.assertFalse(check())
        conf.write_text('{customScript:"play.rb"}')
        self.assertFalse(check())

    def test_interface_navigation_browser_settings_and_exit(self):
        for i in range(14): self.game(self.games/f'{i:02}',title=f'Game {i:02}')
        self.assertEqual(self.ui(','.join(['right']*12))['homeTarget'],12)
        # The top bar: the gear opens Settings, the info icon About.
        state=self.ui(SETTINGS)
        self.assertEqual((state['focus'],state['page'],state['setting']),(1,0,0))
        self.assertEqual(self.ui('touch:925:31')['focus'],1)
        self.assertEqual(self.ui(ABOUT)['focus'],2)
        self.assertEqual(self.ui('touch:725:31')['focus'],2)
        state=self.ui(ABOUT+',circle')
        self.assertTrue(state['homeActive'])
        # Exit is the last settings row; Circle leaves Settings instead.
        self.assertTrue(self.ui(SETTINGS+',up,cross')['exitRequested'])
        state=self.ui(SETTINGS+',up,circle')
        self.assertFalse(state['exitRequested'])
        self.assertTrue(state['homeActive'])
        # Add games: Start, or the toolbar button.
        state=self.ui('start')
        self.assertEqual(state['browserPath'],str(self.storage)+'/')
        self.assertEqual(self.ui('up,left,left,cross')['browserPath'],str(self.storage)+'/')
        self.assertEqual(self.ui('start,cross')['browserPath'],str(self.root))
        state=self.ui('start,start')
        self.assertFalse(state['browserOpen'])
        self.assertTrue(state['homeActive'])
        state=self.ui(SETTINGS+',down,down,cross')
        self.assertEqual(state['page'],3)
    def test_focus_moves_between_shelf_toolbar_and_top_bar(self):
        self.game(self.games/'A',title='Ao Oni')
        state=self.ui('')
        self.assertEqual((state['homeZone'],state['homeTarget']),(0,0))
        state=self.ui('up')
        self.assertEqual(state['homeZone'],1)
        self.assertFalse(state['launchRequested'])
        self.assertEqual(self.ui('up,up')['homeZone'],2)
        self.assertEqual(self.ui('up,down')['homeZone'],0)
        self.assertEqual(self.ui('up,up,down,down')['homeZone'],0)
        # Settings opens on its first row; Down moves the focus to the second.
        self.assertEqual(self.ui(SETTINGS)['setting'],0)
        self.assertEqual(self.ui(SETTINGS+',down')['setting'],1)
    def test_display_name_survives_refresh_without_changing_game_or_save_identity(self):
        game=self.game(self.games/'Witch-ver-1.09a',2,'The Witchs House ver 1.09a F5')
        save=game/'Save01.rvdata';save.write_bytes(b'original save')
        before=self.cmd('list')[0]
        ini=(game/'Game.ini').read_bytes()
        self.cmd('rename',game,name='The Witch’s House')
        after=self.cmd('list')[0]
        self.assertEqual(after['name'],'The Witch’s House')
        for key in ['path','inner','config','kind','rgss']:
            self.assertEqual(before[key],after[key])
        self.assertEqual((game/'Game.ini').read_bytes(),ini)
        self.assertEqual(save.read_bytes(),b'original save')
        self.assertEqual(self.cmd('refresh')[0]['name'],'The Witch’s House')
        self.cmd('rename',game,name='  ')
        self.assertEqual(self.cmd('list')[0]['name'],before['name'])
        for bad in ['a\nb','a\x00b','a'*129]:
            self.assertIn('error',self.cmd('rename',game,name=bad))
        self.assertEqual(self.cmd('list')[0]['name'],before['name'])

    def test_display_name_external_zip_and_keyboard_cancel(self):
        external=self.game(self.storage/'external/Game',title='Original')
        self.cmd('add',external)
        out=self.run_binary('--actions',RENAME,'--rename','Renamed')
        self.assertEqual(out['homeEntries'][out['homeTarget']]['name'],'Renamed')
        self.assertFalse(out['launchRequested'])
        before=(self.root/'game-names.json').read_bytes()
        self.run_binary('--actions',RENAME,'--rename','--cancel')
        self.assertEqual((self.root/'game-names.json').read_bytes(),before)
        zip_path=self.zip(self.games/'wrapped.zip')
        content=zip_path.read_bytes()
        self.cmd('rename',zip_path,kind='zip',name='Mogeko Castle')
        listing=self.cmd('list')
        self.assertEqual(next(e for e in listing if e['kind']=='zip')['name'],'Mogeko Castle')
        self.assertEqual(zip_path.read_bytes(),content)
        self.assertEqual(list((self.root/'cache').iterdir()),[])
        (self.root/'game-names.json').write_text('{broken')
        self.assertTrue(self.ui()['entries'])
        self.assertIn('error',self.cmd('rename',external,name='New'))
        self.assertEqual((self.root/'game-names.json').read_text(),'{broken')
    def test_controls_saved_per_game_survive_rename_without_modifying_game(self):
        g=self.game(self.games/'controls',title='Controls fixture')
        save=g/'Save01.rvdata2';save.write_bytes(b'preserve save')
        state=self.ui(GAME+',cross')
        self.assertEqual(state['page'],6)
        self.assertFalse(state['controls']['enabled'])
        state=self.ui(GAME+',cross,cross,scroll:14,cross,right')
        self.assertTrue(state['controls']['enabled'])
        self.assertEqual(state['controls']['front'],1)
        self.assertFalse(state['launchRequested'])
        self.cmd('rename',g,name='Renamed fixture')
        state=self.ui(GAME+',cross')
        self.assertTrue(state['controls']['enabled'])
        self.assertEqual(state['controls']['front'],1)
        self.assertEqual(save.read_bytes(),b'preserve save')
        self.assertEqual(len(list((self.root/'controls').glob('*.json'))),1)
        state=self.ui(GAME+',cross,scroll:17,cross,cross,circle')
        self.assertFalse(state['controlsTesting'])
        self.assertFalse(state['launchRequested'])
        state=self.ui(GAME+',cross,scroll:18,cross')
        self.assertFalse(state['controls']['enabled'])

    def test_buttons_and_touch_zones_map_to_keys(self):
        self.game(self.games/'keys',title='Keys fixture')
        profile=lambda: json.loads(next((self.root/'controls').glob('*.json')).read_text())
        # Cross row: the picker opens; a key from the keyboard rows replaces Confirm.
        self.assertTrue(self.ui(GAME+',cross,down,cross')['controlPicking'])
        state=self.ui(GAME+',cross,down,cross,down,down,down,down,right,right,cross')
        self.assertFalse(state['controlPicking'])
        key=state['controls']['buttons'][0]
        self.assertGreaterEqual(key,1000)                  # a keyboard key, not an RPG Maker action
        self.assertTrue(state['controls']['enabled'])       # mapping turns custom controls on
        self.assertEqual(profile()['buttons'][0],key)
        # Circle leaves the picker without changing the mapping.
        state=self.ui(GAME+',cross,down,down,cross,down,down,circle')
        self.assertFalse(state['controlPicking'])
        self.assertEqual(state['controls']['buttons'][1],12)
        # A saved key outside the offered set is refused on reading.
        path=next((self.root/'controls').glob('*.json'))
        saved=profile();saved['buttons'][0]=1003
        path.write_text(json.dumps(saved))
        self.assertEqual(self.ui(GAME+',cross')['controls']['buttons'][0],13)
        saved['buttons'][0]=key
        path.write_text(json.dumps(saved))
        # Front screen: invisible corners, each mapped like a button.
        state=self.ui(GAME+',cross,scroll:14,cross'+',right'*6+',down,cross,down,down,cross')
        self.assertEqual(state['controls']['front'],6)
        self.assertGreaterEqual(state['controls']['frontLeft'],1000)
        # Rear pad: its own corners.
        state=self.ui(GAME+',cross,scroll:15,cross,right,right,right,down,down,down,cross,down,down,down,cross')
        self.assertEqual(state['controls']['rear'],3)
        self.assertGreaterEqual(state['controls']['rearBottomLeft'],1000)
        # Tap to walk needs the screen, so it clears the front zones.
        state=self.ui(GAME+',cross,scroll:13,cross')
        self.assertTrue(state['controls']['tapToWalk'])
        self.assertEqual(state['controls']['front'],0)

    def test_corrupt_control_profile_does_not_launch_or_overwrite(self):
        self.game(self.games/'controls')
        self.ui(GAME+',cross,cross')
        path=next((self.root/'controls').glob('*.json'))
        path.write_text('{broken')
        state=self.ui(GAME+',cross')
        self.assertFalse(state['launchRequested'])
        self.assertFalse(state['controls']['enabled'])
        self.assertEqual(path.read_text(),'{broken')

    def test_triangle_binds_game_menu_and_returns_to_same_highlight(self):
        self.game(self.games/'a',title='A game')
        target=self.game(self.games/'b',title='B game')
        # Triangle opens the options menu; Game settings is its fourth row.
        state=self.ui('right,triangle')
        self.assertEqual(state['homePanel'],2)
        self.assertFalse(state['gameOptions'])
        self.assertEqual(self.ui('right,triangle,circle')['homePanel'],0)
        state=self.ui('right,'+GAME)
        self.assertTrue(state['gameOptions'])
        self.assertEqual(state['configuredGame'],'B game')
        self.assertEqual(state['page'],8)
        self.assertFalse(state['launchRequested'])
        state=self.ui('right,'+GAME+',cross,cross,scroll:14,cross,right,right')
        self.assertEqual(state['configuredGame'],'B game')
        self.assertEqual(state['controls']['front'],2)
        profile=self.root/'controls'/f"{state['gameConfigKey']}.json"
        self.assertTrue(json.loads(profile.read_text())['enabled'])
        self.assertEqual(len(list((self.root/'controls').glob('*.json'))),1)
        state=self.ui('right,'+GAME+',cross,circle,circle')
        self.assertFalse(state['gameOptions'])
        self.assertTrue(state['homeActive'])
        self.assertEqual(state['homeTarget'],1)
        self.assertEqual(state['homeEntries'][state['homeTarget']]['name'],'B game')
        self.assertFalse(state['launchRequested'])
        # The Add games card has no options menu.
        self.assertEqual(self.ui('right,right,triangle')['homePanel'],0)
        # Home's own buttons do nothing inside the game settings.
        state=self.ui(GAME+',left,r,select,start')
        self.assertEqual(state['configuredGame'],'A game')
        self.assertEqual(state['engineFilter'],0)
        self.assertFalse(state['browserOpen'])
    def test_game_menu_settings_are_isolated_and_reset_to_global_defaults(self):
        a=self.game(self.games/'a',title='A game')
        self.game(self.games/'b',title='B game')
        save=a/'Save01.rvdata2';save.write_bytes(b'unchanged save')
        global_file=self.root/'launcher-config.json'
        global_file.write_text(json.dumps({'smoothScaling':1,'preloadScript':['keep.rb']}))
        state=self.ui(GAME)
        config=self.root/'config'/f"{state['gameConfigKey']}.json"
        config.write_text(json.dumps({'customScript':'keep.rb','frameSkip':False}))
        self.ui(GAME+',down,cross,down,down,cross')
        values=json.loads(config.read_text())
        self.assertEqual(values['smoothScaling'],0)
        self.assertEqual(values['smoothScalingDown'],0)
        self.assertEqual(values['customScript'],'keep.rb')
        self.assertFalse(values['frameSkip'])
        other=self.ui('right,'+GAME+',down,cross')
        self.assertEqual(other['gameSettings']['smoothScaling'],1)
        self.ui(GAME+',down,cross,down,down,square')
        self.assertNotIn('smoothScaling',json.loads(config.read_text()))
        self.assertNotIn('smoothScalingDown',json.loads(config.read_text()))
        self.ui(GAME+',down,down,cross,down,cross')
        self.assertTrue(json.loads(config.read_text())['midiReverb'])
        self.ui(GAME+',down,down,down,cross,cross')
        self.assertEqual(json.loads(config.read_text())['performanceMetrics'],1)
        self.assertEqual(json.loads(global_file.read_text()),{'smoothScaling':1,'preloadScript':['keep.rb']})
        self.assertEqual(save.read_bytes(),b'unchanged save')
        config.write_text('{broken')
        state=self.ui(GAME)
        self.assertFalse(state['gameOptions'])
        self.assertEqual(config.read_text(),'{broken')
        self.assertFalse(state['launchRequested'])

    def test_wrapped_zip_game_menu_uses_launch_config_identity_without_extracting(self):
        path=self.zip(self.games/'probe.zip',inner='Wrapper/Nested')
        before=path.read_bytes()
        state=self.ui(GAME+',cross,cross')
        self.assertEqual(state['configuredGame'],'ZIP game')
        self.assertTrue(state['controls']['enabled'])
        direct=self.cmd('game',path,kind='zip',inner='Wrapper/Nested')[0]
        self.assertEqual(state['gameConfigKey'],direct['config'])
        self.assertEqual(path.read_bytes(),before)
        self.assertEqual(list((self.root/'cache').iterdir()),[])
        self.assertFalse(state['launchRequested'])
        self.ui(GAME+',scroll:7,cross')
        selection=json.loads((self.root/'selection.json').read_text())
        self.assertEqual(selection['config'],direct['config'])
        self.assertTrue(self.ui(GAME+',scroll:7,cross')['launchRequested'])

    def test_game_menu_touch_and_rename_preserve_selected_context(self):
        self.game(self.games/'a',title='A game')
        self.game(self.games/'b',title='B game')
        # Options menu rows by touch: the fourth is Game settings.
        state=self.ui('right,triangle,touch:700:340')
        self.assertTrue(state['gameOptions'])
        self.assertEqual(state['configuredGame'],'B game')
        state=self.ui('right,'+GAME+',touch:220:160')
        self.assertEqual(state['page'],6)
        state=self.ui('right,'+GAME+',scroll:5,cross')
        self.assertFalse(state['launchRequested'])
        state=self.run_binary('--actions','right,'+GAME+',scroll:5,cross','--rename','Renamed')
        self.assertEqual(state['configuredGame'],'Renamed')
        self.assertTrue(state['gameOptions'])
        state=self.run_binary('--actions','right,'+GAME+',scroll:5,cross,circle','--rename','New title')
        self.assertEqual(state['homeEntries'][state['homeTarget']]['name'],'New title')
        # Renamed from the options menu, the game keeps the focus even when
        # sorting by name moves it.
        state=self.run_binary('--actions','up,cross,'+'down,'*7+'cross,circle,down,'+RENAME,
                              '--rename','Zebra')
        self.assertEqual([e['name'] for e in state['homeEntries']],['New title','Zebra'])
        self.assertEqual(state['homeTarget'],1)
    def test_library_scans_added_games_and_external_folders_on_each_boot(self):
        self.assertEqual(self.ui()['entries'],[])
        self.game(self.games/'added',title='Added at next boot')
        self.assertEqual(self.ui()['entries'][0]['name'],'Added at next boot')
        outside=self.storage/'external';outside.mkdir()
        self.cmd('add-search',outside)
        self.game(outside/'later',title='External added later')
        self.assertIn('External added later',[e['name'] for e in self.ui()['entries']])

    def test_game_menu_missing_rtp_and_browser_return_keep_games_context(self):
        self.game(self.games/'rtp',title='RTP fixture',rtp='RPGVXAce')
        state=self.ui(GAME+',scroll:7,cross')
        self.assertEqual(state['missingRtp'],'RPGVXAce')
        self.assertFalse(state['launchRequested'])
        state=self.ui(GAME+',scroll:7,cross,cross,circle,circle')
        self.assertFalse(state['gameOptions'])
        self.assertTrue(state['homeActive'])
        self.assertEqual(state['homeTarget'],0)
        state=self.ui(GAME+',scroll:4,cross,cross,start,circle,circle')
        self.assertFalse(state['gameOptions'])
        self.assertFalse(state['browserOpen'])
        self.assertTrue(state['homeActive'])
    def test_release_notices_are_version_ordered_scrollable_and_dismissible(self):
        newer=next_tag(VERSION_TAG)
        release={'draft':False,'prerelease':True,'tag_name':newer,
            'name':'Next Alpha','body':'\n'.join('Change '+str(i) for i in range(30)),
            'html_url':'https://github.com/Asukate/mkxp-z-vita/releases/tag/'+newer}
        fixture=self.storage/'releases.json';fixture.write_text(json.dumps([release]))
        state=self.run_binary('--release-fixture',fixture,'--actions','down,down')
        self.assertEqual(state['releaseTag'],newer)
        self.assertGreater(state['releaseLines'],30)
        self.assertEqual(state['releaseScroll'],2)
        state=self.run_binary('--release-fixture',fixture,'--actions','cross')
        self.assertEqual(state['releaseLines'],0)
        self.assertFalse(state['launchRequested'])
        release['tag_name']=VERSION_TAG;fixture.write_text(json.dumps([release]))
        self.assertEqual(self.run_binary('--release-fixture',fixture)['releaseLines'],0)

    def test_startup_release_check_setting_persists(self):
        state=self.ui(SETTINGS+',down,down,down,down,down,cross,cross')
        self.assertEqual(state['page'],7)
        self.assertFalse(state['automaticUpdates'])
        self.assertFalse(self.ui()['automaticUpdates'])
        # The gear, then the sixth row twice, then the switch on that page.
        state=self.ui('touch:925:31,touch:220:362,touch:220:362,touch:220:160')
        self.assertEqual(state['page'],7)
        self.assertTrue(state['automaticUpdates'])
        # The brand in the top bar goes home.
        self.assertTrue(self.ui('touch:925:31,touch:100:31')['homeActive'])

if __name__ == '__main__':
    unittest.main(verbosity=2)
