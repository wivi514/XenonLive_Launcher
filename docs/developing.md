# Developing the launcher

The player-facing README is at the root. This is the rest: how it is built
and released, how it is put together, and how to drive it with nobody at
the screen.

C++20, SDL2, Dear ImGui (vendored), `libxlive` linked statically. One
binary; the runtime dependencies are SDL2 and libcurl. The in-game overlay
reuses the same ImGui widgets inside the port's swapchain, which is why the
stack is what it is.

## Building

Needs the XenonLive checkout beside this one (`~/GithubRepo/XenonLive`, or
`-DXLIVE_ROOT=`), SDL2 development headers and libcurl.

    cmake -S . -B build -G Ninja -DCMAKE_CXX_COMPILER=clang++
    cmake --build build
    ./build/xenonlive_launcher

Dear ImGui 1.91.9b, miniz 3.0.2 (the zip reader for the Windows bundle),
zstd 1.5.7's single-file decoder (the Linux tarball) and stb_image.h 2.30
(the PNG decoder for the tiles) are vendored under `thirdparty/` rather than fetched, for the same reason the
ports vendor their dependencies: a checkout must still build in a decade. If
the directory is ever lost, `tools/fetch_thirdparty.sh` re-creates it from the
pinned archives and checks their hashes.

## Releases

    tools/release_linux.sh      # XenonLiveLauncher-linux-x86_64.tar.zst and .AppImage
    tools/release_windows.sh    # XenonLiveLauncher-windows-x86_64.zip

Both build in containers and need only podman. Linux builds on the ports'
old base (Ubuntu 22.04: glibc floor 2.34, the same SDL2 the ports ship) with
libcurl and OpenSSL linked statically — distributions disagree about libcurl's
symbol versioning, so a dynamic one linked on either side fails on the other
— and the system CA bundle found at run time. Windows is cross-compiled with
mingw-w64; its libcurl is static on Schannel, so the Windows certificate store
is the trust store and nothing TLS-related ships. The output lands in
`~/Release/XenonLive_Launcher/V<version>/` with a `SHA256SUMS`.

**Which game build a launcher installs follows how the launcher itself was
packaged**: the AppImage installs the games' AppImages, the tarball installs
their `.tar.zst`, Windows installs the zip. An install is refused when the
release's `SHA256SUMS` does not list the asset.

## What it does

A single window with a rail of tabs:

- **Sign in** — gamertag, password, *Sign in* or *Register*. That is all a
  player types: the launcher talks to `https://xenonlive.wivision.ca` and the
  games it starts follow it. A failure shows the server's own code
  (`bad_credentials`, `taken`). Above the form, the **saved accounts**: every
  account this machine has signed into, with *Use* (no password) and
  *Forget*. *Switch account* on the Home card does the same while signed in,
  and *Add another account* brings the form back without signing out. A
  self-hoster or a developer finds the server under *Advanced: server*.
- **Home** — the account card, and the **Games**: every port the launcher
  knows, with *Install*, *Update to vX*, *Play*, the release notes, and
  where to put your game. Releases come from the ports' GitHub release
  pages and are checked against the release's `SHA256SUMS` before they are
  put in place; a release without one is refused.
