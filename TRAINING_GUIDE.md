# Chess AI Neural Network Training Guide

This guide explains the complete training system implemented for the chess AI neural network agents.

## Overview

The training system supports **three modes**:
1. **None (Mode 0)**: Inference only - no training
2. **Heuristic Training (Mode 1)**: Train using material evaluation
3. **Distillation Training (Mode 2)**: Train using tree search amplification

## Training Architecture

### Neural Network Structure

- **Input**: 781 features
  - 768: Piece-square representation (12 piece types × 64 squares)
  - 4: Castling rights
  - 1: Side to move
  - 8: En passant file (one-hot encoded)
- **Hidden Layers**: 256 → 128 neurons (ReLU activation)
- **Output**: 1 neuron (Sigmoid activation, outputs 0.0-1.0)

### Training Algorithm

- **Optimizer**: Stochastic Gradient Descent (SGD)
- **Loss Function**: Mean Squared Error (MSE)
- **Learning Rate**: 0.001 (configurable)
- **Backpropagation**: Full backpropagation through all layers

---

## Mode 1: Heuristic Training

### Purpose
Train the neural network to approximate the material evaluation function.

### How It Works

1. **Extract Features**: Convert board state to 781-dimensional feature vector
2. **Compute Material Score**: Count piece values (P=100, N=320, B=330, R=500, Q=900)
3. **Convert to Target**: Transform centipawn score to 0.0-1.0 range using sigmoid:
   ```
   target = 1 / (1 + exp(-score / 600))
   ```
4. **Forward Pass**: Network predicts evaluation
5. **Backward Pass**: Compute gradients
6. **Weight Update**: Apply gradient descent

### When to Use

- **First training stage**: Start with untrained (random) networks
- **Goal**: Learn basic position evaluation based on material

### Configuration

```gdscript
TRAINING_MODE = 1
LOAD_MODELS_ON_START = false
LEARNING_RATE = 0.001
TRAIN_EVERY_N_MOVES = 1
```

### Expected Results

- Network learns to evaluate positions based on material count
- Loss should decrease over time (typical final loss: 0.01-0.05)
- Agents will play moves that preserve/gain material

---

## Mode 2: Distillation Training

### Purpose
Amplify the network's knowledge by training it to match tree search evaluations.

### How It Works

1. **Load Pre-trained Model**: Must have completed heuristic training first
2. **Tree Search "Teacher"**:
   - Agent performs minimax search to depth 3 (configurable)
   - Search uses neural network evaluation at leaf nodes
   - Returns best move score from tree
3. **Extract Features**: Get current position features
4. **Distillation Target**: Use tree search score as "teacher" signal
5. **Train Network**: Network learns to approximate multi-move lookahead

### The Distillation Process

```
Depth 3 Tree Search → Score (e.g., +250)
         ↓
   score_to_target() → Target (e.g., 0.72)
         ↓
   Neural Network ← Train to match this target
```

### Why This Works (Amplification)

The network learns to:
- Predict the result of 3-ply search in a single forward pass
- Recognize tactical patterns (forks, pins, skewers)
- Evaluate positions more deeply than just material

This is a form of **knowledge distillation** where:
- **Teacher**: Tree search (slow but accurate)
- **Student**: Neural network (fast but initially approximate)

### When to Use

- **Second training stage**: After heuristic training
- **Iterative improvement**: Can run multiple times with increasing depth
- **Goal**: Compress multi-move search into single evaluation

### Configuration

```gdscript
TRAINING_MODE = 2
LOAD_MODELS_ON_START = false  # Models loaded automatically in distillation mode
LEARNING_RATE = 0.001
TRAIN_EVERY_N_MOVES = 1
DISTILLATION_SEARCH_DEPTH = 3  # Teacher search depth
```

### Expected Results

- Network learns tactical patterns
- Loss may initially increase (network adapting to harder targets)
- Agents play more tactical, forward-looking moves
- Evaluation becomes more sophisticated than pure material

---

## Training Workflow

### Recommended Training Pipeline

#### Stage 1: Heuristic Training
```gdscript
TRAINING_MODE = 1              # Heuristic mode
LOAD_MODELS_ON_START = false   # Start from random
SAVE_MODELS_AFTER_GAME = true
```

**Run**: Play 1 game (typically 30-60 moves)

