# C.H.E.S.S. - Competitive Hybrid Engine Strategy Simulator

A unique chess engine building strategy game that transforms traditional chess into a competitive meta-game. Build, optimize, and battle with your own custom chess engines!

**Current Stable Version:** v0.1.0-pre-alpha

## What is C.H.E.S.S.?

Instead of just playing chess, you become an engine architect. Design intelligent chess AI agents from scratch, implementing algorithms like alpha-beta pruning, Zobrist hashing, move ordering strategies, and even neural network-based evaluation. Your engine-building choices directly impact competitive success.

The game features multiplayer battles where you assemble teams of engines and face off tactically. Manage your roster, make strategic substitutions, develop game plans, and adapt to your opponent's strengths and weaknesses.

Built with charming pixel art visuals and a vibrant card-game aesthetic, C.H.E.S.S. blends the timeless depth of chess with modern strategy gaming.

## Current Features

### Core Chess Engine
- **Full Chess Rules Implementation**: Complete move generation, validation, and special moves (castling, en passant, promotion)
- **FEN Support**: Import and export positions using standard Forsyth-Edwards Notation
- **Move History & Undo**: Full game state tracking with revert capabilities
- **Game State Detection**: Checkmate, stalemate, and draw condition recognition

### AI & Optimization
- **Heuristic Evaluation Engine**: Classic material and positional evaluation
- **Alpha-Beta Pruning**: Efficient tree search with pruning optimization
- **Transposition Tables**: Zobrist hashing for position caching and faster lookups
- **Move Ordering**: MVV-LVA (Most Valuable Victim - Least Valuable Attacker) implementation
- **Iterative Deepening**: Progressive depth search for better move quality
- **Neural Network Agent Structure**: Base framework for ML-based position evaluation (foundation laid)

### Game Modes
- **Player vs Player (PvP)**: Classic two-player chess on the same device
- **Player vs Computer (PvC)**: Challenge the heuristic-based AI engine with all optimizations enabled
- **Perft Test Mode**: Performance analysis tool for validating move generation accuracy

### Visuals & UI
- **Pixel Art Style**: Retro-inspired 16x16 tile-based graphics
- **Custom Pixel Font**: Integrated custom typeface for authentic pixel aesthetic
- **Interactive Board**: Click-to-move interface with visual move hints
- **Highlight System**: Color-coded overlays for valid moves, captures, and last move indicators
- **Piece Sprites**: Complete pixel art sets for all chess pieces in both colors
- **Low-Resolution Design**: 480x270 viewport optimized for pixel-perfect rendering

### Technical Foundation
- **Godot Engine 4.5**: Built on the latest stable Godot engine
- **C++ GDExtension Modules**: High-performance C++ backend for board logic and AI
  - `board.cpp/h`: Core chess rules and state management
  - `agent.cpp/h`: AI search algorithms and evaluation
  - `neural_network.cpp/h`: Neural network integration framework
  - `zobrist.cpp/h`: Transposition table hashing
- **Aseprite Workflow Integration**: Custom pixel art pipeline using Aseprite Wizard addon

## Planned Features

The following features are part of the vision but not yet implemented:

- **Engine Building Mechanics**: Player-driven AI construction and customization system
- **Team Management**: Multi-engine roster with strategic deployment
- **Multiplayer Battles**: Online competitive matchmaking via universal server
- **Tournament Mode**: Friend lobbies and exclusive tournaments
- **Engine Upgrades**: Progressive unlock system for advanced algorithms
- **Neural Network Training**: Player-customizable machine learning models
- **Advanced Move Ordering Options**: Multiple strategies beyond MVV-LVA
- **Performance Analytics**: Detailed engine statistics and performance metrics
- **Spectator Mode**: Watch engine battles unfold
- **Replay System**: Review and analyze past matches
- **Audio System**: Sound effects and background music

### Known Limitations

- Main menu UI is currently commented out in favor of direct scene testing
- No multiplayer networking implementation yet
- Neural network evaluation is structural only (placeholder)
- Limited to single-device play
- No save/load functionality for custom engines
- No progression or unlock system

## Quick Start

### Prerequisites

