# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

C.H.E.S.S. is a Godot 4 pixel-art chess game. The Godot project root (`res://`) is the `C.H.E.S.S/` subdirectory, not the repo root. All chess rules and AI live in a C++ GDExtension (`C.H.E.S.S/modules/`) built with SCons against a separately cloned `godot-cpp/`. GDScript scenes in `C.H.E.S.S/scenes/` provide the UI. Escape the dots in `C.H.E.S.S` when using it in a regex. Export output paths contain spaces and parentheses, so quote them.

## Building the GDExtension

Prerequisites: `godot-cpp/` must be cloned at the repo root on branch `4.5` (it is gitignored: `git clone -b 4.5 https://github.com/godotengine/godot-cpp.git`), and SCons must be installed (`pip install -r requirements.txt`). Run scons from the repo root.

```bash
scons platform=linux target=template_debug -j$(nproc)                  # the build the editor / `godot --path` load (manifest's linux.debug entry)
scons platform=linux target=template_release -j$(nproc)
scons platform=windows target=template_debug use_mingw=yes -j$(nproc)  # MinGW cross-compile from Linux/WSL
scons platform=linux target=template_debug compiledb=yes               # also writes compile_commands.json
scons platform=linux target=template_debug debug_symbols=yes          # symbols without changing the output name
```

`build_modules.sh` (Linux/WSL, all platforms) and `build_modules_macos.sh` (native Mac) are interactive wrappers around these commands. Prefer calling scons directly.

- Output goes to `C.H.E.S.S/bin/libkCC_modules.<platform>.<target>.<arch>.{so,dll}`. The name must match an entry in `C.H.E.S.S/bin/gdexample.gdextension`, or the extension fails to load. Godot logs a "GDExtension dynamic library not found" error, which is easy to miss among the parse errors that follow. These options change the suffix and break the match: `dev_build=yes`, `precision=double`, `threads=no`, `target=editor`, any arch other than x86_64 on Linux/Windows (including the auto-detected default on an ARM host), and any arch other than `universal` on macOS. macOS output is forced to `.so` in `SConstruct` to match the manifest.
- New `.cpp` files must sit directly in `C.H.E.S.S/modules/`, because the Glob is not recursive. Objects go to `build_cache/<platform>/`.
- The code is compiled with `-fno-exceptions`, and `NDEBUG` is defined for both template targets, so `assert()` does nothing.
- The manifest has no `reloadable = true`, so the editor won't hot-reload the library. Restart the editor or game after rebuilding.
- The binaries are gitignored. On a fresh clone, every script that uses `Board`/`Agent`/`NeuralNet` as a type fails to parse until the extension is built.

## Running and testing

There is no test suite, linter, or formatter config, and no Godot binary in the repo.

```bash
godot --headless --path C.H.E.S.S --editor --quit    # one-time headless import (creates C.H.E.S.S/.godot/)
godot --headless --path C.H.E.S.S --script <file.gd> # run a throwaway `extends SceneTree` script; call quit() at the end
```

- The main scene (`scenes/main/main.tscn`) is effectively empty: `setup_main_menu_ui()` is commented out and the menu's scene paths are stale. To try a mode, run its `.tscn` directly (e.g. `godot --path C.H.E.S.S res://scenes/perft_analysis/perft_analysis.tscn`). The mode scenes never quit on their own, so wrap non-interactive runs in `timeout`.
- **Checking move-generation changes:** write a SceneTree script that does `Board.new()`, then `setup_board(fen)`, sums `get_perft_analysis(d).values()` for `d >= 1` (`d = 0` effectively never returns), and calls `quit()`. Expected values: start position 20 / 400 / 8902 / 197281 / 4865609. The start position never castles or promotes, so also test Kiwipete (`r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1` → 48 / 2039 / 97862). The Perft scene prints the same counts but compares them against nothing, and its per-move divide covers only the final depth (`perft_max_depth`).
- Perft only exercises the *fast* move pipeline (see below). It does **not** validate UI move legality or game-end detection.

## Architecture

### C++ layer (`C.H.E.S.S/modules/`)

`register_types.cpp` (entry symbol `chess_ai_library_init`) registers `Board` (Node2D), `NeuralNet` (Node2D), and `Agent` (extends `NeuralNet`). GDScript only sees methods bound in each class's `_bind_methods`. To expose one, add a `ClassDB::bind_method` line, rebuild, and restart the editor.