**Result**: Models saved to:
- `res://assets/models/white_agent.nn`
- `res://assets/models/black_agent.nn`

#### Stage 2: Distillation Training
```gdscript
TRAINING_MODE = 2              # Distillation mode
LOAD_MODELS_ON_START = false   # Auto-loads for distillation
SAVE_MODELS_AFTER_GAME = true
DISTILLATION_SEARCH_DEPTH = 3  # Start with depth 3
```

**Run**: Play 1 game

**Result**: Models improved and re-saved

#### Stage 3: Advanced Distillation (Optional)
```gdscript
TRAINING_MODE = 2
DISTILLATION_SEARCH_DEPTH = 4  # Increase depth
```

**Run**: Play additional games for further refinement

---

## Implementation Details

### C++ Side (Low-Level Training)

#### `NeuralNet` Class ([neural_network.h](src/neural_network.h), [neural_network.cpp](src/neural_network.cpp))

**Core Methods**:
- `train_single_example()`: Complete training step (forward + backward + update)
- `backpropagate()`: Compute gradients via backpropagation
- `update_weights()`: Apply gradient descent
- `clear_gradients()`: Reset gradient accumulators

**Backpropagation Algorithm**:
```cpp
// 1. Output layer delta
delta[L] = (output - target) * sigmoid'(output)

// 2. Hidden layer deltas (reverse order)
for layer L-1 to 1:
    delta[l] = Σ(delta[l+1] * weights[l]) * activation'(z[l])

// 3. Compute gradients
∇W[l] = delta[l+1] ⊗ activation[l]
∇b[l] = delta[l+1]

// 4. Update weights
W[l] -= learning_rate * ∇W[l]
b[l] -= learning_rate * ∇b[l]
```

#### `Agent` Class ([agent.h](src/agent.h), [agent.cpp](src/agent.cpp))

**Training Methods**:
- `train_on_current_position()`: Heuristic training wrapper
- `train_on_batch()`: Batch training support
- `score_to_target()`: Converts centipawn score to 0.0-1.0 target

**Score Conversion**:
```cpp
target = 1 / (1 + exp(-score / 600))

Examples:
  +300 centipawns → 0.75
     0 centipawns → 0.50
  -300 centipawns → 0.25
  +1000 centipawns → 0.88
```

### GDScript Side (High-Level Orchestration)

#### [computer_vs_computer.gd](keychan-chess/scenes/computer_vs_computer.gd)

**Training Functions**:
- `train_agents_on_current_position()`: Main training coordinator
- `train_heuristic()`: Mode 1 implementation
- `train_distillation()`: Mode 2 implementation
- `finalize_training()`: End-of-game statistics

**Distillation Implementation**:
```gdscript
func train_distillation(agent, color):
    # 1. Get current features
    features = agent.get_features_for_color(color)

    # 2. Run tree search (teacher)
    search = agent.run_iterative_deepening(DISTILLATION_SEARCH_DEPTH)
    search_score = search["score"]

    # 3. Convert score to target
    target = agent.score_to_target(search_score)

    # 4. Train network (student)
    loss = agent.train_single_example(features, target, LEARNING_RATE)

    return loss
```

---

## Model Files

### File Format

Binary format (`.nn` extension):
```
Magic Number: "NNWB" (4 bytes)
Version: 1 (4 bytes)
Layer Sizes: [num_layers] × 4 bytes
Activation Functions: [num_hidden] × 4 bytes
Weights & Biases: Float arrays
```

### Storage Location

```
res://assets/models/
├── white_agent.nn  # White's neural network
└── black_agent.nn  # Black's neural network
```

### Loading Models

**Automatic (in code)**:
```gdscript
LOAD_MODELS_ON_START = true
```

**Manual (GDScript)**:
```gdscript
white_agent.load_network("res://assets/models/white_agent.nn")
black_agent.load_network("res://assets/models/black_agent.nn")
```

**Manual (C++)**:
```cpp
agent->load_network("res://assets/models/white_agent.nn");
```

---

## Monitoring Training

### Console Output

#### During Game:
```
[Heuristic Training] Move 10 | Avg Loss: 0.034562 | White Loss: 0.031245 | Black Loss: 0.037879
```

#### End of Game:
```
=== TRAINING SUMMARY ===
Training Mode: Heuristic
Total positions trained: 84
Average loss: 0.028934
Final material evaluation:
  White's perspective: 0 centipawns
  Black's perspective: 0 centipawns
```

