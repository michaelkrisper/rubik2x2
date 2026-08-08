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
/// packs the state's distance from its frontier's root and which frontier claimed
/// it; that is enough to read a solution back, because breadth-first marks are
/// exact — from a node at distance d, any neighbour at d−1 on the same side is a
/// parent, and there are only six neighbours to try. Storing a distance instead of
/// a parent pointer is what lets a mark fit in 16 bits, halving the array to 7 MB
/// so more of it stays in cache. A generation stamp in the top bits makes the array
/// reusable across solves without clearing it each time; it is cleared only when
/// the eleven-bit stamp wraps.
/// </para>
/// </remarks>
public static class BidirectionalSearch
{
    // marks[state] layout: bits 0-3 distance from its frontier's root,
    // bit 4 frontier side, bits 5-15 generation stamp.
    private const int DepthBits = 4;
    private const int DepthMask = (1 << DepthBits) - 1;
    private const int SideBit = 1 << DepthBits;
    private const int GenerationShift = DepthBits + 1;
    private const int GenerationMask = ~(SideBit | DepthMask);
    private const uint GenerationLimit = (ushort.MaxValue >> GenerationShift) - 1;

    private static readonly Lock Gate = new();
    private static ushort[]? _marks;
    private static uint _generation;
    private static int[] _forwardLevel = new int[1 << 15];
    private static int[] _backwardLevel = new int[1 << 15];
    private static int[] _following = new int[1 << 15];

    /// <summary>Bytes the reusable search scratch retains between solves.</summary>
    public static long RetainedBytes =>
        ((long)(_marks?.Length ?? 0) * sizeof(ushort))
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

        ushort[] marks = _marks ??= new ushort[Coordinates.StateCount];
        if (++_generation > GenerationLimit)
        {
            Array.Clear(marks);
            _generation = 1;
        }

        int generation = (int)(_generation << GenerationShift);
        marks[start] = (ushort)generation;
        marks[Coordinates.SolvedIndex] = (ushort)(generation | SideBit);
        _forwardLevel[0] = start;
        _backwardLevel[0] = Coordinates.SolvedIndex;
        int forwardCount = 1;
        int backwardCount = 1;

        // The two sides advance independently — each round grows only one of them —
        // so each carries its own level depth, which is what the marks record.
        int forwardDepth = 0;
        int backwardDepth = 0;

        for (int round = 0; round < maxDepth; round++)
        {
            // Always grow the cheaper side, which keeps both halves shallow.
            bool fromGoal = backwardCount < forwardCount;
            int[] level = fromGoal ? _backwardLevel : _forwardLevel;
            int count = fromGoal ? backwardCount : forwardCount;
            int side = fromGoal ? SideBit : 0;
            int levelDepth = fromGoal ? backwardDepth : forwardDepth;
            // Every state reached this round gets the same mark, so the store in the
            // inner loop is a constant.
            ushort reached = (ushort)(generation | side | (levelDepth + 1));
            int[] following = _following;
            int found = 0;

            for (int i = 0; i < count; i++)
            {
                // Name the mark words this loop wants a few entries from now.
                // Most of the search sits in one instruction -- the mark load, a
                // random word of a 7.3 MB array against a few MB of cache -- so
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

                // Six children per node, unrolled: the loop body is three array reads
                // and a compare, so the per-move indirection would be a real share.
                int child = p0[permutation] + o0[orientation];
                int seen = marks[child];
                if ((seen & GenerationMask) != generation)
                {
                    marks[child] = reached;
                    following[found++] = child;
                }
                else if ((seen & SideBit) != side)
                {
                    return BuildSolution(marks, tables, child, node, 0, fromGoal, generation);
                }

                child = p1[permutation] + o1[orientation];
                seen = marks[child];
                if ((seen & GenerationMask) != generation)
                {
                    marks[child] = reached;
                    following[found++] = child;
                }
                else if ((seen & SideBit) != side)
                {
                    return BuildSolution(marks, tables, child, node, 1, fromGoal, generation);
                }

                child = p2[permutation] + o2[orientation];
                seen = marks[child];
                if ((seen & GenerationMask) != generation)
                {
                    marks[child] = reached;
                    following[found++] = child;
                }
                else if ((seen & SideBit) != side)
                {
                    return BuildSolution(marks, tables, child, node, 2, fromGoal, generation);
                }

                child = p3[permutation] + o3[orientation];
                seen = marks[child];
                if ((seen & GenerationMask) != generation)
                {
                    marks[child] = reached;
                    following[found++] = child;
                }
                else if ((seen & SideBit) != side)
                {
                    return BuildSolution(marks, tables, child, node, 3, fromGoal, generation);
                }

                child = p4[permutation] + o4[orientation];
                seen = marks[child];
                if ((seen & GenerationMask) != generation)
                {
                    marks[child] = reached;
                    following[found++] = child;
                }
                else if ((seen & SideBit) != side)
                {
                    return BuildSolution(marks, tables, child, node, 4, fromGoal, generation);
                }

                child = p5[permutation] + o5[orientation];
                seen = marks[child];
                if ((seen & GenerationMask) != generation)
                {
                    marks[child] = reached;
                    following[found++] = child;
                }
                else if ((seen & SideBit) != side)
                {
                    return BuildSolution(marks, tables, child, node, 5, fromGoal, generation);
                }
            }

            if (fromGoal)
            {
                _following = _backwardLevel;
                _backwardLevel = following;
                backwardCount = found;
                backwardDepth++;
            }
            else
            {
                _following = _forwardLevel;
                _forwardLevel = following;
                forwardCount = found;
                forwardDepth++;
            }

            if (found == 0)
            {
                break;
            }
        }

