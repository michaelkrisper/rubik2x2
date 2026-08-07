namespace Rubik2x2.Solvers;

/// <summary>
/// Korf's iterative-deepening A* with pattern databases — the textbook optimal cube
/// solver.
/// </summary>
/// <remarks>
/// <para>
/// Depth-first passes with a rising limit on <c>g + h</c>, where <c>h</c> is the
/// admissible bound of <see cref="PatternDatabases"/>. Memory is a stack of at most
/// 14 frames; nothing else is retained.
/// </para>
/// <para>
/// It is here for comparison, not because it wins. On a 2x2x2 it loses to plain
/// bidirectional search and measurably so: the bound reaches only distance 8 of 14,
/// too loose to prune a puzzle this small, so the last iteration walks a tree of
/// millions of nodes. Pattern databases pay off when a puzzle has more pieces than
/// you can afford to track — a pocket cube has not.
/// </para>
/// </remarks>
public static class IdaStarSolver
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
        int index = Coordinates.Encode(state);
        if (index == Coordinates.SolvedIndex)
        {
            return string.Empty;
        }

        PatternDatabases databases = PatternDatabases.Shared;
        Search search = new()
        {
            Tables = CoordinateMoveTables.Shared,
            PermutationBound = databases.PermutationDistances,
            OrientationBound = databases.OrientationDistances,
            Path = new int[maxDepth],
        };

        for (int limit = databases.Bound(index); limit <= maxDepth; limit++)
        {
            if (search.Descend(index, 0, limit, -1))
            {
                return search.ToMoves();
            }
        }

        throw new UnsolvableCubeException($"no solution within {maxDepth} moves");
    }

    /// <summary>One depth-first pass, carrying everything the recursion touches.</summary>
    private sealed class Search
    {
        public required CoordinateMoveTables Tables { get; init; }

        public required byte[] PermutationBound { get; init; }

        public required byte[] OrientationBound { get; init; }

        public required int[] Path { get; init; }

        public int Length { get; private set; }

        public bool Descend(int index, int depth, int limit, int previous)
        {
            int permutation = Math.DivRem(index, Coordinates.OrientationCount, out int orientation);
            int bound = Math.Max(PermutationBound[permutation], OrientationBound[orientation]) - 1;
            if (depth + bound > limit)
            {
                return false;
            }

            if (index == Coordinates.SolvedIndex)
            {
                Length = depth;
                return true;
            }

            for (int move = 0; move < Moves.SolveCount; move++)
            {
                // Moves.Solve pairs every turn with its inverse at adjacent indices,
                // so `move ^ 1` is exactly the turn that would undo the previous one.
                if ((move ^ 1) == previous)
                {
                    continue;
                }

                Path[depth] = move;
                int child = Tables.WidePermutation(move)[permutation] + Tables.Orientation(move)[orientation];
                if (Descend(child, depth + 1, limit, move))
                {
                    return true;
                }
            }

            return false;
        }

        public string ToMoves()
        {
            Span<char> moves = stackalloc char[Length];
            for (int i = 0; i < moves.Length; i++)
            {
                moves[i] = Moves.Solve[Path[i]];
            }

            return new string(moves);
        }
    }
}
