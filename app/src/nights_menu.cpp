// nights-nx (2026-11-22, the user: "turn on the in game menu and add rumble as an option from it"): NiGHTS's rows on the
// base's game menu (blur-nx's, app/src/game_menu.*: opened with input_menu_combo, A+B+X+Y+L+R by default, F8 on the
// PC; closed with + / Escape), through its game hook (SetGameMenuRows; banjo-nx-ult's src/banjo_menu.cpp's form).
//
// The base's rows stay but for these changes:
// - Rumble (input_rumble, the base's port 59): the row is taken out (2026-11-23, the user: "remove rumble from the
//   settings please since the game doesn't use it"). NiGHTS never asks for vibration in what was played (no
//   XInputSetState call), so the row did nothing; input_rumble stays off in the compiled-in settings
//   (app/nights_switch_defaults.toml). From 2026-11-22 until then it was here with the default Off.
// - The frame rate: the base's frame_cap row out (NiGHTS paces each frame at the game's own presentation interval,
//   src/nights_frame_hooks.cpp), NiGHTS's own nights_true60 in its place (30 fps: the camera paced evenly, each frame
//   one whole step; 60 fps, the default since 2026-11-23: one logic step a frame), read at the start (*): written for
//   the next one. The game's
//   button-sequence codes register at 60 fps without a setting of their own (the user, 2026-11-22: "get rid of button
//   codes at 60fps just hardcode it on when 60fps is turned on"; src/nights_frame_hooks.cpp).
// The texts are in the menu's twelve languages (game_menu_text.h's order).
#include "game_menu.h"
#include "game_menu_text.h"

#include <rex/cvar.h>
#include <rex/logging.h>

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

REXCVAR_DEFINE_BOOL(nights_menu_check, false, "NiGHTS/Diagnostics",
                    "At start, the game menu's Settings rows built in every language without the UI: each row's "
                    "choices, default and the value the game runs with (\"[menu check]\" lines)");

