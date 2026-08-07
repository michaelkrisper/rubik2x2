using System.Collections.Frozen;

namespace Rubik2x2;

/// <summary>
/// The compact state encoding every solver runs on: one integer per cube.
/// </summary>
/// <remarks>
/// <para>
/// <see cref="CubeState"/> is what a human reads, not what a search wants. Because
/// the six solve moves never touch the down-back-right corner, that corner can be
/// held still, and the rest of the cube is described by far less: which of the 7
/// free corner <em>pieces</em> sits in each of the 7 free <em>slots</em>
/// (7! = 5040 arrangements) and how each is twisted (3^7, minus the constraint that
/// the twists sum to a multiple of 3, so 3^6 = 729). That is
/// 5040 x 729 = 3,674,160 states — exactly the reachable space.
/// </para>
/// <para>
/// The split pays twice. A move acts on the two halves independently, so applying
/// one is two array lookups instead of rebuilding 24 stickers
/// (<see cref="CoordinateMoveTables"/>); and each half on its own is a genuine
/// quotient of the state graph, which hands IDA* its pattern databases for free.
/// </para>
/// </remarks>
public static class Coordinates
{
    /// <summary>Slots whose contents can change; the eighth corner is held still.</summary>
    public const int FreeSlots = 7;

    /// <summary>Distinct twist patterns: 3^6, the seventh twist being implied.</summary>
    public const int OrientationCount = 729;

    /// <summary>Arrangements of the seven free pieces: 7!.</summary>
    public const int PermutationCount = 5040;

    /// <summary>Reachable states: 3,674,160.</summary>
    public const int StateCount = PermutationCount * OrientationCount;

    /// <summary>The index of a solved cube, which the ranking makes zero.</summary>
    public const int SolvedIndex = 0;

    /// <summary>
    /// Each corner slot as its three facelets, in a consistent rotational sense so
    /// that "twist" means the same thing at every slot. Slot 7 is the fixed corner.
    /// </summary>
    public static ReadOnlySpan<byte> Slots =>
    [
        16, 0, 14,  // 0 left-up-back
        21, 15, 1,  // 1 right-up-back
        17, 4, 2,   // 2 left-up-front
        20, 3, 5,   // 3 right-up-front
        19, 8, 6,   // 4 left-down-front
        22, 7, 9,   // 5 right-down-front
        18, 12, 10, // 6 left-down-back
        23, 11, 13, // 7 right-down-back — fixed, not part of the index
    ];

    private const int ColorKeyCount = CubeState.ColorCount * CubeState.ColorCount * CubeState.ColorCount;
    private const byte NoPiece = 0xFF;

    private static readonly int[] Factorials = [1, 1, 2, 6, 24, 120, 720, 5040];

    private static readonly FrozenDictionary<int, GoalOrientation> GoalsByFixedCorner = BuildGoals();

    /// <summary>The goal the canonical solved cube describes: yellow up, red front.</summary>
    private static readonly GoalOrientation CanonicalGoal = Goal(CubeState.Solved);

    /// <summary>
    /// A solved cube in one particular orientation, with the piece colours read off it.
    /// </summary>
    /// <remarks>
    /// A cube may be handed over in any of the 24 orientations. Its fixed corner's
    /// three colours match exactly one rotation of the solved cube, so that rotation —
    /// not the canonical solved state — defines what "solved" means for this cube, and
    /// the goal index stays 0 whichever way the cube is held.
    /// </remarks>
    internal sealed class GoalOrientation
    {
        /// <summary>The solved cube this scramble is aiming at.</summary>
        public required CubeState Solved { get; init; }

        /// <summary>Colour triple of each of the eight pieces, as read on <see cref="Solved"/>.</summary>
        public required byte[] PieceColors { get; init; }

        /// <summary>Colour triple key to <c>piece * 4 + twist</c>, or <see cref="NoPiece"/>.</summary>
        public required byte[] PieceByColors { get; init; }
    }

