#include "rubik/bfs.hpp"

#include <bit>
#include <cstdint>
#include <format>
#include <utility>
#include <vector>

#include "rubik/coords.hpp"

namespace rubik {
namespace {

/// A path of at most 14 solve moves packed into one word: three bits per move
/// from the bottom up, the length in the top six. Keeping paths in a register
/// instead of a `std::string` is what lets the frontier maps stay flat and
/// allocation-free per entry.
class PackedPath {
  public:
    constexpr PackedPath() = default;

    [[nodiscard]] constexpr PackedPath push(std::size_t move) const {
        const std::size_t length = size();
        return PackedPath{(raw_ & mask) | (static_cast<std::uint64_t>(move) << (3 * length)) |
                          (static_cast<std::uint64_t>(length + 1) << length_shift)};
    }

    [[nodiscard]] constexpr std::size_t size() const {
        return static_cast<std::size_t>(raw_ >> length_shift);
    }

    [[nodiscard]] constexpr std::size_t operator[](std::size_t index) const {
        return static_cast<std::size_t>((raw_ >> (3 * index)) & 0b111);
    }

    /// The moves in order.
    [[nodiscard]] Moves forward() const {
        Moves moves;
        moves.reserve(size());
        for (std::size_t i = 0; i < size(); ++i) {
            moves.push_back(to_char(solve_moves[(*this)[i]]));
        }
        return moves;
    }

    /// The moves undone: reversed, with every turn flipped. A path *to* the
    /// goal read this way is a path *from* it.
    [[nodiscard]] Moves inverted() const {
        Moves moves;
        moves.reserve(size());
        for (std::size_t i = size(); i-- > 0;) {
            // Inverses sit at adjacent indices in `solve_moves`.
            moves.push_back(to_char(solve_moves[(*this)[i] ^ 1u]));
        }
        return moves;
    }

  private:
    static constexpr std::size_t length_shift = 58;
    static constexpr std::uint64_t mask = (std::uint64_t{1} << length_shift) - 1;

    explicit constexpr PackedPath(std::uint64_t raw) : raw_(raw) {}

    std::uint64_t raw_ = 0;
};

/// Open-addressed map from state index to the path that reached it.
///
/// A `std::unordered_map` would allocate a node per state, and this inserts
/// tens of thousands of them per solve; linear probing over one flat vector
/// keeps the whole frontier in a handful of cache-friendly pages.
class IndexMap {
  public:
    explicit IndexMap(std::size_t capacity) : slots_(capacity, Slot{}) {
        shift_ = 64 - static_cast<unsigned>(std::countr_zero(capacity));
        limit_ = capacity / 2;
    }

    [[nodiscard]] const PackedPath* find(std::uint32_t key) const {
        for (std::size_t i = bucket(key);; i = (i + 1) & (slots_.size() - 1)) {
            if (slots_[i].key == key) {
                return &slots_[i].path;
            }
            if (slots_[i].key == empty) {
                return nullptr;
            }
        }
    }

    /// Insert unless present. Returns false when the key was already there.
    bool insert(std::uint32_t key, PackedPath path) {
        for (std::size_t i = bucket(key);; i = (i + 1) & (slots_.size() - 1)) {
            if (slots_[i].key == key) {
                return false;
            }
            if (slots_[i].key == empty) {
                slots_[i] = Slot{key, path};
                if (++size_ > limit_) {
                    grow();
                }
                return true;
            }
        }
    }

    [[nodiscard]] std::size_t size() const { return size_; }

  private:
    static constexpr std::uint32_t empty = 0xFFFF'FFFFu;  // no state index reaches this
    struct Slot {
        std::uint32_t key = empty;
        PackedPath path{};
    };

    [[nodiscard]] std::size_t bucket(std::uint32_t key) const {
        // Fibonacci hashing: state indices are dense and clustered, so the low
        // bits alone would collide badly.
        return static_cast<std::size_t>((key * 0x9E37'79B9'7F4A'7C15ull) >> shift_);
    }

    void grow() {
        IndexMap larger(slots_.size() * 2);
        for (const Slot& slot : slots_) {
            if (slot.key != empty) {
                larger.insert(slot.key, slot.path);
            }
        }
        *this = std::move(larger);
    }

    std::vector<Slot> slots_;
    std::size_t size_ = 0;
    std::size_t limit_ = 0;
    unsigned shift_ = 0;
};

/// One half of the search: everything it has seen, and the level it is about
/// to expand.
struct Frontier {
    IndexMap seen;
    std::vector<std::uint32_t> level;
};

}  // namespace

Result<Moves> solve_bfs(const State& state) {
    const Result<std::uint32_t> start = coords::encode(state);
    if (!start) {
        return std::unexpected(start.error());
    }
    if (*start == coords::solved_index) {
        return Moves{};
    }

    const coords::MoveTables& tables = coords::move_tables();
    constexpr std::size_t initial_capacity = 1u << 12;

    Frontier forward{IndexMap{initial_capacity}, {*start}};
    Frontier backward{IndexMap{initial_capacity}, {coords::solved_index}};
    forward.seen.insert(*start, PackedPath{});
    backward.seen.insert(coords::solved_index, PackedPath{});

    for (int depth = 0; depth < max_depth; ++depth) {
        // Always grow the cheaper side, which keeps both halves shallow.
        const bool from_goal = backward.level.size() < forward.level.size();
        Frontier& growing = from_goal ? backward : forward;
        const Frontier& facing = from_goal ? forward : backward;

        std::vector<std::uint32_t> following;
        following.reserve(growing.level.size() * 4);

        for (std::uint32_t node : growing.level) {
            const PackedPath path = *growing.seen.find(node);
            const std::uint32_t quotient = node / coords::orientation_count;
            const std::uint32_t remainder = node % coords::orientation_count;

            for (std::size_t move = 0; move < tables.size(); ++move) {
                const std::uint32_t child =
                    tables[move].permutation_wide[quotient] + tables[move].orientation[remainder];
                const PackedPath extended = path.push(move);
                if (const PackedPath* met = facing.seen.find(child)) {
                    const Moves from_start = from_goal ? met->forward() : extended.forward();
                    const Moves to_goal = from_goal ? extended.inverted() : met->inverted();
                    return from_start + to_goal;
                }
                if (growing.seen.insert(child, extended)) {
                    following.push_back(child);
                }
            }
        }

        growing.level = std::move(following);
        if (forward.level.empty() || backward.level.empty()) {
            break;
        }
    }

    return fail(ErrorKind::unsolvable, std::format("no solution within {} moves", max_depth));
}

}  // namespace rubik