### Interpreting Loss

- **Good loss**: < 0.05 (network is learning well)
- **Acceptable loss**: 0.05 - 0.10
- **High loss**: > 0.10 (may need more training or lower learning rate)

**Note**: Loss may increase when switching from heuristic to distillation (network adapting to harder targets).

---

## Advanced Usage

### Custom Training Loop

```gdscript
# Train for multiple games
for game in range(10):
    # Reset board
    board.setup_board("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1")

    # Play game with training
    while not board.is_game_over():
        make_next_ai_move()

        if move_count % TRAIN_EVERY_N_MOVES == 0:
            train_agents_on_current_position()

    # Save after each game
    if game % 5 == 0:
        save_trained_models()
```

### Adjusting Learning Rate

```gdscript
# Fast learning (may be unstable)
LEARNING_RATE = 0.01

# Standard learning
LEARNING_RATE = 0.001

# Fine-tuning (slow but stable)
LEARNING_RATE = 0.0001
```

### Progressive Distillation

```gdscript
# Stage 1: Heuristic
TRAINING_MODE = 1
# ... play games ...

# Stage 2: Shallow search
TRAINING_MODE = 2
DISTILLATION_SEARCH_DEPTH = 2
# ... play games ...

# Stage 3: Medium search
DISTILLATION_SEARCH_DEPTH = 3
# ... play games ...

# Stage 4: Deep search
DISTILLATION_SEARCH_DEPTH = 4
# ... play games ...
```

---

## Troubleshooting

### Error: "Distillation mode requires existing trained models"

**Cause**: Trying to use mode 2 without pre-trained models

**Solution**: Run heuristic training first (mode 1)

### Loss Not Decreasing

**Possible causes**:
1. Learning rate too high → Reduce to 0.0001
2. Network architecture too small → Increase hidden layer sizes
3. Training data too noisy → Increase TRAIN_EVERY_N_MOVES

### Network Outputs Always 0.5

**Cause**: Weights not being updated

**Solution**: Verify `TRAINING_MODE > 0` and check console for training messages

### Models Not Saving

**Cause**: Directory doesn't exist

**Solution**: Create `res://assets/models/` manually or check write permissions

---

## Theory: Why This Works

### Heuristic Training (Supervised Learning)

The material evaluation function provides a simple but **consistent** supervision signal:
- **Pros**: Fast, stable, always available
- **Cons**: Limited to material counting, misses tactics
- **Result**: Network learns basic evaluation

### Distillation Training (Self-Amplification)

Tree search + neural network = **better evaluation** than either alone:

1. **Network alone**: Fast but shallow (1-ply)
2. **Tree search**: Slow but deep (3-ply+)
3. **Distillation**: Network learns to approximate 3-ply in 1-ply!

This is called **knowledge distillation** and **self-amplification**:
- Network becomes its own teacher
- Each iteration makes the network stronger
- Amplifies knowledge without external data

### Mathematical Perspective

**Heuristic**: `Network ← Material(position)`

**Distillation**: `Network ← TreeSearch(Network, position, depth)`

The network learns to predict the result of tree search, effectively learning:
- Tactical patterns
- Positional concepts
- Multi-move plans

---

## Future Improvements

### Potential Enhancements

1. **Batch Training**: Accumulate gradients over multiple positions
2. **Replay Buffer**: Store best positions for repeated training
3. **Curriculum Learning**: Gradually increase distillation depth
4. **Policy Network**: Add move prediction head
5. **Temporal Difference Learning**: Train on position deltas

### AlphaZero-Style Training

Current system can be extended toward AlphaZero by:
1. Adding Monte Carlo Tree Search (MCTS)
2. Using game outcomes as targets
3. Training on self-play games
4. Adding policy head for move prediction

---

## Summary

This training system implements a **two-stage pipeline**:

**Stage 1 (Heuristic)**: Learn material evaluation
- Fast bootstrapping
- Stable training
- Foundation knowledge

**Stage 2 (Distillation)**: Self-amplification through search
- Learn tactics
- Compress lookahead
- Continuous improvement

The combination creates an agent that:
- Evaluates positions accurately
- Plays tactically sound moves
- Improves through self-play

**Key Innovation**: The network learns to predict tree search results, effectively making it "think deeper" in a single forward pass.
