# DialogueSourceTags: see which plugin a dialogue line comes from

An SKSE plugin for Skyrim Special Edition and Anniversary Edition (a single DLL works on SE 1.5.97 and all AE 1.6.x versions through Address Library).

When many mods add dialogue, the list of player response options becomes long and it is hard to tell which lines are vanilla and which were added by a mod. DialogueSourceTags puts a colored `[PluginName]` tag in front of every response option in the dialogue menu:

```
[Skyrim]      Tell me about the war.
[MyDialogMod] Let's talk about something else.
[AnotherMod]  I have a proposition for you.
```

Each plugin gets its own color, generated automatically from the plugin's file name (so the color stays the same between game sessions). Official game files are shown in grey so that mod-added lines stand out. All of this can be changed in the INI.

> VR is not included in this build: it is built on `commonlibsse-ng-flatrim` (SE + AE).

## Requirements

- Skyrim SE or AE
- [SKSE64](https://skse.silverlock.org/) for your game version
- [Address Library for SKSE Plugins](https://www.nexusmods.com/skyrimspecialedition/mods/32444)
- [SKSE Menu Framework](https://www.nexusmods.com/skyrimspecialedition/mods/120352) (optional, only needed for the in-game settings menu)

## How to get the DLL (cloud build, nothing to install)

1. Sign up at [github.com](https://github.com) (free).
2. Create a new repository (the **New** button), for example `DialogueSourceTags`.
3. Upload **all files from this archive, including folders**, and make sure the hidden `.github` folder is included (**Add file → Upload files**). If `.github` did not upload, use **Add file → Create new file**, type `.github/workflows/build.yml` as the name and paste in the contents of that file.
4. Open the **Actions** tab. The build starts automatically. If it does not, choose **Build DialogueSourceTags → Run workflow**.
5. Wait 10–20 minutes (the first time, vcpkg builds the libraries). When the green check mark appears, open the run and download `DialogueSourceTags` from the **Artifacts** section.

## Installation

Copy the contents of the `Data` folder from the artifact into the game's `Data` folder (or install it as an archive with a mod manager):

```
Data/SKSE/Plugins/DialogueSourceTags.dll
Data/SKSE/Plugins/DialogueSourceTags.ini
```

No ESP is needed, nothing is stored in save games, and the mod can be removed at any time.

## Settings menu (SKSE Menu Framework)

If SKSE Menu Framework is installed, a **DialogueSourceTags → Settings** page appears in its Mod Control Panel. It has:

- all general options (enable, source, colored tags, hide official files, file extension, tag length, logging);
- a list of **plugins seen in dialogues**: for each plugin a color picker, a short-name field and a reset button, with a live preview of the tag. Plugins appear in the list after their lines have been shown in the game;
- **Save to INI** (writes the values to `DialogueSourceTags.ini`, comments in the file are preserved) and **Reload from INI**.

Changes apply the next time the dialogue list refreshes (re-open the dialogue or pick a topic). Without the framework the plugin works as before and is configured through the INI.

The menu is designed for SKSE Menu Framework 3. During the build, the workflow downloads only the `SKSEMenuFramework.h` header from the [API repository](https://github.com/QTR-Modding/SKSE-Menu-Framework-3-API): the plugin needs neither ImGui nor `imgui.dll`. Before registering the page, the plugin checks that all required functions exist in the installed framework version. If they do not, the menu is disabled and the reason is written to the log. To build without the menu on purpose, untick `menu` when running the workflow manually.

## Configuration (DialogueSourceTags.ini)

| Key | Meaning |
|---|---|
| `Enabled` | `1` enabled, `0` disabled |
| `Source` | `Creator` (default): the plugin that created the line. `LastModifier`: the last plugin that changed it. `Both`: shows `[Creator>LastModifier]` when they differ |
| `UseColor` | `1` colored tags, `0` tags in the normal text color |
| `HideOfficial` | `1` = do not tag lines from official game files, so only mod-added lines are marked |
| `ShowExtension` | `1` = `MyMod.esp`, `0` = `MyMod` |
| `MaxTagLength` | Maximum tag length in characters, `0` = no limit |
| `Log` | `1` = write the first tagged lines to the log (to verify that the mod works) |

`[Colors]` overrides the automatic color of a plugin: `MyMod.esp=FFAA00` (RRGGBB). `[Names]` sets a custom short name: `MyMod.esp=MM`.

## How it works

The dialogue menu keeps a list of response options. Each option knows the dialogue record (INFO) it comes from, and each record knows which plugin files define it. The plugin hooks the dialogue menu's message handler and adds a plain `[PluginName]` tag to the text of every option before the menu displays it. Because the list sets the text color of whole lines itself, the colors are applied separately: while the dialogue menu is open, the tag part of each entry gets its own text format every frame, so the color survives the list redrawing its entries.

## Diagnostics

Log file: `Documents\My Games\Skyrim Special Edition\SKSE\DialogueSourceTags.log` (for GOG: `Skyrim Special Edition GOG`).

- `Hook installed on DialogueMenu::ProcessMessage` means the plugin loaded and installed its hook.
- `Hook installed on DialogueMenu::AdvanceMovie` and `Recolor: tags colored through ...` mean the coloring works. If the tags have no color, the log contains a short report about the dialogue list; send it to the author.
- `DialogueSourceTags menu registered in SKSE Menu Framework` means the menu is connected.
- Lines starting with `Tagged line` show which lines were tagged. If you open a dialogue and see no such lines, the options are probably built at a different moment than the plugin expects. Send the log to the author.

## Limitations

- Only response options that belong to a dialogue record (INFO) are tagged. Special entries without one (for example some built-in menu entries) are left as they are.
- Other mods that rewrite the dialogue menu text (for example, ones that renumber or recolor options) may interact with the tags. The `Both`, `HideOfficial` and `UseColor` options can help.
- Only the original plugin that defined the line is known to the game; lines created at runtime by scripts have no source plugin and are left untouched.

## Project structure

```
src/main.cpp       entry point, log, SKSE events
src/Settings.*     INI reading and writing, thread-safe settings snapshots
src/Dialogue.*     hook on the dialogue menu, tag building (colors, names)
src/Recolor.*      colors the tags inside the Flash dialogue list
src/UI.*           menu for SKSE Menu Framework
CMakeLists.txt, vcpkg*.json   build (CommonLibSSE NG via vcpkg)
.github/workflows/build.yml   build on GitHub Actions
```
