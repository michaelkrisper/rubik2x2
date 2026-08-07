//! A hasher for dense integer keys.
//!
//! The bidirectional search keys its frontiers by state index — a `u32` below
//! 3,674,160. `std`'s default `SipHash` is a cryptographic-strength hash aimed
//! at hostile input, and a solve spends a measurable fraction of its time in
//! it. Multiplying by a 64-bit odd constant (Fibonacci hashing) spreads a small
//! integer over the whole word for a couple of cycles instead, which is all the
//! table needs: the keys are ours, not an attacker's.

use std::collections::HashMap;
use std::hash::{BuildHasherDefault, Hasher};

/// 2^64 / golden ratio, odd, well-mixed in the high bits.
const MULTIPLIER: u64 = 0x9E37_79B9_7F4A_7C15;

/// Fibonacci hashing for integer keys. Not a general-purpose hasher: it assumes
/// keys arrive as a single integer write, and falls back to a byte-wise FNV-ish
/// mix for anything else.
#[derive(Default)]
pub struct IntHasher(u64);

impl Hasher for IntHasher {
    fn write(&mut self, bytes: &[u8]) {
        for &byte in bytes {
            self.0 = (self.0 ^ u64::from(byte)).wrapping_mul(MULTIPLIER);
        }
    }

    fn write_u32(&mut self, value: u32) {
        self.0 = u64::from(value).wrapping_mul(MULTIPLIER);
    }

    fn write_u64(&mut self, value: u64) {
        self.0 = value.wrapping_mul(MULTIPLIER);
    }

    fn finish(&self) -> u64 {
        // `HashMap` takes the bucket from the low bits and its control tag from
        // the high ones, so both ends have to carry entropy.
        self.0 ^ (self.0 >> 29)
    }
}

/// A [`HashMap`] keyed by integers, hashed by [`IntHasher`].
pub type IntMap<K, V> = HashMap<K, V, BuildHasherDefault<IntHasher>>;
