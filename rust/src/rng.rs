//! A tiny deterministic random number generator.
//!
//! Only `scramble` needs randomness, and it needs reproducibility more than
//! statistical quality, so SplitMix64 — the seeding routine of the xoshiro
//! family — is plenty and fits in a dozen lines. No dependency required.

use std::time::{SystemTime, UNIX_EPOCH};

/// SplitMix64.
#[derive(Debug, Clone)]
pub struct Rng(u64);

impl Rng {
    /// A generator that will always produce the same sequence for a seed.
    pub fn new(seed: u64) -> Self {
        Self(seed)
    }

    /// A generator seeded from the clock.
    pub fn from_clock() -> Self {
        let nanos = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .map(|since| since.as_nanos() as u64)
            .unwrap_or(0x2545_F491_4F6C_DD1D);
        Self(nanos)
    }

    pub fn next_u64(&mut self) -> u64 {
        self.0 = self.0.wrapping_add(0x9E37_79B9_7F4A_7C15);
        let mut z = self.0;
        z = (z ^ (z >> 30)).wrapping_mul(0xBF58_476D_1CE4_E5B9);
        z = (z ^ (z >> 27)).wrapping_mul(0x94D0_49BB_1331_11EB);
        z ^ (z >> 31)
    }

    /// A value in `0..bound`, via the multiply-shift trick — no modulo bias
    /// worth caring about at the sizes used here.
    pub fn below(&mut self, bound: u64) -> u64 {
        ((u128::from(self.next_u64()) * u128::from(bound)) >> 64) as u64
    }
}

impl Default for Rng {
    fn default() -> Self {
        Self::from_clock()
    }
}
