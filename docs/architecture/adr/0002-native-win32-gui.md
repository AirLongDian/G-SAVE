# ADR 0002: Qt Quick on-demand GUI

- Status: accepted, implementation revised
- Date: 2026-08-11
- Revised: 2026-08-12

## Context

G-SAVE needs a modern Windows configuration and repository-management GUI while
the resident Core must remain unchanged in its resource model. The GUI runs only
when the user opens it, so its transient memory and package size are less
important than maintainability, accessibility and a clear experience for game
players. A Qt 6.10 toolchain is available. The GUI must still reuse the same
statically linked Repository Engine and must not add a resident helper, tray
process, web server, RPC endpoint or worker executable.

## Decision

Build one `gsave-gui.exe` with Qt 6 Quick Controls and QML, dynamically linked to
Qt. A thin C++ controller exposes the existing `GuiModel`, Repository Engine,
Windows Credential Manager and Core process controls directly to QML. QML owns
only presentation and form state; repository and configuration rules remain in
C++. There is no JSON transport, IPC service or duplicated Git implementation.
The GUI is an on-demand foreground process and owns no component after it exits.

The visual direction remains a "save timeline console": graphite-blue surfaces,
amber save points, a restrained cyan state accent, Segoe UI Variable for player
facing copy and Cascadia Mono only for commit identifiers and paths. The
distinctive element is the repository history rail. History is always scoped by
game first and then by repository; versions from unrelated games are never
mixed in one selector or timeline.

Cloud configuration is player-facing rather than Git-facing. Every configured
game is included automatically; the page has no second game selector or per-game
cloud opt-in. It asks only for a Git service address, one access Token and a
shared upload timing choice. It detects GitHub, Gitee, GitLab or Gitea from the host and service API,
then reads the authenticated account name itself. The GUI derives stable private
repository names from the support-package game ID (the generic package ID is
derived from its executable), detects or creates each required repository, and
configures the local `origin` URL. Multiple independent save roots receive
numbered repository names and are never pushed into one unrelated history.
The GUI shows the complete game-to-private-repository mapping as confirmation,
not as a selectable list. Disabling or removing protection remains a game-support
operation. Remote names, repository URLs, account names and credential references are not
editable player fields. Tokens remain in Windows Credential Manager and never
enter TOML or Git history.

The GUI reads and atomically replaces `config.toml`, calls Repository Engine
directly, stores secrets in Windows Credential Manager and manages the Core
process through Windows process APIs. One session-local named manual-reset event
is added solely for graceful Core shutdown. It carries no data, exposes no
status, creates no idle application thread, and is not a GUI/Core business API.

Support discovery runs only when installation is requested. The GUI creates a
restricted, memory- and instruction-bounded Lua state, exposes only read-only
known-folder/Steam/path inspection functions, accepts multiple repository paths,
and destroys the state immediately. The confirmed package is copied under the
configuration directory so TOML never depends on a source checkout or temporary
import path.

## Safety boundaries

- History reads may occur while Core runs.
- Restore, divergence resolution and remote integration require the game process to
  be stopped. The GUI gracefully pauses Core for the operation and restarts it
  only if it was running beforehand.
- Restore first commits outstanding work as a recovery point, then creates a
  new commit from the selected historical tree. It never rewrites history.
- Binary-save divergence is resolved at whole-commit granularity. The GUI never
  offers per-file selection and never synthesizes a worktree from files taken
  from different heads.
- The user selects either the local head or the remote head as the continuing
  main branch. Before switching the main branch, the unselected head receives a
  durable side-branch reference so the complete alternative timeline remains a
  visible fork. No content merge commit is created by default.
- Publishing a local-wins decision preserves the old remote head remotely
  before updating the remote main reference. Core stays stopped until the
  choice is completed or safely cancelled.
- Normal operation assumes one GUI performs management actions at a time. The
  product does not add a GUI singleton or cross-GUI locking machinery.
- Removing game support removes only TOML registration; it never deletes the
  save directory or `.git` history.
- WebDAV and OneDrive appear as planned backends but remain unavailable until
  their object/ref atomicity protocol is separately verified.
- Local commit policy is independent from remote setup. A remote URL becomes
  mandatory only when an automatic push trigger is selected.

## Consequences

The GUI deployment includes the Qt libraries and QML modules selected by
`windeployqt`, using dynamic linking. This is a deliberate package-size tradeoff
for substantially less hand-written widget code and easier UI maintenance. The
Core target does not find, include or link Qt and its shipped executable remains
unchanged. GUI resource use is intentionally not held to Core idle limits
because the GUI does not remain resident.
