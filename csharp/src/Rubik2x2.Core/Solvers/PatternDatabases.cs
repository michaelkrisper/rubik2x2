namespace Rubik2x2.Solvers;

/// <summary>
/// Exact distances for the two coordinate projections of the cube — the heuristic
/// behind <see cref="IdaStarSolver"/>.
/// </summary>
/// <remarks>
/// <para>
/// A pattern database holds exact distances for a relaxed version of the puzzle,
/// which makes them a lower bound for the real one. Here the relaxations are free:
/// a move acts on the permutation coordinate and the orientation coordinate
/// independently, so each coordinate on its own is a genuine quotient of the state
/// graph — solving the cube necessarily solves both projections. Two breadth-first
/// searches over 5040 and 729 nodes, a fraction of a millisecond, and the larger of
/// the two distances is an admissible bound.
/// </para>
/// <para>
/// It is a weak one. Both databases top out at distance 8 while real positions reach
/// 14, because a pocket cube is nothing but the pieces the databases already cover —
/// there is no third of the puzzle left to abstract away.
/// </para>
/// </remarks>
public sealed class PatternDatabases
{
    private readonly byte[] _permutation;
    private readonly byte[] _orientation;

    private PatternDatabases(byte[] permutation, byte[] orientation)
    {
        _permutation = permutation;
        _orientation = orientation;
    }

    /// <summary>The databases, built once on first use.</summary>
    public static PatternDatabases Shared { get; } = Build();

    /// <summary>Bytes both databases occupy together.</summary>
    public int ByteCount => _permutation.Length + _orientation.Length;

    /// <summary>The largest bound each database can ever return.</summary>
    public (int Permutation, int Orientation) Reach =>
        (_permutation.Max() - 1, _orientation.Max() - 1);

    /// <summary>Searches both projections. Every call produces the same tables.</summary>
    public static PatternDatabases Build()
    {
        CoordinateMoveTables tables = CoordinateMoveTables.Shared;
        int[][] permutationMoves = new int[Moves.SolveCount][];
        int[][] orientationMoves = new int[Moves.SolveCount][];
        for (int move = 0; move < Moves.SolveCount; move++)
        {
            permutationMoves[move] = tables.Permutation(move);
            orientationMoves[move] = tables.Orientation(move);
        }

        return new PatternDatabases(
            Project(permutationMoves, Coordinates.PermutationCount),
            Project(orientationMoves, Coordinates.OrientationCount));
    }

    /// <summary>An admissible lower bound on the moves left from an encoded state.</summary>
    /// <param name="index">The encoded state.</param>
    public int Bound(int index)
    {
        int permutation = Math.DivRem(index, Coordinates.OrientationCount, out int orientation);
        return Math.Max(_permutation[permutation], _orientation[orientation]) - 1;
    }

    internal byte[] PermutationDistances => _permutation;

    internal byte[] OrientationDistances => _orientation;

    /// <summary>Exact distance-to-solved within one coordinate, stored as distance + 1.</summary>
    private static byte[] Project(int[][] moves, int size)
    {
        byte[] distances = new byte[size];
        distances[0] = 1;
        List<int> level = [0];
        int depth = 0;

        while (level.Count > 0)
        {
            depth++;
            List<int> following = [];
            foreach (int node in level)
            {
                foreach (int[] table in moves)
                {
                    int child = table[node];
                    if (distances[child] == 0)
                    {
                        distances[child] = (byte)(depth + 1);
                        following.Add(child);
                    }
                }
            }

            level = following;
        }

        return distances;
    }
}
