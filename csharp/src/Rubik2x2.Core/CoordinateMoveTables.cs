namespace Rubik2x2;

/// <summary>
/// One pair of lookup tables per solve move, turning a move into two array reads.
/// </summary>
/// <remarks>
/// <para>
/// A move rearranges the slots and adds a fixed twist at each, so it acts on the
/// permutation coordinate and the orientation coordinate independently:
/// <code>
/// next.permutation[slot] = current.permutation[source[slot]]
/// next.orientation[slot] = current.orientation[source[slot]] + twist[slot]  (mod 3)
/// </code>
/// Neither half depends on the other, so each move needs only a 5040-entry table
/// mapping permutation rank to permutation rank and a 729-entry table for the
/// twists. Both are derived by applying the move to the identity state, where the
/// resulting permutation <em>is</em> the slot mapping and the orientation
/// <em>is</em> the twist picked up.
/// </para>
/// <para>
/// <see cref="WidePermutation"/> is the same table pre-multiplied by 729, so a
/// search computes a child with a single addition rather than a multiply and an
/// add — worth having in a loop that runs tens of millions of times.
/// </para>
/// </remarks>
public sealed class CoordinateMoveTables
{
    private readonly int[][] _permutation;
    private readonly int[][] _widePermutation;
    private readonly int[][] _orientation;

    private CoordinateMoveTables(int[][] permutation, int[][] widePermutation, int[][] orientation)
    {
        _permutation = permutation;
        _widePermutation = widePermutation;
        _orientation = orientation;
    }

    /// <summary>
    /// The tables, built once on first use — roughly 35k entries, a few milliseconds.
    /// </summary>
    public static CoordinateMoveTables Shared { get; } = Build();

    /// <summary>The move letters these tables describe, in <see cref="Moves.Solve"/> order.</summary>
    public static ReadOnlySpan<char> MoveNames => Moves.Solve;

    /// <summary>Permutation rank to permutation rank, for one move.</summary>
    /// <param name="move">A move index below <see cref="Moves.SolveCount"/>.</param>
    public int[] Permutation(int move) => _permutation[move];

    /// <summary>
    /// <see cref="Permutation(int)"/> pre-multiplied by <see cref="Coordinates.OrientationCount"/>.
    /// </summary>
    /// <param name="move">A move index below <see cref="Moves.SolveCount"/>.</param>
    public int[] WidePermutation(int move) => _widePermutation[move];

    /// <summary>Orientation rank to orientation rank, for one move.</summary>
    /// <param name="move">A move index below <see cref="Moves.SolveCount"/>.</param>
    public int[] Orientation(int move) => _orientation[move];

    /// <summary>Applies one solve move to an encoded state.</summary>
    /// <param name="index">The encoded state.</param>
    /// <param name="move">A move index below <see cref="Moves.SolveCount"/>.</param>
    public int Apply(int index, int move)
    {
        int permutation = Math.DivRem(index, Coordinates.OrientationCount, out int orientation);
        return _widePermutation[move][permutation] + _orientation[move][orientation];
    }

    private static CoordinateMoveTables Build()
    {
        int[][] permutationTables = new int[Moves.SolveCount][];
        int[][] widePermutationTables = new int[Moves.SolveCount][];
        int[][] orientationTables = new int[Moves.SolveCount][];

        Span<byte> identityPermutation = stackalloc byte[Coordinates.FreeSlots];
        Span<byte> noTwists = stackalloc byte[Coordinates.FreeSlots];
        for (byte slot = 0; slot < Coordinates.FreeSlots; slot++)
        {
            identityPermutation[slot] = slot;
        }

        Coordinates.GoalOrientation goal = Coordinates.Goal(CubeState.Solved);
        CubeState identity = Coordinates.FromCubies(identityPermutation, noTwists, goal);

        Span<byte> source = stackalloc byte[Coordinates.FreeSlots];
        Span<byte> twist = stackalloc byte[Coordinates.FreeSlots];
        Span<byte> current = stackalloc byte[Coordinates.FreeSlots];
        Span<byte> next = stackalloc byte[Coordinates.FreeSlots];

        for (int move = 0; move < Moves.SolveCount; move++)
        {
            Coordinates.ToCubies(identity.Apply(Moves.Solve[move]), goal, source, twist);

            int[] permutations = new int[Coordinates.PermutationCount];
            int[] wide = new int[Coordinates.PermutationCount];
            for (int rank = 0; rank < permutations.Length; rank++)
            {
                Coordinates.UnrankPermutation(rank, current);
                for (int slot = 0; slot < Coordinates.FreeSlots; slot++)
                {
                    next[slot] = current[source[slot]];
                }

                permutations[rank] = Coordinates.RankPermutation(next);
                wide[rank] = permutations[rank] * Coordinates.OrientationCount;
            }

            int[] orientations = new int[Coordinates.OrientationCount];
            for (int rank = 0; rank < orientations.Length; rank++)
            {
                Coordinates.UnrankOrientation(rank, current);
                for (int slot = 0; slot < Coordinates.FreeSlots; slot++)
                {
                    next[slot] = (byte)((current[source[slot]] + twist[slot]) % 3);
                }

                orientations[rank] = Coordinates.RankOrientation(next);
            }

            permutationTables[move] = permutations;
            widePermutationTables[move] = wide;
            orientationTables[move] = orientations;
        }

        return new CoordinateMoveTables(permutationTables, widePermutationTables, orientationTables);
    }
}
