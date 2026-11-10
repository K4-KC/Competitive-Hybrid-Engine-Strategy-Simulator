#include "board.h"
#include "zobrist.h"
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <cstring>
#include <algorithm>

using namespace godot;

// ==================== STATIC MEMBER DEFINITIONS ====================

bool Board::tables_initialized = false;
uint8_t Board::knight_attack_squares[64][8];
uint8_t Board::knight_attack_count[64];
uint8_t Board::king_attack_squares[64][8];
uint8_t Board::king_attack_count[64];
uint8_t Board::squares_to_edge[64][8];

// Knight move offsets: {file_delta, rank_delta}
static const int KNIGHT_DELTAS[8][2] = {
    {1, 2}, {2, 1}, {2, -1}, {1, -2},
    {-1, -2}, {-2, -1}, {-2, 1}, {-1, 2}
};

// King move offsets
static const int KING_DELTAS[8][2] = {
    {0, 1}, {0, -1}, {1, 0}, {-1, 0},
    {1, 1}, {1, -1}, {-1, 1}, {-1, -1}
};

// Direction offsets (N, S, E, W, NE, NW, SE, SW)
static const int DIR_OFFSETS[8] = {8, -8, 1, -1, 9, 7, -7, -9};

// ==================== STATIC INITIALIZATION ====================

void Board::init_attack_tables() {
    if (tables_initialized) return;
    
    for (int sq = 0; sq < 64; sq++) {
        int file = sq % 8;
        int rank = sq / 8;
        
        // Knight attacks
        knight_attack_count[sq] = 0;
        for (int i = 0; i < 8; i++) {
            int new_file = file + KNIGHT_DELTAS[i][0];
            int new_rank = rank + KNIGHT_DELTAS[i][1];
            if (new_file >= 0 && new_file < 8 && new_rank >= 0 && new_rank < 8) {
                knight_attack_squares[sq][knight_attack_count[sq]++] = new_rank * 8 + new_file;
            }
        }
        
        // King attacks
        king_attack_count[sq] = 0;
        for (int i = 0; i < 8; i++) {
            int new_file = file + KING_DELTAS[i][0];
            int new_rank = rank + KING_DELTAS[i][1];
            if (new_file >= 0 && new_file < 8 && new_rank >= 0 && new_rank < 8) {
                king_attack_squares[sq][king_attack_count[sq]++] = new_rank * 8 + new_file;
            }
        }
        
        // Squares to edge
        squares_to_edge[sq][0] = 7 - rank;
        squares_to_edge[sq][1] = rank;
        squares_to_edge[sq][2] = 7 - file;
        squares_to_edge[sq][3] = file;
        squares_to_edge[sq][4] = (7 - rank < 7 - file) ? 7 - rank : 7 - file;
        squares_to_edge[sq][5] = (7 - rank < file) ? 7 - rank : file;
        squares_to_edge[sq][6] = (rank < 7 - file) ? rank : 7 - file;
        squares_to_edge[sq][7] = (rank < file) ? rank : file;
    }
    
    tables_initialized = true;
}

// ==================== ZOBRIST HASHING ====================

int Board::get_zobrist_piece_index(uint8_t piece) const {
    if (IS_EMPTY(piece)) return -1;
    
    uint8_t type = GET_PIECE_TYPE(piece);
    bool is_white = IS_WHITE(piece);
    
    return (type - 1) + (is_white ? 0 : 6);
}

void Board::hash_piece(uint8_t piece, uint8_t square) {
    int piece_index = get_zobrist_piece_index(piece);
    if (piece_index >= 0 && square < 64) {
        current_hash ^= Zobrist::piece_keys[piece_index][square];
    }
}

void Board::hash_castling(int right) {
    if (right >= 0 && right < 4) {
        current_hash ^= Zobrist::castling_keys[right];
    }
}

void Board::hash_en_passant(uint8_t ep_square) {
    if (ep_square < 64) {
        int file = ep_square % 8;
        current_hash ^= Zobrist::en_passant_keys[file];
    }
}

void Board::hash_side() {
    current_hash ^= Zobrist::side_key;
}

uint64_t Board::calculate_hash() const {
    uint64_t hash = 0;
    
    for (int sq = 0; sq < 64; sq++) {
        uint8_t piece = squares[sq];
        if (!IS_EMPTY(piece)) {
            int piece_index = get_zobrist_piece_index(piece);
            if (piece_index >= 0) {
                hash ^= Zobrist::piece_keys[piece_index][sq];
            }
        }
    }
    
    for (int i = 0; i < 4; i++) {
        if (castling_rights[i]) {
            hash ^= Zobrist::castling_keys[i];
        }
    }
    
    if (en_passant_target < 64) {
        int file = en_passant_target % 8;
        hash ^= Zobrist::en_passant_keys[file];
    }
    
    if (turn == 1) {
        hash ^= Zobrist::side_key;
    }
    
    return hash;
}

// ==================== CONSTRUCTOR/DESTRUCTOR ====================

Board::Board() {
    Zobrist::init();
    init_attack_tables();
    
    clear_board();
    turn = 0;
    en_passant_target = 255;
    halfmove_clock = 0;
    fullmove_number = 1;
    promotion_pending = false;
    promotion_pending_from = 0;
    promotion_pending_to = 0;
    white_king_pos = 4;
    black_king_pos = 60;
    current_hash = 0;
    
    for (int i = 0; i < 4; i++) {
        castling_rights[i] = true;
    }
}

Board::~Board() {
}

void Board::_ready() {
    initialize_starting_position();
}

void Board::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_turn"), &Board::get_turn);
    ClassDB::bind_method(D_METHOD("get_piece_on_square", "pos"), &Board::get_piece_on_square);
    ClassDB::bind_method(D_METHOD("set_piece_on_square", "pos", "piece"), &Board::set_piece_on_square);
    ClassDB::bind_method(D_METHOD("setup_board", "fen_notation"), &Board::setup_board);
    ClassDB::bind_method(D_METHOD("get_fen"), &Board::get_fen);
    ClassDB::bind_method(D_METHOD("attempt_move", "start", "end"), &Board::attempt_move);
    ClassDB::bind_method(D_METHOD("commit_promotion", "type_str"), &Board::commit_promotion);
    ClassDB::bind_method(D_METHOD("revert_move"), &Board::revert_move);
    ClassDB::bind_method(D_METHOD("get_moves"), &Board::get_moves);
    ClassDB::bind_method(D_METHOD("get_all_possible_moves", "color"), &Board::get_all_possible_moves);
    ClassDB::bind_method(D_METHOD("get_legal_moves_for_piece", "square"), &Board::get_legal_moves_for_piece);
    ClassDB::bind_method(D_METHOD("get_perft_analysis", "depth"), &Board::get_perft_analysis);
    ClassDB::bind_method(D_METHOD("make_move", "start", "end"), &Board::make_move);
    ClassDB::bind_method(D_METHOD("is_checkmate", "color"), &Board::is_checkmate);
    ClassDB::bind_method(D_METHOD("is_stalemate", "color"), &Board::is_stalemate);
    ClassDB::bind_method(D_METHOD("is_check", "color"), &Board::is_check);
    ClassDB::bind_method(D_METHOD("is_game_over"), &Board::is_game_over);
    ClassDB::bind_method(D_METHOD("get_game_result"), &Board::get_game_result);
    ClassDB::bind_method(D_METHOD("pos_to_coords", "pos"), &Board::pos_to_coords);
    ClassDB::bind_method(D_METHOD("coords_to_pos", "rank", "file"), &Board::coords_to_pos);
    ClassDB::bind_method(D_METHOD("square_to_algebraic", "pos"), &Board::square_to_algebraic);
    ClassDB::bind_method(D_METHOD("algebraic_to_square", "algebraic"), &Board::algebraic_to_square);
}

