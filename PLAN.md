# XenonLive Launcher — the plan

The user-facing half of XenonLive: sign in, see friends, invite them, see
achievements, get notified, and launch a title. Everything the server and
`libxlive` already hold, with a window on it. Written 2026-09-11 at the end of
the session that wired phase 2 into the Case West port; this is what the next
session builds, here, in this directory.

Decisions already taken by the operator — do not re-ask:

- **Stack: C++20 + SDL2 + Dear ImGui**, `libxlive` linked statically, the same
  toolchain as the ports. One binary, no runtime dependencies. The in-game
  overlay comes later and reuses the same ImGui widgets inside the port's
  Vulkan swapchain, which is the reason for the choice.
- **Location: this directory**, a new repo, sibling of `~/GithubRepo/XenonLive`
  (server + library) and the two Dead Rising ports.
- **Presence is on by default** for anyone signed in (decided 2026-09-10).
- **An invitation from a friend is taken on the player's behalf** in the port
  when there is no launcher to ask; once the launcher exists it is the place
  that asks, and the port's auto-accept can then be turned off.

Read first, in this order:

1. `~/GithubRepo/XenonLive/client/include/xlive/client.h` — the only header a
   client includes. Everything social is already a call on `xlive::Client`.
2. `~/GithubRepo/XenonLive/docs/friends-and-invites.md` and `docs/client-library.md`.
3. `~/GithubRepo/Dead_Rising_2_Case_West_Xenon_Recomp/runtime/host/window.cpp`,
   `Host_RunLauncher()` (line ~1818) — the port's existing pre-boot settings
   window, an SDL2 window with a 5x7 font. That stays; it is per-port visual
   settings. This launcher is the account and social front end that runs
   *before* it and launches the port.

## What exists, what is missing

`libxlive` is title-centric: `Client::Start` refuses `title_id == 0`, syncs
one title's achievements, publishes presence for it, and keeps its cache under
one data directory that the game also uses. A launcher is a second client of
the same account with no title. So the library needs a small, additive
**launcher mode**, and that is the first piece of work — in the XenonLive repo,
not here.

The server needed one thing for two clients to coexist, and it is **done and
tested** (uncommitted in `~/GithubRepo/XenonLive`, 2026-09-11):
`gateway.Hub.PlayingTitle()`; a launcher connecting beside a running game no
longer overwrites "playing Case West" with "online", and the game quitting
while the launcher stays drops presence to "online" instead of leaving a stale
title. `server/internal/gateway/hub_test.go` covers it. Commit it with the
Case West/phase-2 work that is also sitting uncommitted there.

## Step 1 — libxlive launcher mode (in `~/GithubRepo/XenonLive/client`)

Additive changes to `include/xlive/client.h` and `src/client.cpp`. Keep the
library's two rules: no call blocks on the network; signed out, every read
returns what the ports return today.

1. **`Options::launcher = false`**. When true, `title_id` may be 0. In launcher
   mode:
   - `SyncTitle()` is skipped (no title to sync).
   - `SyncSocial()` skips the `GET /v1/me/titles/{id}/invite` read.
   - **Presence is never published** — `PublishPresence()` is a no-op. The
     launcher has nothing to say about what the player is doing; the server
     marks the connection "online" by itself and the game's word wins.
   - **The store opens `data_dir/launcher/`** but **`SessionFile()` stays at
     `data_dir/session.json`** — the file the game reads. Today `SessionFile()`
     is `store.dir()/session.json` when the store is open, which would put the
     launcher's tokens where no game looks. The launcher and the game must not
     share a store directory (queue and cache files, no locking) but must share
     the session file; this split is the whole point.
   - Gateway URL is `/v1/gateway?title_id=00000000` — the server already
     treats 0 as "a launcher" (`gateway_handler.go`).
