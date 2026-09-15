# XenonLive Launcher

Xbox Live for the Dead Rising 2 PC ports — *Case Zero* and *Case West*,
rebuilt from the Xbox 360 games by [XenonRecomp](https://github.com/wivi514).
One account, a friends list, invitations into co-op, achievements and
gamerscore, messages, all of it in the launcher and over the game while you
play. It talks to [XenonLive](https://github.com/wivi514/XenonLive), a small
server run for these games.

**You need your own copy of each game.** The launcher installs the ports
(the programs that run the games) but ships no game data: each port asks
for the XBLA package your Xbox 360 downloaded, and turns it into a playable
game on first run.

Built by one developer, with AI assistance. Free. No ads, no telemetry.

## Get started

**1. Download** the launcher for your system from the
[releases page](https://github.com/wivi514/XenonLive_Launcher/releases/latest):

| System | File | Then |
|---|---|---|
| Windows 10/11 | `XenonLiveLauncher-windows-x86_64.zip` | unzip anywhere, run `xenonlive_launcher.exe` |
| Linux | `XenonLiveLauncher-linux-x86_64.AppImage` | `chmod +x` it, run it (needs FUSE, like any AppImage) |
| Linux, no FUSE | `XenonLiveLauncher-linux-x86_64.tar.zst` | unpack, run `XenonLiveLauncher/xenonlive_launcher` |
| Steam Deck | `XenonLiveLauncher-steamdeck-x86_64.tar.gz` | unpack, add `xenonlive_launcher` as a non-Steam game (below) |

**On a Steam Deck**: in Desktop mode, unpack the Steam Deck tarball
somewhere on the internal drive, add `XenonLiveLauncher/xenonlive_launcher`
to Steam as a non-Steam game; it then runs from Game mode, full screen,
with a larger UI, and the whole launcher works from the pad — the stick or
d-pad moves, **A** picks, **B** goes back, **LB/RB** switch between the
tabs on the left. Typing (a gamertag, a password, a message) uses the
Deck's keyboard: **Steam + X**. The games' overlay works the same way
(**View+Menu** opens it, LB/RB switch its tabs).

**Requirements**: Windows 10/11 64-bit, or Linux x86_64 (glibc 2.34 or
newer — anything from 2022 on; SteamOS 3 included). The games themselves
need a Vulkan-capable GPU; their pages say more.

Windows may show a SmartScreen warning the first time ("unknown publisher")
— *More info → Run anyway*. The launcher is not signed; the `SHA256SUMS`
file on the release page lets you check what you downloaded.

**2. Create an account.** A gamertag (starts with a letter, up to 15
letters, digits and single spaces — the 360's rule) and a password of at
least 8 characters. That is the whole account.

An **email is optional**. It exists for one thing: getting back in if you
forget your password. Nothing else is ever sent to it — no news, no
updates — and no other player ever sees it. **Without one, a forgotten
password means the account is lost**; the launcher says so on the form.
You can add or remove it later on the Account screen.

**3. Install a game.** On Home, press *Install* on Case Zero or Case West.
The launcher downloads the port's latest release from GitHub, checks it
against the release's checksums, and unpacks it. The card then shows a
folder path: **put your game's XBLA package in it**, or press *Play* and
drop the package onto the game's own window. The port unpacks the game on
its first run, once.

Each launcher installs the games built for it: the Windows launcher the
Windows zip, the AppImage the games' AppImages, the tarball the `.tar.zst`
builds, the Steam Deck tarball the games' Steam Deck builds.

**4. Play.** *Play* starts the game signed in as you. Leave the launcher
open while you play: that is what keeps you online for your friends.

## The games

| Game | Xbox 360 title id | The port |
|---|---|---|
| **Dead Rising 2: Case Zero** (2010, XBLA) | `58410a8d` | [Dead_Rising_2_Case_Zero_Xenon_Recomp](https://github.com/wivi514/Dead_Rising_2_Case_Zero_Xenon_Recomp) — [releases](https://github.com/wivi514/Dead_Rising_2_Case_Zero_Xenon_Recomp/releases) |
| **Dead Rising 2: Case West** (2010, XBLA) | `58410b00` | [Dead_Rising_2_Case_West_Xenon_Recomp](https://github.com/wivi514/Dead_Rising_2_Case_West_Xenon_Recomp) — [releases](https://github.com/wivi514/Dead_Rising_2_Case_West_Xenon_Recomp/releases) |

Both are native PC builds of the Xbox 360 games, made with XenonRecomp:
the 360 code recompiled, the graphics and sound re-done for PC, the Xbox
Live parts pointed at XenonLive. Each port's page has its own README —
what it needs, what works, what does not — and is where its bugs are
tracked. The launcher installs whichever release the port's page marks as
latest (v1.1.0 or newer has the XenonLive features); the port's *Release
notes* button on Home opens it.

What XenonLive brings to each: signing in with your gamertag, achievements
and gamerscore that persist, friends and where they are, invitations and
online co-op (Case West), messages, and the in-game overlay.

## Languages

The launcher speaks the six languages the games do: English, French,
Spanish, Italian, Japanese and Korean. It starts in your system's language
and the picker is on the sign-in screen and under Account. Japanese and
Korean need nothing installed — the launcher carries its own glyphs.

Translations are one table, `common/strings.tsv` (one column per language,
English as the key); a correction or a new language is a pull request
against that file, and a draft can be tried without a rebuild by putting a
`<code>.tsv` under `launcher/lang/` in the XenonLive data folder. The
in-game overlay follows the launcher's language.

## In the launcher

- **Home** — your games: install, update, play, release notes. Updates are
  found by themselves — the launcher checks GitHub when it starts and every
  five minutes — and offered once, never forced.
- **Friends** — add someone by gamertag; accept, decline, remove, block.
  You see where a friend is ("playing Case West – Navigating the menus")
  once they have accepted you. *Invite* sends them into your co-op session.
- **Messages** — up to 256 characters, to friends. The last 20 between you
  and a person are kept. A message arriving is a notification with *Reply*,
  in the launcher and over the game.
- **Invites** — invitations waiting for you. *Accept* starts the game and
  joins, like the console did; if the game is already running, it joins
  from there.
- **Achievements** — every achievement of an installed game with its art,
  description, score and when you earned it. Secret ones stay secret until
  you have them.
- **A friend's profile** — click their name: gamerscore, member since,
  every game with how far they are, and *Compare achievements* — theirs
  next to yours, with a filter for "what they have and I don't".
- **Issues** — bug reports (below).
- **Account** — click your gamercard at the bottom left: recovery email,
  country, change password (which signs every other device out).
- **Support** — a link to GitHub Sponsors and to the ports' repositories.

## In the game

Press **Shift+Tab** — or **View+Menu** together on a controller (the two
small buttons in the middle; Back+Start on a 360 pad), or the Guide button
— while playing: an overlay opens over the game with your friends,
invitations, messages and achievements, and a box to add a friend by
gamertag. It is built for the pad: **LB/RB** switch tabs, the stick or
d-pad moves, **A** picks, the right stick scrolls, **B** closes. (Typing a
gamertag or a message needs a keyboard, or the Steam Deck's on-screen
one.) Notifications show
over the game even while the overlay is closed (the *Notifications*
switch in the overlay's header turns them off; invitations and messages
then wait in the panel with a count on their tab): a friend coming online, an invitation, a message — and when you
earn an achievement, the popup the console had: its picture, *Achievement
unlocked*, the name, the score and the description.

## Reporting a bug

When something goes wrong in a game, press its **capture key** (F9). The
port saves a screenshot, the last minute of its log, and what your
machine is. Back in the launcher, the **Issues** tab lists the capture:
pick it, see exactly what would be sent, give it a title, say what
happened and how to make it happen again, and *Send* — or *Delete* if it
was nothing. Nothing leaves your machine until you press Send.

Every player can read and search the *words* of every report (title, what
happened, steps, and whether it is open, fixed or closed), so check
whether your bug is already known before sending it. **Only the developer
sees the screenshot, the log and your machine details.**

The ports' capture key is still being added; until it lands in a release,
report bugs on the port's GitHub page (links on the Support tab).

## Forgot your password?

On the sign-in screen, *Forgot your password?* — enter your gamertag, and
a **new password** is mailed to the email on your account. Sign in with
it; that makes it your password and signs every other device out. Then
change it to something of your own under Account. The mailed password
works for a day. Until you use it your old one still works, so someone
asking in your name costs you one email and nothing else (one mail per
quarter hour per account). An account without an email cannot be
recovered — see above.

## What is stored, and who sees what

| What | Where | Who sees it |
|---|---|---|
| Gamertag, gamerscore | the server | everyone (friends search by gamertag) |
| Password | the server, hashed (argon2id) | nobody |
| Recovery email | the server | only you, on the Account screen |
| Country | the server | friends, on your gamercard |
| Where you are in a game (presence) | the server, while you are online | friends who you have accepted |
| Achievements per game | the server | friends who you have accepted |
| Messages | the server, last 20 per pair | you and the recipient |
| Bug report words | the server | every player |
| Bug report screenshot, log, machine | the server | the developer only |
| Your sign-in tokens, installed games, saved accounts | your machine (below) | — |

There is no telemetry and no analytics. The launcher makes exactly two
kinds of network request: to the XenonLive server for the account and
social features, and to GitHub for releases.

On disk:

| System | Launcher data | Installed games |
|---|---|---|
| Windows | `%APPDATA%\XenonLive` | `%LOCALAPPDATA%\XenonLive\games` |
| Linux / Steam Deck | `~/.config/XenonLive` | `~/.local/share/XenonLive/games` |

Several people can share one machine: every account signed in here is
remembered (tokens only, never the password) and *Switch account* on Home
swaps between them. *Sign out* forgets the tokens and asks for the
password next time; *Forget* on the sign-in screen removes the entry.

## Updating

Games and the launcher itself update from the launcher. A newer game
release shows *Update to vX* on its card; updating replaces the port's
files and never touches your game data or saves. A newer launcher shows a
banner on Home; *Update and restart* downloads it, checks it against the
release's checksums, swaps it in and restarts.

## When something does not work

| You see | What it means |
|---|---|
| `bad_credentials` | wrong gamertag or password |
| `taken` | that gamertag (or email) already has an account |
| `bad_gamertag` / `bad_password` | the rules above |
| `no_email` on Forgot | the account was created without an email; it cannot be recovered |
| `too_soon` on Forgot | a password was mailed in the last quarter hour; check your inbox and spam |
| `mail_unavailable` | the server you are on has no email set up (only on a self-hosted one) |
| "offline: retrying in 4s" under the form | the server cannot be reached; the launcher keeps trying |
| "could not check for releases" on Home | GitHub did not answer (offline, or its 60-requests-an-hour limit); it tries again in five minutes |
| "put your XBLA package in:" on a game | the port has no game data yet — see *Get started*, step 3 |
| the AppImage does nothing on Linux | no FUSE: use the `.tar.zst` instead, or run it with `APPIMAGE_EXTRACT_AND_RUN=1` |
| *Invite* is greyed out | you are not in a co-op session in the game; an invitation names one |
| Windows: nothing happens | the log is at `%APPDATA%\XenonLive\launcher\launcher.log` |

A bug in the launcher itself: open an issue on this repository with the log
(Windows: the file above; Linux: run it from a terminal and copy the
output).

## For developers

### Want your XenonRecomp game on XenonLive?

If you're working on an Xbox 360 recomp and want to add XenonLive support, get in touch with me.

You **do not need to implement the XenonLive launcher/backend integration yourself**. Send me your project and the information I need, and I can handle the XenonLive side of the integration for your title.

Depending on what the game supports, this can include:

* XenonLive accounts and sign-in
* Achievements and gamerscore
* Friends and player presence
* Game/session invites
* Online multiplayer / co-op integration
* Messages and notifications
* The in-game XenonLive overlay
* Launcher installation, updates and release detection
* Game-specific presence such as menus, levels, modes or other activity
* Issue reporting / capture integration
* Other Xbox Live functionality that makes sense for the title

Not every Xbox 360 game uses the same Live functionality, so the exact integration can be adapted to the game.

### What I need from you

When contacting me, send whatever applies to your project:

* Game name
* Xbox 360 Title ID
* Link to your recomp repository
* Link to your releases/builds
* The name or names you want shown in the credits
* Your GitHub profile or project page
* Patreon, Ko-fi, GitHub Sponsors or any other support links you want displayed
* Any website, Discord or community link you want associated with the project
* A short description of the recomp
* Which XenonLive features you want supported
* Any special installation or launch requirements

I want XenonLive to properly credit the people actually doing the recomp work. Your project page, credits and support links can be shown alongside the game in the launcher so players know **who made the port and where to support or follow its development**.

I'll handle adding the title to XenonLive, the launcher integration and the XenonLive-specific work needed for the game. We'll coordinate on any game-side hooks or information I need from your recomp.

The goal is to make it easy for other Xbox 360 recomp projects to get achievements, social features and online functionality working without every developer having to build their own Xbox Live replacement and launcher infrastructure.

### Contact

You can also contact me through GitHub: [@wivi514](https://github.com/wivi514).

For developers who want to work directly on XenonLive itself, building, releasing, the overlay internals, `launcher.json`, self-hosting a server and the environment variables used by the launcher are documented in [`docs/developing.md`](docs/developing.md).

## Credits

XenonLive is a spare-time project by one developer, built with AI
assistance. The face is Selawik (Microsoft, SIL Open Font License); Dear
ImGui, SDL2, libcurl, miniz, zstd and stb_image are listed with their
licenses in `THIRD_PARTY.md` in every release. The games are Capcom's; the
ports contain none of their data.

Running the server costs money every month. If XenonLive has given you
something, [GitHub Sponsors](https://github.com/sponsors/wivi514) is the
way to give a little back.
