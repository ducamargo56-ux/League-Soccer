// Copyright 2019 Google LLC & Bastiaan Konings
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "usereventmanager.hpp"

#include "environmentmanager.hpp"

#include <cstdarg>
#include <cstdio>

namespace {
void Log8BitDo(const char* fmt, ...) {
  FILE* f = std::fopen("diagnostico_8bitdo.log", "a");
  if (!f) return;
  va_list args;
  va_start(args, fmt);
  std::vfprintf(f, fmt, args);
  va_end(args);
  std::fprintf(f, "\n");
  std::fclose(f);
}
}  // namespace

extern void AddGamepad(int deviceIndex, int gamepadID);
extern void RemoveGamepad(int gamepadID);

namespace blunted {

template <>
UserEventManager* Singleton<UserEventManager>::singleton = 0;

UserEventManager::UserEventManager() {
  lastKeyTime_ms = 0;

  // SDL_EnableKeyRepeat(0, SDL_DEFAULT_REPEAT_INTERVAL);

  // yes, SDL starts mousebuttons at 1...
  for (int i = 1; i < 32; i++) {
    mousePressed[i] = false;
  }

  for (int j = 0; j < _JOYSTICK_MAX; j++) {
    joystick[j] = nullptr;
    joystickInstanceNow[j] = -1;
    for (int i = 0; i < _JOYSTICK_MAXBUTTONS; i++) {
      joyButtonPressed[j][i] = false;
    }
  }

  for (int j = 0; j < _JOYSTICK_MAX; j++) {
    for (int i = 0; i < _JOYSTICK_MAXAXES; i++) {
      joyAxis[j][i] = 0.0;
      joyAxisCalibration[j][i][0] = -32768.0;
      joyAxisCalibration[j][i][1] = 32767.0;
      joyAxisCalibration[j][i][2] = 0.0;
    }
  }

  // init the joy and gamecontroller subsystems!
  int initResult = SDL_Init(SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER);
  Log8BitDo("=== Inicio do diagnostico 8BitDo ===");
  Log8BitDo("SDL_Init resultado=%d erro=%s", initResult, SDL_GetError());
  Log8BitDo("SDL_NumJoysticks=%d limite=%d", SDL_NumJoysticks(), _JOYSTICK_MAX);

  // Load community SDL_GameControllerDB mappings if present (zlib / permissive license)
  int numMappings = SDL_GameControllerAddMappingsFromFile("data/gamecontrollerdb.txt");
  if (numMappings > 0) {
    printf("[userevent] Loaded %d gamecontroller mappings from data/gamecontrollerdb.txt\n",
           numMappings);
  }

  const int detected = SDL_NumJoysticks();
  for (int i = 0; i < detected && i < _JOYSTICK_MAX; i++) {
    Log8BitDo("OPEN index=%d nome=%s instance-prevista=%d", i,
              SDL_JoystickNameForIndex(i) ? SDL_JoystickNameForIndex(i) : "(null)",
              static_cast<int>(SDL_JoystickGetDeviceInstanceID(i)));
    joystick[i] = SDL_JoystickOpen(i);
    if (joystick[i]) {
      joystickInstanceNow[i] = SDL_JoystickInstanceID(joystick[i]);
      Log8BitDo("OPEN OK index=%d slot=%d instance=%d botoes=%d eixos=%d hats=%d",
                i, i, static_cast<int>(joystickInstanceNow[i]),
                SDL_JoystickNumButtons(joystick[i]), SDL_JoystickNumAxes(joystick[i]),
                SDL_JoystickNumHats(joystick[i]));
      AddGamepad(i, i);
    } else {
      Log8BitDo("OPEN FALHOU index=%d erro=%s", i, SDL_GetError());
    }
  }
  if (detected > _JOYSTICK_MAX)
    Log8BitDo("AVISO: %d dispositivos excedem limite; extras ignorados",
              detected - _JOYSTICK_MAX);
  SDL_JoystickEventState(SDL_ENABLE);
}

UserEventManager::~UserEventManager() {
  for (int i = 0; i < _JOYSTICK_MAX; i++) {
    if (joystick[i]) {
      SDL_JoystickClose(joystick[i]);
      joystick[i] = nullptr;
    }
  }
}

void UserEventManager::Exit() {}

void UserEventManager::InputSDLEvent(const SDL_Event& event) {
  switch (event.type) {
    case SDL_JOYDEVICEADDED: {
      const SDL_JoystickID addedInstance =
          SDL_JoystickGetDeviceInstanceID(event.jdevice.which);
      const int existingSlot = FindJoystickSlot(addedInstance);
      Log8BitDo("DEVICE ADDED index=%d instance=%d slot-existente=%d",
                event.jdevice.which, static_cast<int>(addedInstance), existingSlot);
      if (existingSlot >= 0)
        break;

      // SDL_JOYDEVICEADDED reports the *device index* for the new joystick,
      // suitable for SDL_JoystickOpen(). Allocate the next free dense slot and
      // remember the joystick's stable instance ID there.
      printf("[userevent] Gamepad connected: %d\n", event.jdevice.which);
      SDL_Joystick* joy = SDL_JoystickOpen(event.jdevice.which);
      if (!joy) {
        Log8BitDo("DEVICE ADDED OPEN FALHOU erro=%s", SDL_GetError());
        break;
      }
      std::unique_lock<std::mutex> lock(joyButtonPressedMutex);
      int slot = FindFreeJoystickSlot();
      if (slot < 0) {
        Log8BitDo("DEVICE ADDED sem slot livre");
        SDL_JoystickClose(joy);
        break;
      }
      joystick[slot] = joy;
      joystickInstanceNow[slot] = SDL_JoystickInstanceID(joy);
      Log8BitDo("DEVICE ADDED OK index=%d slot=%d instance=%d",
                event.jdevice.which, slot, static_cast<int>(joystickInstanceNow[slot]));
      for (int i = 0; i < _JOYSTICK_MAXBUTTONS; i++)
        joyButtonPressed[slot][i] = false;
      for (int i = 0; i < _JOYSTICK_MAXAXES; i++) {
        joyAxis[slot][i] = 0.0;
        joyAxisCalibration[slot][i][0] = -32768.0;
        joyAxisCalibration[slot][i][1] = 32767.0;
        joyAxisCalibration[slot][i][2] = 0.0;
      }
      lock.unlock();
      // Constructing the HIDGamepad reloads its config, which re-enters the
      // UserEventManager (SetJoystickAxisCalibration), so add it unlocked.
      AddGamepad(event.jdevice.which, slot);
      break;
    }
    case SDL_JOYDEVICEREMOVED: {
      // SDL_JOYDEVICEREMOVED reports the *instance ID* (not a device index).
      // Resolve it back to our dense slot, close the handle, drop it from the
      // controller list, then pack the remaining joysticks down so the slots
      // stay densely 0..count-1 (this is what avoids the index renumbering bug
      // that previously left stale device indices pointing at the wrong data).
      printf("[userevent] Gamepad disconnected: %d\n", event.jdevice.which);
      std::unique_lock<std::mutex> lock(joyButtonPressedMutex);
      int slot = FindJoystickSlot(event.jdevice.which);
      Log8BitDo("DEVICE REMOVED instance=%d slot=%d", event.jdevice.which, slot);
      if (slot < 0)
        break;
      if (joystick[slot]) {
        SDL_JoystickClose(joystick[slot]);
        joystick[slot] = nullptr;
      }
      joystickInstanceNow[slot] = -1;
      lock.unlock();
      RemoveGamepad(slot);
      lock.lock();
      CompactJoystickSlots(slot);
      break;
    }
    case SDL_KEYDOWN:
      // OS auto-repeat must not refresh the press time: menus implement their
      // own repeat from the moment the key first went down.
      if (event.key.repeat)
        break;
      keyPressedMutex.lock();
      keyPressed[event.key.keysym.sym].pressTime_ms =
          EnvironmentManager::GetInstance().GetTime_ms();
      lastKeyTime_ms = keyPressed[event.key.keysym.sym].pressTime_ms;
      keyPressedMutex.unlock();
      break;
    case SDL_KEYMAPCHANGED:
      keymapGeneration++;
      break;
    case SDL_KEYUP:
      keyPressedMutex.lock();
      keyPressed.erase(event.key.keysym.sym);
      keyPressedMutex.unlock();
      break;
    case SDL_MOUSEBUTTONDOWN:
      mousePressedMutex.lock();
      if (event.button.button >= 0 && event.button.button < 32)
        mousePressed[event.button.button] = true;
      mousePressedMutex.unlock();
      break;
    case SDL_MOUSEBUTTONUP:
      mousePressedMutex.lock();
      if (event.button.button >= 0 && event.button.button < 32)
        mousePressed[event.button.button] = false;
      mousePressedMutex.unlock();
      break;
    case SDL_JOYAXISMOTION: {
      std::unique_lock<std::mutex> lock(joyButtonPressedMutex);
      int joyID = FindJoystickSlot(event.jaxis.which);
      if (joyID >= 0 && event.jaxis.axis < _JOYSTICK_MAXAXES)
        joyAxis[joyID][event.jaxis.axis] = event.jaxis.value;
      if (event.jaxis.value > 16000 || event.jaxis.value < -16000)
        Log8BitDo("AXIS instance=%d slot=%d axis=%u valor=%d",
                  event.jaxis.which, joyID, event.jaxis.axis, event.jaxis.value);
      break;
    }
    case SDL_JOYHATMOTION: {
      std::unique_lock<std::mutex> lock(joyButtonPressedMutex);
      int joyID = FindJoystickSlot(event.jhat.which);
      Log8BitDo("HAT instance=%d slot=%d hat=%u valor=%u",
                event.jhat.which, joyID, event.jhat.hat, event.jhat.value);
      if (joyID >= 0 && event.jhat.hat == 0) {
        // Map D-pad hat states to virtual button indices (16..19):
        // Button 16: Up, 17: Right, 18: Down, 19: Left
        joyButtonPressed[joyID][16] = (event.jhat.value & SDL_HAT_UP) != 0;
        joyButtonPressed[joyID][17] = (event.jhat.value & SDL_HAT_RIGHT) != 0;
        joyButtonPressed[joyID][18] = (event.jhat.value & SDL_HAT_DOWN) != 0;
        joyButtonPressed[joyID][19] = (event.jhat.value & SDL_HAT_LEFT) != 0;
      }
      break;
    }
    case SDL_JOYBUTTONDOWN: {
      std::unique_lock<std::mutex> lock(joyButtonPressedMutex);
      int joyID = FindJoystickSlot(event.jbutton.which);
      Log8BitDo("BUTTON DOWN instance=%d slot=%d botao=%u",
                event.jbutton.which, joyID, event.jbutton.button);
      if (joyID >= 0 && event.jbutton.button < _JOYSTICK_MAXBUTTONS)
        joyButtonPressed[joyID][event.jbutton.button] = true;
      break;
    }
    case SDL_JOYBUTTONUP: {
      std::unique_lock<std::mutex> lock(joyButtonPressedMutex);
      int joyID = FindJoystickSlot(event.jbutton.which);
      Log8BitDo("BUTTON UP instance=%d slot=%d botao=%u",
                event.jbutton.which, joyID, event.jbutton.button);
      if (joyID >= 0 && event.jbutton.button < _JOYSTICK_MAXBUTTONS)
        joyButtonPressed[joyID][event.jbutton.button] = false;
      break;
    }
  }
}

int UserEventManager::FindJoystickSlot(SDL_JoystickID instance) const {
  for (int s = 0; s < _JOYSTICK_MAX; s++) {
    if (joystickInstanceNow[s] == instance)
      return s;
  }
  return -1;
}

int UserEventManager::FindFreeJoystickSlot() const {
  for (int s = 0; s < _JOYSTICK_MAX; s++) {
    if (joystickInstanceNow[s] == -1)
      return s;
  }
  return -1;
}

void UserEventManager::CompactJoystickSlots(int removedSlot) {
  for (int s = removedSlot; s + 1 < _JOYSTICK_MAX; s++) {
    joystick[s] = joystick[s + 1];
    joystickInstanceNow[s] = joystickInstanceNow[s + 1];
    for (int i = 0; i < _JOYSTICK_MAXBUTTONS; i++)
      joyButtonPressed[s][i] = joyButtonPressed[s + 1][i];
    for (int i = 0; i < _JOYSTICK_MAXAXES; i++) {
      joyAxis[s][i] = joyAxis[s + 1][i];
      joyAxisCalibration[s][i][0] = joyAxisCalibration[s + 1][i][0];
      joyAxisCalibration[s][i][1] = joyAxisCalibration[s + 1][i][1];
      joyAxisCalibration[s][i][2] = joyAxisCalibration[s + 1][i][2];
    }
  }
  int top = _JOYSTICK_MAX - 1;
  joystick[top] = nullptr;
  joystickInstanceNow[top] = -1;
  for (int i = 0; i < _JOYSTICK_MAXBUTTONS; i++)
    joyButtonPressed[top][i] = false;
  for (int i = 0; i < _JOYSTICK_MAXAXES; i++) {
    joyAxis[top][i] = 0.0;
    joyAxisCalibration[top][i][0] = -32768.0;
    joyAxisCalibration[top][i][1] = 32767.0;
    joyAxisCalibration[top][i][2] = 0.0;
  }
}

bool UserEventManager::SetJoystickRumble(int joyID, float low_frequency, float high_frequency,
                                         unsigned int duration_ms) {
  if (joyID >= 0 && joyID < _JOYSTICK_MAX && joystick[joyID]) {
#if SDL_VERSION_ATLEAST(2, 0, 9)
    return SDL_JoystickRumble(joystick[joyID], static_cast<Uint16>(low_frequency * 65535),
                              static_cast<Uint16>(high_frequency * 65535), duration_ms) == 0;
#else
    return false;
#endif
  }
  return false;
}

bool UserEventManager::GetKeyboardState(SDL_Keycode code) const {
  std::unique_lock<std::mutex> lock(keyPressedMutex);
  return keyPressed.count(code) > 0;
}

std::map<SDL_Keycode, TimedKeyPress> UserEventManager::GetKeyboardState() const {
  std::unique_lock<std::mutex> lock(keyPressedMutex);
  return keyPressed;
}

void UserEventManager::SetKeyboardState(SDL_Keycode key, bool newState) {
  std::unique_lock<std::mutex> lock(keyPressedMutex);
  if (!newState) {
    keyPressed.erase(key);
  } else {
    keyPressed[key].pressTime_ms = EnvironmentManager::GetInstance().GetTime_ms();
  }
}

unsigned long UserEventManager::GetLastKeyPressDiff_ms() {
  std::unique_lock<std::mutex> lock(keyPressedMutex);
  return EnvironmentManager::GetInstance().GetTime_ms() - lastKeyTime_ms;
}

unsigned long UserEventManager::GetLastKeyPressDiff_ms(SDL_Keycode key) {
  std::unique_lock<std::mutex> lock(keyPressedMutex);
  // find(), not operator[]: inserting a key released since the caller's
  // snapshot would leave it reading as held forever.
  auto it = keyPressed.find(key);
  if (it == keyPressed.end())
    return 0;
  return EnvironmentManager::GetInstance().GetTime_ms() - it->second.pressTime_ms;
}

void UserEventManager::ReleaseAllInputs() {
  {
    std::unique_lock<std::mutex> lock(keyPressedMutex);
    keyPressed.clear();
  }
  {
    std::unique_lock<std::mutex> lock(mousePressedMutex);
    for (int i = 0; i < 32; i++)
      mousePressed[i] = false;
  }
  std::unique_lock<std::mutex> lock(joyButtonPressedMutex);
  for (int j = 0; j < _JOYSTICK_MAX; j++) {
    for (int i = 0; i < _JOYSTICK_MAXBUTTONS; i++)
      joyButtonPressed[j][i] = false;
  }
}

bool UserEventManager::GetJoystickAxisInitialState(int joyID, int axisID, float* state) const {
  if (joyID < 0 || joyID >= _JOYSTICK_MAX || axisID < 0 || axisID >= _JOYSTICK_MAXAXES || !state)
    return false;
  std::unique_lock<std::mutex> lock(joyButtonPressedMutex);
  Sint16 initial = 0;
  if (!joystick[joyID] || !SDL_JoystickGetAxisInitialState(joystick[joyID], axisID, &initial))
    return false;
  *state = initial;
  return true;
}

bool UserEventManager::GetMouseButtonState(int sdlButtonID) const {
  if (sdlButtonID < 0 || sdlButtonID >= 32)
    return false;
  std::unique_lock<std::mutex> lock(mousePressedMutex);
  return mousePressed[sdlButtonID];
}

Vector3 UserEventManager::GetMouseRelativePos() const {
  Vector3 mousePos;
  mousePos.coords[2] = 0;
  int x, y;
  SDL_GetRelativeMouseState(&x, &y);
  mousePos.coords[0] = x;
  mousePos.coords[1] = y;
  return mousePos;
}

bool UserEventManager::GetJoyButtonState(int joyID, int sdlJoyButtonID) const {
  if (joyID < 0 || joyID >= _JOYSTICK_MAX || sdlJoyButtonID < 0 ||
      sdlJoyButtonID >= _JOYSTICK_MAXBUTTONS)
    return false;
  std::unique_lock<std::mutex> lock(joyButtonPressedMutex);
  return joyButtonPressed[joyID][sdlJoyButtonID];
}

void UserEventManager::SetJoyButtonState(int joyID, int sdlJoyButtonID, bool newState) {
  if (joyID < 0 || joyID >= _JOYSTICK_MAX || sdlJoyButtonID < 0 ||
      sdlJoyButtonID >= _JOYSTICK_MAXBUTTONS)
    return;
  std::unique_lock<std::mutex> lock(joyButtonPressedMutex);
  joyButtonPressed[joyID][sdlJoyButtonID] = newState;
}

float UserEventManager::GetJoystickAxis(int joyID, int axisID, bool deadzone) const {
  if (joyID < 0 || joyID >= _JOYSTICK_MAX || axisID < 0 || axisID >= _JOYSTICK_MAXAXES)
    return 0.0f;
  std::unique_lock<std::mutex> lock(joyButtonPressedMutex);

  float min = joyAxisCalibration[joyID][axisID][0];
  float max = joyAxisCalibration[joyID][axisID][1];
  float rest = joyAxisCalibration[joyID][axisID][2];

  float value = joyAxis[joyID][axisID];

  if (value < min)
    value = min;
  if (value > max)
    value = max;
  float scale = max - min;
  if (scale == 0.0)
    scale = 0.01;  // avoid division by zero, axis would be defunct though if scale evaluates to 0

  // bring value in range 0 .. 1
  value -= min;
  value /= scale;

  // bring rest in range 0 .. 1
  rest -= min;
  rest /= scale;

  // deadzone
  if (deadzone)
    if (fabs(rest - value) < 0.1)
      value = rest;

  if (value < rest) {
    // bring value in range 0 .. -1
    if (rest > 0.001f) {
      value /= rest;
      value -= 1.0;
    } else {
      value = -1.0f;
    }
  } else if (value > rest) {
    // bring value in range 0 .. 1
    scale = 1.0 - rest;
    if (scale > 0.001f) {
      value -= rest;
      value /= scale;
    } else {
      value = 1.0f;
    }
  } else {  // value == rest
    value = 0.0;
  }

  return value;
}

float UserEventManager::GetJoystickAxisRaw(int joyID, int axisID) const {
  if (joyID < 0 || joyID >= _JOYSTICK_MAX || axisID < 0 || axisID >= _JOYSTICK_MAXAXES)
    return 0.0f;
  std::unique_lock<std::mutex> lock(joyButtonPressedMutex);
  return joyAxis[joyID][axisID];
}

float UserEventManager::GetJoystickAxisCalibrationMin(int joyID, int axisID) {
  if (joyID < 0 || joyID >= _JOYSTICK_MAX || axisID < 0 || axisID >= _JOYSTICK_MAXAXES)
    return 0.0f;
  std::unique_lock<std::mutex> lock(joyButtonPressedMutex);
  return joyAxisCalibration[joyID][axisID][0];
}

float UserEventManager::GetJoystickAxisCalibrationMax(int joyID, int axisID) {
  if (joyID < 0 || joyID >= _JOYSTICK_MAX || axisID < 0 || axisID >= _JOYSTICK_MAXAXES)
    return 0.0f;
  std::unique_lock<std::mutex> lock(joyButtonPressedMutex);
  return joyAxisCalibration[joyID][axisID][1];
}

float UserEventManager::GetJoystickAxisCalibrationRest(int joyID, int axisID) {
  if (joyID < 0 || joyID >= _JOYSTICK_MAX || axisID < 0 || axisID >= _JOYSTICK_MAXAXES)
    return 0.0f;
  std::unique_lock<std::mutex> lock(joyButtonPressedMutex);
  return joyAxisCalibration[joyID][axisID][2];
}

void UserEventManager::SetJoystickAxisCalibration(int joyID, int axisID, float min, float max,
                                                  float rest) {
  if (joyID < 0 || joyID >= _JOYSTICK_MAX || axisID < 0 || axisID >= _JOYSTICK_MAXAXES)
    return;
  std::unique_lock<std::mutex> lock(joyButtonPressedMutex);
  joyAxisCalibration[joyID][axisID][0] = min;
  joyAxisCalibration[joyID][axisID][1] = max;
  joyAxisCalibration[joyID][axisID][2] = rest;

  // rest has to be within min/max range
  if (joyAxisCalibration[joyID][axisID][2] < joyAxisCalibration[joyID][axisID][0])
    joyAxisCalibration[joyID][axisID][2] = joyAxisCalibration[joyID][axisID][0];
  if (joyAxisCalibration[joyID][axisID][2] > joyAxisCalibration[joyID][axisID][1])
    joyAxisCalibration[joyID][axisID][2] = joyAxisCalibration[joyID][axisID][1];
  joyAxis[joyID][axisID] = rest;
}

}  // namespace blunted