- **Godot Engine 4.5+** ([Download](https://godotengine.org/download))
- **Python 3.6+** ([Download](https://python.org))
- **Git** ([Download](https://git-scm.com))
- Platform-specific tools (see [Platform Setup](#platform-setup) below)

### Installation

1. **Clone the repository**
   ```bash
   git clone https://github.com/K4-KC/C.H.E.S.S.-Competitive-Hybrid-Engine-Strategy-Simulator
   cd C.H.E.S.S.-Competitive-Hybrid-Engine-Strategy-Simulator
   ```

2. **Install Python dependencies**
   ```bash
   pip install -r requirements.txt
   ```

3. **Clone godot-cpp bindings**
   ```bash
   git clone https://github.com/godotengine/godot-cpp.git
   cd godot-cpp
   git checkout 4.5
   cd ..
   ```

4. **Build the C++ modules** (see [Platform Setup](#platform-setup) for detailed instructions)

5. **Open in Godot**
   - Launch Godot Engine
   - Click "Import" and select `C.H.E.S.S.-Competitive-Hybrid-Engine-Strategy-Simulator/project.godot`
   - Press F5 to run!

## Platform Setup

### Windows (WSL Required)

Windows builds require WSL (Windows Subsystem for Linux) with Ubuntu.

1. **Install WSL**
   ```powershell
   # Run in PowerShell as Administrator
   wsl --install -d Ubuntu
   ```
   Restart your computer if prompted.

2. **Setup WSL Environment**
   ```bash
   # Inside WSL Ubuntu terminal
   sudo apt update
   sudo apt install build-essential python3 python3-pip mingw-w64 git
   pip3 install scons
   ```

3. **Navigate to project in WSL**
   ```bash
   # Windows drives are mounted at /mnt/
   cd /mnt/c/PathToYourProject/C.H.E.S.S.-Competitive-Hybrid-Engine-Strategy-Simulator
   ```

4. **Build modules**
   ```bash
   chmod +x build_modules.sh
   ./build_modules.sh
   ```
   The script will prompt you to select platforms and build types. For Windows development, choose option 1 (Windows) and select your preferred build type.

### Linux

1. **Install dependencies**
   ```bash
   # Debian/Ubuntu
   sudo apt install build-essential python3 python3-pip git

   # Fedora
   sudo dnf install gcc-c++ python3 python3-pip git

   # Arch
   sudo pacman -S base-devel python python-pip git
   ```

2. **Install SCons**
   ```bash
   pip3 install scons
   ```

3. **Build modules**
   ```bash
   chmod +x build_modules.sh
   ./build_modules.sh
   ```

### macOS

1. **Install Xcode Command Line Tools**
   ```bash
   xcode-select --install
   ```

2. **Install Python and SCons**
   ```bash
   # Using Homebrew (install from https://brew.sh if needed)
   brew install python3
   pip3 install scons
   ```

3. **Build modules**
   ```bash
   chmod +x build_modules.sh
   ./build_modules.sh
   ```

## Optional Setup: Stockfish

Stockfish is included for debugging, perft tests, and position analysis.

Download the appropriate Stockfish binary for your platform from [the official Stockfish website](https://stockfishchess.org/download/) and place it in the `stockfish/` directory.

### Windows
Download the Windows binary and extract `stockfish.exe` to the `stockfish/` directory.

### Linux
```bash
cd stockfish/src
make -j $(nproc) build ARCH=x86-64-avx2
```

### macOS
```bash
cd stockfish/src
make -j $(sysctl -n hw.ncpu) build ARCH=apple-silicon  # For Apple Silicon
# or
make -j $(sysctl -n hw.ncpu) build ARCH=x86-64-avx2    # For Intel Macs
```

The compiled binary will be in `stockfish/src/`.

## Project Structure

```
C.H.E.S.S./
├── C.H.E.S.S/                 # Main Godot project
│   ├── assets/               # Sprites, fonts, textures
│   ├── scenes/               # Game scenes (.tscn/.gd)
│   ├── modules/              # C++ source code
│   ├── bin/                  # Compiled libraries (.dll/.so)
│   └── project.godot
├── aseprite/                 # Pixel art source files
├── stockfish/                # Chess engine for testing
├── godot-cpp/                # Godot C++ bindings (clone separately)
├── build_modules.sh          # Cross-platform build script
├── SConstruct                # SCons build configuration
└── requirements.txt          # Python dependencies
```

## Contributing

We'd love your help! This is a casual project with a friendly development environment.

### Areas We Need Help With

- **Visuals**: More pixel art, animations, UI elements
- **AI Improvements**: Better move ordering, opening books, neural network integration
- **Game Features**: Engine building UI, team management, multiplayer networking
- **Testing**: Bug fixes, performance profiling, documentation
- **Translations**: Help make C.H.E.S.S. accessible worldwide

### How to Contribute

1. Fork the repository
2. Create a feature branch (`git checkout -b cool-new-feature`)
3. Make your changes and test them
4. Commit with a clear message (`git commit -m "Add cool feature"`)
5. Push and open a Pull Request

No need to write essays in your PR - just explain what you changed and why. We're here to have fun and build something cool together!

### Code Style

- **GDScript**: Follow [Godot's style guide](https://docs.godotengine.org/en/stable/tutorials/scripting/gdscript/gdscript_styleguide.html)
- **C++**: Use tabs, meaningful names, comment tricky parts

## Getting Help

- **Build Issues**: Double-check you have all prerequisites installed
- **Bug Reports**: Open an issue with what you expected vs what happened
- **Questions**: Start a discussion in the discord server [Woodworks](https://discord.gg/n4kdHQFh)

## License

MIT License

## Acknowledgments

- Built with **Godot Engine 4.5**
- Pixel art made with **Aseprite**
- Powered by the timeless game of chess

---