2. **Sign-in, as tickets** (they cannot block the UI thread either):
   - `Ticket SignIn(const std::string& gamertag, const std::string& password);`
     → `POST /v1/auth/login` `{gamertag, password, device: "launcher"}`.
   - `Ticket Register(const std::string& gamertag, const std::string& password);`
     → `POST /v1/auth/register`, same body. The server answers **201**, and
     the gamertag must start with a letter and be ≤ 15 characters.
   - `void SignOut();` — clears tokens, deletes `session.json`, resets
     identity to the offline constants, drops the gateway, emits
     `SigninChanged`; queues `POST /v1/auth/logout` best-effort.
   - These go through the same op queue (`Impl::SessionOp`, new shape
     `kShapeAuth`), but **`EnqueueSessionOp` must not refuse them for
     `access_token.empty()`** — that check is exactly what a sign-in has to
     get past. On success: set both tokens, `SaveSessionTokens()`,
     `SyncIdentity()` (which emits `SigninChanged`), and let the worker's next
     pass open the gateway. Collect with the existing
     `Poll(Ticket, SocialResult&)`; a failed sign-in's `error` is the server's
     code (`bad_credentials`, `gamertag_taken`, …).
   - Bootstrap: if `Start()` finds no session file, a launcher must still
     `Start()` successfully and sit signed out with the worker running, so a
     later `SignIn` can use the same transport. Check `Start()`'s early
     returns for the signed-out case; the ports already rely on "no session is
     a success", so this should already hold.
3. **Achievements for the UI**:
   ```cpp
   struct Achievement { uint16_t id; std::string name, locked_description,
                        unlocked_description; uint32_t score; bool hidden;
                        bool unlocked; std::string unlocked_at; };
   struct TitleInfo { uint32_t title_id; std::string name;
                      uint32_t gamerscore, max_gamerscore;
                      std::vector<Achievement> achievements; };
   struct TitleResult { bool ok; std::string error; long http_status; TitleInfo title; };
   Ticket LoadTitle(uint32_t title_id);
   OpStatus Poll(Ticket, TitleResult&);
   ```
   Two GETs behind one ticket: `GET /v1/titles/{id}` (definitions, with
   `locked_description` / `unlocked_description` / `hidden` — see
   `server/internal/httpapi/title_handlers.go`, `achievementDef`) and
   `GET /v1/me/titles/{id}/achievements` (`unlocked`, `unlocked_at`,
   `gamerscore`, `max_gamerscore`). New shape `kShapeTitle`, and a
   `title_results` map beside `session_results` / `social_results`.
4. **The player's own presence**, so the launcher can invite a friend into the
   session the *game* is in: `GET /v1/profiles/{my xuid}/presence` — the
   store's `PresenceOf` lets a profile always see its own, and the answer
   carries `session_id` and `joinable`. Expose it as
   `Ticket LoadMyPresence(); OpStatus Poll(Ticket, Presence&)` or fold it into
   `SocialResult` (add a `Presence presence` field, shape `kShapePresence`).
   Then `SendInvite(session_id, xuid)` — which already exists — has a session
   to name. Refresh it when the friends screen opens; do not poll it.
5. **Tests**, in `client/tests/` with the existing `TEST()` harness
   (`harness.h`, `test_social.cpp` for the style). Unit: launcher mode with
   `title_id 0` starts, sign-in fails immediately when offline, `SessionFile()`
   lands in `data_dir` not `data_dir/launcher`. End to end (`e2e_` prefix,
   skipped without `XLIVE_TEST_SERVER`): register → sign out → sign in →
   identity comes back; `LoadTitle` for the imported test title; two clients
   on one account, one with a title and one without, and the friend sees
   "playing" not "online" after the launcher connects (this is the server
   change above, seen from the client). All three client build dirs
   (`build`, `build-asan`, `build-tsan`) must stay green; the dev-environment
   memory has the commands.

Do **not** put UI concerns in the library: no strings for the UI, no images,
no polling helpers. The launcher polls tickets once per frame; that is fine.

## Step 2 — this project

```
XenonLive_Launcher/
  CMakeLists.txt
  README.md
  thirdparty/imgui/          vendored Dear ImGui 1.91.9b (MIT): imgui*.cpp/h,
                             imstb_*.h, backends/imgui_impl_sdl2.*,
                             backends/imgui_impl_sdlrenderer2.*, LICENSE.txt
  src/
    main.cpp                 SDL2 window + SDL_Renderer, ImGui frame loop,
                             one xlive::Client in launcher mode
    app.h / app.cpp          screen state machine, event queue from the client
    config.h / config.cpp    launcher.json: server URL, titles
    launch.h / launch.cpp    spawn a title (posix fork/exec; CreateProcess on Windows)
    screens/
      signin.cpp             sign in / register
      home.cpp               titles + Play, the account card
      friends.cpp            list, presence, add / accept / remove / block, invite
      invites.cpp            inbox: accept (launches the title) / decline
      achievements.cpp       per title, from LoadTitle
    toasts.cpp               event notifications (friend online, invite, unlock)
  tools/
    fetch_imgui.sh           re-fetches the pinned tarball if thirdparty/ is ever lost
```