- **Friends** — the list, grouped into requests received, friends and requests
  sent, with presence ("playing Dead Rising Case West - Navigating the
  menus"). Add by gamertag, accept, decline, remove, block, and *Invite* —
  enabled once the game you are running is in a session, because an
  invitation names the session.
- **Messages** — the people you have written to (newest first, unread
  count on each), then the rest of your friends; the conversation as
  bubbles, yours in green; a box to write in with the count against the
  256-character limit (Enter sends, Ctrl+Enter breaks a line). The server
  keeps the last 20 messages between two people and that is what is shown.
  A message arriving is a notification with a *Reply* button — in the
  launcher and, through the overlay, in the game — and a badge on the
  Messages blade until it is read. *Message* on a friend's row and on their
  profile opens the conversation.
- **A friend's profile** — from their name or *Profile* on the Friends tab:
  gamerscore, country, member since, presence, the same Invite / Remove /
  Accept buttons, and every title with how far they are in it. *Compare
  achievements* on a title lists each achievement with their unlock date
  next to yours, filterable to "only what they have and you don't" and the
  reverse. The progress and the comparison are only there once they have
  accepted you as a friend — the server's presence rule, applied to what a
  player has done as well as where they are.
- **Invites** — the inbox. *Accept* launches the title if it is not running;
  if it is, the server tells the running game and it joins from there.
- **Achievements** — per installed title: the tile, name, the locked or
  unlocked description, score, unlock date. Locked tiles are dimmed; hidden
  ones read "Secret achievement" with no art until unlocked. The art is the
  SPA's own, served by the server and cached under `launcher/images/` since
  it never changes.
- **Account** — from the gamercard at the bottom of the rail (or *Account*
  on Home): the recovery email (add, change, remove — with the same promise
  as at registration), the two-letter country, and a password change that
  signs every other device out.
- **Issues** — bug reports. A port's capture key writes a screenshot, the
  log around the moment and the machine under `captures/` (the format is
  XenonLive's `docs/bug-reports.md`); this tab lists them, shows exactly
  what would go, and takes a title, what happened and how to reproduce
  before *Send* — or *Delete* if it was nothing. The same tab searches
  every report's words, with its state (open, fixed, closed), so a bug is
  found before it is reported twice. Only the developer ever sees the
  files and the machine.
- **Support** — a word about who makes this, a link to GitHub Sponsors,
  and the ports' repositories for bug reports and pull requests. The links
  are one table in `screens/support.cpp`; a new one is one line.
- **Toasts** — bottom right: a friend coming online or starting a game, a
  request, an invitation (with an *Accept* button), the connection changing.

The launcher stays open while the game runs. That is deliberate: it is what
keeps the player "online" between games, and the server's presence logic
counts on a player having a launcher and a game connected at once.

## The in-game overlay

`overlay/` is a second product of this repo: a static library a port links
to get a Steam-style overlay inside the game — **Shift+Tab** (or **Back+Start**
on a pad) opens a panel over the running title with the friends list
(presence, *Invite*), the invitation inbox (*Accept* / *Decline*), messages
(read a conversation, reply from the keyboard; a message arriving mid-game
is a toast with "Shift+Tab to reply"), the title's achievements (tile, name, description, score, unlock date; what you
have and what you have not, secret ones hidden until earned) and
notifications that show even while it is closed: an invitation arriving,
a friend coming online, an achievement unlocking.

It draws with Dear ImGui through the Vulkan backend straight onto the port's
swapchain image, right before present, using dynamic rendering — no render
pass or pipeline of the port's is touched — and reads everything from the
game's own `libxlive` client. Case West links it (`CW_XLIVE_OVERLAY`, on by
default when this checkout is beside the port); the port's side is a dozen
lines: forward SDL events and libxlive events, gate the game's input while
the overlay is open, one call in the swapchain blit. `CW_XLIVE_OVERLAY=0`
turns it off at runtime.

Accepting an invitation in the overlay tells the server, which tells the same
game (`invite_taken`), and the port joins exactly as it does when the launcher
accepted — with the overlay built, the port no longer takes an invitation on
the player's behalf.

## Games

The launcher installs the XenonRecomp ports from their public releases:

| Game | Title id | Release page |
|---|---|---|
| Dead Rising 2: Case Zero | `58410a8d` | github.com/wivi514/Dead_Rising_2_Case_Zero_Xenon_Recomp |
| Dead Rising 2: Case West | `58410b00` | github.com/wivi514/Dead_Rising_2_Case_West_Xenon_Recomp |

**It ships no game data.** Each port needs your own copy of the game — the
XBLA package your Xbox 360 downloaded — and the port's first run turns it
into everything else. After *Install*, the card says where to put it:
`<install dir>/assets/package/`. Dropping it onto the game's own window after
*Play* works too; that is the port's own installer.