namespace nights::menu {
namespace {

using Setting = blur::GameMenuDialog::Setting;
using Choice = blur::GameMenuDialog::Choice;

enum NightsText : int { L_FRAMERATE, H_FRAMERATE, kNightsTextCount };

// English, French, German, Italian, Spanish, Polish, Russian, Portuguese, Japanese, Korean, Chinese (Simplified),
// Chinese (Traditional): game_menu_text.h's rows.
const char* const kText[blur::kMenuLanguages][kNightsTextCount] = {
    {"Frame rate",
     "30 fps: the game's own rate, each frame one whole step, the camera paced evenly. 60 fps: one step of the game a "
     "frame; its speed is the same."},
    {"Fréquence d'images",
     "30 fps : la cadence du jeu, chaque image une étape entière, la caméra régulière. 60 fps : une étape du jeu par "
     "image ; sa vitesse reste la même."},
    {"Bildrate",
     "30 fps: die eigene Bildrate des Spiels, jedes Bild ein ganzer Schritt, die Kamera gleichmäßig. 60 fps: ein "
     "Spielschritt pro Bild; das Tempo bleibt gleich."},
    {"Frequenza fotogrammi",
     "30 fps: la frequenza del gioco, ogni fotogramma un passo intero, la telecamera regolare. 60 fps: un passo di "
     "gioco per fotogramma; la velocità resta la stessa."},
    {"Velocidad de fotogramas",
     "30 fps: la velocidad propia del juego, cada fotograma un paso entero, la cámara uniforme. 60 fps: un paso del "
     "juego por fotograma; su velocidad es la misma."},
    {"Liczba klatek",
     "30 fps: własne tempo gry, każda klatka to cały krok, płynna kamera. 60 fps: jeden krok gry na klatkę; szybkość "
     "gry bez zmian."},
    {"Частота кадров",
     "30 fps: собственная частота игры, каждый кадр — целый шаг, камера движется ровно. 60 fps: один шаг игры за "
     "кадр; скорость та же."},
    {"Taxa de quadros",
     "30 fps: a taxa do próprio jogo, cada quadro um passo inteiro, a câmera regular. 60 fps: um passo do jogo por "
     "quadro; a velocidade é a mesma."},
    {"フレームレート",
     "30 fps：ゲーム本来のフレームレート。1フレームごとに1ステップ全体を表示し、カメラが滑らかに動きます。60 fps：1フレーム"
     "につき1ステップ。ゲームの速度は変わりません。"},
    {"프레임 레이트",
     "30 fps: 게임 본래의 프레임 레이트로, 매 프레임이 한 단계 전체이며 카메라가 고르게 움직입니다. 60 fps: 프레임마다 게임 "
     "한 단계, 게임 속도는 같습니다."},
    {"帧率",
     "30 fps：游戏本身的帧率，每帧为一个完整步骤，镜头平稳。60 fps：每帧一个游戏步骤，游戏速度不变。"},
    {"幀率",
     "30 fps：遊戲本身的幀率，每幀為一個完整步驟，鏡頭平穩。60 fps：每幀一個遊戲步驟，遊戲速度不變。"},
};

int g_row = 0;
std::string Text(int id) { return kText[g_row][id]; }

void NightsRows(std::vector<Setting>& s, int language_row, const std::string& /*file*/) {
  g_row = language_row >= 0 && language_row < blur::kMenuLanguages ? language_row : 0;
  // Rumble out (2026-11-23, the user: "remove rumble from the settings please since the game doesn't use it": NiGHTS
  // never calls XInputSetState in what was played, docs/nights-ultimate-notes.md "Rumble"); input_rumble stays off in
  // the compiled-in settings. The docked resolution's default is NiGHTS's 1920x1080 (1.0.2's), not the base's 1728x972.
  s.erase(std::remove_if(s.begin(), s.end(), [](const Setting& r) { return r.key == "input_rumble"; }), s.end());
  for (Setting& r : s) {
    if (r.key == "banjo_renut_render_height_docked")
      for (std::size_t i = 0; i < r.choices.size(); ++i)
        if (r.choices[i].raw == "1080")
          r.default_choice = int(i);
  }
  // The frame rate: frame_cap's place (after the language), or the end.
  std::size_t at = s.size();
  for (std::size_t i = 0; i < s.size(); ++i)
    if (s[i].key == "frame_cap") {
      s.erase(s.begin() + std::ptrdiff_t(i));
      at = i;
      break;
    }
  if (at == s.size())
    for (std::size_t i = 0; i < s.size(); ++i)
      if (s[i].key == "language")
        at = i + 1;
  std::vector<Setting> rows;
  if (rex::cvar::GetFlagInfo("nights_true60")) {
    Setting r;
    r.label = Text(L_FRAMERATE);
    r.key = "nights_true60";
    r.restart = true;
    r.choices = {Choice{"30 fps", "false", "false"}, Choice{"60 fps", "true", "true"}};
    r.default_choice = 1;  // 2026-11-23 (the user): 60 fps by default, 30 the option
    r.help = Text(H_FRAMERATE);
    rows.push_back(std::move(r));
  }
  s.insert(s.begin() + std::ptrdiff_t(at), rows.begin(), rows.end());
}

// nights_menu_check: the Settings tab's rows built in every language without the UI (blur::GameMenuSettingsRows, the
// game hook included), each logged with its choices, its default and the value the game runs with now.
void CheckMenuRows() {
  int problems = 0;
  for (int lang = 0; lang < blur::kMenuLanguages; ++lang) {
    const std::vector<Setting> rows = blur::GameMenuSettingsRows(lang, "renut.toml");
    std::string order;
    for (const Setting& r : rows) {
      if (r.label.empty() || r.help.empty()) {
        ++problems;
        REXLOG_WARN("[menu check] language row {}: {} has no {}", lang, r.key, r.label.empty() ? "label" : "help");
      }
      order += (order.empty() ? "" : " | ") + r.label + (r.restart ? " *" : "");
    }
    if (lang != 0) {
      REXLOG_INFO("[menu check] language row {}: {} rows: {}", lang, rows.size(), order);
      continue;
    }
    for (const Setting& r : rows) {
      const std::string now = blur::GameMenuCurrentRaw(r);
      std::string choices;
      int shown = -1;
      for (std::size_t i = 0; i < r.choices.size(); ++i) {
        choices += (choices.empty() ? "" : ", ") + r.choices[i].label + "=" + r.choices[i].raw;
        if (r.choices[i].raw == now)
          shown = int(i);
      }
      REXLOG_INFO("[menu check] {}{} ({}): now {} ({}), default {}, {}{}: {}", r.label, r.restart ? " *" : "", r.key,
                  now, shown >= 0 ? r.choices[std::size_t(shown)].label : std::string("not one of the choices"),
                  r.default_choice >= 0 && std::size_t(r.default_choice) < r.choices.size()
                      ? r.choices[std::size_t(r.default_choice)].label
                      : std::string("?"),
                  r.always_in_file ? "always in the file" : "in the file when not the default",
                  r.apply_on_close ? ", taken when the menu closes" : "", choices);
    }
  }
  REXLOG_INFO("[menu check] {} language rows checked: {}", blur::kMenuLanguages,
              problems ? std::to_string(problems) + " problems (above)" : std::string("every text present"));
}

}  // namespace

void InstallMenuRows() {
  blur::SetGameMenuRows(&NightsRows);
  if (REXCVAR_GET(nights_menu_check))
    CheckMenuRows();
}

}  // namespace nights::menu
