#include "agent.h"
#include "neural_network.h"
#include <godot_cpp/variant/utility_functions.hpp>
#include <cstring>
#include <cmath>
#include <algorithm>

using namespace godot;

// ==================== FEATURE EXTRACTION ====================

void Agent::extract_features(uint8_t color) {
    if (!board) return;

    // Resize and clear feature vector
    input_features.resize(NN_TOTAL_INPUTS);
    std::fill(input_features.begin(), input_features.end(), 0.0f);

    const uint8_t* squares = board->get_squares();
    const bool mirror_board = (color == COLOR_BLACK);

    // ==================== PIECE-SQUARE FEATURES (768 inputs) ====================
    // 12 planes: P, N, B, R, Q, K (white), p, n, b, r, q, k (black)
    // Each plane has 64 squares

    // Use piece lists for faster iteration (avoid scanning empty squares)
    const uint8_t* white_pieces = board->get_white_piece_list();
    const uint8_t* black_pieces = board->get_black_piece_list();
    const uint8_t white_count = board->get_white_piece_count();
    const uint8_t black_count = board->get_black_piece_count();

    // Process white pieces
    for (uint8_t i = 0; i < white_count; i++) {
        const uint8_t sq = white_pieces[i];
        const uint8_t piece = squares[sq];
        const uint8_t piece_type = GET_PIECE_TYPE(piece);

        // White pieces: P=0, N=1, B=2, R=3, Q=4, K=5
        const int plane = piece_type - 1;

        // Mirror square horizontally if playing as black
        const int feature_square = mirror_board ? mirror_square_horizontal(sq) : sq;

        // Feature index = plane * 64 + square
        const int feature_idx = plane * 64 + feature_square;
        input_features[feature_idx] = 1.0f;
    }

    // Process black pieces
    for (uint8_t i = 0; i < black_count; i++) {
        const uint8_t sq = black_pieces[i];
        const uint8_t piece = squares[sq];
        const uint8_t piece_type = GET_PIECE_TYPE(piece);

        // Black pieces: p=6, n=7, b=8, r=9, q=10, k=11
        const int plane = (piece_type - 1) + 6;

        // Mirror square horizontally if playing as black
        const int feature_square = mirror_board ? mirror_square_horizontal(sq) : sq;

        // Feature index = plane * 64 + square
        const int feature_idx = plane * 64 + feature_square;
        input_features[feature_idx] = 1.0f;
    }

    // ==================== CASTLING RIGHTS (4 inputs) ====================
    const bool* castling = board->get_castling_rights();
    int castling_offset = NN_PIECE_INPUTS;  // 768

    if (mirror_board) {
        // Mirror castling rights horizontally: swap white and black castling rights
        input_features[castling_offset + 0] = castling[2] ? 1.0f : 0.0f;  // Black Kingside → position 0
        input_features[castling_offset + 1] = castling[3] ? 1.0f : 0.0f;  // Black Queenside → position 1
        input_features[castling_offset + 2] = castling[0] ? 1.0f : 0.0f;  // White Kingside → position 2
        input_features[castling_offset + 3] = castling[1] ? 1.0f : 0.0f;  // White Queenside → position 3
    } else {
        input_features[castling_offset + 0] = castling[0] ? 1.0f : 0.0f;  // White Kingside
        input_features[castling_offset + 1] = castling[1] ? 1.0f : 0.0f;  // White Queenside
        input_features[castling_offset + 2] = castling[2] ? 1.0f : 0.0f;  // Black Kingside
        input_features[castling_offset + 3] = castling[3] ? 1.0f : 0.0f;  // Black Queenside
    }

    // ==================== SIDE TO MOVE (1 input) ====================
    int turn_offset = castling_offset + NN_CASTLING_INPUTS;  // 772
    if (mirror_board) {
        // From black's mirrored perspective: 1.0 = black to move, 0.0 = white to move
        input_features[turn_offset] = (board->get_turn() == 1) ? 1.0f : 0.0f;
    } else {
        input_features[turn_offset] = (board->get_turn() == 0) ? 1.0f : 0.0f;  // 1.0 = white to move
    }

    // ==================== EN PASSANT (8 inputs, one-hot by file) ====================
    int ep_offset = turn_offset + NN_TURN_INPUT;  // 773
    uint8_t ep_target = board->get_en_passant_target();
    if (ep_target != 255) {
        // Mirror en passant square if playing as black
        uint8_t mirrored_ep = mirror_board ? mirror_square_horizontal(ep_target) : ep_target;
        int ep_file = mirrored_ep % 8;
        input_features[ep_offset + ep_file] = 1.0f;
    }
    // If no en passant, all 8 inputs remain 0.0
}