A launcher running as an **AppImage** installs the port's AppImage — one
file, which the port treats as sitting beside its data root — so an *Update*
replaces that one file and never touches `assets/`. A launcher from the
**tarball** unpacks the port's `.tar.zst`, and Windows the zip, over the
previous version without deleting anything, so the unpacked game and the
shader cache survive. Installed games live under
`~/.local/share/XenonLive/games/<game>/` (Windows:
`%LOCALAPPDATA%\XenonLive\games\`); `"games_dir"` in `launcher.json` moves
them. A machine without FUSE gets `APPIMAGE_EXTRACT_AND_RUN=1`, which the
AppImage runtime honours.

The launcher checks every catalog game against GitHub's latest release when
it starts and every five minutes after; there is no button. A newer release
than the installed one is announced once, with *Update to …* on the card.
The launcher's own release is checked in the same round: a release build
(the scripts stamp `XL_VERSION`; a tree build says `dev` and stays out of
it) shows a banner on Home when GitHub's latest differs, and *Update and
restart* downloads the new one beside this one, verifies it against the
release's `SHA256SUMS`, swaps it in and restarts. An AppImage is renamed
over `$APPIMAGE`; a tarball install has each file renamed over its old
self; on Windows a script waits for the .exe to exit, moves the files
over and starts the new launcher. The launcher's directory has to be
writable for any of it. That is one unauthenticated request per game (and
one for the launcher) per check — 36 an hour, under GitHub's 60 an hour
per address. A check that fails shows
one amber line on Home and tries again on the next round, never a toast.

A build you point at by hand — a dev tree, say — still works: a `titles` entry
in `launcher.json` with `exe`/`cwd`/`env` and no `key` shows up under *Builds
from launcher.json* with a *Play* button, and the launcher leaves it alone.

## Where things live

Everything but the games is in the XenonLive data directory —
`~/.config/XenonLive` on Linux, `%APPDATA%\XenonLive` on Windows, `~/Library/Application
Support/XenonLive` on macOS; `XLIVE_DATA_DIR` overrides it (and puts the games
under `<that>/games`):

| File | What |
|---|---|
| `session.json` | the tokens and the server, written by the launcher and read by every game |
| `launcher.json` | this launcher's config: server, installed games |
| `launcher/` | the launcher's own cache, kept apart from a game's files in the same directory |
| `launcher/accounts/<xuid>.json` | one saved account each: gamertag, server, tokens |
| `launcher/images/<title>/` | the title's tiles, fetched once |

Saved accounts are how one machine holds several gamertags. The game only
ever reads `session.json`, so switching is: the active account's latest
tokens are saved (the library rotates them whenever it refreshes, and the
saved copy follows), the chosen account's tokens are written over
`session.json`, and the client restarts as that account. The server is not
told — a switch is not a sign-out, and the account you left stays signed in
and usable. *Sign out* does tell the server, which revokes that account's
tokens; the entry keeps its name and asks for the password next time.
*Forget* removes the entry. A switch is refused while a game started from
the launcher is running: that game holds the current account's session and
would write its refreshed tokens back over the new one.

`launcher.json`, as the launcher writes it after installing Case West:

```json
{
  "server": "https://xenonlive.wivision.ca",
  "allow_insecure": false,
  "titles": [
    {
      "key": "case_west",
      "version": "v1.0.1",
      "title_id": "58410b00",
      "name": "Dead Rising 2: Case West",
      "exe": "/home/you/.local/share/XenonLive/games/case_west/CaseWestRecomp-linux-x86_64.AppImage",
      "cwd": "/home/you/.local/share/XenonLive/games/case_west",
      "env": { "CW_XLIVE_ONLINE": "1", "CW_XLIVE_COOP": "1" }
    }
  ]
}
```

`server` defaults to the public server, `https://xenonlive.wivision.ca`,
whose certificate the system trust store already accepts. `allow_insecure`
sets `XLIVE_ALLOW_INSECURE=1` for the launcher and for every title it starts
— plain http, no certificate check — and is for a local development server
only (`"server": "http://127.0.0.1:18080", "allow_insecure": true`).

A title is started with the launcher's environment plus its `env` on top. The
shipped `cw_defaults.env` / `cz_defaults.env` already turn the renderer and
the port's pre-boot settings window on; the launcher adds the XenonLive half
(`*_XLIVE_ONLINE=1` to be told it is signed in, `*_XLIVE_COOP=1` for sessions
and invitations). Releases published before the ports' `xlive-integration`
branch shipped ignore both and simply play offline.

## Look and font

Dark grey and black with the 360's green: a rail of blades on the left (the
selected one a green blade with a lime edge, the invite count as a badge),
the gamercard tile at its foot, lime section headings, and notifications
with the lime edge the console's own had. `common/theme.{h,cpp}` holds the
palette, the style and those widgets, and the in-game overlay draws with
the same file, so a player sees one thing in the launcher and the same thing
over the game.

The face is **Selawik** (Microsoft, SIL Open Font License), an open font
with Segoe UI's metrics — what the 360 dashboard was set in — vendored under
`thirdparty/selawik/` and compiled into the binary as bytes, so nothing is
looked up at run time. Regular for the body, semibold for headings and the
wordmark. It covers Latin with accents; `"font_path"` and `"font_size"` in
`launcher.json` swap in another TTF (for CJK, say).