// ==================== BOARD SETUP ====================

void Board::clear_board() {
    memset(squares, 0, sizeof(squares));
    move_history.clear();
    move_history_notation.clear();
    white_king_pos = 255;
    black_king_pos = 255;
    white_piece_count = 0;
    black_piece_count = 0;
    current_hash = 0;
}

void Board::rebuild_piece_lists() {
    white_piece_count = 0;
    black_piece_count = 0;

    for (uint8_t sq = 0; sq < 64; sq++) {
        uint8_t piece = squares[sq];
        if (!IS_EMPTY(piece)) {
            add_piece_to_list(sq, piece);
        }
    }
}

void Board::update_king_cache() {
    white_king_pos = 255;
    black_king_pos = 255;
    for (int i = 0; i < 64; i++) {
        if (GET_PIECE_TYPE(squares[i]) == PIECE_KING) {
            if (IS_WHITE(squares[i])) white_king_pos = i;
            else black_king_pos = i;
        }
    }
}

void Board::initialize_starting_position() {
    clear_board();
    
    squares[0] = MAKE_PIECE(PIECE_ROOK, COLOR_WHITE);
    squares[1] = MAKE_PIECE(PIECE_KNIGHT, COLOR_WHITE);
    squares[2] = MAKE_PIECE(PIECE_BISHOP, COLOR_WHITE);
    squares[3] = MAKE_PIECE(PIECE_QUEEN, COLOR_WHITE);
    squares[4] = MAKE_PIECE(PIECE_KING, COLOR_WHITE);
    squares[5] = MAKE_PIECE(PIECE_BISHOP, COLOR_WHITE);
    squares[6] = MAKE_PIECE(PIECE_KNIGHT, COLOR_WHITE);
    squares[7] = MAKE_PIECE(PIECE_ROOK, COLOR_WHITE);
    
    for (int i = 8; i < 16; i++) {
        squares[i] = MAKE_PIECE(PIECE_PAWN, COLOR_WHITE);
    }
    
    for (int i = 48; i < 56; i++) {
        squares[i] = MAKE_PIECE(PIECE_PAWN, COLOR_BLACK);
    }
    
    squares[56] = MAKE_PIECE(PIECE_ROOK, COLOR_BLACK);
    squares[57] = MAKE_PIECE(PIECE_KNIGHT, COLOR_BLACK);
    squares[58] = MAKE_PIECE(PIECE_BISHOP, COLOR_BLACK);
    squares[59] = MAKE_PIECE(PIECE_QUEEN, COLOR_BLACK);
    squares[60] = MAKE_PIECE(PIECE_KING, COLOR_BLACK);
    squares[61] = MAKE_PIECE(PIECE_BISHOP, COLOR_BLACK);
    squares[62] = MAKE_PIECE(PIECE_KNIGHT, COLOR_BLACK);
    squares[63] = MAKE_PIECE(PIECE_ROOK, COLOR_BLACK);
    
    turn = 0;
    for (int i = 0; i < 4; i++) castling_rights[i] = true;
    en_passant_target = 255;
    halfmove_clock = 0;
    fullmove_number = 1;
    promotion_pending = false;
    white_king_pos = 4;
    black_king_pos = 60;

    rebuild_piece_lists();
    current_hash = calculate_hash();
}

bool Board::parse_fen(const String &fen) {
    clear_board();
    
    PackedStringArray parts = fen.split(" ");
    if (parts.size() < 1) return false;
    
    String placement = parts[0];
    int square = 56;
    
    for (int i = 0; i < placement.length(); i++) {
        char32_t c = placement[i];
        
        if (c == '/') {
            square -= 16;
            continue;
        }
        
        if (c >= '1' && c <= '8') {
            square += (c - '0');
            continue;
        }
        
        uint8_t piece = PIECE_NONE;
        uint8_t color = (c >= 'A' && c <= 'Z') ? COLOR_WHITE : COLOR_BLACK;
        
        char32_t lower_c = (c >= 'A' && c <= 'Z') ? (c + 32) : c;
        
        switch (lower_c) {
            case 'p': piece = PIECE_PAWN; break;
            case 'n': piece = PIECE_KNIGHT; break;
            case 'b': piece = PIECE_BISHOP; break;
            case 'r': piece = PIECE_ROOK; break;
            case 'q': piece = PIECE_QUEEN; break;
            case 'k': piece = PIECE_KING; break;
            default: return false;
        }
        
        if (square >= 0 && square < 64) {
            squares[square] = MAKE_PIECE(piece, color);
        }
        square++;
    }
    
    if (parts.size() >= 2) {
        turn = (parts[1] == "b") ? 1 : 0;
    }
    
    castling_rights[0] = false;
    castling_rights[1] = false;
    castling_rights[2] = false;
    castling_rights[3] = false;
    
    if (parts.size() >= 3 && parts[2] != "-") {
        String castling = parts[2];
        for (int i = 0; i < castling.length(); i++) {
            char32_t c = castling[i];
            if (c == 'K') castling_rights[0] = true;
            if (c == 'Q') castling_rights[1] = true;
            if (c == 'k') castling_rights[2] = true;
            if (c == 'q') castling_rights[3] = true;
        }
    }
    
    en_passant_target = 255;
    if (parts.size() >= 4 && parts[3] != "-") {
        en_passant_target = algebraic_to_square(parts[3]);
    }
    
    halfmove_clock = 0;
    if (parts.size() >= 5) {
        halfmove_clock = parts[4].to_int();
    }
    
    fullmove_number = 1;
    if (parts.size() >= 6) {
        fullmove_number = parts[5].to_int();
    }
    
    update_king_cache();
    rebuild_piece_lists();
    current_hash = calculate_hash();
    
    return true;
}