// ==================== STATIC MEMBER DEFINITIONS ====================

TTEntry* Agent::tt_table = nullptr;
bool Agent::tt_initialized = false;
uint8_t Agent::tt_age = 0;

int16_t Agent::mvv_lva_table[7][7];
bool Agent::mvv_lva_initialized = false;

// Piece values for MVV-LVA scoring
static const int MVV_LVA_PIECE_VALUES[7] = {0, 100, 300, 300, 500, 900, 10000};

// ==================== STATIC INITIALIZATION ====================

void Agent::init_tt() {
    if (tt_initialized) return;
    
    tt_table = new TTEntry[TT_SIZE];
    memset(tt_table, 0, sizeof(TTEntry) * TT_SIZE);
    tt_initialized = true;
    tt_age = 0;
}

void Agent::init_mvv_lva_table() {
    if (mvv_lva_initialized) return;
    
    for (int victim = 0; victim < 7; victim++) {
        for (int attacker = 0; attacker < 7; attacker++) {
            if (victim == PIECE_NONE || attacker == PIECE_NONE) {
                mvv_lva_table[victim][attacker] = 0;
            } else {
                mvv_lva_table[victim][attacker] = 
                    MVV_LVA_PIECE_VALUES[victim] * 10 - MVV_LVA_PIECE_VALUES[attacker];
            }
        }
    }
    mvv_lva_initialized = true;
}

// ==================== TRANSPOSITION TABLE ====================

void Agent::tt_clear() {
    if (tt_table) {
        memset(tt_table, 0, sizeof(TTEntry) * TT_SIZE);
    }
    tt_age = 0;
}

void Agent::tt_new_search() {
    tt_age++;
}

void Agent::tt_store(uint64_t key, int score, int depth, int flag, uint8_t best_from, uint8_t best_to) {
    if (!tt_table) return;
    
    size_t index = key % TT_SIZE;
    TTEntry* entry = &tt_table[index];
    
    bool should_replace = 
        entry->key == 0 ||
        entry->key == key ||
        entry->age != tt_age ||
        entry->depth <= depth;
    
    if (should_replace) {
        entry->key = key;
        entry->score = static_cast<int16_t>(score);
        entry->depth = static_cast<int8_t>(depth);
        entry->flag = static_cast<uint8_t>(flag);
        entry->best_from = best_from;
        entry->best_to = best_to;
        entry->age = tt_age;
    }
}

TTEntry* Agent::tt_probe(uint64_t key) const {
    if (!tt_table) return nullptr;
    
    size_t index = key % TT_SIZE;
    TTEntry* entry = &tt_table[index];
    
    if (entry->key == key) {
        return entry;
    }
    
    return nullptr;
}

// ==================== KILLER MOVES ====================

void Agent::clear_killers() {
    for (int ply = 0; ply < MAX_PLY; ply++) {
        killer_moves[ply][0].clear();
        killer_moves[ply][1].clear();
    }
}

void Agent::store_killer(int ply, uint8_t from, uint8_t to) {
    if (ply < 0 || ply >= MAX_PLY) return;
    
    if (killer_moves[ply][0].matches(from, to)) return;
    
    killer_moves[ply][1] = killer_moves[ply][0];
    killer_moves[ply][0].set(from, to);
}

int Agent::is_killer(int ply, uint8_t from, uint8_t to) const {
    if (ply < 0 || ply >= MAX_PLY) return 0;
    
    if (killer_moves[ply][0].matches(from, to)) return 1;
    if (killer_moves[ply][1].matches(from, to)) return 2;
    return 0;
}

// ==================== HISTORY HEURISTIC ====================

void Agent::clear_history() {
    memset(history_table, 0, sizeof(history_table));
}

