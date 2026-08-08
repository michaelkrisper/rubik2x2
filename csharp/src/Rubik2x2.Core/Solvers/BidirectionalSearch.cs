using System.Runtime.CompilerServices;
using System.Runtime.Intrinsics.X86;
namespace Rubik2x2.Solvers;

/// <summary>
/// Bidirectional breadth-first search: the solver that needs no precomputation.
/// </summary>
/// <remarks>
/// <para>
/// Two frontiers grow a level at a time — one from the scramble, one from the goal —
/// and the search stops where they touch. Level sizes peak at 1,350,852 states at
/// depth 11, so a one-sided search would walk most of the space; meeting in the
/// middle needs depth 7 from each side, where only 33,058 states live.
/// </para>
/// <para>
/// Expanding by whole levels is what makes the answer optimal: when the frontiers
/// meet, every shorter pairing has already been tried, so the first meeting point is
/// a shortest solution — no heuristic and no tie-breaking required.
/// </para>
/// <para>
/// The state index is dense — every value below <see cref="Coordinates.StateCount"/>
/// is a real cube — so the visited sets are one flat array indexed by state, not a
/// dictionary: no hashing, no collision probing, no per-node allocation. Each entry
/// packs the arriving move, the parent state and which frontier claimed it, so a
/// solution is read back by walking parent pointers from the meeting point. A
/// generation stamp in the top bits makes the array reusable across solves without
/// clearing 14 MB each time; it is cleared only when the six-bit stamp wraps.
/// </para>
/// </remarks>
public static class BidirectionalSearch
{
    // marks[state] layout: bits 0-2 arriving move (7 = frontier root),
    // bits 3-24 parent state, bit 25 frontier side, bits 26-31 generation stamp.
    private const int MoveBits = 3;
    private const uint MoveMask = (1u << MoveBits) - 1;
    private const int RootMove = 7;
    private const int SideShift = 25;
    private const int GenerationShift = 26;
    private const uint GenerationLimit = (1u << (32 - GenerationShift)) - 1;

    private static readonly Lock Gate = new();
    private static uint[]? _marks;
    private static uint _generation;
    private static int[] _forwardLevel = new int[1 << 15];
    private static int[] _backwardLevel = new int[1 << 15];
    private static int[] _following = new int[1 << 15];

    /// <summary>Bytes the reusable search scratch retains between solves.</summary>
    public static long RetainedBytes =>
        ((long)(_marks?.Length ?? 0) * sizeof(uint))
        + ((long)(_forwardLevel.Length + _backwardLevel.Length + _following.Length) * sizeof(int));

    /// <summary>Finds a shortest solution.</summary>
    /// <param name="state">The cube to solve.</param>
    /// <param name="maxDepth">Give up beyond this many moves; 14 suffices for every cube.</param>
    /// <returns>A shortest move sequence; empty when the cube is already solved.</returns>
    /// <exception cref="UnsolvableCubeException">
    /// The stickers form no reachable cube, or no solution exists within <paramref name="maxDepth"/>.
    /// </exception>
    public static string Solve(CubeState state, int maxDepth = DistanceTable.MaxDistance)
    {
        int start = Coordinates.Encode(state);
        if (start == Coordinates.SolvedIndex)
        {
            return string.Empty;
        }

        // The scratch arrays are shared across calls; solves are serialized so that
        // concurrent callers (the test runner, for one) cannot tear each other's marks.
        lock (Gate)
        {
            return SolveLocked(start, maxDepth);
        }
    }

