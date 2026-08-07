namespace Rubik2x2;

/// <summary>
/// The twelve quarter turns of a pocket cube, as facelet permutations.
/// </summary>
/// <remarks>
/// Lowercase and uppercase are inverse quarter turns of one face:
/// <c>l/L</c> left column down/up, <c>r/R</c> right column down/up,
/// <c>t/T</c> top layer left/right, <c>b/B</c> bottom layer left/right,
/// <c>f/F</c> front face clockwise/counter-clockwise, <c>a/A</c> back face
/// clockwise/counter-clockwise. The tables are not trusted but checked: the test
/// suite pins the algebra they must obey, and building the distance table
/// reproduces OEIS A079762, which no wrong table could do.
/// </remarks>
public static class Moves
{
    /// <summary>Every move letter, inverses paired at adjacent indices.</summary>
    public const string All = "lLrRtTbBfFaA";

    /// <summary>
    /// The quarter turns the solvers use, inverses at adjacent indices.
    /// </summary>
    /// <remarks>
    /// A fourth face would be redundant: turning a face equals turning the opposite
    /// face plus a whole-cube rotation. These six never touch the down-back-right
    /// corner (<see cref="FixedCorner"/>), which is what pins the cube's orientation.
    /// The pairing lets IDA* prune the previous move's inverse with <c>index ^ 1</c>.
    /// </remarks>
    public const string Solve = "lLtTfF";

    /// <summary>How many moves <see cref="Solve"/> offers.</summary>
    public const int SolveCount = 6;

    /// <summary>Facelets of the corner the <see cref="Solve"/> moves leave in place.</summary>
    public static ReadOnlySpan<byte> FixedCorner => [11, 13, 23];

    /// <summary>
    /// The twelve permutations, concatenated in the order of <see cref="All"/>.
    /// </summary>
    private static ReadOnlySpan<byte> Permutations =>
    [
        12, 1, 14, 3, 0, 5, 2, 7, 4, 9, 6, 11, 8, 13, 10, 15, 18, 16, 19, 17, 20, 21, 22, 23, // l
        4, 1, 6, 3, 8, 5, 10, 7, 12, 9, 14, 11, 0, 13, 2, 15, 17, 19, 16, 18, 20, 21, 22, 23, // L
        0, 13, 2, 15, 4, 1, 6, 3, 8, 5, 10, 7, 12, 9, 14, 11, 16, 17, 18, 19, 21, 23, 20, 22, // r
        0, 5, 2, 7, 4, 9, 6, 11, 8, 13, 10, 15, 12, 1, 14, 3, 16, 17, 18, 19, 22, 20, 23, 21, // R
        2, 0, 3, 1, 20, 21, 6, 7, 8, 9, 10, 11, 12, 13, 17, 16, 4, 5, 18, 19, 15, 14, 22, 23, // t
        1, 3, 0, 2, 16, 17, 6, 7, 8, 9, 10, 11, 12, 13, 21, 20, 15, 14, 18, 19, 4, 5, 22, 23, // T
        0, 1, 2, 3, 4, 5, 22, 23, 9, 11, 8, 10, 19, 18, 14, 15, 16, 17, 6, 7, 20, 21, 13, 12, // b
        0, 1, 2, 3, 4, 5, 18, 19, 10, 8, 11, 9, 23, 22, 14, 15, 16, 17, 13, 12, 20, 21, 6, 7, // B
        0, 1, 19, 17, 6, 4, 7, 5, 22, 20, 10, 11, 12, 13, 14, 15, 16, 8, 18, 9, 2, 21, 3, 23, // f
        0, 1, 20, 22, 5, 7, 4, 6, 17, 19, 10, 11, 12, 13, 14, 15, 16, 3, 18, 2, 9, 21, 8, 23, // F
        18, 16, 2, 3, 4, 5, 6, 7, 8, 9, 23, 21, 13, 15, 12, 14, 10, 17, 11, 19, 20, 0, 22, 1, // a
        21, 23, 2, 3, 4, 5, 6, 7, 8, 9, 16, 18, 14, 12, 15, 13, 1, 17, 0, 19, 20, 11, 22, 10, // A
    ];

    /// <summary>The facelet permutation of one move.</summary>
    /// <param name="move">A move letter from <see cref="All"/>.</param>
    /// <exception cref="ArgumentOutOfRangeException">The letter is not a move.</exception>
    public static ReadOnlySpan<byte> Permutation(char move)
    {
        int index = All.IndexOf(move);
        if (index < 0)
        {
            throw new ArgumentOutOfRangeException(nameof(move), move, $"not a move letter (allowed: {All})");
        }

        return Permutations.Slice(index * CubeState.FaceletCount, CubeState.FaceletCount);
    }

    /// <summary>True when every character of <paramref name="text"/> is a move letter.</summary>
    /// <param name="text">The candidate move sequence.</param>
    public static bool IsMoveSequence(ReadOnlySpan<char> text)
    {
        if (text.IsEmpty)
        {
            return false;
        }

        foreach (char move in text)
        {
            if (!All.Contains(move, StringComparison.Ordinal))
            {
                return false;
            }
        }

        return true;
    }

    /// <summary>Undoes a sequence: reverse the order and flip every turn.</summary>
    /// <param name="moves">The sequence to invert.</param>
    public static string Invert(ReadOnlySpan<char> moves)
    {
        int length = moves.Length;
        Span<char> inverted = length <= 64 ? stackalloc char[length] : new char[length];
        for (int i = 0; i < length; i++)
        {
            char move = moves[length - 1 - i];
            inverted[i] = char.IsUpper(move) ? char.ToLowerInvariant(move) : char.ToUpperInvariant(move);
        }

        return new string(inverted);
    }

    /// <summary>
    /// The permutation of "<paramref name="first"/>, then <paramref name="second"/>".
    /// </summary>
    /// <param name="first">The permutation applied first.</param>
    /// <param name="second">The permutation applied second.</param>
    /// <remarks>
    /// Moves read their sources, so composing reads through: applying <c>a</c> then
    /// <c>b</c> gives <c>result[i] = a[b[i]]</c>.
    /// </remarks>
    public static byte[] Compose(ReadOnlySpan<byte> first, ReadOnlySpan<byte> second)
    {
        byte[] composed = new byte[CubeState.FaceletCount];
        for (int i = 0; i < composed.Length; i++)
        {
            composed[i] = first[second[i]];
        }

        return composed;
    }
}