void Agent::update_history(uint8_t from, uint8_t to, int depth) {
    if (from >= 64 || to >= 64) return;
    
    int32_t bonus = depth * depth;
    history_table[from][to] += bonus;
    
    if (history_table[from][to] > HISTORY_MAX) {
        for (int f = 0; f < 64; f++) {
            for (int t = 0; t < 64; t++) {
                history_table[f][t] /= 2;
            }
        }
    }
}

int32_t Agent::get_history(uint8_t from, uint8_t to) const {
    if (from >= 64 || to >= 64) return 0;
    return history_table[from][to];
}

// ==================== MOVE ORDERING ====================

int16_t Agent::score_move(const FastMove &m, uint8_t tt_best_from, uint8_t tt_best_to, int ply) const {
    // TT best move has highest priority
    if (m.from == tt_best_from && m.to == tt_best_to && tt_best_from != 255) {
        return SCORE_TT_MOVE;
    }
    
    int16_t score = 0;
    uint8_t promo_piece = (m.flags >> 3) & 7;
    bool is_capture = (m.flags & 1) || (m.flags & 2);
    
    // Promotions
    if (promo_piece) {
        if (promo_piece == PIECE_QUEEN) {
            score = SCORE_QUEEN_PROMOTION;
        } else {
            score = SCORE_OTHER_PROMOTION + promo_piece * 10;
        }
        
        if (is_capture) {
            uint8_t victim_type = GET_PIECE_TYPE(m.captured);
            score += mvv_lva_table[victim_type][PIECE_PAWN];
        }
    }
    // Captures - MVV-LVA
    else if (is_capture) {
        uint8_t victim_type = GET_PIECE_TYPE(m.captured);
        uint8_t attacker_type = GET_PIECE_TYPE(board->get_piece_on_square(m.from));
        score = SCORE_CAPTURE_BASE + mvv_lva_table[victim_type][attacker_type];
    }
    // Quiet moves - Killers and History
    else {
        int killer_idx = is_killer(ply, m.from, m.to);
        if (killer_idx == 1) {
            score = SCORE_KILLER_1;
        } else if (killer_idx == 2) {
            score = SCORE_KILLER_2;
        } else {
            int32_t hist = get_history(m.from, m.to);
            if (hist > 0) {
                score = static_cast<int16_t>(std::min(static_cast<int32_t>(SCORE_HISTORY_MAX), hist / 10));
            } else {
                score = SCORE_QUIET_MOVE;
            }
            
            // Small bonus for castling
            if (m.flags & 4) {
                score += 50;
            }
        }
    }
    
    return score;
}

void Agent::score_moves(MoveList &moves, uint8_t tt_best_from, uint8_t tt_best_to, int ply) const {
    for (int i = 0; i < moves.count; i++) {
        moves.moves[i].score = score_move(moves.moves[i], tt_best_from, tt_best_to, ply);
    }
}

void Agent::sort_moves(MoveList &moves) const {
    // Counting sort - optimized for the known range of move scores
    // Scores range from 0 to ~30000, but we can bucket them efficiently

    if (moves.count <= 1) return;

    // Use insertion sort for small lists (faster due to cache locality)
    if (moves.count < 10) {
        for (int i = 1; i < moves.count; i++) {
            FastMove key = moves.moves[i];
            int j = i - 1;
            while (j >= 0 && moves.moves[j].score < key.score) {
                moves.moves[j + 1] = moves.moves[j];
                j--;
            }
            moves.moves[j + 1] = key;
        }
        return;
    }

    // For larger lists, use std::sort (fast enough with good compilers)
    std::sort(moves.moves, moves.moves + moves.count,
        [](const FastMove &a, const FastMove &b) {
            return a.score > b.score;
        }
    );
}

// ==================== ALPHA-BETA SEARCH ====================