String Board::generate_fen() const {
    String fen = "";
    
    for (int rank = 7; rank >= 0; rank--) {
        int empty_count = 0;
        
        for (int file = 0; file < 8; file++) {
            int sq = rank * 8 + file;
            uint8_t piece = squares[sq];
            
            if (IS_EMPTY(piece)) {
                empty_count++;
            } else {
                if (empty_count > 0) {
                    fen += String::num_int64(empty_count);
                    empty_count = 0;
                }
                
                char piece_char = '.';
                switch (GET_PIECE_TYPE(piece)) {
                    case PIECE_PAWN:   piece_char = 'p'; break;
                    case PIECE_KNIGHT: piece_char = 'n'; break;
                    case PIECE_BISHOP: piece_char = 'b'; break;
                    case PIECE_ROOK:   piece_char = 'r'; break;
                    case PIECE_QUEEN:  piece_char = 'q'; break;
                    case PIECE_KING:   piece_char = 'k'; break;
                }
                
                if (IS_WHITE(piece)) piece_char -= 32;
                fen += String::chr(piece_char);
            }
        }
        
        if (empty_count > 0) fen += String::num_int64(empty_count);
        if (rank > 0) fen += "/";
    }
    
    fen += (turn == 0) ? " w " : " b ";
    
    String castling = "";
    if (castling_rights[0]) castling += "K";
    if (castling_rights[1]) castling += "Q";
    if (castling_rights[2]) castling += "k";
    if (castling_rights[3]) castling += "q";
    if (castling.is_empty()) castling = "-";
    fen += castling;
    
    fen += " ";
    if (en_passant_target == 255) {
        fen += "-";
    } else {
        fen += square_to_algebraic(en_passant_target);
    }
    
    fen += " " + String::num_int64(halfmove_clock);
    fen += " " + String::num_int64(fullmove_number);
    
    return fen;
}

// ==================== ATTACK DETECTION ====================

bool Board::is_square_attacked_fast(uint8_t pos, uint8_t attacking_color) const {
    uint8_t attacker_color = (attacking_color == 0) ? COLOR_WHITE : COLOR_BLACK;
    
    // Knight attacks
    for (int i = 0; i < knight_attack_count[pos]; i++) {
        uint8_t sq = knight_attack_squares[pos][i];
        if (GET_PIECE_TYPE(squares[sq]) == PIECE_KNIGHT && GET_COLOR(squares[sq]) == attacker_color) {
            return true;
        }
    }
    
    // King attacks
    for (int i = 0; i < king_attack_count[pos]; i++) {
        uint8_t sq = king_attack_squares[pos][i];
        if (GET_PIECE_TYPE(squares[sq]) == PIECE_KING && GET_COLOR(squares[sq]) == attacker_color) {
            return true;
        }
    }
    
    // Pawn attacks
    int pawn_dir = (attacking_color == 0) ? -8 : 8;
    int file = pos % 8;
    
    if (file > 0) {
        int sq = pos + pawn_dir - 1;
        if (sq >= 0 && sq < 64) {
            if (GET_PIECE_TYPE(squares[sq]) == PIECE_PAWN && GET_COLOR(squares[sq]) == attacker_color) {
                return true;
            }
        }
    }
    if (file < 7) {
        int sq = pos + pawn_dir + 1;
        if (sq >= 0 && sq < 64) {
            if (GET_PIECE_TYPE(squares[sq]) == PIECE_PAWN && GET_COLOR(squares[sq]) == attacker_color) {
                return true;
            }
        }
    }
    
    // Sliding pieces
    for (int dir = 0; dir < 8; dir++) {
        int offset = DIR_OFFSETS[dir];
        int dist = squares_to_edge[pos][dir];
        int sq = pos;
        
        for (int d = 0; d < dist; d++) {
            sq += offset;
            uint8_t piece = squares[sq];
            
            if (!IS_EMPTY(piece)) {
                if (GET_COLOR(piece) == attacker_color) {
                    uint8_t type = GET_PIECE_TYPE(piece);
                    if (type == PIECE_QUEEN) return true;
                    if (dir < 4 && type == PIECE_ROOK) return true;
                    if (dir >= 4 && type == PIECE_BISHOP) return true;
                }
                break;
            }
        }
    }
    
    return false;
}

bool Board::is_king_in_check(uint8_t color) const {
    uint8_t king_pos = (color == 0) ? white_king_pos : black_king_pos;
    if (king_pos == 255) return false;
    return is_square_attacked_fast(king_pos, 1 - color);
}

bool Board::has_legal_moves() const {
    MoveList moves;
    generate_all_pseudo_legal(moves);
    
    uint8_t current_color = turn;
    Board* self = const_cast<Board*>(this);
    
    uint8_t ep_before = en_passant_target;
    bool castling_before[4];
    for (int i = 0; i < 4; i++) castling_before[i] = castling_rights[i];
    uint64_t hash_before = current_hash;
    
    for (int i = 0; i < moves.count; i++) {
        FastMove &m = moves.moves[i];
        
        self->make_move_fast(m);
        
        uint8_t our_king = (current_color == 0) ? white_king_pos : black_king_pos;
        bool legal = !is_square_attacked_fast(our_king, 1 - current_color);
        
        self->unmake_move_fast(m, ep_before, castling_before, hash_before);
        
        if (legal) return true;
    }
    
    return false;
}

// ==================== MOVE GENERATION ====================

inline void Board::generate_pawn_moves(uint8_t pos, MoveList &moves) const {
    uint8_t piece = squares[pos];
    uint8_t color = GET_COLOR(piece);
    int direction = (color == COLOR_WHITE) ? 8 : -8;
    int start_rank = (color == COLOR_WHITE) ? 1 : 6;
    int promo_rank = (color == COLOR_WHITE) ? 7 : 0;
    int rank = pos / 8;
    int file = pos % 8;
    
    int to = pos + direction;
    if (to >= 0 && to < 64 && IS_EMPTY(squares[to])) {
        int to_rank = to / 8;
        if (to_rank == promo_rank) {
            moves.add(pos, to, (PIECE_QUEEN << 3), 0);
            moves.add(pos, to, (PIECE_ROOK << 3), 0);
            moves.add(pos, to, (PIECE_BISHOP << 3), 0);
            moves.add(pos, to, (PIECE_KNIGHT << 3), 0);
        } else {
            moves.add(pos, to);
        }
        
        if (rank == start_rank) {
            int to2 = pos + 2 * direction;
            if (IS_EMPTY(squares[to2])) {
                moves.add(pos, to2);
            }
        }
    }
    
    int capture_dirs[2] = {direction - 1, direction + 1};
    for (int i = 0; i < 2; i++) {
        int to_sq = pos + capture_dirs[i];
        if (to_sq < 0 || to_sq >= 64) continue;
        
        int to_file = to_sq % 8;
        int file_diff = to_file - file;
        if (file_diff < -1 || file_diff > 1) continue;
        
        int to_rank = to_sq / 8;
        
        if (!IS_EMPTY(squares[to_sq]) && GET_COLOR(squares[to_sq]) != color) {
            if (to_rank == promo_rank) {
                moves.add(pos, to_sq, 1 | (PIECE_QUEEN << 3), squares[to_sq]);
                moves.add(pos, to_sq, 1 | (PIECE_ROOK << 3), squares[to_sq]);
                moves.add(pos, to_sq, 1 | (PIECE_BISHOP << 3), squares[to_sq]);
                moves.add(pos, to_sq, 1 | (PIECE_KNIGHT << 3), squares[to_sq]);
            } else {
                moves.add(pos, to_sq, 1, squares[to_sq]);
            }
        }
        else if (to_sq == en_passant_target) {
            int captured_sq = to_sq - direction;
            moves.add(pos, to_sq, 2, squares[captured_sq]);
        }
    }
}

