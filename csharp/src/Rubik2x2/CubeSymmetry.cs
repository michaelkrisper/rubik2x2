using System.Collections.Immutable;

namespace Rubik2x2;

/// <summary>
/// The 24 ways of holding a cube, as facelet permutations.
/// </summary>
/// <remarks>
/// A whole-cube rotation is just two opposite faces turned the same way, so the
/// group is generated from the move tables rather than tabulated separately. It
/// matters because a scramble may be handed over in any orientation: the fixed
/// corner's colours pick out which of these 24 rotations of the solved cube the
/// scramble is aiming at (see <see cref="Coordinates"/>).
/// </remarks>
public static class CubeSymmetry
{
    /// <summary>The 24 whole-cube rotations, the identity first.</summary>
    public static ImmutableArray<byte[]> Rotations { get; } = BuildRotations();

    /// <summary>Re-reads a state from a different vantage point.</summary>
    /// <param name="state">The state to turn.</param>
    /// <param name="rotation">One of <see cref="Rotations"/>.</param>
    public static CubeState Rotate(CubeState state, ReadOnlySpan<byte> rotation) => state.Apply(rotation);

    /// <summary>All 24 whole-cube rotations of <paramref name="state"/>.</summary>
    /// <param name="state">The state to turn every which way.</param>
    public static IEnumerable<CubeState> Orientations(CubeState state)
    {
        foreach (byte[] rotation in Rotations)
        {
            yield return state.Apply(rotation);
        }
    }

    private static ImmutableArray<byte[]> BuildRotations()
    {
        // Turning a face and its opposite face the same way leaves no layer behind:
        // the whole cube turns. Three axes generate all 24 orientations.
        byte[][] generators =
        [
            Moves.Compose(Moves.Permutation('l'), Moves.Permutation('r')),
            Moves.Compose(Moves.Permutation('t'), Moves.Permutation('b')),
            Moves.Compose(Moves.Permutation('f'), Moves.Permutation('a')),
        ];

        byte[] identity = new byte[CubeState.FaceletCount];
        for (byte i = 0; i < identity.Length; i++)
        {
            identity[i] = i;
        }

        Dictionary<string, byte[]> seen = new(24) { [Key(identity)] = identity };
        Queue<byte[]> frontier = new();
        frontier.Enqueue(identity);
        List<byte[]> ordered = [identity];

        while (frontier.TryDequeue(out byte[]? current))
        {
            foreach (byte[] generator in generators)
            {
                byte[] composed = Moves.Compose(current, generator);
                if (seen.TryAdd(Key(composed), composed))
                {
                    ordered.Add(composed);
                    frontier.Enqueue(composed);
                }
            }
        }

        return [.. ordered];

        static string Key(byte[] permutation) => Convert.ToHexString(permutation);
    }
}
