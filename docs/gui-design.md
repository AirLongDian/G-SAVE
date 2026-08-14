# G-SAVE GUI design contract

> 2026-08-12: the implementation moved from hand-painted Win32 controls to Qt
> Quick/QML. The visual language below remains valid; page structure now follows
> a player-first game → repository → timeline hierarchy, and technical Git
> settings use progressive disclosure.

## Primary jobs

1. Install, enable, disable and remove a game's support registration.
2. Read the save timeline, inspect adapter metadata and restore a selected point.
3. Configure Git remote/authentication, compare divergent histories and choose
   one whole commit timeline as main while retaining the other as a side branch.
4. Start, gracefully stop and restart Core; configure elevated logon startup.
5. Configure commit and push policies by game.
6. Run a package's sandboxed `install(context)` on demand, then copy the
   confirmed package into the configuration directory before registration.

## Navigation

```text
+----------------------+---------------------------------------------------+
| G-SAVE               | page title                         global status   |
| save timeline        +---------------------------------------------------+
| Games                |                                                   |
| Saves                |                  active page                      |
| Sync & auth          |                                                   |
| Core service         |                                                   |
|                      |                                                   |
| config path          |                                                   |
+----------------------+---------------------------------------------------+
```

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
- No password or token is echoed back after saving.
- A Git remote URL is optional for local-only commit policies and manual push.
  Automatic push modes require it.