inline void Board::generate_knight_moves(uint8_t pos, MoveList &moves) const {
    uint8_t color = GET_COLOR(squares[pos]);
    
    for (int i = 0; i < knight_attack_count[pos]; i++) {
        uint8_t to = knight_attack_squares[pos][i];
        uint8_t target = squares[to];
        
        if (IS_EMPTY(target)) {
            moves.add(pos, to);
        } else if (GET_COLOR(target) != color) {
            moves.add(pos, to, 1, target);
        }
    }
}

inline void Board::generate_bishop_moves(uint8_t pos, MoveList &moves) const {
    uint8_t color = GET_COLOR(squares[pos]);
    
    for (int dir = 4; dir < 8; dir++) {
        int offset = DIR_OFFSETS[dir];
        int dist = squares_to_edge[pos][dir];
        int sq = pos;
        
        for (int d = 0; d < dist; d++) {
            sq += offset;
            uint8_t target = squares[sq];
            
            if (IS_EMPTY(target)) {
                moves.add(pos, sq);
            } else {
                if (GET_COLOR(target) != color) {
                    moves.add(pos, sq, 1, target);
                }
                break;
            }
        }
    }
}

inline void Board::generate_rook_moves(uint8_t pos, MoveList &moves) const {
    uint8_t color = GET_COLOR(squares[pos]);
    
    for (int dir = 0; dir < 4; dir++) {
        int offset = DIR_OFFSETS[dir];
        int dist = squares_to_edge[pos][dir];
        int sq = pos;
        
        for (int d = 0; d < dist; d++) {
            sq += offset;
            uint8_t target = squares[sq];
            
            if (IS_EMPTY(target)) {
                moves.add(pos, sq);
            } else {
                if (GET_COLOR(target) != color) {
                    moves.add(pos, sq, 1, target);
                }
                break;
            }
        }
    }
}

inline void Board::generate_queen_moves(uint8_t pos, MoveList &moves) const {
    generate_rook_moves(pos, moves);
    generate_bishop_moves(pos, moves);
}

inline void Board::generate_king_moves(uint8_t pos, MoveList &moves) const {
    uint8_t color = GET_COLOR(squares[pos]);
    
    for (int i = 0; i < king_attack_count[pos]; i++) {
        uint8_t to = king_attack_squares[pos][i];
        uint8_t target = squares[to];
        
        if (IS_EMPTY(target)) {
            moves.add(pos, to);
        } else if (GET_COLOR(target) != color) {
            moves.add(pos, to, 1, target);
        }
    }
}

inline void Board::generate_castling_moves(uint8_t pos, MoveList &moves) const {
    uint8_t color_val = GET_COLOR(squares[pos]);
    uint8_t color = (color_val == COLOR_WHITE) ? 0 : 1;
    
    int rights_k = (color == 0) ? 0 : 2;
    if (castling_rights[rights_k]) {
        int king_pos = (color == 0) ? 4 : 60;
        if (pos == king_pos &&
            IS_EMPTY(squares[king_pos + 1]) && 
            IS_EMPTY(squares[king_pos + 2]) &&
            !is_square_attacked_fast(king_pos, 1 - color) &&
            !is_square_attacked_fast(king_pos + 1, 1 - color) &&
            !is_square_attacked_fast(king_pos + 2, 1 - color)) {
            moves.add(pos, pos + 2, 4);
        }
    }
    
    int rights_q = (color == 0) ? 1 : 3;
    if (castling_rights[rights_q]) {
        int king_pos = (color == 0) ? 4 : 60;
        if (pos == king_pos &&
            IS_EMPTY(squares[king_pos - 1]) && 
            IS_EMPTY(squares[king_pos - 2]) &&
            IS_EMPTY(squares[king_pos - 3]) &&
            !is_square_attacked_fast(king_pos, 1 - color) &&
            !is_square_attacked_fast(king_pos - 1, 1 - color) &&
            !is_square_attacked_fast(king_pos - 2, 1 - color)) {
            moves.add(pos, pos - 2, 4);
        }
    }
}

void Board::generate_all_pseudo_legal(MoveList &moves) const {
    moves.clear();

    // Use piece lists for faster iteration (avoid scanning empty squares)
    const uint8_t* piece_list;
    uint8_t piece_count;

    if (turn == 0) {
        piece_list = white_piece_list;
        piece_count = white_piece_count;
    } else {
        piece_list = black_piece_list;
        piece_count = black_piece_count;
    }

    for (uint8_t i = 0; i < piece_count; i++) {
        uint8_t sq = piece_list[i];
        uint8_t piece = squares[sq];

        switch (GET_PIECE_TYPE(piece)) {
            case PIECE_PAWN:   generate_pawn_moves(sq, moves); break;
            case PIECE_KNIGHT: generate_knight_moves(sq, moves); break;
            case PIECE_BISHOP: generate_bishop_moves(sq, moves); break;
            case PIECE_ROOK:   generate_rook_moves(sq, moves); break;
            case PIECE_QUEEN:  generate_queen_moves(sq, moves); break;
            case PIECE_KING:
                generate_king_moves(sq, moves);
                generate_castling_moves(sq, moves);
                break;
        }
    }
}

// ==================== FAST MAKE/UNMAKE ====================

void Board::make_move_fast(const FastMove &m) {
    uint8_t moving_piece = squares[m.from];
    uint8_t piece_type = GET_PIECE_TYPE(moving_piece);
    uint8_t color = GET_COLOR(moving_piece);

    if (en_passant_target < 64) {
        hash_en_passant(en_passant_target);
    }

    if (m.flags & 2) {
        int capture_sq = m.to + ((color == COLOR_WHITE) ? -8 : 8);
        remove_piece_from_list(capture_sq, squares[capture_sq]);
        hash_piece(squares[capture_sq], capture_sq);
        squares[capture_sq] = 0;
    }

    if ((m.flags & 1) && !(m.flags & 2)) {
        remove_piece_from_list(m.to, squares[m.to]);
        hash_piece(squares[m.to], m.to);
    }

    if (m.flags & 4) {
        int move_dist = (int)m.to - (int)m.from;
        if (move_dist == 2) {
            uint8_t rook = squares[m.from + 3];
            remove_piece_from_list(m.from + 3, rook);
            hash_piece(rook, m.from + 3);
            hash_piece(rook, m.from + 1);
            squares[m.from + 1] = rook;
            squares[m.from + 3] = 0;
            add_piece_to_list(m.from + 1, rook);
        } else {
            uint8_t rook = squares[m.from - 4];
            remove_piece_from_list(m.from - 4, rook);
            hash_piece(rook, m.from - 4);
            hash_piece(rook, m.from - 1);
            squares[m.from - 1] = rook;
            squares[m.from - 4] = 0;
            add_piece_to_list(m.from - 1, rook);
        }
    }

    hash_piece(moving_piece, m.from);

    remove_piece_from_list(m.from, moving_piece);
    squares[m.to] = moving_piece;
    squares[m.from] = 0;

    uint8_t promo_piece = (m.flags >> 3) & 7;
    if (promo_piece) {
        squares[m.to] = MAKE_PIECE(promo_piece, color);
        hash_piece(squares[m.to], m.to);
    } else {
        hash_piece(moving_piece, m.to);
    }
    add_piece_to_list(m.to, squares[m.to]);
    
    if (piece_type == PIECE_KING) {
        if (color == COLOR_WHITE) white_king_pos = m.to;
        else black_king_pos = m.to;
    }
    
    en_passant_target = 255;
    if (piece_type == PIECE_PAWN) {
        int move_dist = (int)m.to - (int)m.from;
        if (move_dist == 16 || move_dist == -16) {
            en_passant_target = (m.from + m.to) / 2;
            hash_en_passant(en_passant_target);
        }
    }
    
    if (piece_type == PIECE_KING) {
        if (color == COLOR_WHITE) {
            if (castling_rights[0]) { hash_castling(0); castling_rights[0] = false; }
            if (castling_rights[1]) { hash_castling(1); castling_rights[1] = false; }
        } else {
            if (castling_rights[2]) { hash_castling(2); castling_rights[2] = false; }
            if (castling_rights[3]) { hash_castling(3); castling_rights[3] = false; }
        }
    }
    
    if ((m.from == 0 || m.to == 0) && castling_rights[1]) { hash_castling(1); castling_rights[1] = false; }
    if ((m.from == 7 || m.to == 7) && castling_rights[0]) { hash_castling(0); castling_rights[0] = false; }
    if ((m.from == 56 || m.to == 56) && castling_rights[3]) { hash_castling(3); castling_rights[3] = false; }
    if ((m.from == 63 || m.to == 63) && castling_rights[2]) { hash_castling(2); castling_rights[2] = false; }
    
    hash_side();
    turn = 1 - turn;
}