    private static string SolveLocked(int start, int maxDepth)
    {
        CoordinateMoveTables tables = CoordinateMoveTables.Shared;
        int[] p0 = tables.WidePermutation(0), p1 = tables.WidePermutation(1), p2 = tables.WidePermutation(2);
        int[] p3 = tables.WidePermutation(3), p4 = tables.WidePermutation(4), p5 = tables.WidePermutation(5);
        int[] o0 = tables.Orientation(0), o1 = tables.Orientation(1), o2 = tables.Orientation(2);
        int[] o3 = tables.Orientation(3), o4 = tables.Orientation(4), o5 = tables.Orientation(5);

        uint[] marks = _marks ??= new uint[Coordinates.StateCount];
        if (++_generation > GenerationLimit)
        {
            Array.Clear(marks);
            _generation = 1;
        }

        uint generation = _generation << GenerationShift;
        uint forwardStamp = generation;
        uint backwardStamp = generation | (1u << SideShift);

        marks[start] = forwardStamp | RootMove;
        marks[Coordinates.SolvedIndex] = backwardStamp | RootMove;
        _forwardLevel[0] = start;
        _backwardLevel[0] = Coordinates.SolvedIndex;
        int forwardCount = 1;
        int backwardCount = 1;

        for (int depth = 0; depth < maxDepth; depth++)
        {
            // Always grow the cheaper side, which keeps both halves shallow.
            bool fromGoal = backwardCount < forwardCount;
            int[] level = fromGoal ? _backwardLevel : _forwardLevel;
            int count = fromGoal ? backwardCount : forwardCount;
            uint own = fromGoal ? backwardStamp : forwardStamp;
            int[] following = _following;
            int found = 0;

            for (int i = 0; i < count; i++)
            {
                // Name the mark words this loop wants a few entries from now.
                // Most of the search sits in one instruction -- the mark load, a
                // random word of a 14.7 MB array against a few MB of cache -- so
                // the loop waits on memory, not on arithmetic. Recomputing an
                // address early lets those misses overlap rather than stall in
                // turn. A prefetch is only a hint, so being wrong costs nothing.
                if (Sse.IsSupported && i + PrefetchLookahead < count)
                {
                    int ahead = level[i + PrefetchLookahead];
                    int aheadPermutation =
                        Math.DivRem(ahead, Coordinates.OrientationCount, out int aheadOrientation);
                    PrefetchMark(marks, p0[aheadPermutation] + o0[aheadOrientation]);
                    PrefetchMark(marks, p1[aheadPermutation] + o1[aheadOrientation]);
                    PrefetchMark(marks, p2[aheadPermutation] + o2[aheadOrientation]);
                    PrefetchMark(marks, p3[aheadPermutation] + o3[aheadOrientation]);
                    PrefetchMark(marks, p4[aheadPermutation] + o4[aheadOrientation]);
                    PrefetchMark(marks, p5[aheadPermutation] + o5[aheadOrientation]);
                }

                int node = level[i];
                int permutation = Math.DivRem(node, Coordinates.OrientationCount, out int orientation);
                if (found + Moves.SolveCount > following.Length)
                {
                    Array.Resize(ref following, following.Length * 2);
                }

                uint parent = own | ((uint)node << MoveBits);

                // Six children per node, unrolled: the loop body is three array reads
                // and a compare, so the per-move indirection would be a real share.
                int child = p0[permutation] + o0[orientation];
                uint seen = marks[child];
                if (seen >> GenerationShift != generation >> GenerationShift)
                {
                    marks[child] = parent | 0;
                    following[found++] = child;
                }
                else if ((seen ^ own) >> SideShift == 1)
                {
                    return BuildSolution(marks, child, node, 0, fromGoal);
                }

                child = p1[permutation] + o1[orientation];
                seen = marks[child];
                if (seen >> GenerationShift != generation >> GenerationShift)
                {
                    marks[child] = parent | 1;
                    following[found++] = child;
                }
                else if ((seen ^ own) >> SideShift == 1)
                {
                    return BuildSolution(marks, child, node, 1, fromGoal);
                }

                child = p2[permutation] + o2[orientation];
                seen = marks[child];
                if (seen >> GenerationShift != generation >> GenerationShift)
                {
                    marks[child] = parent | 2;
                    following[found++] = child;
                }
                else if ((seen ^ own) >> SideShift == 1)
                {
                    return BuildSolution(marks, child, node, 2, fromGoal);
                }

                child = p3[permutation] + o3[orientation];
                seen = marks[child];
                if (seen >> GenerationShift != generation >> GenerationShift)
                {
                    marks[child] = parent | 3;
                    following[found++] = child;
                }
                else if ((seen ^ own) >> SideShift == 1)
                {
                    return BuildSolution(marks, child, node, 3, fromGoal);
                }

                child = p4[permutation] + o4[orientation];
                seen = marks[child];
                if (seen >> GenerationShift != generation >> GenerationShift)
                {
                    marks[child] = parent | 4;
                    following[found++] = child;
                }
                else if ((seen ^ own) >> SideShift == 1)
                {
                    return BuildSolution(marks, child, node, 4, fromGoal);
                }

                child = p5[permutation] + o5[orientation];
                seen = marks[child];
                if (seen >> GenerationShift != generation >> GenerationShift)
                {
                    marks[child] = parent | 5;
                    following[found++] = child;
                }
                else if ((seen ^ own) >> SideShift == 1)
                {
                    return BuildSolution(marks, child, node, 5, fromGoal);
                }
            }

            if (fromGoal)
            {
                _following = _backwardLevel;
                _backwardLevel = following;
                backwardCount = found;
            }
            else
            {
                _following = _forwardLevel;
                _forwardLevel = following;
                forwardCount = found;
            }

            if (found == 0)
            {
                break;
            }
        }

        throw new UnsolvableCubeException($"no solution within {maxDepth} moves");
    }

