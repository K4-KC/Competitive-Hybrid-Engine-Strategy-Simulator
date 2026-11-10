#ifndef AGENT_H
#define AGENT_H

#include "neural_network.h"
#include "board.h"
#include <godot_cpp/variant/dictionary.hpp>
#include <cstdint>

using namespace godot;

// ==================== NEURAL NETWORK INPUT CONFIGURATION ====================

// Input layer size: 12 piece types × 64 squares + extras
// Piece planes: P, N, B, R, Q, K (white), p, n, b, r, q, k (black)
#define NN_PIECE_PLANES     12
#define NN_SQUARES          64
#define NN_PIECE_INPUTS     (NN_PIECE_PLANES * NN_SQUARES)  // 768

// Additional inputs
#define NN_CASTLING_INPUTS  4   // WK, WQ, BK, BQ
#define NN_TURN_INPUT       1   // Side to move (1.0 = white, 0.0 = black)
#define NN_EP_INPUTS        8   // En passant file (one-hot, or 0 if none)

// Total input size
#define NN_TOTAL_INPUTS     (NN_PIECE_INPUTS + NN_CASTLING_INPUTS + NN_TURN_INPUT + NN_EP_INPUTS)  // 781

// ==================== EVALUATION CONSTANTS ====================

#define CHECKMATE_SCORE 100000
#define STALEMATE_SCORE 0

// Piece values for fallback/material evaluation (centipawns)
#define PAWN_VALUE   100
#define KNIGHT_VALUE 320
#define BISHOP_VALUE 330
#define ROOK_VALUE   500
#define QUEEN_VALUE  900

// ==================== MOVE ORDERING CONSTANTS ====================

#define SCORE_TT_MOVE           30000
#define SCORE_QUEEN_PROMOTION   20000
#define SCORE_CAPTURE_BASE      10000
#define SCORE_OTHER_PROMOTION   9000
#define SCORE_KILLER_1          8000
#define SCORE_KILLER_2          7500
#define SCORE_HISTORY_MAX       7000
#define SCORE_QUIET_MOVE        0

// ==================== TRANSPOSITION TABLE ====================

#define TT_FLAG_EXACT  0
#define TT_FLAG_ALPHA  1
#define TT_FLAG_BETA   2

#define TT_SIZE 1048576  // 2^20 entries (~24MB)

struct TTEntry {
    uint64_t key;
    int16_t score;
    int8_t depth;
    uint8_t flag;
    uint8_t best_from;
    uint8_t best_to;
    uint8_t age;
    uint8_t padding;
};

// ==================== KILLER MOVES ====================

#define MAX_PLY 64

struct KillerMove {
    uint8_t from;
    uint8_t to;
    
    inline bool matches(uint8_t f, uint8_t t) const { return from == f && to == t; }
    inline void set(uint8_t f, uint8_t t) { from = f; to = t; }
    inline void clear() { from = 255; to = 255; }
    inline bool is_valid() const { return from != 255; }
};


class Agent : public NeuralNet {
    GDCLASS(Agent, NeuralNet)

private:
    // ==================== BOARD REFERENCE ====================
    Board* board;  // Pointer to the board being analyzed

    // ==================== NEURAL NETWORK CONTROL ====================
    bool use_neural_network;

    // Input feature vector (populated by extract_features)
    std::vector<float> input_features;

    // Extract board state into neural network input format
    // If color is COLOR_BLACK (16), mirrors the board horizontally
    void extract_features(uint8_t color);

    // Mirror a square index horizontally (rank 0 ↔ rank 7, etc.)
    inline uint8_t mirror_square_horizontal(uint8_t square) const {
        int rank = square / 8;
        int file = square % 8;
        int mirrored_rank = 7 - rank;
        return mirrored_rank * 8 + file;
    }

    // ==================== TRANSPOSITION TABLE ====================
    static TTEntry* tt_table;
    static bool tt_initialized;
    static uint8_t tt_age;
    
    static void init_tt();
    void tt_store(uint64_t key, int score, int depth, int flag, uint8_t best_from, uint8_t best_to);
    TTEntry* tt_probe(uint64_t key) const;
    void tt_clear();
    void tt_new_search();
    
    // ==================== KILLER MOVES ====================
    KillerMove killer_moves[MAX_PLY][2];
    
    void clear_killers();
    void store_killer(int ply, uint8_t from, uint8_t to);
    int is_killer(int ply, uint8_t from, uint8_t to) const;
    
    // ==================== HISTORY HEURISTIC ====================
    int32_t history_table[64][64];
    static const int32_t HISTORY_MAX = 400000;
    
    void clear_history();
    void update_history(uint8_t from, uint8_t to, int depth);
    int32_t get_history(uint8_t from, uint8_t to) const;
    
    // ==================== MVV-LVA TABLE ====================
    static int16_t mvv_lva_table[7][7];
    static bool mvv_lva_initialized;
    static void init_mvv_lva_table();
    
    // ==================== MOVE ORDERING ====================
    int16_t score_move(const FastMove &m, uint8_t tt_best_from, uint8_t tt_best_to, int ply) const;
    void score_moves(MoveList &moves, uint8_t tt_best_from, uint8_t tt_best_to, int ply) const;
    void sort_moves(MoveList &moves) const;
    
    // ==================== SEARCH ALGORITHMS ====================
    int minimax_internal(int depth, int ply, int alpha, int beta, bool is_maximizing);

protected:
    static void _bind_methods();

public:
    Agent();
    ~Agent();

    void _ready() override;

    // ==================== BOARD BINDING ====================
    void set_board(Board* p_board);
    Board* get_board() const { return board; }

    // ==================== EVALUATION ====================
    // Main evaluation function - returns score from the given color's perspective
    // color: COLOR_WHITE (8) or COLOR_BLACK (16)
    int evaluate(uint8_t color);

    // Simple material evaluation (fallback/baseline)
    int evaluate_material() const;

    // Debug: Get raw feature vector as Array (from white's perspective)
    Array get_features();

    // Get features from a specific color's perspective
    Array get_features_for_color(uint8_t color);

    // ==================== NEURAL NETWORK CONTROL ====================
    // Enable/disable neural network evaluation
    void set_use_neural_network(bool use_nn);
    bool get_use_neural_network() const { return use_neural_network; }

    // ==================== SEARCH INTERFACE ====================
    Dictionary run_iterative_deepening(int max_depth);
    Dictionary get_best_move(int depth);

    // ==================== TRAINING INTERFACE ====================
    // Train on the current board position using material evaluation as target
    // This trains the neural network to match the material evaluation function
    // color: The perspective to train from (COLOR_WHITE or COLOR_BLACK)
    // learning_rate: Step size for gradient descent
    // Returns the loss (mean squared error)
    float train_on_current_position(uint8_t color, float learning_rate);

    // Train on a batch of positions collected during a game
    // positions: Array of feature vectors (each is an Array of 781 floats)
    // targets: Array of target values (floats between 0.0 and 1.0)
    // learning_rate: Step size for gradient descent
    // Returns average loss across the batch
    float train_on_batch(const Array &positions, const Array &targets, float learning_rate);

    // Convert material evaluation score to a 0.0-1.0 target for neural network training
    // Positive scores (good for current color) → values closer to 1.0
    // Negative scores (bad for current color) → values closer to 0.0
    float score_to_target(int material_score) const;
};

#endif // AGENT_H