void Board::unmake_move_fast(const FastMove &m, uint8_t ep_before, bool castling_before[4], uint64_t hash_before) {
    uint8_t moving_piece = squares[m.to];
    uint8_t color = GET_COLOR(moving_piece);
    uint8_t piece_type = GET_PIECE_TYPE(moving_piece);

    uint8_t promo_piece = (m.flags >> 3) & 7;
    if (promo_piece) {
        moving_piece = MAKE_PIECE(PIECE_PAWN, color);
        piece_type = PIECE_PAWN;
    }

    remove_piece_from_list(m.to, squares[m.to]);
    squares[m.from] = moving_piece;
    squares[m.to] = (m.flags & 2) ? 0 : m.captured;
    add_piece_to_list(m.from, moving_piece);

    if ((m.flags & 1) && !(m.flags & 2)) {
        if (!IS_EMPTY(m.captured)) add_piece_to_list(m.to, m.captured);
    }

    if (m.flags & 2) {
        int capture_sq = m.to + ((color == COLOR_WHITE) ? -8 : 8);
        squares[capture_sq] = m.captured;
        if (!IS_EMPTY(m.captured)) add_piece_to_list(capture_sq, m.captured);
    }

    if (m.flags & 4) {
        int move_dist = (int)m.to - (int)m.from;
        if (move_dist == 2) {
            uint8_t rook = squares[m.from + 1];
            remove_piece_from_list(m.from + 1, rook);
            squares[m.from + 3] = rook;
            squares[m.from + 1] = 0;
            add_piece_to_list(m.from + 3, rook);
        } else {
            uint8_t rook = squares[m.from - 1];
            remove_piece_from_list(m.from - 1, rook);
            squares[m.from - 4] = rook;
            squares[m.from - 1] = 0;
            add_piece_to_list(m.from - 4, rook);
        }
    }
    
    if (piece_type == PIECE_KING) {
        if (color == COLOR_WHITE) white_king_pos = m.from;
        else black_king_pos = m.from;
    }
    
    for (int i = 0; i < 4; i++) castling_rights[i] = castling_before[i];
    en_passant_target = ep_before;
    current_hash = hash_before;
    turn = 1 - turn;
}

// ==================== LEGACY API HELPERS ====================

bool Board::would_be_in_check_after_move(uint8_t from, uint8_t to, uint8_t color) {
    uint8_t captured = squares[to];
    uint8_t moving_piece = squares[from];
    
    squares[to] = moving_piece;
    squares[from] = 0;
    
    uint8_t ep_captured = 0;
    uint8_t ep_capture_sq = 255;
    if (GET_PIECE_TYPE(moving_piece) == PIECE_PAWN && to == en_passant_target) {
        int direction = (color == 0) ? -8 : 8;
        ep_capture_sq = to + direction;
        ep_captured = squares[ep_capture_sq];
        squares[ep_capture_sq] = 0;
    }
    
    uint8_t old_king_pos = (color == 0) ? white_king_pos : black_king_pos;
    if (GET_PIECE_TYPE(moving_piece) == PIECE_KING) {
        if (color == 0) white_king_pos = to;
        else black_king_pos = to;
    }
    
    bool in_check = is_king_in_check(color);
    
    if (GET_PIECE_TYPE(moving_piece) == PIECE_KING) {
        if (color == 0) white_king_pos = old_king_pos;
        else black_king_pos = old_king_pos;
    }
    
    squares[from] = moving_piece;
    squares[to] = captured;
    if (ep_capture_sq != 255) {
        squares[ep_capture_sq] = ep_captured;
    }
    
    return in_check;
}

void Board::add_pawn_moves(uint8_t pos, Array &moves) const {
    uint8_t piece = squares[pos];
    uint8_t color = GET_COLOR(piece);
    int direction = (color == COLOR_WHITE) ? 8 : -8;
    int start_rank = (color == COLOR_WHITE) ? 1 : 6;
    int rank = pos / 8;
    int file = pos % 8;
    
    int to = pos + direction;
    if (to >= 0 && to < 64 && IS_EMPTY(squares[to])) {
        moves.append(to);
        
        if (rank == start_rank) {
            int to2 = pos + 2 * direction;
            if (IS_EMPTY(squares[to2])) {
                moves.append(to2);
            }
        }
    }
    
    int capture_dirs[2] = {direction - 1, direction + 1};
    for (int i = 0; i < 2; i++) {
        int to_sq = pos + capture_dirs[i];
        if (to_sq < 0 || to_sq >= 64) continue;
        
        int to_file = to_sq % 8;
        int file_diff = to_file - file;
        if (file_diff < -1 || file_diff > 1) continue;
        
        uint8_t target = squares[to_sq];
        if (!IS_EMPTY(target) && GET_COLOR(target) != color) {
            moves.append(to_sq);
        }
        else if (to_sq == en_passant_target) {
            moves.append(to_sq);
        }
    }
}

void Board::add_knight_moves(uint8_t pos, Array &moves) const {
    uint8_t color = GET_COLOR(squares[pos]);
    
    for (int i = 0; i < knight_attack_count[pos]; i++) {
        uint8_t to = knight_attack_squares[pos][i];
        uint8_t target = squares[to];
        if (IS_EMPTY(target) || GET_COLOR(target) != color) {
            moves.append((int)to);
        }
    }
}