Vendor ImGui rather than FetchContent — the project's own argument ("a port
pins this checkout for a decade; code we own beats a module that may not
resolve in 2035", `docs/friends-and-invites.md`). The tarball is
`https://github.com/ocornut/imgui/archive/refs/tags/v1.91.9b.tar.gz`
(1.8 MB; fetching it worked from this machine on 2026-09-11). Only the files
listed above; not `examples/`, `docs/`, `misc/`. Use the **SDL_Renderer2**
backend, not OpenGL or Vulkan: nothing here needs a GPU API, and it is the
backend with the fewest ways to fail on a stranger's machine.

CMake, like the ports:

```cmake
cmake_minimum_required(VERSION 3.20)
project(xenonlive_launcher CXX)
set(CMAKE_CXX_STANDARD 20)
set(XLIVE_ROOT "$ENV{HOME}/GithubRepo/XenonLive" CACHE PATH "XenonLive source root")
add_subdirectory("${XLIVE_ROOT}/client" "${CMAKE_CURRENT_BINARY_DIR}/xlive")
find_package(SDL2 REQUIRED)           # 2.32 is on this machine; pkg-config also works
add_library(imgui STATIC thirdparty/imgui/imgui.cpp ... backends/imgui_impl_sdl2.cpp
            backends/imgui_impl_sdlrenderer2.cpp)
target_link_libraries(xenonlive_launcher PRIVATE xlive::client imgui SDL2::SDL2)
```

`libxlive` already needs libcurl for HTTPS (`XLIVE_WITH_CURL`), so the
launcher inherits that and nothing else.

### The client

One `xlive::Client` (construct it directly, not `Instance()`; the launcher is
a host that wants explicit lifetime), `Options{launcher = true, title_id = 0,
server_url = <from launcher.json or empty>}`, `on_event` pushing into a
mutex-guarded queue the UI thread drains each frame — **events arrive on the
library's worker thread**, never touch ImGui from there. `log` to stderr.

`data_dir` stays default (`UserDataDir()`, `~/.local/share/XenonLive`) so the
session file lands where the games look. `XLIVE_DATA_DIR` overrides both, for
testing with the two-account harness.

### Config: `launcher.json` in the data dir

```json
{
  "server": "http://127.0.0.1:18080",
  "allow_insecure": true,
  "titles": [
    { "title_id": "58410b00", "name": "Dead Rising 2: Case West",
      "exe": "/home/.../Dead_Rising_2_Case_West_Xenon_Recomp/runtime/build/cw_runtime",
      "cwd": "/home/.../runtime/build",
      "env": { "CW_XLIVE_ONLINE": "1", "CW_XLIVE_COOP": "1", "CW_LAUNCHER": "1" } }
  ]
}
```

The launcher does not discover titles; the player adds them (a file picker is
overkill for v1 — a text field for the exe path, and the title id read from
the port with `--diag` or typed). `server` written here is also what
`session.json` carries, so a game started outside the launcher still finds it.
`allow_insecure` sets `XLIVE_ALLOW_INSECURE=1` in the launched title's env and
in the launcher's own process env before `Start()` — the library reads the
env, not an option.

### Launching a title

`launch.cpp`: fork/exec with `cwd` and the merged env (Windows: `CreateProcessW`
with an env block). Keep the child's pid; poll `waitpid(WNOHANG)` each frame so
the home screen can show "running" and re-enable Play when it exits. Never
block the UI on the child. The launcher stays open while the game runs — that
is what keeps the player "online" between games and what the server's
last-connection logic was written for.

### Screens (one ImGui window, a left rail of tabs)

- **Sign in** — shown while `identity().state != SignedInToLive` and no ticket
  is pending. Server URL (from config, editable), gamertag, password, *Sign in*
  and *Register*. Poll the ticket every frame; show the server's error code as
  text ("bad_credentials", "gamertag_taken"). Remember nothing but the server
  URL; the library keeps the tokens.
- **Home** — account card (gamertag, gamerscore from `identity()`,
  `status()` line), the titles list with *Play*, and *Sign out*.
- **Friends** — `friends()` each frame (it is a cache read). Group into
  friends / requests received / requests sent, using `Relation`. Per friend:
  presence line (`presence.rich_text`, `title_name`, `joinable`), *Invite*
  (enabled when my own presence has a `session_id` — step 1.4), *Remove*,
  *Block*. Requests received: *Accept* (`AddFriend(xuid)` — asking back is
  accepting) / *Decline* (`RemoveFriend`). An *Add by gamertag* field →
  `AddFriendByGamertag`. Poll every ticket you issue and toast the failure
  code; never assume success.
