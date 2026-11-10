#ifndef BOARD_H
#define BOARD_H

#include <godot_cpp/classes/node2d.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/binder_common.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/vector2i.hpp>
#include <vector>
#include <cstdint>
#include <cstring>
#include <climits>
#include <algorithm>

using namespace godot;

// Piece type constants (lowest 3 bits)
#define PIECE_NONE   0
#define PIECE_PAWN   1
#define PIECE_KNIGHT 2
#define PIECE_BISHOP 3
#define PIECE_ROOK   4
#define PIECE_QUEEN  5
#define PIECE_KING   6

// Color constants (bits 3-4)
#define COLOR_NONE  0
#define COLOR_WHITE 8
#define COLOR_BLACK 16

// Masks for bitwise operations
#define PIECE_TYPE_MASK 7
#define COLOR_MASK      24

// Helper macros - inline for performance
#define GET_PIECE_TYPE(square) ((square) & PIECE_TYPE_MASK)
#define GET_COLOR(square) ((square) & COLOR_MASK)
#define MAKE_PIECE(type, color) ((type) | (color))
#define IS_EMPTY(square) (((square) & PIECE_TYPE_MASK) == 0)
#define IS_WHITE(square) (((square) & COLOR_MASK) == COLOR_WHITE)
#define IS_BLACK(square) (((square) & COLOR_MASK) == COLOR_BLACK)

// Direction constants
#define DIR_N   8
#define DIR_S  -8
#define DIR_E   1
#define DIR_W  -1
#define DIR_NE  9
#define DIR_NW  7
#define DIR_SE -7
#define DIR_SW -9

// ==================== MOVE STRUCTURES ====================

// Lightweight move for fast operations
struct FastMove {
    uint8_t from;
    uint8_t to;
    uint8_t flags;      // bit 0: capture, bit 1: ep, bit 2: castling, bits 3-5: promotion piece
    uint8_t captured;
    int16_t score;      // Move ordering score (used by NeuralNet)
};

// Full move structure for game history
struct Move {
    uint8_t from;
    uint8_t to;
    uint8_t captured_piece;
    uint8_t promotion_piece;
    bool is_castling;
    bool is_en_passant;
    uint8_t en_passant_target_before;
    uint8_t halfmove_clock_before;
    bool castling_rights_before[4];
    uint64_t hash_before;
};

// Pre-allocated move list to avoid heap allocations
struct MoveList {
    FastMove moves[256];
    int count;
    
    inline void clear() { count = 0; }
    inline void add(uint8_t from, uint8_t to, uint8_t flags = 0, uint8_t captured = 0) {
        moves[count++] = {from, to, flags, captured, 0};
    }
};

// ==================== BOARD CLASS (STATE MANAGER) ====================

class Board : public Node2D {
    GDCLASS(Board, Node2D)

    // Allow NeuralNet to access private members for efficient evaluation
    friend class NeuralNet;

private:
    // Board state: 64 squares
    uint8_t squares[64];

    // Cached king positions for fast lookup
    uint8_t white_king_pos;
    uint8_t black_king_pos;

    // ==================== PIECE LIST OPTIMIZATION ====================
    // Track all piece positions for fast iteration (avoid scanning empty squares)
    uint8_t white_piece_list[16];  // Max 16 pieces per side
    uint8_t black_piece_list[16];
    uint8_t white_piece_count;
    uint8_t black_piece_count;

    void rebuild_piece_lists();
    inline void add_piece_to_list(uint8_t square, uint8_t piece) {
        if (IS_WHITE(piece)) {
            if (white_piece_count < 16) {
                white_piece_list[white_piece_count++] = square;
            }
        } else {
            if (black_piece_count < 16) {
                black_piece_list[black_piece_count++] = square;
            }
        }
    }
    inline void remove_piece_from_list(uint8_t square, uint8_t piece) {
        if (IS_WHITE(piece)) {
            for (uint8_t i = 0; i < white_piece_count; i++) {
                if (white_piece_list[i] == square) {
                    white_piece_list[i] = white_piece_list[--white_piece_count];
                    break;
                }
            }
        } else {
            for (uint8_t i = 0; i < black_piece_count; i++) {
                if (black_piece_list[i] == square) {
                    black_piece_list[i] = black_piece_list[--black_piece_count];
                    break;
                }
            }
        }
    }
    
    // Game state
    uint8_t turn;
    std::vector<Move> move_history;
    std::vector<String> move_history_notation;
    
    // Castling rights: [0]=WK, [1]=WQ, [2]=BK, [3]=BQ
    bool castling_rights[4];
    
    // En passant target square (0-63, or 255 if none)
    uint8_t en_passant_target;
    
    // Halfmove clock and fullmove number
    uint8_t halfmove_clock;
    uint16_t fullmove_number;
    
    // Promotion handling
    uint8_t promotion_pending_from;
    uint8_t promotion_pending_to;
    bool promotion_pending;
    
    // ==================== ZOBRIST HASHING ====================
    uint64_t current_hash;

    // ==================== PRECOMPUTED TABLES ====================
    static bool tables_initialized;
    static uint8_t knight_attack_squares[64][8];
    static uint8_t knight_attack_count[64];
    static uint8_t king_attack_squares[64][8];
    static uint8_t king_attack_count[64];
    static uint8_t squares_to_edge[64][8];
    