void Board::add_bishop_moves(uint8_t pos, Array &moves) const {
    uint8_t color = GET_COLOR(squares[pos]);
    
    for (int dir = 4; dir < 8; dir++) {
        int offset = DIR_OFFSETS[dir];
        int dist = squares_to_edge[pos][dir];
        int sq = pos;
        
        for (int d = 0; d < dist; d++) {
            sq += offset;
            uint8_t target = squares[sq];
            
            if (IS_EMPTY(target)) {
                moves.append(sq);
            } else {
                if (GET_COLOR(target) != color) {
                    moves.append(sq);
                }
                break;
            }
        }
    }
}

void Board::add_rook_moves(uint8_t pos, Array &moves) const {
    uint8_t color = GET_COLOR(squares[pos]);
    
    for (int dir = 0; dir < 4; dir++) {
        int offset = DIR_OFFSETS[dir];
        int dist = squares_to_edge[pos][dir];
        int sq = pos;
        
        for (int d = 0; d < dist; d++) {
            sq += offset;
            uint8_t target = squares[sq];
            
            if (IS_EMPTY(target)) {
                moves.append(sq);
            } else {
                if (GET_COLOR(target) != color) {
                    moves.append(sq);
                }
                break;
            }
        }
    }
}

void Board::add_queen_moves(uint8_t pos, Array &moves) const {
    add_rook_moves(pos, moves);
    add_bishop_moves(pos, moves);
}

void Board::add_king_moves(uint8_t pos, Array &moves) const {
    uint8_t color = GET_COLOR(squares[pos]);
    
    for (int i = 0; i < king_attack_count[pos]; i++) {
        uint8_t to = king_attack_squares[pos][i];
        uint8_t target = squares[to];
        if (IS_EMPTY(target) || GET_COLOR(target) != color) {
            moves.append((int)to);
        }
    }
}

bool Board::can_castle_kingside(uint8_t color) const {
    int rights_idx = (color == 0) ? 0 : 2;
    if (!castling_rights[rights_idx]) return false;
    
    int king_pos = (color == 0) ? 4 : 60;
    
    if (!IS_EMPTY(squares[king_pos + 1]) || !IS_EMPTY(squares[king_pos + 2])) {
        return false;
    }
    
    if (is_square_attacked_fast(king_pos, 1 - color)) return false;
    if (is_square_attacked_fast(king_pos + 1, 1 - color)) return false;
    if (is_square_attacked_fast(king_pos + 2, 1 - color)) return false;
    
    return true;
}

bool Board::can_castle_queenside(uint8_t color) const {
    int rights_idx = (color == 0) ? 1 : 3;
    if (!castling_rights[rights_idx]) return false;
    
    int king_pos = (color == 0) ? 4 : 60;
    
    if (!IS_EMPTY(squares[king_pos - 1]) || !IS_EMPTY(squares[king_pos - 2]) || !IS_EMPTY(squares[king_pos - 3])) {
        return false;
    }
    
    if (is_square_attacked_fast(king_pos, 1 - color)) return false;
    if (is_square_attacked_fast(king_pos - 1, 1 - color)) return false;
    if (is_square_attacked_fast(king_pos - 2, 1 - color)) return false;
    
    return true;
}

void Board::add_castling_moves(uint8_t pos, Array &moves) const {
    uint8_t color_val = GET_COLOR(squares[pos]);
    uint8_t color = (color_val == COLOR_WHITE) ? 0 : 1;
    
    if (can_castle_kingside(color)) {
        moves.append(pos + 2);
    }
    if (can_castle_queenside(color)) {
        moves.append(pos - 2);
    }
}

Array Board::get_pseudo_legal_moves_for_piece(uint8_t pos) const {
    Array moves;
    
    if (pos >= 64) return moves;
    
    uint8_t piece = squares[pos];
    if (IS_EMPTY(piece)) return moves;
    
    switch (GET_PIECE_TYPE(piece)) {
        case PIECE_PAWN:   add_pawn_moves(pos, moves); break;
        case PIECE_KNIGHT: add_knight_moves(pos, moves); break;
        case PIECE_BISHOP: add_bishop_moves(pos, moves); break;
        case PIECE_ROOK:   add_rook_moves(pos, moves); break;
        case PIECE_QUEEN:  add_queen_moves(pos, moves); break;
        case PIECE_KING:   
            add_king_moves(pos, moves); 
            add_castling_moves(pos, moves);
            break;
    }
    
    return moves;
}

void Board::make_move_internal(uint8_t from, uint8_t to, Move &move_record) {
    uint8_t moving_piece = squares[from];
    uint8_t piece_type = GET_PIECE_TYPE(moving_piece);
    uint8_t color = GET_COLOR(moving_piece);
    
    move_record.from = from;
    move_record.to = to;
    move_record.captured_piece = squares[to];
    move_record.promotion_piece = 0;
    move_record.is_castling = false;
    move_record.is_en_passant = false;
    move_record.en_passant_target_before = en_passant_target;
    move_record.halfmove_clock_before = halfmove_clock;
    move_record.hash_before = current_hash;
    for (int i = 0; i < 4; i++) {
        move_record.castling_rights_before[i] = castling_rights[i];
    }
    
    if (en_passant_target < 64) {
        hash_en_passant(en_passant_target);
    }
    
    if (piece_type == PIECE_PAWN && to == en_passant_target) {
        move_record.is_en_passant = true;
        int capture_sq = to + ((color == COLOR_WHITE) ? -8 : 8);
        move_record.captured_piece = squares[capture_sq];
        remove_piece_from_list(capture_sq, squares[capture_sq]);
        hash_piece(squares[capture_sq], capture_sq);
        squares[capture_sq] = 0;
    }

    if (!move_record.is_en_passant && !IS_EMPTY(squares[to])) {
        remove_piece_from_list(to, squares[to]);
        hash_piece(squares[to], to);
    }

    if (piece_type == PIECE_KING) {
        int move_dist = (int)to - (int)from;

        if (move_dist == 2) {
            move_record.is_castling = true;
            uint8_t rook = squares[from + 3];
            remove_piece_from_list(from + 3, rook);
            hash_piece(rook, from + 3);
            hash_piece(rook, from + 1);
            squares[from + 3] = 0;
            squares[from + 1] = rook;
            add_piece_to_list(from + 1, rook);
        } else if (move_dist == -2) {
            move_record.is_castling = true;
            uint8_t rook = squares[from - 4];
            remove_piece_from_list(from - 4, rook);
            hash_piece(rook, from - 4);
            hash_piece(rook, from - 1);
            squares[from - 4] = 0;
            squares[from - 1] = rook;
            add_piece_to_list(from - 1, rook);
        }
    }

    hash_piece(moving_piece, from);
    hash_piece(moving_piece, to);

    remove_piece_from_list(from, moving_piece);
    squares[to] = moving_piece;
    squares[from] = 0;
    add_piece_to_list(to, moving_piece);
    
    if (piece_type == PIECE_KING) {
        if (color == COLOR_WHITE) white_king_pos = to;
        else black_king_pos = to;
    }
    
    en_passant_target = 255;
    if (piece_type == PIECE_PAWN) {
        int move_dist = (int)to - (int)from;
        if (move_dist == 16 || move_dist == -16) {
            en_passant_target = (from + to) / 2;
            hash_en_passant(en_passant_target);
        }
    }
    
    if (piece_type == PIECE_KING) {
        if (color == COLOR_WHITE) {
            if (castling_rights[0]) { hash_castling(0); castling_rights[0] = false; }
            if (castling_rights[1]) { hash_castling(1); castling_rights[1] = false; }
        } else {
            if (castling_rights[2]) { hash_castling(2); castling_rights[2] = false; }
            if (castling_rights[3]) { hash_castling(3); castling_rights[3] = false; }
        }
    }
    
    if ((from == 0 || to == 0) && castling_rights[1]) { hash_castling(1); castling_rights[1] = false; }
    if ((from == 7 || to == 7) && castling_rights[0]) { hash_castling(0); castling_rights[0] = false; }
    if ((from == 56 || to == 56) && castling_rights[3]) { hash_castling(3); castling_rights[3] = false; }
    if ((from == 63 || to == 63) && castling_rights[2]) { hash_castling(2); castling_rights[2] = false; }
    
    if (piece_type == PIECE_PAWN || move_record.captured_piece != 0) {
        halfmove_clock = 0;
    } else {
        halfmove_clock++;
    }
    
    if (color == COLOR_BLACK) {
        fullmove_number++;
    }
    
    hash_side();
    turn = 1 - turn;
}

