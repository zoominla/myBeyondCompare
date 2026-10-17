# mbc

**mbc** is a **clean-room C++20 re-implementation of [Beyond Compare](https://www.scootersoftware.com/bcomp.php)'s compare features**. It ships as a single native binary that provides a CLI, a local web-based GUI (`mbc gui`), folder / text / hex comparison, 3-way merge, reports, and `BCSessions.xml` session compatibility — with no third-party compare engine underneath.

### ✨ Highlights

- 🖥️ **CLI + GUI in one binary** — builds with `make` (g++/clang++, C++20), `build_win.bat` (Visual Studio 2022 / MSVC), or `build_wsl.sh`.
- 📂 **Beyond Compare feature set** — folder compare tree (filters, Show/Options, copy/delete, sync, reports), side-by-side text compare with in-place editing, hex compare, and 3-way merge with an editable output pane.
- 🌐 **Bilingual UI** — English and Chinese (中文), switchable at runtime from the tab bar and remembered in the browser.
- 📇 **Session compatibility** — reads/writes `BCSessions.xml`; every GUI comparison is automatically recorded in a `History` session.
- ✅ **Fuzz-tested** — the CLI is fuzzed against GNU `diff` / `patch` / `diff3` when present; the GUI is driven through a headless browser over CDP.
- 📄 **`BComp.com`-compatible exit codes**.

## Contents

- [🚀 Quick Start](#-quick-start)
  - [Build](#build)
  - [Test](#test)
  - [CLI](#cli)
  - [GUI](#gui)
- [💡 Usage Tips](#-usage-tips)
- [🏛 Architecture](#-architecture)
- [🛡️ Compatibility & Testing](#️-compatibility--testing)
- [⚠️ Risks and Limitations](#️-risks-and-limitations)
- [📄 License](#-license)
- [🙏 Acknowledgments](#-acknowledgments)

## 🚀 Quick Start

### Build

```
make            # g++/clang++, C++20; needs python3 for the GUI page
build_win.bat   # Visual Studio 2022 / MSVC
build_wsl.sh    # WSL
```

### Test

```
MBC=./mbc python3 tests/test_mbc.py   # CLI + GUI API; fuzzes against GNU diff/patch/diff3 when present
node tests/ui_cdp.js                  # GUI UI test (Windows, needs Node + Chrome/Edge); drives a headless browser over CDP
```

### CLI

Run `mbc help` for the full option list. Exit codes follow `BComp.com`.

### GUI

```
mbc gui [LEFT [RIGHT]] [LEFT BASE RIGHT] [--port N] [--no-open] [--view dir|text|hex|merge] [--sessions FILE]
```

Starts a local web server on 127.0.0.1 (random port, random token, Host-header check) and opens the default browser.

| Area | What it provides |
|---|---|
| Tabs | Tabbed workspace; exit via the "Exit" tab button or Ctrl+C |
| Home | Quick compare; Sessions manager (reads/writes `BCSessions.xml`) |
| Folder compare | Tree view; filters, Show/Options, copy/delete, sync, reports |
| Text compare | Side-by-side, inline highlights, edit in place, copy sections, undo/redo, find, encoding/EOL, rules, save, reports |
| Hex compare | Byte-level comparison |
| 3-way merge | Merge view with editable output |

On Windows, double-clicking `mbc.exe` (no arguments, started from Explorer) opens the GUI directly; run from a terminal without arguments it prints the help. `MBC_NO_OPEN=1` suppresses opening the browser.

Sessions: the history is read from `BCSessions.xml` in the process's current directory (created empty when it does not exist); sessions saved from the GUI (or `mbc session save`) are written back to it. Every comparison started in the GUI (folder / text / hex / merge) is also recorded automatically in a `History` session folder (newest first, max. 100 entries, no duplicates, nothing is recorded for files opened from a folder compare or for comparisons already saved under a name). `--sessions FILE` or the environment variable `MBC_SESSIONS` selects another file.

Layout: in text, folder, hex and merge views every pane has the same width by default, a draggable divider (double-click: equal widths) and its own horizontal scrollbar. Folder rows have a clickable ▼/▶ twisty that collapses / expands the folder.

Languages: English and Chinese (中文). The button at the top right of the tab bar switches the language; the choice is stored in the browser (default: follows the browser language).

## 💡 Usage Tips

- **Keep the session file where you expect it.** History is read from / written back to `BCSessions.xml` in the process's current directory — override it with `--sessions FILE` or the environment variable `MBC_SESSIONS`.
- **Every GUI comparison lands in `History`** — newest first, max. 100 entries, de-duplicated. Comparisons opened from a folder compare, or already saved under a name, are not recorded.
- **Double-click the divider** between panes to reset them to equal widths.
- **Use the ▼/▶ twisty** on folder rows to collapse / expand the folder tree.
- **Launch from Explorer to get the GUI** — on Windows, double-clicking `mbc.exe` with no arguments opens the GUI; from a terminal without arguments it prints help.
- **Skip the browser** — set `MBC_NO_OPEN=1` to start the server without opening the default browser.
- **Add UI strings in both languages** — the UI is written in English; `web/i18n.js` holds the Chinese dictionary and translates the DOM (text nodes, tooltips, placeholders) and reverts it when switching back, leaving file names and file contents alone. Add new strings to `I18N_ZH` / `I18N_PAT` there — `tests/ui_scenarios5.js` fails when a string stays untranslated.

## 🏛 Architecture

A single C++20 binary: the CLI entry point, plus a local HTTP server that serves the web GUI and its JSON API.

| Module (src/) | Purpose |
|---|---|
| `main` | CLI |
| `common` | Shared utilities |
| `textfile` | Encodings / EOL |
| `diff` | LCS/patience line diff, inline diff, rules |
| `merge` | 3-way merge |
| `hexcmp` | Hex compare |
| `dircmp` | Folder compare, filters, sync |
| `report` | Text / folder reports |
| `session` | `BCSessions.xml` reader/writer |
| `script` | BC-style script subset |
| `json` | JSON |
| `httpd` | Local HTTP server |
| `gui` / `gui_api` | Web GUI and its JSON API |

The GUI page is built from `web/*` by `embed.py` into `src/gui_page.cpp` (checked in; the Makefile regenerates it). The implementation is clean-room; script syntax is written from general knowledge of BC scripting, not from the installed binary.

## 🛡️ Compatibility & Testing

| Check | How it is verified |
|---|---|
| CLI | Fuzzed against GNU `diff` / `patch` / `diff3` when present — `tests/test_mbc.py` covers CLI + GUI API |
| GUI | Driven through a headless browser over CDP — `tests/ui_cdp.js` (Windows, needs Node + Chrome/Edge) |
| Exit codes | Follow `BComp.com` |

## ⚠️ Risks and Limitations

- **Not implemented:** FTP/cloud, archive/registry/picture/audio/table compare, native (non-web) GUI.
- **Script compatibility is best-effort.** Script syntax is written from general knowledge of BC scripting, not from the installed binary.
- **Build prerequisites:** python3 is needed to generate the GUI page; the GUI UI test additionally needs Node and Chrome/Edge on Windows.

## 📄 License

Licensed under the [MIT License](LICENSE.txt). Copyright (c) 2026 morluto.

## 🙏 Acknowledgments

- [Beyond Compare](https://www.scootersoftware.com/bcomp.php) by Scooter Software — the reference product this project re-implements (clean-room).
- GNU `diff` / `patch` / `diff3` — used as fuzz-testing oracles.
