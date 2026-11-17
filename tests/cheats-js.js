// Check launcher/cheats.js (the MV/MZ cheats) against small stand-ins for
// the stock rpg_objects methods it uses, with the same protocol checks as
// tests/cheats.rb.   node tests/cheats-js.js
'use strict';
const fs = require('fs');
const path = require('path');
const vm = require('vm');
const assert = require('assert');

class Game_Battler {
  constructor() { this._hp = 10; this._mp = 0; this._tp = 0; this._states = []; }
  get hp() { return this._hp; }
  get mp() { return this._mp; }
  setHp(v) { this._hp = Math.max(0, Math.min(v, this.mhp)); }
  setMp(v) { this._mp = Math.max(0, Math.min(v, this.mmp)); }
  setTp(v) { this._tp = v; }
  maxTp() { return 100; }
  isAlive() { return this._hp > 0; }
  states() { return this._states.map((id) => ({id})); }
  removeState(id) { this._states = this._states.filter((s) => s !== id); }
  deathStateId() { return 1; }
  recoverAll() { this._hp = this.mhp; this._mp = this.mmp; this._states = []; }
}
class Game_Actor extends Game_Battler {
  constructor(id, name) { super(); this._id = id; this._name = name; this.level = 1; this._exp = 0; this._plus = [0, 0, 0, 0, 0, 0, 0, 0]; }
  get mhp() { return 100; }
  get mmp() { return 50; }
  actorId() { return this._id; }
  name() { return this._name; }
  isActor() { return true; }
  maxLevel() { return 99; }
  changeLevel(l) { this.level = l; this._exp = l * 10; }
  currentExp() { return this._exp; }
  changeExp(e) { this._exp = e; this.level = Math.max(1, Math.floor(e / 10)); }
  gainExp(e) { this.changeExp(this._exp + e); }
  param(i) { return 10 + this._plus[i]; }
  addParam(i, v) { this._plus[i] += v; }
  paramMax(i) { return i === 0 ? 9999 : 999; }
  expForLevel(l) { return l * 10; }
  paySkillCost() { this.setMp(this.mp - 5); }
}
class Game_Enemy extends Game_Battler {
  get mhp() { return 500; }
  get mmp() { return 0; }
  isActor() { return false; }
  performCollapse() { this.collapsed = true; }
}
class Game_Party {
  constructor() { this._gold = 0; this._items = new Map(); this.battle = false; }
  gold() { return this._gold; }
  maxGold() { return 99999999; }
  gainGold(n) { this._gold = Math.max(0, Math.min(this._gold + n, this.maxGold())); }
  loseGold(n) { this.gainGold(-n); }
  allMembers() { return ctx.actors; }
  battleMembers() { return ctx.actors; }
  inBattle() { return this.battle; }
  numItems(i) { return this._items.get(i) || 0; }
  gainItem(i, n) { this._items.set(i, Math.max(0, Math.min(this.numItems(i) + n, 99))); }
  loseItem(i, n) { this.gainItem(i, -n); }
  consumeItem(i) { this.loseItem(i, 1); }
  maxItems() { return 99; }
}
class Game_Player {
  constructor() { this.x = 3; this.y = 4; this._moveSpeed = 4; }
  direction() { return 2; }
  update() {}
  isDebugThrough() { return false; }
  canEncounter() { return true; }
  reserveTransfer(...a) { this.reserved = a; }
}
class Game_System { constructor() { this.save = false; this.menu = false; } enableSave() { this.save = true; } enableMenu() { this.menu = true; } }
class Scene_Map {}
class Scene_Save {}
class Scene_Battle {}

const ctx = {Game_Battler, Game_Actor, Game_Enemy, Game_Party, Game_Player, Game_System, Scene_Map, Scene_Save, Scene_Battle,
  Utils: {RPGMAKER_NAME: 'MZ'}, JSON, Math, String, Array, Object, isFinite};