**Board** (`board.h/.cpp`) is the entire rules engine.
- It is a `uint8_t squares[64]` mailbox: index `rank*8 + file`, a1=0, h8=63. A piece byte combines the type in the low 3 bits (P=1 N=2 B=3 R=4 Q=5 K=6) with the color (8=white, 16=black). These are unbound `#define`s, so each board-drawing script redeclares them as GDScript consts.
- **Two color conventions.** `get_turn()` and Board's color params (`is_check`, `is_checkmate`, `get_all_possible_moves`, ...) use 0=white/1=black. Agent's `evaluate`, `get_features_for_color`, and `train_on_current_position` take 8/16.
- **Two duplicated move-generation pipelines.** A rule change or bug fix must be applied to both.
  1. *UI/Array path*: `get_legal_moves_for_piece` → `add_*_moves`/`can_castle_*` → `would_be_in_check_after_move` filter. `make_move_internal` fills a `Move` record that its public callers (`attempt_move`, `commit_promotion`, `make_move`) push onto `move_history`. `revert_move` pops the record and calls `revert_move_internal`. This path maintains the halfmove/fullmove clocks and backs `is_checkmate`/`is_stalemate`/`is_game_over`/`get_game_result`.
  2. *Fast path*: `MoveList`/`FastMove` (flags 1=capture, 2=en passant, 4=castling, promotion type `<<3`). `generate_all_pseudo_legal` iterates piece lists. `make_move_fast`/`unmake_move_fast` skip history and clocks, and callers must snapshot ep, castling, and hash before making a move. Perft and Agent search use this path. The make, test-own-king, unmake legality sequence is repeated in `has_legal_moves`, `count_all_moves`, and `get_perft_analysis`.
  Neither path detects pins. A move is legal if making it does not leave the mover's own king attacked.
- **No Board call is read-only.** Legal-move queries and game-end checks temporarily mutate `squares[]` (`has_legal_moves` does so via `const_cast` despite being `const`).
- **Piece lists** (`white_piece_list`/`black_piece_list`, squares only) feed the fast path and Agent eval. Any code that persistently writes `squares[]` must update them, removing *before* overwriting because the list is picked by the piece's color bits (see commit 7e857b4). The bound `set_piece_on_square` does **not** update them.
- Game end covers checkmate, stalemate, and the 50-move rule, but not repetition or insufficient material. `get_game_result()` returns 0=ongoing, 1=white wins, 2=black wins, 3=draw.
- `Board._ready()` resets to the start position, so call `setup_board(fen)` *after* `add_child(board)`. A Board that is never added to the tree stays empty until `setup_board`.
- Human move protocol: `attempt_move(from, to)` returns 0 (rejected), 1 (made), or 2 (promotion pending, board unchanged). On 2, call `commit_promotion("q"|"r"|"b"|"n")`. The bound `make_move(from, to)` skips legality and turn checks and always promotes to queen. It is how AI moves are applied.

**Agent** (`agent.h/.cpp`) inherits `NeuralNet`, so each Agent has its own weights. It holds a non-owning `Board*` (`set_board`) and searches by making and unmaking moves **in place on that shared Board**.
- Search is alpha-beta minimax. White maximizes, black minimizes, and iterative deepening goes by depth only. There is no quiescence, time limit, or stop flag. Search logic exists in **four copies**: the max and min branches of `minimax_internal`, plus the root loops in `get_best_move` and `run_iterative_deepening`. Changes to search, ordering, or the TT must go into all four.
- Score convention: minimax assumes leaf scores are centipawns from white's side. `evaluate_material()` does this and ignores its `color` arg. The NN branch of `evaluate()` does not: features are mirrored for black, the targets are side-relative `score_to_target` = sigmoid(cp/600), and the output is cast straight to `int`. TT scores are stored as `int16_t`, so values outside ±32767 (including `CHECKMATE_SCORE` = 100000) wrap.
- The transposition table is static and process-wide: all Agent instances share it, and it is never cleared. Results come back as `{from, to, score[, depth]}`, with no promotion piece.

**NeuralNet** (`neural_network.h/.cpp`) is a dense MLP with runtime-defined layer sizes, trained by SGD, using a binary `.nn` format. The input is the 781-feature encoding built in `Agent::extract_features`.
- `save_network`/`load_network` always prepend `res://models/` and append `.nn`, so pass bare names (`"white_agent"`). Saves land in `C.H.E.S.S/models/`, which is not gitignored. `res://` is read-only in exports.
- `load_network` does not allocate the forward/backprop buffers; only `initialize_neural_network` does. Call it with the same layer sizes before using a loaded net, or the first forward pass with hidden layers crashes.