int Agent::minimax_internal(int depth, int ply, int alpha, int beta, bool is_maximizing) {
    int original_alpha = alpha;
    
    // TT Probe
    uint64_t hash = board->get_hash();
    TTEntry* tt_entry = tt_probe(hash);
    uint8_t tt_best_from = 255;
    uint8_t tt_best_to = 255;
    
    if (tt_entry) {
        tt_best_from = tt_entry->best_from;
        tt_best_to = tt_entry->best_to;
        
        if (tt_entry->depth >= depth) {
            int tt_score = tt_entry->score;
            
            switch (tt_entry->flag) {
                case TT_FLAG_EXACT:
                    return tt_score;
                case TT_FLAG_ALPHA:
                    if (tt_score <= alpha) return tt_score;
                    if (tt_score < beta) beta = tt_score;
                    break;
                case TT_FLAG_BETA:
                    if (tt_score >= beta) return tt_score;
                    if (tt_score > alpha) alpha = tt_score;
                    break;
            }
        }
    }
    
    // Terminal node check
    uint8_t current_turn = board->get_turn();
    bool in_check = board->is_king_in_check(current_turn);
    bool has_moves = board->has_legal_moves();
    
    if (!has_moves) {
        if (in_check) {
            return is_maximizing ? (-CHECKMATE_SCORE + ply) : (CHECKMATE_SCORE - ply);
        } else {
            return STALEMATE_SCORE;
        }
    }
    
    // Leaf node - evaluate
    if (depth == 0) {
        // Evaluate from the perspective of the player who initiated the search
        // This is determined by checking if we're at an even or odd ply
        // At ply 0 (root), we evaluate from current player's perspective
        // We need to determine the root player's color
        uint8_t root_color = (ply % 2 == 0) ? current_turn : (1 - current_turn);
        uint8_t eval_color = (root_color == 0) ? COLOR_WHITE : COLOR_BLACK;

        int score = evaluate(eval_color);
        tt_store(hash, score, 0, TT_FLAG_EXACT, 255, 255);
        return score;
    }
    
    // Generate and sort moves
    MoveList moves;
    board->generate_all_pseudo_legal(moves);
    score_moves(moves, tt_best_from, tt_best_to, ply);
    sort_moves(moves);
    
    uint8_t current_color = current_turn;
    
    uint8_t ep_before = board->get_en_passant_target();
    bool castling_before[4];
    const bool* cr = board->get_castling_rights();
    for (int i = 0; i < 4; i++) castling_before[i] = cr[i];
    uint64_t hash_before = hash;
    
    uint8_t best_move_from = 255;
    uint8_t best_move_to = 255;
    
    if (is_maximizing) {
        int best_score = INT_MIN;
        
        for (int i = 0; i < moves.count; i++) {
            FastMove &m = moves.moves[i];
            
            board->make_move_fast(m);
            
            uint8_t our_king = board->get_king_pos(current_color);
            if (!board->is_square_attacked_fast(our_king, 1 - current_color)) {
                int score = minimax_internal(depth - 1, ply + 1, alpha, beta, false);
                
                if (score > best_score) {
                    best_score = score;
                    best_move_from = m.from;
                    best_move_to = m.to;
                }
                
                if (score > alpha) {
                    alpha = score;
                }
                
                if (score >= beta) {
                    board->unmake_move_fast(m, ep_before, castling_before, hash_before);
                    
                    // Update killers and history for quiet moves
                    bool is_capture = (m.flags & 1) || (m.flags & 2);
                    bool is_promotion = ((m.flags >> 3) & 7) != 0;
                    if (!is_capture && !is_promotion) {
                        store_killer(ply, m.from, m.to);
                        update_history(m.from, m.to, depth);
                    }
                    
                    tt_store(hash_before, best_score, depth, TT_FLAG_BETA, best_move_from, best_move_to);
                    return best_score;
                }
            }
            
            board->unmake_move_fast(m, ep_before, castling_before, hash_before);
        }
        
        int tt_flag = (best_score <= original_alpha) ? TT_FLAG_ALPHA : TT_FLAG_EXACT;
        tt_store(hash_before, best_score, depth, tt_flag, best_move_from, best_move_to);
        
        return best_score;
    } else {
        int best_score = INT_MAX;
        
        for (int i = 0; i < moves.count; i++) {
            FastMove &m = moves.moves[i];
            
            board->make_move_fast(m);
            
            uint8_t our_king = board->get_king_pos(current_color);
            if (!board->is_square_attacked_fast(our_king, 1 - current_color)) {
                int score = minimax_internal(depth - 1, ply + 1, alpha, beta, true);
                
                if (score < best_score) {
                    best_score = score;
                    best_move_from = m.from;
                    best_move_to = m.to;
                }
                
                if (score < beta) {
                    beta = score;
                }
                
                if (score <= alpha) {
                    board->unmake_move_fast(m, ep_before, castling_before, hash_before);
                    
                    // Update killers and history for quiet moves
                    bool is_capture = (m.flags & 1) || (m.flags & 2);
                    bool is_promotion = ((m.flags >> 3) & 7) != 0;
                    if (!is_capture && !is_promotion) {
                        store_killer(ply, m.from, m.to);
                        update_history(m.from, m.to, depth);
                    }
                    
                    tt_store(hash_before, best_score, depth, TT_FLAG_ALPHA, best_move_from, best_move_to);
                    return best_score;
                }
            }
            
            board->unmake_move_fast(m, ep_before, castling_before, hash_before);
        }
        
        tt_store(hash_before, best_score, depth, TT_FLAG_EXACT, best_move_from, best_move_to);
        
        return best_score;
    }
}