ctx.globalThis = ctx;
ctx.actors = [new Game_Actor(1, 'Reid'), new Game_Actor(2, 'Priscilla')];
ctx.$gameParty = new Game_Party();
ctx.$gamePlayer = new Game_Player();
ctx.$gameMap = {mapId: () => 1, width: () => 20, height: () => 15, requestRefresh() { this.refreshed = true; }};
ctx.$gameSystem = new Game_System();
ctx.$gameTroop = {e: [new Game_Enemy(), new Game_Enemy()], members() { return this.e; }};
ctx.$gameTroop.e.forEach((e) => e.setHp(500));
ctx.$gameSwitches = {d: {}, value(i) { return !!this.d[i]; }, setValue(i, v) { this.d[i] = v; }};
ctx.$gameVariables = {d: {}, value(i) { return this.d[i] || 0; }, setValue(i, v) { this.d[i] = v; }};
ctx.$dataSystem = {switches: ['', 'Door open', ''], variables: ['', 'Coins'], currencyUnit: 'G'};
ctx.$dataItems = [null, {id: 1, name: 'Potion'}, {id: 2, name: ''}];
ctx.$dataWeapons = [null, {id: 1, name: 'Sword'}];
ctx.$dataArmors = [null, {id: 1, name: 'Shield'}];
ctx.$dataMapInfos = [null, {name: 'Town'}, {name: 'Cave'}];
ctx.SceneManager = {_scene: new Scene_Map(), push(k) { this.pushed = k; }};
ctx.BattleManager = {abort() { this.aborted = true; }};
vm.createContext(ctx);
vm.runInContext(fs.readFileSync(path.join(__dirname, '../launcher/cheats.js'), 'utf8'), ctx, {filename: 'cheats.js'});

let rows = [];
function ask(cmd, row = 0) {
  const r = JSON.parse(ctx.HardRPGCheats.request(cmd, row));
  if (r.rows) rows = r.rows;
  (r.set || []).forEach(([i, v]) => { rows[i] = v; });
  return r;
}
function at(label) {
  const i = rows.findIndex((r) => r.l === label || r.l.startsWith(label));
  assert(i >= 0, `no row ${label} in ${rows.map((r) => r.l)}`);
  return i;
}
const press = (label, b = 'cross') => ask(b, at(label));
const value = (label) => rows[at(label)].v;
// Switches, variables, game rules: a warning page every time, then the page.
const careful = (label) => {
  const w = press(label);
  assert.strictEqual(w.page, 'careful');
  assert.strictEqual(w.focus, 2);
  return press('I know what I am changing');
};

// A number row: Cross opens the menu's editor; "set:<n>" is its answer.
const set = (label, n) => ask(`set:${n}`, at(label));

let r = ask('open');
assert.strictEqual(r.page, 'warning');
assert.strictEqual(r.focus, 2);
assert(ask('back').close);
assert.strictEqual(ask('open').page, 'warning');
r = press('I understand, show the cheats');
assert.strictEqual(r.page, 'main');
assert(r.subtitle.startsWith('RPG Maker MZ') && r.subtitle.includes('Map 1 Town (3, 4)'));
assert(rows.every((x) => x.l !== 'Amount'));
assert(rows.filter((x) => x.k === 'nav').every((x) => x.n));
const goldRow = rows[at('Gold')];
assert.deepStrictEqual([goldRow.k, goldRow.x, goldRow.lo, goldRow.hi, goldRow.a], ['number', 0, 0, 99999999, ['99,999,999']]);
press('Gold'); press('Gold', 'square'); press('Gold', 'left');
assert.strictEqual(ctx.$gameParty.gold(), 0); // buttons alone change nothing
r = set('Gold', 123456);
assert.strictEqual(ctx.$gameParty.gold(), 123456);
assert.deepStrictEqual([r.m, r.a], ['Gold is now {}.', ['123,456']]);
assert(value('Gold').startsWith('123,456'));
set('Gold', 1e12);
assert.strictEqual(ctx.$gameParty.gold(), 99999999);
set('Gold', 7);
assert.strictEqual(ctx.$gameParty.gold(), 7);
set('Gold', 'x');
assert.strictEqual(ctx.$gameParty.gold(), 7);

// Party.
press('Party');
const reid = ctx.actors[0];
press('God mode');
reid.setHp(1);
assert.strictEqual(reid.hp, 100);
reid.paySkillCost();
assert.strictEqual(reid.mp, 50);
ctx.actors[1].setHp(1);
assert.strictEqual(ctx.actors[1].hp, 1);
set('Level', 42);
assert.strictEqual(reid.level, 42);
set('Level', 500);
assert.strictEqual(reid.level, 99);
set('Level', 0);
assert.strictEqual(reid.level, 1);
press('Party member', 'right');
assert(value('Party member').startsWith('Priscilla'));
press('Party member', 'square');
assert(value('Party member').startsWith('Priscilla'));
press('Full heal');
assert.strictEqual(ctx.actors[1].hp, 100);
set('Attack', 250);
assert.strictEqual(value('Attack'), '250');
set('Whole party: level', 7);
assert(ctx.actors.every((a) => a.level === 7));
r = ask('back');
assert.strictEqual(rows[r.focus].l, 'Party');

