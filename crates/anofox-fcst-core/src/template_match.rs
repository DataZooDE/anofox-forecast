//! Exact sub-quadratic template-match counting for approximate / sample
//! entropy (#304).
//!
//! [`crate::stats::template_match_counts_band`] counts, for every length-`m`
//! template `i`, the templates `j` (self included) within Chebyshev distance
//! `r`. Its band search only prunes on the first coordinate, so the work stays
//! O(n^2) for long series. This module returns the same counts in
//! O(n log n) (m = 2) and O(n log^2 n) (m = 3).
//!
//! # Rank-space reduction
//!
//! Every coordinate of every template is a value of the series `x`, so the
//! sorted, deduplicated values `u` give each `x[k]` an integer rank. For a fixed
//! query value `a`, the set of values `v` that match it on one coordinate is a
//! contiguous run of `u`: each match predicate below is a conjunction of
//! conditions that are monotone in `v` (IEEE subtraction rounds monotonically)
//! and holds at `v = a`. So template `i`'s match set is an integer box in rank
//! space, and counting matches is exact orthogonal range counting.
//!
//! # Exactness
//!
//! The intervals are found by binary search with the *same* float expressions
//! the band search evaluates, so the counts are bit-identical, not just equal
//! for well-behaved data:
//!
//! - coordinate 0 uses the band prefilter and the distance check,
//!   `!(v < a - r) && v <= a + r && (a - v).abs() <= r`. This is not the same as
//!   the distance check alone: for `a = 1.0`, `v = -1e-17`, `r = 1.0`,
//!   `a - r` is `0.0`, so the band excludes `v` although `(a - v).abs()` rounds
//!   to exactly `1.0`. The relation is asymmetric (the reverse query accepts).
//! - coordinates `1..m` use only `(a - v).abs() <= r`.
//!
//! Callers must pass finite `x` and a finite `r >= 0` (the dispatcher in
//! `stats.rs` falls back to the band search otherwise).

/// Sentinel `tag` of a point item.
const POINT: u32 = u32::MAX;
/// Bit of a query item's `tag` marking a negative inclusion-exclusion sign.
const NEG: u32 = 1 << 31;

/// Upper bound on CDQ items (points + corner queries) held at once. An item is
/// 20 bytes and the merge scratch doubles it, so a pass peaks at ~320 MB.
const MAX_CDQ_ITEMS_PER_PASS: usize = 8_000_000;

/// Ranks of `x` plus, per position, the rank interval of matching values.
struct RankSpace {
    /// Number of distinct values (Fenwick size).
    size: usize,
    rank: Vec<u32>,
    /// Coordinate-0 (band-aware) interval, for template starts `0..n_m`.
    lo0: Vec<u32>,
    hi0: Vec<u32>,
    /// Distance-only interval, for every position `0..n`.
    lo: Vec<u32>,
    hi: Vec<u32>,
}

impl RankSpace {
    fn new(x: &[f64], n_m: usize, r: f64) -> Self {
        let mut u = x.to_vec();
        u.sort_by(f64::total_cmp);
        // Merges -0.0 and 0.0, which behave identically in every predicate.
        u.dedup_by(|a, b| a == b);

        let rank: Vec<u32> = x
            .iter()
            .map(|&a| u.partition_point(|&v| v < a) as u32)
            .collect();

        // The band search's `!(v < a - r)`; equal to `v >= a - r` because `a`
        // and `r` are finite, so `a - r` is never NaN.
        let band = |a: f64, v: f64| v >= a - r && v <= a + r && (a - v).abs() <= r;
        let dist = |a: f64, v: f64| (a - v).abs() <= r;

        let (lo0, hi0) = x[..n_m].iter().map(|&a| interval(&u, a, band)).unzip();
        let (lo, hi) = x.iter().map(|&a| interval(&u, a, dist)).unzip();

        Self {
            size: u.len(),
            rank,
            lo0,
            hi0,
            lo,
            hi,
        }
    }
}

