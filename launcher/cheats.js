// HardRPG cheats for RPG Maker MV and MZ (loaded after the game's plugins).
//
// The MV/MZ answer to launcher/cheats.rb: the same Cheats page in the
// in-game menu (L+R+Start), drawn by the launcher's own UI, with the same
// protocol. The runtime calls HardRPGCheats.request(cmd, row) while the game
// is paused and hands the JSON string it returns to the menu (GameMenu::cheat
// in src/native_launcher/game_menu.h). See cheats.rb for the protocol.
//
// Nothing is hooked until a cheat that needs it is turned on, and the
// switch-like cheats live in this object only: nothing goes into the save.
(function (root) {
  'use strict';
  var SPEEDS = [null, 4, 5, 6];
  var MULTIPLIERS = [1, 2, 5, 10];
  var VARIABLE_LIMIT = 999999999; // variables have no limit of their own
  var PAGES = {main: 'Cheats', party: 'Party', items: 'Items', weapons: 'Weapons', armors: 'Armor',
               battle: 'Battle', move: 'Movement', teleport: 'Teleport', switches: 'Switches',
               variables: 'Variables', rules: 'Game rules', maps: 'Pick a map',
               warning: 'Before you use cheats', careful: 'Careful'};
  var S = {
    page: 'main', stack: [], actor: 0, god: {}, through: false, speed: 0, noEncounters: false,
    expRate: 0, goldRate: 0, freeItems: false, allowSave: false, allowMenu: false,
    warned: false, careful: null, positions: [null, null, null], target: null, pending: null, internal: false, hooked: {}
  };
  var rows, handlers, reply, changed, rebuild, focus, current;

  function g(name) { return root[name]; }
  function fmt(n) {
    if (typeof n !== 'number' || !isFinite(n)) return String(n);
    var s = String(Math.abs(Math.trunc(n))).replace(/\B(?=(\d{3})+(?!\d))/g, ',');
    return n < 0 ? '-' + s : s;
  }
  function ready() { return !!(g('$gameParty') && g('$gamePlayer') && g('$gameMap')); }
  function scene() { var M = g('SceneManager'); return M ? M._scene : null; }
  function onMap() { var C = g('Scene_Map'); return !!(C && scene() instanceof C); }
  function inBattle() { return !!(g('$gameParty').inBattle && g('$gameParty').inBattle()); }
  function engineName() { return g('Utils') && g('Utils').RPGMAKER_NAME === 'MZ' ? 'RPG Maker MZ' : 'RPG Maker MV'; }

  function row(kind, label, value, on, note, raw, handler) {
    var r = {k: kind, l: label};
    if (value !== undefined && value !== null) r.v = String(value);
    if (on !== undefined && on !== null) r.on = !!on;
    if (note) r.n = note;
    if (raw) r.r = 1;
    rows.push(r);
    handlers[rows.length - 1] = handler || null;
    return r;
  }
  function message(text) {
    reply.m = text;
    reply.a = Array.prototype.slice.call(arguments, 1).map(String);
  }
  function openPage(page) { S.stack.push([S.page, current]); S.page = page; rebuild = true; }
  function refresh() { rebuild = true; focus = current; }
  function step(b) { return b === 'left' ? -1 : 1; }
  function cycle(i, b, n) { return ((i + step(b)) % n + n) % n; }
  function resumeWith(job) { S.pending = job; installPlayerHook(); reply.resume = true; }
  function internally(fn) { var was = S.internal; S.internal = true; try { fn(); } finally { S.internal = was; } }
  // A number the player types: Cross opens the menu's editor on value, kept
  // within lo..hi; set gets the new value. shown: the text in the row.
  function number(label, value, lo, hi, shown, raw, note, set) {
    value = typeof value === 'number' && isFinite(value) ? Math.trunc(value) : 0;
    hi = Math.max(Math.trunc(hi), lo);
    var r = row('number', label, shown !== null && shown !== undefined ? shown : fmt(value), null,
                note || '<cross> Type the number you want.', raw, function (b, v) { if (b === 'set') set(v); });
    r.x = Math.max(lo, Math.min(hi, value));
    r.lo = lo;
    r.hi = hi;
    return r;
  }

  // ---- pages ---------------------------------------------------------------
  function usable(page) {
    if (!ready()) return false;
    return page === 'battle' ? inBattle() : true;
  }
  function build() {
    rows = [];
    handlers = [];
    if (!ready()) { row('plain', 'Load or start a game first.'); return rows; }
    PAGE_ROWS[S.page]();
    return rows;
  }
  function gold() { return g('$gameParty').gold(); }
  function currency() { var d = g('$dataSystem'); return d && d.currencyUnit ? d.currencyUnit : 'G'; }
  function mapName(id) { var m = g('$dataMapInfos') && g('$dataMapInfos')[id]; return m && m.name ? m.name : ''; }
  function place(p) {
    if (!p) return 'Empty';
    var n = mapName(p[0]);
    return 'Map ' + p[0] + (n ? ' ' + n : '') + ' \u00b7 ' + p[1] + ', ' + p[2];
  }
  function subtitle() {
    if (!ready()) return '';
    var p = g('$gamePlayer'), id = g('$gameMap').mapId(), n = mapName(id);
    return engineName() + ' · ' + fmt(gold()) + ' ' + currency() + ' · Map ' + id + (n ? ' ' + n : '') +
           ' (' + p.x + ', ' + p.y + ')';
  }

  // Through the game's own gain/lose, so its limit and plugins see it.
  function setGold(value) {
    var party = g('$gameParty');
    value = Math.max(0, Math.min(value, party.maxGold()));
    var delta = value - gold();
    internally(function () {
      if (delta > 0) party.gainGold(delta);
      else if (delta < 0) party.loseGold(-delta);
    });
    message('Gold is now {}.', fmt(gold()));
  }

  var PAGE_ROWS = {
    main: function () {
      var max = g('$gameParty').maxGold();
      number('Gold', gold(), 0, max, fmt(gold()) + ' ' + currency(), false,
             "<cross> Type how much gold you want. Up to {}, this game's own limit.", setGold).a = [fmt(max)];
      // Switches, variables and game rules: a warning first, every time.
      var nav = function (page, note) {
        row('nav', PAGES[page], null, null, note, false, function () {
          if (page === 'switches' || page === 'variables' || page === 'rules') { openPage('careful'); S.careful = page; }
          else openPage(page);
        });
      };
      nav('party', 'Each member: god mode, level, experience, healing and stats.');
      nav('items', 'Set how many of each item you carry.');
      nav('weapons', 'Set how many of each weapon you carry.');
      nav('armors', 'Set how many of each armor you carry.');
      if (inBattle()) nav('battle', 'Win now, weaken the enemies, heal, or leave the battle.');
      else row('plain', 'Battle', 'Only in battle', null, 'Open the menu during a battle to win it, heal or leave.');
      nav('move', 'Walk through walls, walking speed, no random battles.');
      nav('teleport', 'Go to any map and tile, and keep spots to come back to.');
      nav('switches', "The game's on/off flags, by name. For players who know the game.");
      nav('variables', "The game's numbers, by name. For players who know the game.");
      nav('rules', 'More experience or gold, items not used up, saving and menu always allowed.');
    },

    warning: function () {
      row('plain', 'Cheats can make a game unstable.');
      row('plain', 'Save in a new slot before you change anything.');
      row('primary', 'I understand, show the cheats', null, null,
          'Cheats change the running game directly, and the game may not expect it: events can break, ' +
          'and a save made afterwards keeps the change. Use them at your own discretion.', false,
          function () { S.warned = true; S.page = 'main'; rebuild = true; focus = null; });
      focus = 2;
    },

    careful: function () {
      row('plain', 'Only for players who know this game well.');
      row('plain', 'A wrong value can break the story or the save.');
      row('primary', 'I know what I am changing', null, null,
          'Games use these to track the story and events. Change one only if you know exactly why: ' +
          'a wrong value can lock you out of a quest or break the save for good. Save in a new slot first.', false,
          function () { S.page = S.careful || 'main'; rebuild = true; focus = null; });
      focus = 2;
    },

    party: function () {
      var list = g('$gameParty').allMembers();
      if (!list.length) { row('plain', 'Nobody is in the party.'); return; }
      if (S.actor >= list.length) S.actor = 0;
      var a = list[S.actor], id = a.actorId();
      row('choice', 'Party member', a.name() + ' (' + (S.actor + 1) + '/' + list.length + ')', null,
          'Left and right: whose rows these are.', false,
          function (b) { S.actor = cycle(S.actor, b, list.length); refresh(); });
      row('toggle', 'God mode', null, !!S.god[id], 'HP and MP stay full and skills cost nothing.', false, function () {
        if (S.god[id]) delete S.god[id]; else S.god[id] = true;
        installGodHook();
        if (S.god[id]) heal(a);
        refresh();
        message(S.god[id] ? 'God mode on for {}.' : 'God mode off for {}.', a.name());
      });
      number('Level', a.level, 1, a.maxLevel(), null, false, null, function (v) {
        setLevel(a, v);
        refresh();
        message('{} is now level {}.', a.name(), a.level);
      });
      number('Experience', a.currentExp(), 0, a.expForLevel(a.maxLevel()), null, false, null, function (v) {
        internally(function () { a.changeExp(Math.max(0, v), false); });
        refresh();
        message('{} has {} experience, level {}.', a.name(), fmt(a.currentExp()), a.level);
      });
      row('action', 'Full heal', a.hp + '/' + a.mhp, null, null, false, function () { heal(a); message('{} is fully healed.', a.name()); });
      row('action', 'Clear states', null, null, null, false, function () { clearStates(a); message('States cleared for {}.', a.name()); });
      var names = ['Max HP', 'Max MP', 'Attack', 'Defense', 'Magic attack', 'Magic defense', 'Agility', 'Luck'];
      names.forEach(function (n, i) {
        number(n, a.param(i), 1, a.paramMax(i), null, false, null, function (v) {
          a.addParam(i, v - a.param(i));
          refresh();
        });
      });
      row('action', 'Whole party: full heal', null, null, null, false, function () {
        list.forEach(heal);
        message('The party is fully healed.');
      });
      var allGod = list.every(function (m) { return S.god[m.actorId()]; });
      row('toggle', 'Whole party: god mode', null, allGod, null, false, function () {
        list.forEach(function (m) { if (allGod) delete S.god[m.actorId()]; else S.god[m.actorId()] = true; });
        installGodHook();
        if (!allGod) list.forEach(heal);
        refresh();
        message(allGod ? 'God mode off for the party.' : 'God mode on for the party.');
      });
      var top = Math.max.apply(null, list.map(function (m) { return m.maxLevel(); }));
      var highest = Math.max.apply(null, list.map(function (m) { return m.level; }));
      number('Whole party: level', highest, 1, top, null, false, '<cross> Type the level for everyone in the party.', function (v) {
        list.forEach(function (m) { setLevel(m, v); });
        refresh();
        message('Everyone in the party is now level {}.', String(v));
      });
    },

    items: function () { itemRows(g('$dataItems')); },
    weapons: function () { itemRows(g('$dataWeapons'), true); },
    armors: function () { itemRows(g('$dataArmors'), true); },

    battle: function () {
      if (!inBattle()) { row('plain', 'Only in battle'); return; }
      var troop = g('$gameTroop');
      row('action', 'Win the battle', null, null, null, false, function () {
        troop.members().forEach(function (e) {
          if (e.isAlive()) { e.setHp(0); if (e.performCollapse) e.performCollapse(); }
        });
        reply.resume = true;
      });
      row('action', 'Enemies to 1 HP', null, null, null, false, function () {
        troop.members().forEach(function (e) { if (e.hp > 1) e.setHp(1); });
        message('Every enemy has 1 HP.');
      });
      row('action', 'Full heal the party', null, null, null, false, function () {
        g('$gameParty').battleMembers().forEach(heal);
        message('The party is fully healed.');
      });
      row('action', 'Leave the battle', null, null, null, false, function () {
        g('BattleManager').abort();
        reply.resume = true;
      });
    },

    move: function () {
      row('toggle', 'Walk through walls', null, S.through, 'Walls, water and people no longer stop you.', false, function () {
        S.through = !S.through;
        installPlayerHook();
      });
      var sp = SPEEDS[S.speed];
      row('choice', 'Walking speed', sp === null ? "Game's own" : ['Normal', 'Fast', 'Faster'][sp - 4], null,
          'Kept even when the game changes it.', false, function (b) { S.speed = cycle(S.speed, b, SPEEDS.length); installPlayerHook(); });
      row('toggle', 'No random battles', null, S.noEncounters, 'Random encounters stop; battles set by events still happen.', false,
          function () { S.noEncounters = !S.noEncounters; installPlayerHook(); });
    },

    teleport: function () {
      var p = g('$gamePlayer'), map = g('$gameMap');
      if (!S.target) S.target = [map.mapId(), p.x, p.y];
      var infos = g('$dataMapInfos') || [], ids = [];
      for (var i = 1; i < infos.length; i++) if (infos[i]) ids.push(i);
      var n = mapName(S.target[0]);
      row('nav', 'Map', n ? S.target[0] + ' ' + n : S.target[0], null, '<cross> Pick the map from a list of its names.', false, function () {
        if (!ids.length) { message('This game has no map list.'); return; }
        openPage('maps');
        focus = Math.max(0, ids.indexOf(S.target[0]));
      });
      var size = mapSize(S.target[0]);
      ['X', 'Y'].forEach(function (axis, k) {
        number(axis, S.target[k + 1], 0, Math.max(size[k] - 1, 0), null, false,
               '<cross> Type the tile to arrive on. 0 is the left (X) or top (Y) edge.', function (v) { S.target[k + 1] = v; });
      });
      row('primary', 'Go there', null, null, 'Closes the menu and moves you to the map and tile above.', false,
          function () { teleport(S.target[0], S.target[1], S.target[2]); });
      row('action', 'Back to where I am', null, null, 'Sets the rows above to the place you are now.', false, function () {
        S.target = [map.mapId(), p.x, p.y];
        refresh();
      });
      [0, 1, 2].forEach(function (i) {
        row('action', 'Spot ' + (i + 1), place(S.positions[i]), null,
            '<cross> Go to this spot. <square> Save where you are now into it. Kept until HardRPG closes.', false, function (b) {
          if (b === 'square') {
            S.positions[i] = [map.mapId(), p.x, p.y];
            message('Spot {} saved: {}.', i + 1, place(S.positions[i]));
          } else if (S.positions[i]) teleport.apply(null, S.positions[i]);
          else message('Spot {} is empty. <square> saves where you are now into it.', i + 1);
        }).sq = 'Save spot here';
      });
    },

    // Every map by name: Cross picks it for Teleport.
    maps: function () {
      var infos = g('$dataMapInfos') || [];
      for (var i = 1; i < infos.length; i++) if (infos[i]) (function (id) {
        row('action', ('00' + id).slice(-3) + (infos[id].name ? ' ' + infos[id].name : ''), null,
            S.target && S.target[0] === id, null, true, function () {
          var size = mapSize(id);
          S.target = [id, Math.min(S.target[1], size[0] - 1), Math.min(S.target[2], size[1] - 1)];
          var prev = S.stack.pop();
          S.page = prev[0];
          focus = prev[1];
          rebuild = true;
        });
      })(i);
    },

    switches: function () {
      var names = g('$dataSystem').switches, sw = g('$gameSwitches'), on = 0;
      for (var k = 1; k < names.length; k++) if (sw.value(k)) on++;
      row('plain', 'Switches that are on', fmt(on) + ' / ' + fmt(names.length - 1));
      for (var id = 1; id < names.length; id++) (function (id) {
        row('toggle', pad(id) + (names[id] ? ' ' + names[id] : ''), null, sw.value(id), null, true, function () {
          sw.setValue(id, !sw.value(id));
          g('$gameMap').requestRefresh();
        });
      })(id);
    },

    variables: function () {
      var names = g('$dataSystem').variables, vars = g('$gameVariables');
      for (var id = 1; id < names.length; id++) (function (id) {
        var v = vars.value(id);
        number(pad(id) + (names[id] ? ' ' + names[id] : ''), v, -VARIABLE_LIMIT, VARIABLE_LIMIT,
               typeof v === 'number' ? fmt(v) : String(JSON.stringify(v)).slice(0, 24), true,
               '<cross> Type the value. The game reads it at once; a wrong value can break events.', function (n) {
          vars.setValue(id, n);
          g('$gameMap').requestRefresh();
        });
      })(id);
    },

    rules: function () {
      row('choice', 'Experience', '×' + MULTIPLIERS[S.expRate], null, 'Multiplies the experience the party earns.', false,
          function (b) { S.expRate = cycle(S.expRate, b, MULTIPLIERS.length); installRateHooks(); });
      row('choice', 'Gold earned', '×' + MULTIPLIERS[S.goldRate], null, 'Multiplies the gold the party earns. Spending is not changed.', false,
          function (b) { S.goldRate = cycle(S.goldRate, b, MULTIPLIERS.length); installRateHooks(); });
      row('toggle', 'Items are not used up', null, S.freeItems, 'Using an item keeps it.', false,
          function () { S.freeItems = !S.freeItems; installItemHook(); });
      row('toggle', 'Saving always allowed', null, S.allowSave, 'For places where the game turned saving off.', false,
          function () { S.allowSave = !S.allowSave; installPlayerHook(); });
      row('toggle', 'Menu always allowed', null, S.allowMenu, 'For places where the game turned its menu off.', false,
          function () { S.allowMenu = !S.allowMenu; installPlayerHook(); });
      row('action', 'Open the save screen', null, null,
          "Closes this menu and opens the game's save screen, even where saving is off.", false, function () {
        if (!onMap()) { message('Opens from the map only.'); return; }
        resumeWith(function () { g('$gameSystem').enableSave(); g('SceneManager').push(g('Scene_Save')); });
      });
    }
  };

  function pad(id) { return ('000' + id).slice(-4); }
  function heal(a) { a.recoverAll(); }
  function clearStates(a) {
    var death = a.deathStateId();
    a.states().map(function (s) { return s.id; }).forEach(function (id) { if (id !== death) a.removeState(id); });
  }
  function setLevel(a, level) {
    level = Math.max(1, Math.min(level, a.maxLevel()));
    internally(function () { a.changeLevel(level, false); });
  }
  function itemRows(list, equip) {
    var items = (list || []).filter(function (i) { return i && i.name; }), party = g('$gameParty');
    var setOwned = function (item, n) { party.gainItem(item, n - party.numItems(item)); };
    var top = items.reduce(function (m, i) { return Math.max(m, party.maxItems(i)); }, 0) || 99;
    number('Set how many of every one', top, 0, top, '', false,
           '<cross> Type one number; every item on this page is set to it. 0 removes them all.', function (v) {
      items.forEach(function (i) { setOwned(i, Math.min(v, party.maxItems(i))); });
      refresh();
      message('You now have {} of each.', fmt(v));
    });
    items.forEach(function (item) {
      number(item.name, party.numItems(item), 0, party.maxItems(item), '×' + party.numItems(item), true,
             equip ? '<cross> Type how many you want to have. Equip lists it only for characters whose class can use it.' : '<cross> Type how many you want to have.', function (v) {
        setOwned(item, v);
        message('{}: you now have {}.', item.name, fmt(party.numItems(item)));
      });
    });
  }
  // Width and height in tiles: the current map from memory, others unknown
  // until loaded (the editor then allows up to 500).
  function mapSize(id) {
    var map = g('$gameMap');
    if (map && map.mapId() === id) return [map.width(), map.height()];
    return [500, 500];
  }
  function teleport(map, x, y) {
    if (!onMap()) { message('Teleport works on the map only.'); return; }
    var p = g('$gamePlayer'), d = p.direction();
    resumeWith(function () { p.reserveTransfer(map, x, y, d, 0); });
  }

  // ---- hooks ---------------------------------------------------------------
  function wrap(cls, name, make) {
    var key = cls + '.' + name, C = g(cls);
    if (S.hooked[key] || !C || !C.prototype[name]) return;
    C.prototype[name] = make(C.prototype[name]);
    S.hooked[key] = true;
  }
  function installPlayerHook() {
    wrap('Game_Player', 'update', function (base) {
      return function () {
        tick(this);
        return base.apply(this, arguments);
      };
    });
    wrap('Game_Player', 'isDebugThrough', function (base) {
      return function () { return S.through || base.apply(this, arguments); };
    });
    wrap('Game_Player', 'canEncounter', function (base) {
      return function () { return !S.noEncounters && base.apply(this, arguments); };
    });
  }
  function tick(player) {
    if (S.pending) { var job = S.pending; S.pending = null; job(); }
    if (SPEEDS[S.speed] !== null) player._moveSpeed = SPEEDS[S.speed];
    var sys = g('$gameSystem');
    if (S.allowSave && sys) sys.enableSave();
    if (S.allowMenu && sys) sys.enableMenu();
  }
  function god(battler) { return battler.isActor && battler.isActor() && S.god[battler.actorId()]; }
  function installGodHook() {
    wrap('Game_Actor', 'setHp', function (base) { return function (v) { return base.call(this, god(this) ? this.mhp : v); }; });
    wrap('Game_Actor', 'setMp', function (base) { return function (v) { return base.call(this, god(this) ? this.mmp : v); }; });
    wrap('Game_Actor', 'setTp', function (base) { return function (v) { return base.call(this, god(this) ? this.maxTp() : v); }; });
    wrap('Game_Actor', 'paySkillCost', function (base) {
      return function () { if (!god(this)) return base.apply(this, arguments); };
    });
  }
  function installItemHook() {
    wrap('Game_Party', 'consumeItem', function (base) {
      return function () { if (!S.freeItems) return base.apply(this, arguments); };
    });
  }
  function installRateHooks() {
    wrap('Game_Party', 'gainGold', function (base) {
      return function (n) {
        if (n > 0 && !S.internal) n *= MULTIPLIERS[S.goldRate];
        return base.call(this, n);
      };
    });
    wrap('Game_Actor', 'gainExp', function (base) {
      return function (n) {
        if (n > 0 && !S.internal) n *= MULTIPLIERS[S.expRate];
        return base.call(this, n);
      };
    });
  }

  // ---- protocol ------------------------------------------------------------
  function request(cmd, rowIndex) {
    reply = {};
    changed = [];
    rebuild = false;
    focus = null;
    try {
      if (cmd === 'open') {
        if (!usable(S.page)) { S.page = 'main'; S.stack = []; }
        // Once per run, before any cheat: they can break a game.
        if (!S.warned && ready()) { S.page = 'warning'; S.stack = []; }
        rebuild = true;
      } else if (cmd === 'back') {
        if (!S.stack.length) return JSON.stringify({close: true});
        var prev = S.stack.pop();
        S.page = prev[0];
        focus = prev[1];
        rebuild = true;
      } else {
        var all0 = build(), r = all0[rowIndex], h = handlers[rowIndex], value = null, button = cmd;
        if (r && cmd.indexOf('set:') === 0) {
          value = /^-?\d+$/.test(cmd.slice(4)) ? parseInt(cmd.slice(4), 10) : null;
          if (value === null || r.k !== 'number' || r.hi === undefined) h = null;
          else { value = Math.max(r.lo, Math.min(r.hi, value)); button = 'set'; }
        } else if (r && r.k === 'choice') {
          // Left and right step through the choices; Cross steps forward.
          if (cmd === 'square') h = null; else button = cmd === 'left' ? 'left' : 'right';
        } else if (r && r.k === 'number') h = null; // number rows change only through the editor
        if (h) { changed.push(rowIndex); current = rowIndex; h(button, value); }
      }
      var all = build();
      if (rebuild) { reply.rows = all; reply.focus = focus || 0; }
      else reply.set = changed.filter(function (i) { return all[i]; }).map(function (i) { return [i, all[i]]; });
      reply.page = S.page;
      reply.title = PAGES[S.page] || 'Cheats';
      reply.subtitle = subtitle();
      return JSON.stringify(reply);
    } catch (e) {
      return JSON.stringify({m: 'That cheat does not work in this game ({}).', a: [e && e.name ? e.name : 'Error'],
                             page: S.page, title: 'Cheats', subtitle: '', focus: 0,
                             rows: [{k: 'plain', l: 'Load or start a game first.'}]});
    }
  }

  root.HardRPGCheats = {request: request, state: S};
})(typeof globalThis !== 'undefined' ? globalThis : this);