- **Invites** — `invites()`. *Accept* → `AcceptInvite(id)`, then if the title
  is configured and not running, **launch it**: the game's own `libxlive`
  finds the accepted invitation at sync (`AcceptedInvite()`), and the Case
  West port answers `XInviteGetAcceptedInfo` from it. If the game is already
  running, nothing more to do: the server pushes `invite_taken` to it, the
  library raises `InviteAccepted`, and the port posts the notification
  (done 2026-09-11, XenonLive `948235a`/`d93fd17`, Case West `fb4ea9c`).
  While a launcher is connected the game never hears the raw `invite`, so
  the launcher IS the place the player is asked. *Decline* → `DeclineInvite`.
- **Achievements** — a title picker (from config), `LoadTitle` on open, then
  the list: name, the locked or unlocked description, score, unlocked date;
  hidden ones show "Secret achievement" until unlocked. No images in v1 — the
  server does not serve them; `tools/spa_import.py --extract-images DIR` can
  put them on disk per title later.
- **Toasts** — bottom-right, 4 s, from the event queue: `FriendsChanged`
  (diff the list to say who came online, like the port's
  `XliveSocial_OnFriendsChanged` does), `InviteReceived` ("X invited you to
  Case West" with an *Accept* button that does what the Invites tab does),
  `InviteAnswered`, `AchievementUnlocked` (only fires in a title client — the
  launcher will not see it unless the library forwards it; skip for v1),
  `ConnectionChanged`.

Font: ImGui's built-in ProggyClean has no accents or CJK. Gamertags are ASCII
(server-enforced), rich-presence strings come from the SPA and are English in
both Dead Rising titles, so v1 is fine with it; note it in the README and
leave a hook for loading a TTF.

### Not in v1, on purpose

Leaderboards (route exists: `GET /v1/titles/{id}/leaderboards/{viewId}`; a
tab later), gamerpics, achievement art, a friend's profile page, settings
beyond the server URL, the in-game overlay, Windows packaging. Each is a tab
or a file later; none changes the shape above.

## Step 3 — verify against a live server

`~/.claude/jobs/5a0c3163/tmp/` had a working `start_xlived.sh` and `social.py`
(register two friended accounts, watch one's friends list); that directory is
cleaned with the job, so rewrite them from the dev-environment memory if gone.
The facts that cost time last session:

- `XLIVE_JWT_SECRET` must be **hex**; run `xlived` on `:18080` against
  `postgres://xlive:xlive@127.0.0.1:55432/xlive?sslmode=disable` with
  `XLIVE_ADMIN_TOKEN=smoke-admin-token`; import Case West's SPA with
  `tools/spa_import.py <port>/assets/game/GAME1.spa --server … --token …`.
- The gateway refuses plain http without `XLIVE_ALLOW_INSECURE=1`.
- `pkill -x xlived` for a stray server, not `pkill -f`.
- `Client::Forget()` cancels a *queued* op; collect fire-and-forget tickets,
  do not forget them.

The acceptance run, two accounts A and B:

1. Launcher as A: register, sign out, sign in. `session.json` appears in
   `XLIVE_DATA_DIR`, `launcher/` beside it holds the launcher's own cache.
2. Add B by gamertag; B's launcher (second `XLIVE_DATA_DIR`) shows the request
   and accepts. Both lists show the other online.
3. A presses Play on Case West (`CW_XLIVE_ONLINE=1 CW_XLIVE_COOP=1`); B's list
   shows *playing Dead Rising Case West — Navigating the menus*; A's own
   launcher still says online, and A's list is unchanged. Quit the game: B
   sees A drop to online, not offline.
4. B hosts (for now over REST: `POST /v1/titles/58410b00/sessions`
   `{"flags":"00000041","public_slots":2}`) and invites A. A's launcher toasts;
   Accept launches Case West, whose log shows
   `XInviteGetAcceptedInfo: session … from …`.
5. Achievements tab for Case West lists 12 with descriptions; unlock one in
   game and re-open the tab.

Then the port work that follows: forward `XamShow*` (friends, invite,
gamercard blades) to the launcher over a local socket — `docs/roadmap.md`,
phase 2. (The port's auto-accept already confines itself to the no-launcher
case; the server routes the raw invitation away from the game whenever a
launcher is connected.)