void Board::revert_move_internal(const Move &move) {
    uint8_t moving_piece = squares[move.to];
    uint8_t color = GET_COLOR(moving_piece);
    uint8_t piece_type = GET_PIECE_TYPE(moving_piece);

    if (move.promotion_piece != 0) {
        moving_piece = MAKE_PIECE(PIECE_PAWN, color);
    }

    remove_piece_from_list(move.to, squares[move.to]);
    squares[move.from] = moving_piece;
    squares[move.to] = move.captured_piece;
    add_piece_to_list(move.from, moving_piece);

    if (move.is_en_passant) {
        squares[move.to] = 0;
        int capture_sq = move.to + ((color == COLOR_WHITE) ? -8 : 8);
        squares[capture_sq] = move.captured_piece;
        if (!IS_EMPTY(move.captured_piece)) add_piece_to_list(capture_sq, move.captured_piece);
    } else if (!IS_EMPTY(move.captured_piece)) {
        add_piece_to_list(move.to, move.captured_piece);
    }

    if (move.is_castling) {
        int move_dist = (int)move.to - (int)move.from;
        if (move_dist == 2) {
            uint8_t rook = squares[move.from + 1];
            remove_piece_from_list(move.from + 1, rook);
            squares[move.from + 1] = 0;
            squares[move.from + 3] = rook;
            add_piece_to_list(move.from + 3, rook);
        } else {
            uint8_t rook = squares[move.from - 1];
            remove_piece_from_list(move.from - 1, rook);
            squares[move.from - 1] = 0;
            squares[move.from - 4] = rook;
            add_piece_to_list(move.from - 4, rook);
        }
    }
    
    if (GET_PIECE_TYPE(moving_piece) == PIECE_KING) {
        if (color == COLOR_WHITE) white_king_pos = move.from;
        else black_king_pos = move.from;
    }
    
    for (int i = 0; i < 4; i++) {
        castling_rights[i] = move.castling_rights_before[i];
    }
    
    en_passant_target = move.en_passant_target_before;
    halfmove_clock = move.halfmove_clock_before;
    current_hash = move.hash_before;
    
    turn = 1 - turn;
    
    if (color == COLOR_BLACK) {
        fullmove_number--;
    }
}

String Board::move_to_notation(const Move &move) const {
    String notation = "";
    notation += square_to_algebraic(move.from);
    notation += square_to_algebraic(move.to);
    
    if (move.promotion_piece != 0) {
        switch (GET_PIECE_TYPE(move.promotion_piece)) {
            case PIECE_QUEEN:  notation += "q"; break;
            case PIECE_ROOK:   notation += "r"; break;
            case PIECE_BISHOP: notation += "b"; break;
            case PIECE_KNIGHT: notation += "n"; break;
        }
    }
    
    return notation;
}

// ==================== PUBLIC API ====================

void Board::setup_board(const String &fen_notation) {
    if (!parse_fen(fen_notation)) {
        initialize_starting_position();
    }
}

String Board::get_fen() const {
    return generate_fen();
}

uint8_t Board::get_turn() const {
    return turn;
}

uint8_t Board::get_piece_on_square(uint8_t pos) const {
    if (pos >= 64) return 0;
    return squares[pos];
}

void Board::set_piece_on_square(uint8_t pos, uint8_t piece) {
    if (pos >= 64) return;
    
    if (!IS_EMPTY(squares[pos])) {
        hash_piece(squares[pos], pos);
    }
    
    squares[pos] = piece;
    
    if (!IS_EMPTY(piece)) {
        hash_piece(piece, pos);
    }
    
    if (GET_PIECE_TYPE(piece) == PIECE_KING) {
        if (IS_WHITE(piece)) white_king_pos = pos;
        else black_king_pos = pos;
    }
}

uint8_t Board::attempt_move(uint8_t start, uint8_t end) {
    if (promotion_pending) return 0;
    if (start >= 64 || end >= 64) return 0;
    
    uint8_t piece = squares[start];
    if (IS_EMPTY(piece)) return 0;
    
    uint8_t color_val = GET_COLOR(piece);
    uint8_t expected_color = (turn == 0) ? COLOR_WHITE : COLOR_BLACK;
    if (color_val != expected_color) return 0;
    
    Array legal_moves = get_legal_moves_for_piece(start);
    bool is_legal = false;
    for (int i = 0; i < legal_moves.size(); i++) {
        if ((int)legal_moves[i] == end) {
            is_legal = true;
            break;
        }
    }
    if (!is_legal) return 0;
    
    uint8_t piece_type = GET_PIECE_TYPE(piece);
    int end_rank = end / 8;
    
    if (piece_type == PIECE_PAWN && (end_rank == 0 || end_rank == 7)) {
        promotion_pending = true;
        promotion_pending_from = start;
        promotion_pending_to = end;
        return 2;
    }
    
    Move move_record;
    make_move_internal(start, end, move_record);
    move_history.push_back(move_record);
    move_history_notation.push_back(move_to_notation(move_record));
    
    return 1;
}

void Board::commit_promotion(const String &type_str) {
    if (!promotion_pending) return;
    
    uint8_t promotion_type = PIECE_QUEEN;
    if (type_str.length() > 0) {
        char32_t c = type_str[0];
        switch (c) {
            case 'q': case 'Q': promotion_type = PIECE_QUEEN; break;
            case 'r': case 'R': promotion_type = PIECE_ROOK; break;
            case 'b': case 'B': promotion_type = PIECE_BISHOP; break;
            case 'n': case 'N': promotion_type = PIECE_KNIGHT; break;
        }
    }
    
    uint8_t piece = squares[promotion_pending_from];
    uint8_t color = GET_COLOR(piece);
    
    Move move_record;
    make_move_internal(promotion_pending_from, promotion_pending_to, move_record);
    
    hash_piece(squares[promotion_pending_to], promotion_pending_to);
    squares[promotion_pending_to] = MAKE_PIECE(promotion_type, color);
    hash_piece(squares[promotion_pending_to], promotion_pending_to);
    
    move_record.promotion_piece = squares[promotion_pending_to];
    
    move_history.push_back(move_record);
    move_history_notation.push_back(move_to_notation(move_record));
    
    promotion_pending = false;
}