    static void init_attack_tables();
    
    // ==================== ZOBRIST HELPERS ====================
    uint64_t calculate_hash() const;
    int get_zobrist_piece_index(uint8_t piece) const;
    void hash_piece(uint8_t piece, uint8_t square);
    void hash_castling(int right);
    void hash_en_passant(uint8_t ep_square);
    void hash_side();
    
    // ==================== INTERNAL HELPERS ====================
    void clear_board();
    void initialize_starting_position();
    bool parse_fen(const String &fen);
    String generate_fen() const;
    void update_king_cache();
    
    void add_pawn_moves(uint8_t pos, Array &moves) const;
    void add_knight_moves(uint8_t pos, Array &moves) const;
    void add_bishop_moves(uint8_t pos, Array &moves) const;
    void add_rook_moves(uint8_t pos, Array &moves) const;
    void add_queen_moves(uint8_t pos, Array &moves) const;
    void add_king_moves(uint8_t pos, Array &moves) const;
    Array get_pseudo_legal_moves_for_piece(uint8_t pos) const;
    
    void make_move_internal(uint8_t from, uint8_t to, Move &move_record);
    void revert_move_internal(const Move &move);
    
    bool can_castle_kingside(uint8_t color) const;
    bool can_castle_queenside(uint8_t color) const;
    void add_castling_moves(uint8_t pos, Array &moves) const;
    
    String move_to_notation(const Move &move) const;
    bool would_be_in_check_after_move(uint8_t from, uint8_t to, uint8_t color);

protected:
    static void _bind_methods();

public:
    Board();
    ~Board();

    void _ready() override;
    
    // ==================== PUBLIC STATE ACCESS ====================
    uint8_t get_turn() const;
    uint8_t get_piece_on_square(uint8_t pos) const;
    void set_piece_on_square(uint8_t pos, uint8_t piece);
    uint64_t get_hash() const { return current_hash; }
    
    // Direct access for NeuralNet (efficient evaluation)
    const uint8_t* get_squares() const { return squares; }
    const bool* get_castling_rights() const { return castling_rights; }
    uint8_t get_en_passant_target() const { return en_passant_target; }
    uint8_t get_white_king_pos() const { return white_king_pos; }
    uint8_t get_black_king_pos() const { return black_king_pos; }

    // Piece list access for optimized iteration
    const uint8_t* get_white_piece_list() const { return white_piece_list; }
    const uint8_t* get_black_piece_list() const { return black_piece_list; }
    uint8_t get_white_piece_count() const { return white_piece_count; }
    uint8_t get_black_piece_count() const { return black_piece_count; }
    
    inline uint8_t get_king_pos(uint8_t color) const {
        return (color == 0) ? white_king_pos : black_king_pos;
    }
    
    // ==================== BOARD SETUP ====================
    void setup_board(const String &fen_notation);
    String get_fen() const;
    
    // ==================== MOVE INTERFACE ====================
    uint8_t attempt_move(uint8_t start, uint8_t end);
    void commit_promotion(const String &type_str);
    void revert_move();
    Array get_moves() const;
    void make_move(uint8_t start, uint8_t end);
    
    // ==================== MOVE GENERATION (Public for NeuralNet) ====================
    bool is_square_attacked_fast(uint8_t pos, uint8_t attacking_color) const;
    bool is_king_in_check(uint8_t color) const;
    bool has_legal_moves() const;
    
    inline void generate_pawn_moves(uint8_t pos, MoveList &moves) const;
    inline void generate_knight_moves(uint8_t pos, MoveList &moves) const;
    inline void generate_bishop_moves(uint8_t pos, MoveList &moves) const;
    inline void generate_rook_moves(uint8_t pos, MoveList &moves) const;
    inline void generate_queen_moves(uint8_t pos, MoveList &moves) const;
    inline void generate_king_moves(uint8_t pos, MoveList &moves) const;
    inline void generate_castling_moves(uint8_t pos, MoveList &moves) const;
    void generate_all_pseudo_legal(MoveList &moves) const;
    
    // Fast make/unmake for search (public for NeuralNet)
    void make_move_fast(const FastMove &m);
    void unmake_move_fast(const FastMove &m, uint8_t ep_before, bool castling_before[4], uint64_t hash_before);
    
    // ==================== GAME STATE QUERIES ====================
    Array get_all_possible_moves(uint8_t color);
    Array get_legal_moves_for_piece(uint8_t square);
    bool is_checkmate(uint8_t color);
    bool is_stalemate(uint8_t color);
    bool is_check(uint8_t color) const;
    bool is_game_over();
    int get_game_result();
    
    // ==================== PERFT (Logic Verification) ====================
    uint64_t count_all_moves(uint8_t depth);
    Dictionary get_perft_analysis(uint8_t depth);
    
    // ==================== UTILITY ====================
    Vector2i pos_to_coords(uint8_t pos) const;
    uint8_t coords_to_pos(int rank, int file) const;
    String square_to_algebraic(uint8_t pos) const;
    uint8_t algebraic_to_square(const String &algebraic) const;
};

#endif // BOARD_H
