#include "input/vita_controls.h"
#include "native_launcher/update_check.h"
#include <cassert>
#include <iostream>
using namespace hardrpg;
int main() {
  ControlProfile p;
  ControlState state;
  ControlSample s;
  s.milliseconds = 100;
  s.buttons = 1;
  assert(!state.update(p, s).actions[13]);
  p.enabled = true;
  assert(!state.update(p, s).actions[13]);
  s.buttons = 0;
  s.milliseconds += 16;
  state.update(p, s);
  s.buttons = 1;
  s.milliseconds += 16;
  assert(state.update(p, s).actions[13]);
  s.buttons = 0;
  s.milliseconds += 16;
  assert(!state.update(p, s).actions[13]);
  p.buttons[0] = 12;
  s.buttons = 1;
  s.milliseconds += 16;
  auto o = state.update(p, s);
  assert(o.actions[12] && !o.actions[13]);
  s.buttons = 0;
  s.lx = 128 + p.deadzone;
  s.milliseconds += 16;
  assert(!state.update(p, s).actions[6]);
  s.lx++;
  s.milliseconds += 16;
  assert(state.update(p, s).actions[6]);
  s.lx = 128;
  p.toggleRun = true;
  s.buttons = 4;
  s.milliseconds += 16;
  assert(state.update(p, s).actions[11]);
  s.milliseconds += 16;
  assert(state.update(p, s).actions[11]);
  s.buttons = 0;
  s.milliseconds += 16;
  assert(state.update(p, s).actions[21]);
  s.buttons = 4;
  s.milliseconds += 16;
  assert(!state.update(p, s).actions[11]);
  s.buttons = 0;
  p.front = 1;
  s.front = {{0, .1f, .9f}, {1, .9f, .9f}};
  s.milliseconds += 16;
  o = state.update(p, s);
  assert(o.actions[11] && o.actions[21] && o.actions[13]);
  s.front = {{0, -.1f, .9f}};
  s.milliseconds += 16;
  assert(!state.update(p, s).actions[11]);
  p.front = 3;
  s.front = {{0, .02f, .775f}};
  s.milliseconds += 16;
  assert(state.update(p, s).actions[4]);
  s.front = {{0, .18f, .775f}};
  s.milliseconds += 16;
  assert(!state.update(p, s).actions[4]);
  p.front = 2;
  s.front = {{1, .7f, .2f}};
  s.milliseconds += 16;
  o = state.update(p, s);
  assert(o.pointer && o.actions[38] && o.x == .7f && o.y == .2f);
  s.front.clear();
  s.milliseconds += 16;
  o = state.update(p, s);
  assert(o.pointer && !o.actions[38] && o.x == .7f);
  p.front = 0;
  p.rear = 1;
  s.rear = {{23, .1f, .8f}};
  s.milliseconds += 16;
  assert(!state.update(p, s).actions[11]);
  s.rear = {{23, .1f, .1f}};
  s.milliseconds += 16;
  assert(!state.update(p, s).actions[11]);
  s.rear.clear();
  s.milliseconds += 16;
  state.update(p, s);
  s.rear = {{24, .1f, .1f}, {25, .9f, .1f}};
  s.milliseconds += 16;
  o = state.update(p, s);
  assert(o.actions[11] && o.actions[12]);
  s.rear.clear();
  s.milliseconds += 16;
  state.update(p, s);
  p.rear = 2;
  s.rear = {{99, .4f, .4f}};
  s.milliseconds += 16;
  o = state.update(p, s);
  float x = o.x;
  s.rear[0].x = .5f;
  s.milliseconds += 16;
  assert(state.update(p, s).x == x);
  s.buttons = 1u << 10;
  s.milliseconds += 16;
  o = state.update(p, s);
  assert(o.x == x && !o.actions[17]);
  s.rear[0].x = .6f;
  s.milliseconds += 16;
  assert(state.update(p, s).x > x);
  s.buttons = 0;
  s.rear.clear();
  p.rightPointer = true;
  s.rx = 255;
  s.milliseconds += 16;
  x = state.update(p, s).x;
  s.milliseconds += 16;
  assert(state.update(p, s).x > x);
  // A press made during a slow frame (400 ms) still counts.
  s.buttons = 0;
  s.rx = 128;
  s.milliseconds += 16;
  state.update(p, s);
  s.buttons = 1;
  s.milliseconds += 400;
  assert(state.update(p, s).actions[12]);
  s.buttons = 0;
  s.milliseconds += 16;
  state.update(p, s);
  s.rx = 255;
  s.buttons = 1;
  s.milliseconds += 1000;
  assert(!state.update(p, s).actions[12]);
  s.buttons = 0;
  s.rx = 128;
  s.milliseconds += 16;
  state.update(p, s);
  s.buttons = 1;
  s.milliseconds += 16;
  assert(state.update(p, s).actions[12]);
  state.reset();
  assert(!state.update(p, s).actions[12]);
  auto copy = ControlProfile::parse(p.json());
  assert(copy.json().stringify() == p.json().stringify());
  for (auto text : {"{\"deadzone\":0}", "{\"front\":7}", "{\"rear\":6}", "{\"rearLeft\":39}",
                    "{\"buttons\":[13]}", "{\"enabled\":1}"}) {
    bool rejected = false;
    try {
      ControlProfile::parse(json5pp::parse(text));
    } catch (...) {
      rejected = true;
    }
    assert(rejected);
  }
  ReleaseVersion v;
  for (auto tag : {"v0.3.0-alpha", "0.3.0", "1.2.3-alpha.2+build.001"})
    assert(v.parse(tag));
  for (auto tag : {"v0.03.0", "0.3", "0.3.0-", "0.3.0-alpha.01", "0.3.0+",
                   "0.3.0+bad..id", "0.3.0+bad/id"})
    assert(!v.parse(tag));
  auto release = [](const char *tag, bool pre) {
    return std::string("{\"draft\":false,\"prerelease\":") +
           (pre ? "true" : "false") + ",\"tag_name\":\"" + tag +
           "\",\"name\":\"Release\",\"body\":\"Changes\\nFixed "
           "stuff\",\"html_url\":\"https://github.com/Asukate/mkxp-z-vita/"
           "releases/tag/" +
           tag + "\"}";
  };
  std::string data = "[" + release("v0.3.0-alpha.10", true) + "," +
                     release("v0.3.0-alpha.2", true) + "," +
                     release("v0.2.1-alpha", true) + "]";
  assert(newerRelease(data, "v0.3.0-alpha").tag == "v0.3.0-alpha.10");
  assert(newerRelease(data, "v0.3.0").tag.empty());
  assert(newerRelease("[" + release("v0.4.0-alpha", false) + "]", "v0.3.0")
             .tag.empty());
  assert(
      newerRelease("[" + release("v0.3.0", false) + "]", "v0.3.0-alpha").tag ==
      "v0.3.0");
  assert(newerRelease("[" + release("v0.3.0-alpha", true) + "]", "v0.3.0-alpha")
             .tag.empty());
  assert(newerRelease("invalid", "v0.3.0-alpha").tag.empty());
  bool retrieved = false;
  assert(newerRelease("[]", "v0.3.0-alpha", &retrieved).tag.empty());
  assert(retrieved);
  newerRelease("invalid", "v0.3.0-alpha", &retrieved);
  assert(!retrieved);
  newerRelease("{}", "v0.3.0-alpha", &retrieved);
  assert(!retrieved);
  assert(newerRelease(std::string(262145, 'x'), "v0.3.0-alpha").tag.empty());
  {
    // A button mapped to a keyboard key holds that key and no RGSS action;
    // the other buttons keep their actions.
    ControlProfile keys;
    keys.enabled = true;
    keys.buttons[0] = controlKeyBase + 22;  // Cross -> S
    keys.frontLeft = controlKeyBase + 4;    // front left zone -> A
    keys.front = 1;
    ControlState held;
    ControlSample idle, down;
    idle.milliseconds = 10;
    held.update(keys, idle);                 // arms on a neutral sample
    down.milliseconds = 20;
    down.buttons = 1 | 2;                    // Cross and Circle
    down.front.push_back({1, 0.1f, 0.9f});
    ControlResult r = held.update(keys, down);
    assert(r.keys[22] && r.keys[4]);
    assert(!r.actions[13] && r.actions[12]);
    int used = 0;
    for (bool k : r.keys)
      used += k;
    assert(used == 2);
    // The profile survives its own JSON, and only offered keys are accepted.
    ControlProfile again = ControlProfile::parse(keys.json());
    assert(again.buttons[0] == controlKeyBase + 22 && again.frontLeft == controlKeyBase + 4);
    assert(controlValid(controlKeyBase + 22) && !controlValid(controlKeyBase + 3) &&
           !controlValid(controlKeyBase + 300) && !controlValid(999));
    auto bad = keys.json();
    bad.as_object()["buttons"].as_array()[0] = controlKeyBase + 3;
    bool refused = false;
    try {
      ControlProfile::parse(bad);
    } catch (const std::exception &) {
      refused = true;
    }
    assert(refused);
    assert(controlName(controlKeyBase + 22) == "Key S" && controlName(13) == "Confirm" &&
           controlName(21) == std::string("SHIFT"));
    // Every picker entry is valid, unique and inside the panel.
    for (int i = 0; i < controlPickCount; ++i) {
      assert(controlValid(controlPicks[i].value) && controlPickIndex(controlPicks[i].value) == i);
      int x, y, w, h;
      controlPickRect(i, x, y, w, h);
      assert(x >= 194 && x + w <= 912 && y >= 146 && y + h <= 460);
    }
  }
  {
    // Four corners of the rear pad, each its own mapping.
    ControlProfile pad;
    pad.enabled = true;
    pad.rear = 3;
    pad.rearLeft = controlKeyBase + 26;         // top left -> W
    pad.rearRight = controlKeyBase + 4;         // top right -> A
    pad.rearBottomLeft = controlKeyBase + 22;   // bottom left -> S
    pad.rearBottomRight = controlKeyBase + 7;   // bottom right -> D
    ControlState state;
    ControlSample s;
    s.milliseconds = 10;
    s.rear.push_back({9, 0.2f, 0.8f});          // a finger resting there while it arms
    state.update(pad, s);
    s.milliseconds = 20;
    assert(!state.update(pad, s).keys[22]);     // the resting finger presses nothing
    auto tap = [&](float x, float y, int id) {
      ControlSample t;
      t.milliseconds = (s.milliseconds += 10);
      t.rear.push_back({9, 0.2f, 0.8f});
      t.rear.push_back({id, x, y});
      return state.update(pad, t);
    };
    assert(tap(0.1f, 0.1f, 20).keys[26]);
    assert(tap(0.9f, 0.1f, 21).keys[4]);
    assert(tap(0.1f, 0.9f, 22).keys[22]);
    ControlResult br = tap(0.9f, 0.9f, 23);
    assert(br.keys[7] && !br.keys[22] && !br.keys[26] && !br.keys[4]);
    // A finger landing in the band between corners takes the nearest one,
    // and one that slides into the band keeps its corner until it is past.
    ControlResult middle = tap(0.5f, 0.5f, 24);
    int pressed = 0;
    for (bool k : middle.keys)
      pressed += k;
    assert(pressed == 1);
    assert(tap(0.3f, 0.1f, 25).keys[26]);
    assert(tap(0.52f, 0.1f, 25).keys[26]);
    assert(tap(0.6f, 0.1f, 25).keys[4]);
    // Two fingers, two corners at once.
    ControlSample two;
    two.milliseconds = (s.milliseconds += 10);
    two.rear.push_back({9, 0.2f, 0.8f});
    two.rear.push_back({30, 0.1f, 0.1f});
    two.rear.push_back({31, 0.9f, 0.9f});
    ControlResult both = state.update(pad, two);
    assert(both.keys[26] && both.keys[7]);
    ControlProfile again = ControlProfile::parse(pad.json());
    assert(again.rear == 3 && again.rearBottomLeft == controlKeyBase + 22 &&
           again.rearBottomRight == controlKeyBase + 7);
  }
  {
    // The front screen as invisible zones: whole, halves and four corners.
    ControlProfile f;
    f.enabled = true;
    f.frontLeft = controlKeyBase + 26;          // W
    f.frontRight = controlKeyBase + 4;          // A
    f.frontBottomLeft = controlKeyBase + 22;    // S
    f.frontBottomRight = controlKeyBase + 7;    // D
    auto touch = [&](int mode, std::vector<ControlContact> contacts) {
      f.front = mode;
      ControlState st;
      ControlSample s;
      s.milliseconds = 10;
      st.update(f, s);
      s.milliseconds = 20;
      s.front = contacts;
      return st.update(f, s);
    };
    ControlResult whole = touch(4, {{1, 0.9f, 0.9f}});
    assert(whole.keys[26] && !whole.keys[7] && !whole.pointer);
    ControlResult halves = touch(5, {{1, 0.1f, 0.9f}, {2, 0.9f, 0.1f}});
    assert(halves.keys[26] && halves.keys[4] && !halves.keys[22]);
    ControlResult corners = touch(6, {{1, 0.1f, 0.9f}, {2, 0.9f, 0.1f}});
    assert(corners.keys[22] && corners.keys[4] && !corners.keys[26] && !corners.keys[7]);
    // A finger already on the screen when the game starts waits to be lifted.
    f.front = 6;
    ControlState st;
    ControlSample s;
    s.milliseconds = 10;
    s.front.push_back({1, 0.9f, 0.9f});
    st.update(f, s);
    s.milliseconds = 20;
    assert(!st.update(f, s).keys[7]);
    ControlProfile again = ControlProfile::parse(f.json());
    assert(again.front == 6 && again.frontBottomRight == controlKeyBase + 7);
  }
  std::cout << "Control transitions, touch filtering, key mappings, profile "
               "validation and release ordering passed\n";
}