/// Rank interval `[lo, hi]` of the values `v` in `u` with `pred(a, v)`, where
/// `pred` holds at `v = a` and its match set is contiguous around `a`.
fn interval(u: &[f64], a: f64, pred: impl Fn(f64, f64) -> bool) -> (u32, u32) {
    let lo = u.partition_point(|&v| v < a && !pred(a, v));
    let hi = u.partition_point(|&v| v <= a || pred(a, v)) - 1;
    (lo as u32, hi as u32)
}

/// Fenwick tree over rank indices `0..size`.
struct Fenwick {
    tree: Vec<i32>,
}

impl Fenwick {
    fn new(size: usize) -> Self {
        Self {
            tree: vec![0; size + 1],
        }
    }

    fn add(&mut self, i: u32, delta: i32) {
        let mut k = i as usize + 1;
        while k < self.tree.len() {
            self.tree[k] += delta;
            k += k & k.wrapping_neg();
        }
    }

    /// Count of inserted ranks `< end`.
    fn prefix(&self, end: usize) -> i64 {
        let mut k = end;
        let mut sum = 0i64;
        while k > 0 {
            sum += self.tree[k] as i64;
            k &= k - 1;
        }
        sum
    }

    /// Count of inserted ranks in `[lo, hi]`.
    fn range(&self, lo: u32, hi: u32) -> i64 {
        self.prefix(hi as usize + 1) - self.prefix(lo as usize)
    }
}

fn into_counts(acc: Vec<i64>) -> Vec<usize> {
    acc.into_iter()
        .map(|c| {
            debug_assert!(c >= 1, "a template always matches itself");
            c as usize
        })
        .collect()
}

/// Template-match counts for `m = 2`: offline sweep over the first coordinate
/// with a Fenwick tree over the second.
pub(crate) fn count_m2(x: &[f64], r: f64) -> Vec<usize> {
    let n_m = x.len() - 1;
    let rs = RankSpace::new(x, n_m, r);

    // Inclusion-exclusion on the first coordinate: + at hi0, - at lo0 - 1.
    // Points and corner queries are bucketed by threshold (counting sort).
    let mut point_start = vec![0usize; rs.size + 1];
    let mut query_start = vec![0usize; rs.size + 1];
    for j in 0..n_m {
        point_start[rs.rank[j] as usize + 1] += 1;
        query_start[rs.hi0[j] as usize + 1] += 1;
        if rs.lo0[j] > 0 {
            query_start[rs.lo0[j] as usize] += 1;
        }
    }
    for t in 0..rs.size {
        point_start[t + 1] += point_start[t];
        query_start[t + 1] += query_start[t];
    }
    let mut points = vec![0u32; n_m];
    let mut queries = vec![0u32; query_start[rs.size]];
    let mut point_fill = point_start.clone();
    let mut query_fill = query_start.clone();
    for i in 0..n_m {
        let t = rs.rank[i] as usize;
        points[point_fill[t]] = i as u32;
        point_fill[t] += 1;
        let t = rs.hi0[i] as usize;
        queries[query_fill[t]] = i as u32;
        query_fill[t] += 1;
        if rs.lo0[i] > 0 {
            let t = rs.lo0[i] as usize - 1;
            queries[query_fill[t]] = i as u32 | NEG;
            query_fill[t] += 1;
        }
    }

    let mut fen = Fenwick::new(rs.size);
    let mut acc = vec![0i64; n_m];
    for t in 0..rs.size {
        for &j in &points[point_start[t]..point_start[t + 1]] {
            fen.add(rs.rank[j as usize + 1], 1);
        }
        for &tag in &queries[query_start[t]..query_start[t + 1]] {
            let i = (tag & !NEG) as usize;
            let c = fen.range(rs.lo[i + 1], rs.hi[i + 1]);
            acc[i] += if tag & NEG != 0 { -c } else { c };
        }
    }

    into_counts(acc)
}

/// One CDQ item: a point `(a, b, c)` (`tag == POINT`, `c_lo == c_hi == c`) or
/// a corner query counting points with `a' <= a`, `b' <= b`, `c' in [c_lo, c_hi]`.
#[derive(Clone, Copy)]
struct Item {
    a: u32,
    b: u32,
    c_lo: u32,
    c_hi: u32,
    tag: u32,
}