// ==================== EVALUATION ====================

int Agent::evaluate(uint8_t color) {
    if (!board) return 0;

    if (use_neural_network && network_initialized) {
        // Extract features and run neural network
        // Board will be mirrored if color is COLOR_BLACK
        extract_features(color);
        float nn_score = forward_pass(input_features);

        // Convert float score to centipawns
        return static_cast<int>(nn_score);
    } else {
        // Use simple material evaluation
        return evaluate_material();
    }
}

int Agent::evaluate_material() const {
    if (!board) return 0;

    int score = 0;
    const uint8_t* squares = board->get_squares();

    // Use piece lists for faster iteration (avoid scanning empty squares)
    const uint8_t* white_pieces = board->get_white_piece_list();
    const uint8_t* black_pieces = board->get_black_piece_list();
    const uint8_t white_count = board->get_white_piece_count();
    const uint8_t black_count = board->get_black_piece_count();

    // Precomputed piece values array (index 0 unused)
    static const int piece_values[7] = {0, PAWN_VALUE, KNIGHT_VALUE, BISHOP_VALUE, ROOK_VALUE, QUEEN_VALUE, 0};

    // Sum white material
    for (uint8_t i = 0; i < white_count; i++) {
        const uint8_t sq = white_pieces[i];
        const uint8_t piece_type = GET_PIECE_TYPE(squares[sq]);
        score += piece_values[piece_type];
    }

    // Subtract black material
    for (uint8_t i = 0; i < black_count; i++) {
        const uint8_t sq = black_pieces[i];
        const uint8_t piece_type = GET_PIECE_TYPE(squares[sq]);
        score -= piece_values[piece_type];
    }

    return score;
}

Array Agent::get_features() {
    Array result;

    if (!board) return result;

    // Default to white's perspective for backward compatibility
    extract_features(COLOR_WHITE);

    for (size_t i = 0; i < input_features.size(); i++) {
        result.append(input_features[i]);
    }

    return result;
}

Array Agent::get_features_for_color(uint8_t color) {
    Array result;

    if (!board) return result;

    extract_features(color);

    for (size_t i = 0; i < input_features.size(); i++) {
        result.append(input_features[i]);
    }

    return result;
}

// ==================== NEURAL NETWORK CONTROL ====================

void Agent::set_use_neural_network(bool use_nn) {
    use_neural_network = use_nn;
}

// ==================== SEARCH INTERFACE ====================

