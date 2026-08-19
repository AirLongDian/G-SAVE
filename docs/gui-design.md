# G-SAVE GUI design contract

> 2026-08-12: the implementation moved from hand-painted Win32 controls to Qt
> Quick/QML. The visual language below remains valid; page structure now follows
> a player-first game → repository → timeline hierarchy, and technical Git
> settings use progressive disclosure.

## Primary jobs

1. Browse a poster card library of installed games and installable packages,
   with search and an optional online package index.
2. Install, enable, disable and remove a game's support registration.
3. Read the save timeline, inspect adapter metadata and open any older version as
   a new named branch, or restore it onto the current timeline.
4. List and switch save timelines, since a branch is one self-contained save set.
5. Configure Git remote/authentication, compare divergent histories and choose
   one whole commit timeline as main while retaining the other as a side branch.
6. Start, gracefully stop and restart Core from the title bar; configure elevated
   logon startup.
7. Stage commit policy edits and flush them with a single save and restart.
8. Run a dedicated package's sandboxed `install(context)` on demand and show
   its automatically detected executable and save roots for confirmation.
   Dedicated packages never open manual path pickers; only generic support does.
9. Copy the confirmed package into the configuration directory before
   registration.
10. Import a locally authored package ZIP, including by dropping it on the
    window, so players can build and share their own packages.

## Navigation

A title bar replaces the former sidebar. The Core state pill on the right is both
the indicator and the switch: clicking it starts or stops the service, and it is
disabled while a transition is in flight so a double click cannot start and stop
at once.

```text
+---------------------------------------------------------------------------+
| G  G-SAVE | Library  Settings          [save+restart]  (o) protection on   |
+---------------------------------------------------------------------------+
| <  back to library                    (only on the game detail page)      |
+---------------------------------------------------------------------------+
|                                                                           |
|                              active page                                  |
|                                                                           |
+---------------------------------------------------------------------------+
```

Three pages:

1. **Library** — poster card grid, search, online index refresh, ZIP import.
   Installed games first, then installable packages. The installed row ends with
   an "add a game without a support package" tile, which is the only entry point
   that opens path pickers.
2. **Game detail** — banner plus two tabs, `支持配置` and `存档管理`.
3. **Settings** — service and logon startup, cloud account, package index source,
   file locations.

Posters come straight from the Steam CDN using the package's `steam_app_id` and
are never cached to disk:

```text
poster https://cdn.cloudflare.steamstatic.com/steam/apps/<appid>/library_600x900.jpg
banner https://cdn.cloudflare.steamstatic.com/steam/apps/<appid>/header.jpg
```

A missing application ID or a failed request falls back to a gradient tile with
the game's initial, so a card is always readable.

## Staged settings

Core reads its configuration once at startup and has no hot reload. Editing a
commit policy therefore stages the change instead of writing it, and the title
bar shows a save action while anything is pending. Closing the window with
pending edits asks to save and restart, discard, or cancel. A service that was
not running stays stopped after saving.

## Tokens

- `ink`: `#EAF0F4`
- `muted`: `#93A4AF`
- `canvas`: `#0C1217`
- `sidebar`: `#101A22`
- `surface`: `#16232C`
- `line`: `#29404D`
- `save-point`: `#F0B45A`
- `healthy`: `#60C2B0`
- `danger`: `#EF776F`

Segoe UI Variable/Segoe UI is used for interface text. Cascadia Mono/Consolas is
used for OIDs, paths and timing values. Motion is limited to the Windows-native
focus and hover transitions; reduced-motion users lose no information.

## Deliberate omissions

- No telemetry, news, account dashboard or live Core event stream.
- No fake WebDAV/OneDrive connect buttons.
- No restore while the configured game process is running.
- No per-file conflict list or file-level merge. A save version is selected only
  as a complete commit tree; the unselected head remains on a side branch.
- No bare checkout of an older commit. Opening an older save always creates and
  switches to a named branch in one operation, because `push_repository`,
  `integrate_repository` and `resolve_divergence` all refuse to run on a detached
  HEAD. A detached repository would keep accepting commits while silently losing
  push, fetch and divergence handling.
- No password or token is echoed back after saving.
- A Git remote URL is optional for local-only commit policies and manual push.
  Automatic push modes require it.

## Save timelines as branches

Each branch is one self-contained save timeline. Branches are how G-SAVE supports
multiple devices and multiple parallel save sets: switching branch switches the
save. The repository must always rest on a named branch.

Timeline operations exposed by the GUI:

- `list_branches` — enumerate local timelines with tip commit and time.
- `suggest_branch_name` — derive `save/<date>-<short id>` and avoid collisions.
- `create_branch_from_commit` — create a branch at a chosen commit and check it
  out atomically.
- `switch_branch` — move to an existing named branch only; a raw commit is never
  accepted.

All four refuse to run with uncommitted save changes and require the game to be
stopped with Core paused. They live behind `#ifndef GSAVE_CORE_ONLY`, so the Core
binary does not link them.