void Board::revert_move() {
    if (move_history.empty()) return;
    
    Move last_move = move_history.back();
    move_history.pop_back();
    move_history_notation.pop_back();
    
    revert_move_internal(last_move);
}

Array Board::get_moves() const {
    Array moves;
    for (size_t i = 0; i < move_history_notation.size(); i++) {
        moves.append(move_history_notation[i]);
    }
    return moves;
}

Array Board::get_all_possible_moves(uint8_t color) {
    Array all_moves;
    uint8_t target_color = (color == 0) ? COLOR_WHITE : COLOR_BLACK;
    
    for (int sq = 0; sq < 64; sq++) {
        uint8_t piece = squares[sq];
        if (!IS_EMPTY(piece) && GET_COLOR(piece) == target_color) {
            Array piece_moves = get_legal_moves_for_piece(sq);
            for (int i = 0; i < piece_moves.size(); i++) {
                Dictionary move_dict;
                move_dict["from"] = sq;
                move_dict["to"] = (int)piece_moves[i];
                all_moves.append(move_dict);
            }
        }
    }
    
    return all_moves;
}

Array Board::get_legal_moves_for_piece(uint8_t square) {
    Array legal_moves;
    
    if (square >= 64) return legal_moves;
    
    uint8_t piece = squares[square];
    if (IS_EMPTY(piece)) return legal_moves;
    
    uint8_t color_val = GET_COLOR(piece);
    uint8_t color = (color_val == COLOR_WHITE) ? 0 : 1;
    
    Array pseudo_moves = get_pseudo_legal_moves_for_piece(square);
    
    for (int i = 0; i < pseudo_moves.size(); i++) {
        uint8_t to = (int)pseudo_moves[i];
        
        if (!would_be_in_check_after_move(square, to, color)) {
            legal_moves.append(to);
        }
    }
    
    return legal_moves;
}

void Board::make_move(uint8_t start, uint8_t end) {
    if (start >= 64 || end >= 64) return;
    
    uint8_t piece = squares[start];
    if (IS_EMPTY(piece)) return;
    
    uint8_t piece_type = GET_PIECE_TYPE(piece);
    int end_rank = end / 8;
    
    Move move_record;
    make_move_internal(start, end, move_record);
    
    if (piece_type == PIECE_PAWN && (end_rank == 0 || end_rank == 7)) {
        uint8_t color = GET_COLOR(piece);
        hash_piece(squares[end], end);
        squares[end] = MAKE_PIECE(PIECE_QUEEN, color);
        hash_piece(squares[end], end);
        move_record.promotion_piece = squares[end];
    }
    
    move_history.push_back(move_record);
    move_history_notation.push_back(move_to_notation(move_record));
}

bool Board::is_checkmate(uint8_t color) {
    if (!is_king_in_check(color)) return false;
    Array all_moves = get_all_possible_moves(color);
    return all_moves.size() == 0;
}

bool Board::is_stalemate(uint8_t color) {
    if (is_king_in_check(color)) return false;
    Array all_moves = get_all_possible_moves(color);
    return all_moves.size() == 0;
}

bool Board::is_check(uint8_t color) const {
    return is_king_in_check(color);
}

bool Board::is_game_over() {
    if (is_checkmate(turn) || is_stalemate(turn)) return true;
    if (halfmove_clock >= 100) return true;
    return false;
}

int Board::get_game_result() {
    if (is_checkmate(0)) return 2;
    if (is_checkmate(1)) return 1;
    if (is_stalemate(turn)) return 3;
    if (halfmove_clock >= 100) return 3;
    return 0;
}

// ==================== PERFT ====================

uint64_t Board::count_all_moves(uint8_t depth) {
    if (depth == 0) return 1;
    
    MoveList moves;
    generate_all_pseudo_legal(moves);
    
    uint64_t nodes = 0;
    uint8_t current_color = turn;
    
    uint8_t ep_before = en_passant_target;
    bool castling_before[4];
    for (int i = 0; i < 4; i++) castling_before[i] = castling_rights[i];
    uint64_t hash_before = current_hash;
    
    for (int i = 0; i < moves.count; i++) {
        FastMove &m = moves.moves[i];
        
        make_move_fast(m);
        
        uint8_t our_king = (current_color == 0) ? white_king_pos : black_king_pos;
        if (!is_square_attacked_fast(our_king, 1 - current_color)) {
            nodes += count_all_moves(depth - 1);
        }
        
        unmake_move_fast(m, ep_before, castling_before, hash_before);
    }
    
    return nodes;
}

Dictionary Board::get_perft_analysis(uint8_t depth) {
    Dictionary result;
    MoveList moves;
    generate_all_pseudo_legal(moves);

    uint8_t current_color = turn;
    uint8_t ep_before = en_passant_target;
    bool castling_before[4];
    for (int i = 0; i < 4; i++) castling_before[i] = castling_rights[i];
    uint64_t hash_before = current_hash;
    
    for (int i = 0; i < moves.count; i++) {
        FastMove &m = moves.moves[i];

        make_move_fast(m);

        uint8_t our_king = (current_color == 0) ? white_king_pos : black_king_pos;
        if (!is_square_attacked_fast(our_king, 1 - current_color)) {
            uint64_t nodes = count_all_moves(depth - 1);
            String move_notation = square_to_algebraic(m.from) + square_to_algebraic(m.to);
            
            uint8_t promo_piece = (m.flags >> 3) & 7;
            if (promo_piece) {
                switch (promo_piece) {
                    case PIECE_QUEEN:  move_notation += "q"; break;
                    case PIECE_ROOK:   move_notation += "r"; break;
                    case PIECE_BISHOP: move_notation += "b"; break;
                    case PIECE_KNIGHT: move_notation += "n"; break;
                }
            }
            
            result[move_notation] = nodes;
        }

        unmake_move_fast(m, ep_before, castling_before, hash_before);
    }

    return result;
}

// ==================== UTILITY ====================

Vector2i Board::pos_to_coords(uint8_t pos) const {
    if (pos >= 64) return Vector2i(-1, -1);
    return Vector2i(pos / 8, pos % 8);
}

uint8_t Board::coords_to_pos(int rank, int file) const {
    if (rank < 0 || rank > 7 || file < 0 || file > 7) return 255;
    return rank * 8 + file;
}

String Board::square_to_algebraic(uint8_t pos) const {
    if (pos >= 64) return "";
    
    int file = pos % 8;
    int rank = pos / 8;
    
    String result = "";
    result += String::chr('a' + file);
    result += String::num_int64(rank + 1);
    
    return result;
}

uint8_t Board::algebraic_to_square(const String &algebraic) const {
    if (algebraic.length() < 2) return 255;
    
    char32_t file_char = algebraic[0];
    char32_t rank_char = algebraic[1];
    
    int file = file_char - 'a';
    int rank = rank_char - '1';
    
    if (file < 0 || file > 7 || rank < 0 || rank > 7) return 255;
    
    return rank * 8 + file;
}