## Driving it from a shell

A few environment variables exist for running the launcher with nobody at the
screen (the acceptance run in `PLAN.md` uses them):

| Variable | Effect |
|---|---|
| `XENONLIVE_SCREENSHOT=file.bmp` | saves the window after two seconds and keeps going |
| `XENONLIVE_TAB=home\|friends\|messages\|invites\|achievements\|issues\|support\|account` | the starting tab |
| `XENONLIVE_PLAY=1` | presses Play on the first title once signed in |
| `XENONLIVE_ACCEPT=1` | presses Accept on the first invitation in the inbox |
| `XENONLIVE_INSTALL=case_west` | presses Install on that game |
| `XENONLIVE_SWITCH=<xuid hex>` | presses Use on that saved account |
| `XENONLIVE_PROFILE=<gamertag>` | opens that friend's profile and presses Compare on the first title |
| `XENONLIVE_MESSAGE=<gamertag>` | opens the conversation with that friend |
| `XENONLIVE_SAY=<text>` | then sends that message |
| `XENONLIVE_SCREENSHOT_MS=5000` | takes the screenshot later than two seconds |
| `XENONLIVE_SIGNIN=register\|forgot` | opens that sign-in form |
| `XENONLIVE_FORGOT=<gamertag>` | asks for a recovery code |
| `XENONLIVE_RECOVER=<gamertag>:<code>:<password>` | sets a new password with a mailed code |
| `XENONLIVE_SET_EMAIL=<addr>` | saves that recovery email from the Account screen (`""` removes it) |
| `XENONLIVE_CAPTURE=<n>` | selects the n-th capture on Issues |
| `XENONLIVE_ISSUE_SEND="title\|what\|steps"` | fills the form on the selected capture and sends it |
| `XENONLIVE_ISSUE_SEARCH=<words>` | searches the reports and selects the first hit |
| `XENONLIVE_FAKE_LAUNCHER_UPDATE=<tag>` | shows the launcher-update banner without asking GitHub |
| `XENONLIVE_APPLY_UPDATE=1` | applies whatever is staged in `.xenonlive-update/` beside the binary, quits, relaunches |

With `SDL_VIDEODRIVER=offscreen` the whole thing runs without a display.

## Not in v1, on purpose

Leaderboards, gamerpics, the Steam Deck tarball (the AppImage runs there
too). Each is a tab or a file later; none changes the shape of what is
here. The Windows build is cross-compiled by `tools/release_windows.sh` and
has been run under wine, not on Windows; the Windows half of the
self-update (`src/selfupdate.cpp`, the `.cmd` that swaps the files) has
not been exercised at all.

## Layout

```
CMakeLists.txt
overlay/                 the in-game overlay library (xlive::overlay), linked by the ports
thirdparty/imgui/        Dear ImGui 1.91.9b, the files this build uses (SDL_Renderer2 and Vulkan backends)
thirdparty/miniz/        miniz 3.0.2
thirdparty/stb/          stb_image.h 2.30
thirdparty/zstd/         zstd 1.5.7, decoder only
tools/release_linux.sh   the Linux release, on the ports' old base
tools/release_windows.sh the Windows release, cross-compiled with mingw-w64
src/
  main.cpp               SDL2 window + SDL_Renderer, the ImGui frame loop
  app.h / app.cpp        the client, its event queue, the tickets, the game
  accounts.h / .cpp      saved accounts and the session.json switch
  images.h / .cpp        achievement and title tiles: fetch, cache, decode
  config.h / config.cpp  launcher.json
  catalog.h / .cpp       the games the launcher can install
  installer.h / .cpp     GitHub release lookup, download, SHA-256 check, install
  selfupdate.h / .cpp    swapping the running launcher for a downloaded one
  captures.h / .cpp      the captures a port leaves for the Issues tab
  archive.h / .cpp       unpacking the Windows zip
  sha256.h / .cpp        SHA-256
  launch.h / launch.cpp  starting a title and noticing it stop
  toasts.h / toasts.cpp  notifications
  screens/               one function per tab (profile.cpp the friend page, messages.cpp the inbox)
tools/fetch_thirdparty.sh  re-vendors ImGui and miniz
PLAN.md                  what this was built from
```