    /// <summary>The solved cube whose fixed corner matches this state's fixed corner.</summary>
    /// <param name="state">Any cube state.</param>
    /// <exception cref="UnsolvableCubeException">No solved orientation has that corner.</exception>
    public static CubeState GoalFor(CubeState state) => Goal(state).Solved;

    /// <summary>Encodes a sticker state as a dense index below <see cref="StateCount"/>.</summary>
    /// <param name="state">The cube to encode; any of its 24 orientations gives the same index.</param>
    /// <exception cref="UnsolvableCubeException">The stickers do not form a reachable cube.</exception>
    public static int Encode(CubeState state) => Encode(state, Goal(state));

    /// <summary>Decodes an index back into stickers, in the canonical solved orientation.</summary>
    /// <param name="index">An index below <see cref="StateCount"/>.</param>
    public static CubeState Decode(int index)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(index);
        ArgumentOutOfRangeException.ThrowIfGreaterThanOrEqual(index, StateCount);

        Span<byte> permutation = stackalloc byte[FreeSlots];
        Span<byte> orientation = stackalloc byte[FreeSlots];
        UnrankPermutation(Math.DivRem(index, OrientationCount, out int orientationRank), permutation);
        UnrankOrientation(orientationRank, orientation);
        return FromCubies(permutation, orientation, CanonicalGoal);
    }

    internal static GoalOrientation Goal(CubeState state)
    {
        int key = ColorKey(state[Moves.FixedCorner[0]], state[Moves.FixedCorner[1]], state[Moves.FixedCorner[2]]);
        return GoalsByFixedCorner.TryGetValue(key, out GoalOrientation? goal)
            ? goal
            : throw new UnsolvableCubeException("the fixed corner's colours occur on no solved cube");
    }

    internal static int Encode(CubeState state, GoalOrientation goal)
    {
        Span<byte> permutation = stackalloc byte[FreeSlots];
        Span<byte> orientation = stackalloc byte[FreeSlots];
        ToCubies(state, goal, permutation, orientation);
        return (RankPermutation(permutation) * OrientationCount) + RankOrientation(orientation);
    }

    /// <summary>Splits a sticker state into (piece per slot, twist per slot).</summary>
    internal static void ToCubies(CubeState state, GoalOrientation goal, Span<byte> permutation, Span<byte> orientation)
    {
        int seen = 0;
        int twists = 0;
        for (int slot = 0; slot < FreeSlots; slot++)
        {
            int key = ColorKey(
                state[Slots[(slot * 3) + 0]],
                state[Slots[(slot * 3) + 1]],
                state[Slots[(slot * 3) + 2]]);
            byte found = goal.PieceByColors[key];
            if (found == NoPiece)
            {
                throw new UnsolvableCubeException($"the colours at slot {slot} are no corner of this cube");
            }

            int piece = found >> 2;
            if (piece >= FreeSlots || (seen & (1 << piece)) != 0)
            {
                throw new UnsolvableCubeException($"the piece at slot {slot} occurs twice");
            }

            seen |= 1 << piece;
            permutation[slot] = (byte)piece;
            orientation[slot] = (byte)(found & 3);
            twists += found & 3;
        }

        if (twists % 3 != 0)
        {
            throw new UnsolvableCubeException("the corner twists do not sum to a multiple of three");
        }
    }

    /// <summary>Rebuilds a sticker state. Inverse of <see cref="ToCubies"/>.</summary>
    internal static CubeState FromCubies(ReadOnlySpan<byte> permutation, ReadOnlySpan<byte> orientation, GoalOrientation goal)
    {
        Span<byte> stickers = stackalloc byte[CubeState.FaceletCount];
        for (int slot = 0; slot < FreeSlots; slot++)
        {
            int piece = permutation[slot] * 3;
            int twist = orientation[slot];
            for (int i = 0; i < 3; i++)
            {
                stickers[Slots[(slot * 3) + i]] = goal.PieceColors[piece + ((twist + i) % 3)];
            }
        }

        for (int i = 0; i < 3; i++)
        {
            stickers[Slots[(FreeSlots * 3) + i]] = goal.PieceColors[(FreeSlots * 3) + i];
        }

        return CubeState.Create(stickers);
    }

    /// <summary>Lehmer rank of a permutation of 0..6; the identity ranks 0.</summary>
    internal static int RankPermutation(ReadOnlySpan<byte> permutation)
    {
        int rank = 0;
        for (int i = 0; i < FreeSlots; i++)
        {
            int smaller = 0;
            for (int j = i + 1; j < FreeSlots; j++)
            {
                if (permutation[j] < permutation[i])
                {
                    smaller++;
                }
            }

            rank = (rank * (FreeSlots - i)) + smaller;
        }

        return rank;
    }

    /// <summary>Inverse of <see cref="RankPermutation"/>.</summary>
    internal static void UnrankPermutation(int rank, Span<byte> permutation)
    {
        Span<byte> available = stackalloc byte[FreeSlots];
        for (byte i = 0; i < FreeSlots; i++)
        {
            available[i] = i;
        }

        int remaining = FreeSlots;
        for (int i = 0; i < FreeSlots; i++)
        {
            int factorial = Factorials[FreeSlots - 1 - i];
            int choice = Math.DivRem(rank, factorial, out rank);
            permutation[i] = available[choice];
            available[(choice + 1)..remaining].CopyTo(available[choice..]);
            remaining--;
        }
    }

    /// <summary>Ranks the twists; the seventh is implied by the other six.</summary>
    internal static int RankOrientation(ReadOnlySpan<byte> orientation)
    {
        int rank = 0;
        for (int slot = FreeSlots - 2; slot >= 0; slot--)
        {
            rank = (rank * 3) + orientation[slot];
        }

        return rank;
    }

    /// <summary>Inverse of <see cref="RankOrientation"/>.</summary>
    internal static void UnrankOrientation(int rank, Span<byte> orientation)
    {
        int total = 0;
        for (int slot = 0; slot < FreeSlots - 1; slot++)
        {
            byte twist = (byte)(rank % 3);
            rank /= 3;
            orientation[slot] = twist;
            total += twist;
        }

        // The twists of a whole cube sum to a multiple of three, which is what makes
        // the seventh redundant.
        orientation[FreeSlots - 1] = (byte)((3 - (total % 3)) % 3);
    }

    private static int ColorKey(int first, int second, int third) =>
        (((first * CubeState.ColorCount) + second) * CubeState.ColorCount) + third;

    private static FrozenDictionary<int, GoalOrientation> BuildGoals()
    {
        Dictionary<int, GoalOrientation> goals = new(CubeSymmetry.Rotations.Length);
        foreach (CubeState solved in CubeSymmetry.Orientations(CubeState.Solved))
        {
            byte[] pieces = new byte[(FreeSlots + 1) * 3];
            for (int i = 0; i < pieces.Length; i++)
            {
                pieces[i] = solved[Slots[i]];
            }

            byte[] lookup = new byte[ColorKeyCount];
            Array.Fill(lookup, NoPiece);
            for (int piece = 0; piece <= FreeSlots; piece++)
            {
                for (int twist = 0; twist < 3; twist++)
                {
                    int key = ColorKey(
                        pieces[(piece * 3) + twist],
                        pieces[(piece * 3) + ((twist + 1) % 3)],
                        pieces[(piece * 3) + ((twist + 2) % 3)]);
                    lookup[key] = (byte)((piece << 2) | twist);
                }
            }

            int corner = ColorKey(
                solved[Moves.FixedCorner[0]],
                solved[Moves.FixedCorner[1]],
                solved[Moves.FixedCorner[2]]);
            goals[corner] = new GoalOrientation
            {
                Solved = solved,
                PieceColors = pieces,
                PieceByColors = lookup,
            };
        }

        return goals.ToFrozenDictionary();
    }
}
