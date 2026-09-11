# XenonLive Launcher

The user-facing half of [XenonLive](https://github.com/wivi514/XenonLive):
sign in, see friends and where they are, invite them, read achievements, get
notified, and launch a title. Everything the server and `libxlive` already
hold, with a window on it.

C++20, SDL2, Dear ImGui (vendored), `libxlive` linked statically. One binary;
the runtime dependencies are SDL2 and libcurl. The in-game overlay planned for
the ports will reuse these ImGui widgets inside the port's swapchain, which is
why the stack is what it is.

## Building

Needs the XenonLive checkout beside this one (`~/GithubRepo/XenonLive`, or
`-DXLIVE_ROOT=`), SDL2 development headers and libcurl.

    cmake -S . -B build -G Ninja -DCMAKE_CXX_COMPILER=clang++
    cmake --build build
    ./build/xenonlive_launcher

Dear ImGui 1.91.9b is vendored under `thirdparty/imgui/` rather than fetched,
for the same reason the ports vendor their dependencies: a checkout must still
build in a decade. If the directory is ever lost, `tools/fetch_imgui.sh`
re-creates it from the pinned tarball and checks its hash.

## What it does

A single window with a rail of tabs:

- **Sign in** — gamertag, password, *Sign in* or *Register*, and the server
  URL. A failure shows the server's own code (`bad_credentials`, `taken`).
  Nothing is remembered but the server; `libxlive` keeps the tokens.
- **Home** — the account card, the titles with *Play*, and the form that adds
  a title (name, title id, executable, working directory, environment).
- **Friends** — the list, grouped into requests received, friends and requests
  sent, with presence ("playing Dead Rising Case West - Navigating the
  menus"). Add by gamertag, accept, decline, remove, block, and *Invite* —
  enabled once the game you are running is in a session, because an
  invitation names the session.
- **Invites** — the inbox. *Accept* launches the title if it is not running;
  if it is, the server tells the running game and it joins from there.
- **Achievements** — per configured title: name, the locked or unlocked
  description, score, unlock date. Hidden ones read "Secret achievement" until
  unlocked.
- **Toasts** — bottom right: a friend coming online or starting a game, a
  request, an invitation (with an *Accept* button), the connection changing.

The launcher stays open while the game runs. That is deliberate: it is what
keeps the player "online" between games, and the server's presence logic
counts on a player having a launcher and a game connected at once.

## Where things live

Everything is in the XenonLive data directory — `~/.config/XenonLive` on
Linux, `%APPDATA%\XenonLive` on Windows, `~/Library/Application
Support/XenonLive` on macOS; `XLIVE_DATA_DIR` overrides it:

| File | What |
|---|---|
| `session.json` | the tokens and the server, written by the launcher and read by every game |
| `launcher.json` | this launcher's config: server, titles |
| `launcher/` | the launcher's own cache, kept apart from a game's files in the same directory |

`launcher.json`:

```json
{
  "server": "http://127.0.0.1:18080",
  "allow_insecure": true,
  "titles": [
    {
      "title_id": "58410b00",
      "name": "Dead Rising 2: Case West",
      "exe": "/home/you/GithubRepo/Dead_Rising_2_Case_West_Xenon_Recomp/runtime/build/cw_runtime",
      "cwd": "/home/you/GithubRepo/Dead_Rising_2_Case_West_Xenon_Recomp/runtime/build",
      "env": { "CW_VKDRAW": "1", "CW_LAUNCHER": "1",
               "CW_XLIVE_ONLINE": "1", "CW_XLIVE_COOP": "1" }
    }
  ]
}
```

The launcher does not discover titles; you add them, on the Home tab or in
this file. The title id is what `cw_runtime --diag` prints. `allow_insecure`
sets `XLIVE_ALLOW_INSECURE=1` for the launcher and for every title it starts —
plain http, no certificate check — and is for a local development server only.

A title is started with the launcher's environment plus its `env` on top. A
dev build of Case West needs `CW_VKDRAW=1` to draw anything (only the shipped
`cw_defaults.env` sets it), `CW_XLIVE_ONLINE=1` to be told it is signed in,
and `CW_XLIVE_COOP=1` for sessions and invitations.

## Font

ImGui's built-in ProggyClean has no accents or CJK. Gamertags are ASCII
(server-enforced) and both Dead Rising titles' presence strings are English,
so v1 is fine with it. `"font_path"` and `"font_size"` in `launcher.json` load
a TTF instead.

## Driving it from a shell

Three environment variables exist for running the launcher with nobody at the
screen (the acceptance run in `PLAN.md` uses them):

| Variable | Effect |
|---|---|
| `XENONLIVE_SCREENSHOT=file.bmp` | saves the window after two seconds and keeps going |
| `XENONLIVE_TAB=home\|friends\|invites\|achievements` | the starting tab |
| `XENONLIVE_PLAY=1` | presses Play on the first title once signed in |
| `XENONLIVE_ACCEPT=1` | presses Accept on the first invitation in the inbox |

With `SDL_VIDEODRIVER=offscreen` the whole thing runs without a display.

## Not in v1, on purpose

Leaderboards, gamerpics, achievement art, a friend's profile page, settings
beyond the server URL, the in-game overlay, Windows packaging. Each is a tab
or a file later; none changes the shape of what is here. `launch.cpp` has the
`CreateProcessW` path already, but it has not been built on Windows.

## Layout

```
CMakeLists.txt
thirdparty/imgui/        Dear ImGui 1.91.9b, the files this build uses
src/
  main.cpp               SDL2 window + SDL_Renderer, the ImGui frame loop
  app.h / app.cpp        the client, its event queue, the tickets, the game
  config.h / config.cpp  launcher.json
  launch.h / launch.cpp  starting a title and noticing it stop
  toasts.h / toasts.cpp  notifications
  screens/               one function per tab
tools/fetch_imgui.sh     re-vendors ImGui
PLAN.md                  what this was built from
```