impl Item {
    fn is_query(&self) -> bool {
        self.tag != POINT
    }

    /// Sort key at one coordinate: points before queries on ties.
    fn key(coord: u32, item: &Item) -> u64 {
        ((coord as u64) << 1) | item.is_query() as u64
    }
}

/// Template-match counts for `m = 3`: 3D dominance counting by CDQ
/// divide-and-conquer (first coordinate by position, second by merge, third by
/// a Fenwick range query).
pub(crate) fn count_m3(x: &[f64], r: f64) -> Vec<usize> {
    count_m3_with_pass_cap(x, r, MAX_CDQ_ITEMS_PER_PASS)
}

/// [`count_m3`] with the per-pass item cap exposed so tests can force the
/// multi-pass path. The four inclusion-exclusion corner families over the
/// first two coordinates are split across passes so that a pass holds at most
/// `max_items_per_pass` items (at least one family per pass).
fn count_m3_with_pass_cap(x: &[f64], r: f64, max_items_per_pass: usize) -> Vec<usize> {
    let n_m = x.len() - 2;
    let rs = RankSpace::new(x, n_m, r);

    let families_per_pass = [4usize, 2, 1]
        .into_iter()
        .find(|&f| n_m * (1 + f) <= max_items_per_pass)
        .unwrap_or(1);

    let mut fen = Fenwick::new(rs.size);
    let mut acc = vec![0i64; n_m];
    let mut items: Vec<Item> = Vec::with_capacity(n_m * (1 + families_per_pass));
    let mut scratch: Vec<Item> = Vec::new();

    for families in (0..4u8).collect::<Vec<_>>().chunks(families_per_pass) {
        items.clear();
        items.extend((0..n_m).map(|j| {
            let c = rs.rank[j + 2];
            Item {
                a: rs.rank[j],
                b: rs.rank[j + 1],
                c_lo: c,
                c_hi: c,
                tag: POINT,
            }
        }));
        for i in 0..n_m {
            let (a_lo, a_hi) = (rs.lo0[i], rs.hi0[i]);
            let (b_lo, b_hi) = (rs.lo[i + 1], rs.hi[i + 1]);
            for &family in families {
                // (use the low corner on a?, use the low corner on b?)
                let (low_a, low_b) = (family & 1 != 0, family & 2 != 0);
                if (low_a && a_lo == 0) || (low_b && b_lo == 0) {
                    continue; // corner below rank 0 counts nothing
                }
                let negative = low_a != low_b;
                items.push(Item {
                    a: if low_a { a_lo - 1 } else { a_hi },
                    b: if low_b { b_lo - 1 } else { b_hi },
                    c_lo: rs.lo[i + 2],
                    c_hi: rs.hi[i + 2],
                    tag: i as u32 | if negative { NEG } else { 0 },
                });
            }
        }

        items.sort_unstable_by_key(|it| Item::key(it.a, it));
        scratch.resize(items.len(), items[0]);
        cdq(&mut items, &mut scratch, &mut fen, &mut acc);
    }

    into_counts(acc)
}

/// Below this many items [`cdq`] counts pairwise instead of recursing.
const CDQ_LEAF: usize = 32;

/// Pairwise [`cdq`] base case: same contract, O(len^2).
fn cdq_leaf(items: &mut [Item], acc: &mut [i64]) {
    for k in 0..items.len() {
        let q = items[k];
        if !q.is_query() {
            continue;
        }
        let c = items[..k]
            .iter()
            .filter(|p| !p.is_query() && p.b <= q.b && q.c_lo <= p.c_lo && p.c_lo <= q.c_hi)
            .count() as i64;
        let i = (q.tag & !NEG) as usize;
        acc[i] += if q.tag & NEG != 0 { -c } else { c };
    }
    items.sort_by_key(|it| Item::key(it.b, it));
}

