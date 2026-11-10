#include "zobrist.h"

namespace Zobrist {

// Define the extern variables
uint64_t piece_keys[12][64];
uint64_t castling_keys[4];
uint64_t en_passant_keys[8];
uint64_t side_key;
bool initialized = false;

// Simple 64-bit pseudo-random number generator (xorshift64)
// We use a fixed seed for reproducibility across runs
class PRNG {
private:
    uint64_t state;
    
public:
    PRNG(uint64_t seed) : state(seed) {}
    
    uint64_t next() {
        // xorshift64 algorithm
        state ^= state >> 12;
        state ^= state << 25;
        state ^= state >> 27;
        return state * 0x2545F4914F6CDD1DULL;
    }
};

void init() {
    if (initialized) return;
    
    // Use a fixed seed for reproducible hashes
    // This allows positions to have the same hash across program restarts
    PRNG rng(0x98765432FEDCBA01ULL);
    
    // Initialize piece keys
    // piece_keys[piece_index][square]
    // piece_index 0-5: White Pawn, Knight, Bishop, Rook, Queen, King
    // piece_index 6-11: Black Pawn, Knight, Bishop, Rook, Queen, King
    for (int piece = 0; piece < 12; piece++) {
        for (int square = 0; square < 64; square++) {
            piece_keys[piece][square] = rng.next();
        }
    }
    
    // Initialize castling keys
    for (int i = 0; i < 4; i++) {
        castling_keys[i] = rng.next();
    }
    
    // Initialize en passant keys (one per file)
    for (int file = 0; file < 8; file++) {
        en_passant_keys[file] = rng.next();
    }
    
    // Initialize side to move key
    side_key = rng.next();
    
    initialized = true;
}

} // namespace Zobrist