Dictionary Agent::get_best_move(int depth) {
    Dictionary result;
    if (!board) return result;
    
    clear_killers();
    clear_history();
    tt_new_search();
    
    MoveList moves;
    board->generate_all_pseudo_legal(moves);
    
    TTEntry* tt_entry = tt_probe(board->get_hash());
    uint8_t tt_best_from = (tt_entry) ? tt_entry->best_from : 255;
    uint8_t tt_best_to = (tt_entry) ? tt_entry->best_to : 255;
    
    score_moves(moves, tt_best_from, tt_best_to, 0);
    sort_moves(moves);
    
    uint8_t current_color = board->get_turn();
    bool is_maximizing = (current_color == 0);
    
    uint8_t ep_before = board->get_en_passant_target();
    bool castling_before[4];
    const bool* cr = board->get_castling_rights();
    for (int i = 0; i < 4; i++) castling_before[i] = cr[i];
    uint64_t hash_before = board->get_hash();
    
    int alpha = INT_MIN;
    int beta = INT_MAX;
    
    int best_score = is_maximizing ? INT_MIN : INT_MAX;
    int best_from = -1;
    int best_to = -1;
    
    for (int i = 0; i < moves.count; i++) {
        FastMove &m = moves.moves[i];
        
        board->make_move_fast(m);
        
        uint8_t our_king = board->get_king_pos(current_color);
        if (!board->is_square_attacked_fast(our_king, 1 - current_color)) {
            int score = minimax_internal(depth - 1, 1, alpha, beta, !is_maximizing);
            
            if (is_maximizing) {
                if (score > best_score) {
                    best_score = score;
                    best_from = m.from;
                    best_to = m.to;
                }
                if (score > alpha) {
                    alpha = score;
                }
            } else {
                if (score < best_score) {
                    best_score = score;
                    best_from = m.from;
                    best_to = m.to;
                }
                if (score < beta) {
                    beta = score;
                }
            }
        }
        
        board->unmake_move_fast(m, ep_before, castling_before, hash_before);
    }
    
    if (best_from >= 0) {
        tt_store(hash_before, best_score, depth, TT_FLAG_EXACT, best_from, best_to);
        
        result["from"] = best_from;
        result["to"] = best_to;
        result["score"] = best_score;
    }
    
    return result;
}

Dictionary Agent::run_iterative_deepening(int max_depth) {
    Dictionary best_result;
    if (!board) return best_result;
    
    clear_killers();
    clear_history();
    tt_new_search();
    
    for (int current_depth = 1; current_depth <= max_depth; current_depth++) {
        Dictionary result;
        
        MoveList moves;
        board->generate_all_pseudo_legal(moves);
        
        TTEntry* tt_entry = tt_probe(board->get_hash());
        uint8_t tt_best_from = (tt_entry) ? tt_entry->best_from : 255;
        uint8_t tt_best_to = (tt_entry) ? tt_entry->best_to : 255;
        
        score_moves(moves, tt_best_from, tt_best_to, 0);
        sort_moves(moves);
        
        uint8_t current_color = board->get_turn();
        bool is_maximizing = (current_color == 0);
        
        uint8_t ep_before = board->get_en_passant_target();
        bool castling_before[4];
        const bool* cr = board->get_castling_rights();
        for (int i = 0; i < 4; i++) castling_before[i] = cr[i];
        uint64_t hash_before = board->get_hash();
        
        int alpha = INT_MIN;
        int beta = INT_MAX;
        
        int best_score = is_maximizing ? INT_MIN : INT_MAX;
        int best_from = -1;
        int best_to = -1;
        
        for (int i = 0; i < moves.count; i++) {
            FastMove &m = moves.moves[i];
            
            board->make_move_fast(m);
            
            uint8_t our_king = board->get_king_pos(current_color);
            if (!board->is_square_attacked_fast(our_king, 1 - current_color)) {
                int score = minimax_internal(current_depth - 1, 1, alpha, beta, !is_maximizing);
                
                if (is_maximizing) {
                    if (score > best_score) {
                        best_score = score;
                        best_from = m.from;
                        best_to = m.to;
                    }
                    if (score > alpha) {
                        alpha = score;
                    }
                } else {
                    if (score < best_score) {
                        best_score = score;
                        best_from = m.from;
                        best_to = m.to;
                    }
                    if (score < beta) {
                        beta = score;
                    }
                }
            }
            
            board->unmake_move_fast(m, ep_before, castling_before, hash_before);
        }
        
        if (best_from >= 0) {
            tt_store(hash_before, best_score, current_depth, TT_FLAG_EXACT, best_from, best_to);
            
            result["from"] = best_from;
            result["to"] = best_to;
            result["score"] = best_score;
            result["depth"] = current_depth;
            
            best_result = result;
            
            // Early termination on checkmate
            if (best_score >= CHECKMATE_SCORE - 100 || best_score <= -CHECKMATE_SCORE + 100) {
                break;
            }
        }
    }
    
    return best_result;
}

// ==================== CONSTRUCTOR/DESTRUCTOR ====================

Agent::Agent() : NeuralNet() {
    board = nullptr;
    use_neural_network = false;
    input_features.reserve(NN_TOTAL_INPUTS);

    init_tt();
    init_mvv_lva_table();

    clear_killers();
    clear_history();
}

Agent::~Agent() {
    // Static members are not deleted here
}