    /// <summary>
    /// Splices the two half-paths at the meeting state: the expanding side's chain via
    /// <paramref name="parent"/> and <paramref name="move"/>, the other side's chain as
    /// recorded in <paramref name="marks"/>. Moves toward the goal read straight off
    /// the forward chain; moves recorded from the goal outward are replayed reversed
    /// with every turn flipped, which for adjacent inverse pairs is <c>move ^ 1</c>.
    /// </summary>
    private static string BuildSolution(uint[] marks, int meeting, int parent, int move, bool fromGoal)
    {
        Span<char> moves = stackalloc char[2 * DistanceTable.MaxDistance];
        int head = DistanceTable.MaxDistance; // Start-side moves grow down from here.
        int tail = DistanceTable.MaxDistance; // Goal-side moves grow up from here.

        // The expanding side's last edge, then its recorded chain back to its root.
        if (fromGoal)
        {
            moves[tail++] = Moves.Solve[move ^ 1];
        }
        else
        {
            moves[--head] = Moves.Solve[move];
        }

        int node = parent;
        uint entry = marks[node];
        while ((entry & MoveMask) != RootMove)
        {
            int step = (int)(entry & MoveMask);
            if (fromGoal)
            {
                moves[tail++] = Moves.Solve[step ^ 1];
            }
            else
            {
                moves[--head] = Moves.Solve[step];
            }

            node = (int)((entry >> MoveBits) & (Coordinates.StateCount - 1 | 0x3FFFFF));
            node = (int)((entry >> MoveBits) & 0x3FFFFF);
            entry = marks[node];
        }

        // The other side's chain from the meeting state back to its root.
        node = meeting;
        entry = marks[node];
        while ((entry & MoveMask) != RootMove)
        {
            int step = (int)(entry & MoveMask);
            if (fromGoal)
            {
                moves[--head] = Moves.Solve[step];
            }
            else
            {
                moves[tail++] = Moves.Solve[step ^ 1];
            }

            node = (int)((entry >> MoveBits) & 0x3FFFFF);
            entry = marks[node];
        }

        return new string(moves[head..tail]);
    }

    /// <summary>Frontier entries of lookahead for the mark prefetch. Measured.</summary>
    private const int PrefetchLookahead = 6;

    /// <summary>Warms the cache line holding one mark word.</summary>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    private static unsafe void PrefetchMark(uint[] marks, int index)
    {
        fixed (uint* at = &marks[index])
        {
            Sse.Prefetch0(at);
        }
    }
}
