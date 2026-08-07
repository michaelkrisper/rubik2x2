namespace Rubik2x2.Tests;

/// <summary>The encoding, checked against the sticker model it compresses.</summary>
public sealed class CoordinatesTests
{
    [Fact]
    public void SolvedCubeEncodesToZero()
    {
        Assert.Equal(Coordinates.SolvedIndex, Coordinates.Encode(CubeState.Solved));
        Assert.Equal(3_674_160, Coordinates.StateCount);
    }

    [Fact]
    public void EncodingRoundTrips()
    {
        // Decoding yields the canonical orientation, so scramble with solve moves only:
        // they leave the fixed corner alone.
        Random random = new(11);
        for (int trial = 0; trial < 200; trial++)
        {
            CubeState state = CubeState.Solved;
            for (int turn = 0; turn < 14; turn++)
            {
                state = state.Apply(Moves.Solve[random.Next(Moves.SolveCount)]);
            }

            Assert.Equal(state, Coordinates.Decode(Coordinates.Encode(state)));
        }
    }

    [Fact]
    public void EveryIndexRoundTrips()
    {
        Random random = new(12);
        for (int trial = 0; trial < 2000; trial++)
        {
            int index = random.Next(Coordinates.StateCount);
            Assert.Equal(index, Coordinates.Encode(Coordinates.Decode(index)));
        }
    }

    [Fact]
    public void ACubeHeldAnyWhichWayEncodesToSomethingEquallyFarFromSolved()
    {
        // The index is relative to whichever corner sits in the fixed slot, so turning
        // the cube in your hands generally changes it. What may not change is what it
        // means: the goal is still index 0 and the distance to it is still the same.
        Assert.All(
            CubeSymmetry.Orientations(CubeState.Solved),
            held => Assert.Equal(Coordinates.SolvedIndex, Coordinates.Encode(held)));

        foreach (CubeState scramble in TestCubes.Scrambles(5))
        {
            int expected = TestCubes.Table.Distance(scramble);
            Assert.All(
                CubeSymmetry.Orientations(scramble),
                held => Assert.Equal(expected, TestCubes.Table.Distance(held)));
        }
    }

    [Fact]
    public void MoveTablesAgreeWithTheStickerModel()
    {
        CoordinateMoveTables tables = CoordinateMoveTables.Shared;
        foreach (CubeState scramble in TestCubes.Scrambles(10))
        {
            int index = Coordinates.Encode(scramble);
            for (int move = 0; move < Moves.SolveCount; move++)
            {
                CubeState turned = scramble.Apply(Moves.Solve[move]);
                Assert.Equal(Coordinates.Encode(turned), tables.Apply(index, move));
            }
        }
    }

    [Fact]
    public void RanksAreABijection()
    {
        HashSet<int> permutationRanks = [];
        Span<byte> permutation = stackalloc byte[Coordinates.FreeSlots];
        for (int rank = 0; rank < Coordinates.PermutationCount; rank++)
        {
            Coordinates.UnrankPermutation(rank, permutation);
            Assert.Equal(rank, Coordinates.RankPermutation(permutation));
            Assert.True(permutationRanks.Add(rank));
        }

        Span<byte> orientation = stackalloc byte[Coordinates.FreeSlots];
        for (int rank = 0; rank < Coordinates.OrientationCount; rank++)
        {
            Coordinates.UnrankOrientation(rank, orientation);
            int twists = 0;
            foreach (byte twist in orientation)
            {
                twists += twist;
            }

            Assert.Equal(0, twists % 3);
            Assert.Equal(rank, Coordinates.RankOrientation(orientation));
        }
    }

    [Fact]
    public void UnreachableCubesAreRejected()
    {
        // Twisting a single corner keeps the colour counts legal but leaves the twists
        // summing to 1 mod 3, which no sequence of turns can do.
        Span<byte> stickers = stackalloc byte[CubeState.FaceletCount];
        CubeState.Solved.Stickers.CopyTo(stickers);
        (byte a, byte b, byte c) = (stickers[16], stickers[0], stickers[14]);
        (stickers[16], stickers[0], stickers[14]) = (b, c, a);
        CubeState twisted = CubeState.Create(stickers);

        Assert.Throws<UnsolvableCubeException>(() => Coordinates.Encode(twisted));

        // A cube whose fixed corner shows two opposite colours, which no corner does.
        CubeState.Solved.Stickers.CopyTo(stickers);
        (stickers[11], stickers[0]) = (stickers[0], stickers[11]);
        CubeState impossibleCorner = CubeState.Create(stickers);
        Assert.Throws<UnsolvableCubeException>(() => Coordinates.Encode(impossibleCorner));
    }
}