/// Adds, for every query, the points before it in `items` (all with a smaller
/// or equal first coordinate) that it dominates on `b` and contains on `c`.
/// Leaves `items` sorted by (`b`, point-first).
fn cdq(items: &mut [Item], scratch: &mut [Item], fen: &mut Fenwick, acc: &mut [i64]) {
    let len = items.len();
    if len <= CDQ_LEAF {
        cdq_leaf(items, acc);
        return;
    }
    let mid = len / 2;
    {
        let (left, right) = items.split_at_mut(mid);
        let (s_left, s_right) = scratch.split_at_mut(mid);
        cdq(left, s_left, fen, acc);
        cdq(right, s_right, fen, acc);
    }

    // Left points into right queries: both halves are sorted by b.
    let (left, right) = items.split_at(mid);
    let mut p = 0;
    for q in right.iter().filter(|it| it.is_query()) {
        while p < mid && left[p].b <= q.b {
            if !left[p].is_query() {
                fen.add(left[p].c_lo, 1);
            }
            p += 1;
        }
        let c = fen.range(q.c_lo, q.c_hi);
        let i = (q.tag & !NEG) as usize;
        acc[i] += if q.tag & NEG != 0 { -c } else { c };
    }
    for it in &left[..p] {
        if !it.is_query() {
            fen.add(it.c_lo, -1);
        }
    }

    // Merge the halves by (b, point-first).
    let (mut l, mut r, mut k) = (0, mid, 0);
    while l < mid && r < len {
        if Item::key(items[l].b, &items[l]) <= Item::key(items[r].b, &items[r]) {
            scratch[k] = items[l];
            l += 1;
        } else {
            scratch[k] = items[r];
            r += 1;
        }
        k += 1;
    }
    scratch[k..k + mid - l].copy_from_slice(&items[l..mid]);
    k += mid - l;
    scratch[k..k + len - r].copy_from_slice(&items[r..len]);
    items.copy_from_slice(scratch);
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::stats::template_match_counts_band;

    /// Tiny deterministic LCG (MMIX constants) giving floats in [0, 1).
    struct Lcg(u64);

    impl Lcg {
        fn next_f64(&mut self) -> f64 {
            self.0 = self
                .0
                .wrapping_mul(6364136223846793005)
                .wrapping_add(1442695040888963407);
            (self.0 >> 11) as f64 / (1u64 << 53) as f64
        }

        fn next_int(&mut self, modulus: u64) -> f64 {
            (self.next_f64() * modulus as f64).floor()
        }
    }

    fn fast(x: &[f64], m: usize, r: f64) -> Vec<usize> {
        match m {
            2 => count_m2(x, r),
            3 => count_m3(x, r),
            _ => unreachable!(),
        }
    }

    fn assert_same(case: &str, x: &[f64], r: f64) {
        for m in [2, 3] {
            if x.len() < m + 1 {
                continue;
            }
            assert_eq!(
                fast(x, m, r),
                template_match_counts_band(x, m, r),
                "case {case}, n = {}, m = {m}, r = {r}",
                x.len()
            );
        }
    }

    fn sd(x: &[f64], ddof: f64) -> f64 {
        let n = x.len() as f64;
        let mean = x.iter().sum::<f64>() / n;
        (x.iter().map(|v| (v - mean).powi(2)).sum::<f64>() / (n - ddof)).sqrt()
    }

    #[test]
    fn template_match_fast_continuous_random() {
        let mut rng = Lcg(42);
        for n in [3, 4, 5, 17, 100, 257, 1000, 3000] {
            let x: Vec<f64> = (0..n).map(|_| rng.next_f64() * 10.0 - 5.0).collect();
            for r in [
                0.2 * sd(&x, 1.0),
                0.2 * sd(&x, 0.0),
                0.05 * sd(&x, 1.0),
                sd(&x, 1.0),
            ] {
                assert_same("continuous", &x, r);
            }
        }
    }

    #[test]
    fn template_match_fast_smooth_series() {
        let x: Vec<f64> = (0..2000)
            .map(|i| (i as f64).sin() + (i % 97) as f64)
            .collect();
        assert_same("sin_mod97", &x, 0.2 * sd(&x, 1.0));
    }

    #[test]
    fn template_match_fast_integer_ties_at_distance_r() {
        let mut rng = Lcg(7);
        for modulus in [3u64, 7] {
            let x: Vec<f64> = (0..600).map(|_| rng.next_int(modulus)).collect();
            for r in [0.0, 0.5, 1.0, 2.0] {
                assert_same("integer", &x, r);
            }
        }
    }

    #[test]
    fn template_match_fast_degenerate_inputs() {
        assert_same("constant", &[3.0; 50], 0.0);
        assert_same("constant r>0", &[3.0; 50], 1.0);
        let repeated: Vec<f64> = (0..300).map(|i| [1.0, 1.0, 2.0, 5.0][i % 4]).collect();
        assert_same("repeated", &repeated, 1.0);
        assert_same("r=0 non-constant", &repeated, 0.0);
        let zeros: Vec<f64> = (0..200)
            .map(|i| [0.0, -0.0, 1.0, -1.0, -0.0][i % 5])
            .collect();
        assert_same("signed zeros", &zeros, 1.0);
        assert_same("signed zeros r=0", &zeros, 0.0);
    }

    /// The band prefilter rounds `a - r`, so it rejects pairs the distance
    /// check alone would accept; the fast path must reproduce that.
    #[test]
    fn template_match_fast_band_rounding_asymmetry() {
        let x: Vec<f64> = (0..120)
            .map(|i| [1.0, -1e-17, -1e-17, 1.0, 0.5][i % 5])
            .collect();
        let abs_only = |x: &[f64], m: usize, r: f64| -> Vec<usize> {
            let n_m = x.len() - m + 1;
            (0..n_m)
                .map(|i| {
                    (0..n_m)
                        .filter(|&j| (0..m).all(|d| (x[i + d] - x[j + d]).abs() <= r))
                        .count()
                })
                .collect()
        };
        assert_ne!(
            template_match_counts_band(&x, 2, 1.0),
            abs_only(&x, 2, 1.0),
            "fixture must exercise the band/distance difference"
        );
        assert_same("asymmetry", &x, 1.0);

        let big: Vec<f64> = (0..120).map(|i| 1e16 + (i % 7) as f64).collect();
        assert_same("1e16 + small ints", &big, 1.0);
        assert_same("1e16 + small ints", &big, 2.0);
    }

    #[test]
    fn template_match_fast_near_boundary_rounding() {
        let mut rng = Lcg(99);
        let (c, r) = (0.1, 0.3);
        let x: Vec<f64> = (0..800)
            .map(|_| {
                let v = c + (rng.next_int(11) - 5.0) * r;
                match rng.next_int(3) as u8 {
                    0 => f64::from_bits(v.to_bits() + 1),
                    1 => f64::from_bits(v.to_bits() - 1),
                    _ => v,
                }
            })
            .collect();
        assert_same("near boundary", &x, r);
    }

    /// The minimum value is frequent, so many corners fall below rank 0.
    #[test]
    fn template_match_fast_low_corners_skipped() {
        let mut rng = Lcg(5);
        let x: Vec<f64> = (0..700)
            .map(|_| {
                if rng.next_f64() < 0.4 {
                    0.0
                } else {
                    rng.next_f64() * 3.0
                }
            })
            .collect();
        assert_same("frequent minimum", &x, 0.5);
    }

    #[test]
    fn template_match_fast_m3_pass_chunking() {
        let mut rng = Lcg(11);
        let x: Vec<f64> = (0..2000).map(|_| rng.next_f64()).collect();
        let r = 0.2 * sd(&x, 1.0);
        let band = template_match_counts_band(&x, 3, r);
        let n_m = x.len() - 2;
        for cap in [n_m * 2, n_m * 3, 0] {
            assert_eq!(count_m3_with_pass_cap(&x, r, cap), band, "cap {cap}");
        }
    }

    #[test]
    fn template_match_fast_dispatcher() {
        let mut rng = Lcg(3);
        let x: Vec<f64> = (0..10_000).map(|_| rng.next_f64()).collect();
        let r = 0.2 * sd(&x, 1.0);
        for m in [2, 3] {
            assert_eq!(
                crate::stats::template_match_counts(&x, m, r),
                template_match_counts_band(&x, m, r),
                "m = {m}"
            );
        }
    }
}
