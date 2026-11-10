#ifndef ZOBRIST_H
#define ZOBRIST_H

#include <cstdint>

// Zobrist hashing namespace
// Contains random 64-bit numbers for computing position hashes
namespace Zobrist {

// Random numbers for each piece type (0-11) on each square (0-63)
// Index: [piece_index][square]
// piece_index: 0-5 = White P,N,B,R,Q,K; 6-11 = Black P,N,B,R,Q,K
extern uint64_t piece_keys[12][64];

// Random numbers for castling rights (4 bits = 16 combinations, but we use 4 individual keys)
// Index: [0]=WK, [1]=WQ, [2]=BK, [3]=BQ
extern uint64_t castling_keys[4];

// Random numbers for en passant file (0-7, or no en passant)
// We only need 8 keys for the file, not 64 for each square
extern uint64_t en_passant_keys[8];

// Random number for side to move (XOR when black to move)
extern uint64_t side_key;

// Flag to check if tables are initialized
extern bool initialized;

// Initialize all Zobrist keys with random numbers
// Uses a seeded PRNG for reproducibility
void init();

// Get the piece index for Zobrist table lookup
// Returns 0-5 for white pieces, 6-11 for black pieces
// piece_type: 1-6 (PAWN to KING)
// is_white: true for white, false for black
inline int get_piece_index(int piece_type, bool is_white) {
    // piece_type is 1-6, we want 0-5 for array indexing
    return (piece_type - 1) + (is_white ? 0 : 6);
}

} // namespace Zobrist

#endif // ZOBRIST_H