void Agent::set_board(Board* p_board) {
    board = p_board;
}

void Agent::_ready() {
    NeuralNet::_ready();
}

// ==================== TRAINING METHODS ====================

float Agent::score_to_target(int material_score) const {
    // Convert centipawn score to a 0.0-1.0 range using a sigmoid-like function
    // This creates a smooth mapping:
    // - Score of +300 (3 pawns ahead) → ~0.75
    // - Score of 0 (equal) → 0.5
    // - Score of -300 (3 pawns behind) → ~0.25
    // - Score of ±1000 → ~0.88 / ~0.12

    // Scale factor determines the steepness of the sigmoid
    // Larger scale = more gradual transition
    const float scale = 600.0f;  // Tuned for centipawn scores

    // Sigmoid formula: 1 / (1 + exp(-x / scale))
    float normalized_score = static_cast<float>(material_score) / scale;
    float target = 1.0f / (1.0f + std::exp(-normalized_score));

    // Clamp to [0.01, 0.99] to avoid extreme targets
    if (target < 0.01f) target = 0.01f;
    if (target > 0.99f) target = 0.99f;

    return target;
}

float Agent::train_on_current_position(uint8_t color, float learning_rate) {
    if (!board || !network_initialized || !use_neural_network) {
        return 0.0f;
    }

    // 1. Extract features from current position
    extract_features(color);

    // 2. Get material evaluation as target
    int material_score = evaluate_material();

    // Adjust score from the perspective of the given color
    // If training from black's perspective, negate the score
    if (color == COLOR_BLACK) {
        material_score = -material_score;
    }

    // 3. Convert score to 0.0-1.0 target range
    float target = score_to_target(material_score);

    // 4. Convert input_features to Array for training
    Array input_array;
    for (const float& feature : input_features) {
        input_array.append(feature);
    }

    // 5. Train on this example
    float loss = train_single_example(input_array, target, learning_rate);

    return loss;
}

float Agent::train_on_batch(const Array &positions, const Array &targets, float learning_rate) {
    if (!network_initialized || !use_neural_network) {
        return 0.0f;
    }

    if (positions.size() != targets.size()) {
        UtilityFunctions::print("Error: positions and targets arrays must have the same size");
        return 0.0f;
    }

    if (positions.size() == 0) {
        return 0.0f;
    }

    float total_loss = 0.0f;

    // Train on each position in the batch
    for (int i = 0; i < positions.size(); i++) {
        Array position_features = positions[i];
        float target = targets[i];

        float loss = train_single_example(position_features, target, learning_rate);
        total_loss += loss;
    }

    // Return average loss
    return total_loss / positions.size();
}

// ==================== GODOT BINDINGS ====================

void Agent::_bind_methods() {
    // Board binding
    ClassDB::bind_method(D_METHOD("set_board", "board"), &Agent::set_board);
    ClassDB::bind_method(D_METHOD("get_board"), &Agent::get_board);

    // Evaluation
    ClassDB::bind_method(D_METHOD("evaluate", "color"), &Agent::evaluate);
    ClassDB::bind_method(D_METHOD("evaluate_material"), &Agent::evaluate_material);
    ClassDB::bind_method(D_METHOD("get_features"), &Agent::get_features);
    ClassDB::bind_method(D_METHOD("get_features_for_color", "color"), &Agent::get_features_for_color);

    // Neural network control
    ClassDB::bind_method(D_METHOD("set_use_neural_network", "use_nn"), &Agent::set_use_neural_network);
    ClassDB::bind_method(D_METHOD("get_use_neural_network"), &Agent::get_use_neural_network);

    // Search methods
    ClassDB::bind_method(D_METHOD("run_iterative_deepening", "max_depth"), &Agent::run_iterative_deepening);
    ClassDB::bind_method(D_METHOD("get_best_move", "depth"), &Agent::get_best_move);

    // Training methods
    ClassDB::bind_method(D_METHOD("train_on_current_position", "color", "learning_rate"), &Agent::train_on_current_position);
    ClassDB::bind_method(D_METHOD("train_on_batch", "positions", "targets", "learning_rate"), &Agent::train_on_batch);
    ClassDB::bind_method(D_METHOD("score_to_target", "material_score"), &Agent::score_to_target);
}