// Rules.
careful('Game rules');
press('Experience', 'right');
ctx.actors[1].gainExp(10);
assert(ctx.actors[1].currentExp() >= 20);
press('Gold earned', 'right');
ctx.$gameParty.loseGold(7);
ctx.$gameParty.gainGold(10);
assert.strictEqual(ctx.$gameParty.gold(), 20);
press('Items are not used up');
ctx.$gameParty.gainItem(ctx.$dataItems[1], 3);
ctx.$gameParty.consumeItem(ctx.$dataItems[1]);
assert.strictEqual(ctx.$gameParty.numItems(ctx.$dataItems[1]), 3);
press('Saving always allowed');
ctx.$gamePlayer.update();
assert(ctx.$gameSystem.save);
assert(press('Open the save screen').resume);
ctx.$gamePlayer.update();
assert.strictEqual(ctx.SceneManager.pushed, Scene_Save);
ask('back');

// Items.
press('Items');
assert(rows.every((x) => x.l !== ''));
set('Potion', 12);
assert.strictEqual(value('Potion'), '×12');
ask('back');
press('Armor');
set('Set how many of every one', 99);
assert.strictEqual(value('Shield'), '×99');
set('Set how many of every one', 0);
assert.strictEqual(value('Shield'), '×0');
ask('back');

// Battle.
assert.strictEqual(rows[at('Battle')].k, 'plain');
ctx.$gameParty.battle = true;
ask('open');
press('Battle');
press('Enemies to 1 HP');
assert(ctx.$gameTroop.e.every((e) => e.hp === 1));
assert(press('Win the battle').resume);
assert(ctx.$gameTroop.e.every((e) => e.hp === 0 && e.collapsed));
press('Leave the battle');
assert(ctx.BattleManager.aborted);
ctx.$gameParty.battle = false;
assert.strictEqual(ask('open').page, 'main');

// Movement.
press('Movement');
press('Walk through walls');
assert(ctx.$gamePlayer.isDebugThrough());
press('Walking speed', 'right'); press('Walking speed', 'right');
press('No random battles');
ctx.$gamePlayer.update();
assert.strictEqual(ctx.$gamePlayer._moveSpeed, 5);
assert(!ctx.$gamePlayer.canEncounter());
ask('back');

// Teleport: the map from a list, the tile typed.
press('Teleport');
assert.deepStrictEqual([rows[at('X')].hi, rows[at('Y')].hi], [19, 14]);
r = press('Map');
assert.strictEqual(r.page, 'maps');
assert.deepStrictEqual(rows.map((x) => x.l), ['001 Town', '002 Cave']);
r = press('002 Cave');
assert.strictEqual(r.page, 'teleport');
assert.strictEqual(value('Map'), '2 Cave');
set('X', 12);
assert.strictEqual(value('X'), '12');
assert(press('Go there').resume);
ctx.$gamePlayer.update();
assert.deepStrictEqual(ctx.$gamePlayer.reserved, [2, 12, 4, 2, 0]);
press('Spot 1', 'square');
assert.strictEqual(value('Spot 1'), 'Map 1 Town \u00b7 3, 4');
assert.strictEqual(rows[at('Spot 1')].sq, 'Save spot here');
ask('back');

// Switches and variables.
careful('Switches');
assert.strictEqual(rows[0].k, 'plain');
assert.strictEqual(rows[0].v, '0 / 2');
assert.strictEqual(rows[1].l, '0001 Door open');
assert.strictEqual(rows[1].r, 1);
ask('cross', 1);
assert(ctx.$gameSwitches.value(1) && rows[1].on && ctx.$gameMap.refreshed);
ask('back');
careful('Variables');
ask('cross', 0); ask('right', 0);
assert.strictEqual(ctx.$gameVariables.value(1), 0);
ask('set:-40', 0);
assert.strictEqual(ctx.$gameVariables.value(1), -40);
assert.strictEqual(rows[0].v, '-40');
ask('back');
assert(ask('back').close);

// Nothing loaded: a row saying so, never an exception.
ctx.$gameParty = null;
assert(JSON.parse(ctx.HardRPGCheats.request('open', 0)).rows[0].l.startsWith('Load or start'));
console.log('cheats.js: OK');
