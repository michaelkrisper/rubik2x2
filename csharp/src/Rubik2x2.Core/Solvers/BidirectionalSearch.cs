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
/// </remarks>
public static class BidirectionalSearch
{
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

        CoordinateMoveTables tables = CoordinateMoveTables.Shared;
        int[][] permutations = new int[Moves.SolveCount][];
        int[][] orientations = new int[Moves.SolveCount][];
        for (int move = 0; move < Moves.SolveCount; move++)
        {
            permutations[move] = tables.WidePermutation(move);
            orientations[move] = tables.Orientation(move);
        }

        Dictionary<int, ulong> forward = new(1 << 16) { [start] = PackedPath.Empty };
        Dictionary<int, ulong> backward = new(1 << 16) { [Coordinates.SolvedIndex] = PackedPath.Empty };
        // The frontier carries each node's path with it, so expanding a level never
        // looks a node up again.
        List<(int Node, ulong Path)> forwardLevel = [(start, PackedPath.Empty)];
        List<(int Node, ulong Path)> backwardLevel = [(Coordinates.SolvedIndex, PackedPath.Empty)];

        for (int depth = 0; depth < maxDepth; depth++)
        {
            // Always grow the cheaper side, which keeps both halves shallow.
            bool fromGoal = backwardLevel.Count < forwardLevel.Count;
            List<(int Node, ulong Path)> level = fromGoal ? backwardLevel : forwardLevel;
            Dictionary<int, ulong> seen = fromGoal ? backward : forward;
            Dictionary<int, ulong> other = fromGoal ? forward : backward;

            List<(int Node, ulong Path)> following = new(level.Count * 3);
            foreach ((int node, ulong path) in level)
            {
                int permutation = Math.DivRem(node, Coordinates.OrientationCount, out int orientation);
                for (int move = 0; move < Moves.SolveCount; move++)
                {
                    int child = permutations[move][permutation] + orientations[move][orientation];
                    ulong extended = PackedPath.Append(path, move);
                    if (!seen.TryAdd(child, extended))
                    {
                        continue;
                    }

                    if (other.TryGetValue(child, out ulong meeting))
                    {
                        return fromGoal
                            ? PackedPath.Join(meeting, extended)
                            : PackedPath.Join(extended, meeting);
                    }

                    following.Add((child, extended));
                }
            }

            if (fromGoal)
            {
                backwardLevel = following;
            }
            else
            {
                forwardLevel = following;
            }

            if (forwardLevel.Count == 0 || backwardLevel.Count == 0)
            {
                break;
            }
        }

        throw new UnsolvableCubeException($"no solution within {maxDepth} moves");
    }

    /// <summary>
    /// A path of up to 14 moves packed into one <see cref="ulong"/>: three bits per
    /// move plus its length, so a frontier of hundreds of thousands of nodes costs no
    /// string allocations at all.
    /// </summary>
    private static class PackedPath
    {
        private const int LengthShift = 48;

        internal const ulong Empty = 0;

        internal static ulong Append(ulong path, int move)
        {
            int length = Length(path);
            return (path & ((1UL << LengthShift) - 1))
                | ((ulong)(uint)move << (length * 3))
                | ((ulong)(length + 1) << LengthShift);
        }

        internal static int Length(ulong path) => (int)(path >> LengthShift);

        /// <summary>
        /// Splices a path from the start with a path from the goal, inverting the latter:
        /// reversed, with every turn flipped — which for adjacent inverse pairs is
        /// <c>move ^ 1</c>.
        /// </summary>
        internal static string Join(ulong fromStart, ulong toGoal)
        {
            int forward = Length(fromStart);
            int backward = Length(toGoal);
            Span<char> moves = stackalloc char[forward + backward];
            for (int i = 0; i < forward; i++)
            {
                moves[i] = Moves.Solve[(int)((fromStart >> (i * 3)) & 7)];
            }

            for (int i = 0; i < backward; i++)
            {
                moves[forward + i] = Moves.Solve[(int)((toGoal >> ((backward - 1 - i) * 3)) & 7) ^ 1];
            }

            return new string(moves);
        }
    }
}