        throw new UnsolvableCubeException($"no solution within {maxDepth} moves");
    }

    /// <summary>
    /// Splices the two half-paths at the meeting state. The marks hold no parent
    /// pointers, only distances, so each chain is rebuilt by walking toward its
    /// root with <see cref="StepToParent"/> — a few dozen lookups once per solve,
    /// against a saved store on every edge of the search. Moves toward the goal
    /// read straight off the forward chain; moves walked from the goal outward are
    /// replayed reversed with every turn flipped, which for adjacent inverse pairs
    /// is <c>move ^ 1</c>.
    /// </summary>
    private static string BuildSolution(
        ushort[] marks, CoordinateMoveTables tables, int meeting, int parent, int move, bool fromGoal, int generation)
    {
        Span<char> moves = stackalloc char[2 * DistanceTable.MaxDistance];
        int head = DistanceTable.MaxDistance; // Start-side moves grow down from here.
        int tail = DistanceTable.MaxDistance; // Goal-side moves grow up from here.

        // The expanding side's last edge, then its chain back to its root.
        if (fromGoal)
        {
            moves[tail++] = Moves.Solve[move ^ 1];
        }
        else
        {
            moves[--head] = Moves.Solve[move];
        }

        int own = generation | (fromGoal ? SideBit : 0);
        int node = parent;
        for (int depth = marks[node] & DepthMask; depth > 0; depth--)
        {
            int step = StepToParent(marks, tables, ref node, own | (depth - 1));
            if (fromGoal)
            {
                moves[tail++] = Moves.Solve[step ^ 1];
            }
            else
            {
                moves[--head] = Moves.Solve[step];
            }
        }

        // The other side's chain from the meeting state back to its root.
        node = meeting;
        for (int depth = marks[node] & DepthMask; depth > 0; depth--)
        {
            int step = StepToParent(marks, tables, ref node, (own ^ SideBit) | (depth - 1));
            if (fromGoal)
            {
                moves[--head] = Moves.Solve[step];
            }
            else
            {
                moves[tail++] = Moves.Solve[step ^ 1];
            }
        }

        return new string(moves[head..tail]);
    }

    /// <summary>
    /// Steps <paramref name="node"/> back to a parent — a neighbour whose mark is
    /// exactly <paramref name="parentMark"/> — and returns the move that led from it
    /// to <paramref name="node"/>. Breadth-first marks are exact, so such a
    /// neighbour always exists and the loop always terminates.
    /// </summary>
    private static int StepToParent(ushort[] marks, CoordinateMoveTables tables, ref int node, int parentMark)
    {
        for (int step = 0; ; step++)
        {
            int back = tables.Apply(node, step ^ 1);
            if (marks[back] == parentMark)
            {
                node = back;
                return step;
            }
        }
    }

    /// <summary>Frontier entries of lookahead for the mark prefetch. Measured.</summary>
    private const int PrefetchLookahead = 6;

    /// <summary>Warms the cache line holding one mark word.</summary>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    private static unsafe void PrefetchMark(ushort[] marks, int index)
    {
        fixed (ushort* at = &marks[index])
        {
            Sse.Prefetch0(at);
        }
    }
}