### GDScript layer (`C.H.E.S.S/scenes/`)

- Each mode scene is self-contained. `_ready` creates its own `Board`, reads an optional `get_tree().root.get_meta("start_fen")`, and draws pieces and highlights from the Aseprite atlas `assets/sprites/board_sheet.{png,json}` (16x16 slices). The board background is a separate texture node in each `.tscn`.
- **There is no shared base class or helper.** Piece consts, asset loading, board drawing, and coordinate conversion are copy-pasted across `player_vs_player/board_0.gd`, `player_vs_computer.gd`, `computer_vs_computer.gd`, and `perft_analysis.gd`. Input and promotion UI are copied too, except in CvC. The copies have drifted (e.g. only PvP has auras, animated highlights, and seasonal sprites). A fix in one usually needs repeating in the others.
- Rendering is pull-based: `refresh_visuals()` polls `get_piece_on_square` for all 64 squares. C++ emits no signals.
- Scene grid coords are `Vector2i(file, 7 - rank)`. The bound `Board.pos_to_coords` returns `Vector2i(rank, file)` and no scene uses it, so don't mix the two.
- PvP: the scene root has no script, and the logic lives in `board_0.gd` on the `CanvasLayer/board0` TextureRect. `player_vs_player/player_vs_player.gd` is orphaned dead code that preloads deleted assets.
- PvC, CvC, and Perft run search or perft on a GDScript `Thread` against the live `Board`, and the C++ side has no locking. While `ai_thinking`/`is_analyzing` is set, the main thread must not call *any* Board method. PvC and CvC return the result via `call_deferred`. Perft prints from the worker thread.
- `computer_vs_computer.gd` is the NN training harness: two Agents share one Board. Config is top-of-script consts (`LOAD_MODELS_ON_START`, `MODEL_*_PATH`, `LEARNING_RATE`, ...) plus `var TRAINING_MODE` (0=none, 1=heuristic, 2=distillation). Training runs on the main thread after each move.
- The `WindowDpi` autoload (`window_dpi.gd`) forces a 512x512 content scale at runtime, overriding `project.godot`'s 480x270 viewport.
- `.tscn` files reference scripts and textures by uid, so move a `.gd`'s `.gd.uid` sidecar with it. Hard-coded `res://` strings (`change_scene_to_file`, `load()`) are not uid-tracked.

## Known broken or stale

- PvC (`player_vs_computer.gd`) does `NeuralNet.new()` and then calls `set_board`/`run_iterative_deepening`. Those are bound only on `Agent`, so this fails even with the extension built.
- CvC passes `res://assets/models/*.nn` to `load_network`/`save_network`, which C++ turns into `res://models/res://assets/...`. With the default `LOAD_MODELS_ON_START = true`, the load fails (logged once), training returns 0.0 loss every move, and the game-end save fails.
- `TRAINING_GUIDE.md` has outdated source paths (`src/`, `keychan-chess/`), model paths, and NN-eval claims. The README mentions Stockfish, which is neither in the repo nor used by code. Its "C++: use tabs" instruction doesn't match the actual code, which uses 4 spaces.
- Godot versions: the project was last saved by **4.6** (`config/features`, node `unique_id`s in `.tscn`), while CI (`deploy.yml`) and the README use 4.5. godot-cpp `4.5` with `compatibility_minimum = "4.5"` loads fine in 4.6, so don't bump it unless CI moves too.

## CI and repo conventions

- **Pushing any change to `C.H.E.S.S/CHANGELOGS.md`, on any branch, triggers a public release** through `deploy.yml` (itch.io, GitHub Release, Discord). The version comes from the first `## ` header, and a `-` in it marks a prerelease. Don't edit the file unless you are cutting a release. CI never builds the GDExtension, so exports currently ship without the native library. Export preset names `windows`/`linux`/`macos` must stay unchanged. Separately, every push, branch, and PR event is posted to Discord (`discord-commits.yml`).
- Indentation: C++ uses 4 spaces, GDScript uses tabs. `.gitattributes` enforces LF line endings.
- **No AI attribution anywhere.** Do not credit AI as an author or co-author in commit messages, PR descriptions, issues, review comments, or release notes. That means no `Co-Authored-By: Claude ...` trailers and no "Generated with Claude Code" lines.
