namespace Rubik2x2.Tests;

/// <summary>The sticker model: the move algebra every other layer is derived from.</summary>
public sealed class CubeStateTests
{
    private static CubeState Scrambled(int seed = 1) => CubeState.Solved.Scramble(12, new Random(seed));

    [Fact]
    public void SolvedCubeIsSolved()
    {
        Assert.True(CubeState.Solved.IsSolved);
        Assert.Equal("yyyyrrrrwwwwppppbbbbgggg", CubeState.Solved.ToString());
    }

    [Fact]
    public void EveryMoveHasOrderFour()
    {
        CubeState start = Scrambled();
        foreach (char move in Moves.All)
        {
            CubeState state = start;
            for (int turn = 1; turn <= 3; turn++)
            {
                state = state.Apply(move);
                Assert.NotEqual(start, state);
            }

            Assert.Equal(start, state.Apply(move));
        }
    }

    [Fact]
    public void MoveAndInverseCancel()
    {
        CubeState start = Scrambled(2);
        for (int i = 0; i < Moves.All.Length; i += 2)
        {
            char move = Moves.All[i];
            char inverse = Moves.All[i + 1];
            Assert.Equal(start, start.Apply(move).Apply(inverse));
            Assert.Equal(start, start.Apply(inverse).Apply(move));
        }
    }

    [Theory]
    [InlineData('l', 'r')]
    [InlineData('t', 'b')]
    [InlineData('f', 'a')]
    public void OppositeFacesCommute(char first, char second)
    {
        CubeState start = Scrambled(3);
        Assert.Equal(start.Apply(first).Apply(second), start.Apply(second).Apply(first));
    }

    [Fact]
    public void SolveMovesFixExactlyTheDownBackRightCorner()
    {
        List<int> fixedFacelets = [];
        for (int facelet = 0; facelet < CubeState.FaceletCount; facelet++)
        {
            bool untouched = true;
            foreach (char move in Moves.Solve)
            {
                untouched &= Moves.Permutation(move)[facelet] == facelet;
            }

            if (untouched)
            {
                fixedFacelets.Add(facelet);
            }
        }

        Assert.Equal([11, 13, 23], fixedFacelets);
    }

    [Fact]
    public void InvertUndoesASequence()
    {
        CubeState start = Scrambled(4);
        const string moves = "lFtLTfLLtF";
        Assert.Equal("fTllFtlTfL", Moves.Invert(moves));
        Assert.Equal(start, start.Apply(moves).Apply(Moves.Invert(moves)));
    }

    [Fact]
    public void ThereAreTwentyFourWaysToHoldACube()
    {
        Assert.Equal(24, CubeSymmetry.Rotations.Length);
        Assert.Equal(24, CubeSymmetry.Orientations(Scrambled(5)).Distinct().Count());
        Assert.All(CubeSymmetry.Orientations(CubeState.Solved), state => Assert.True(state.IsSolved));
    }

    [Fact]
    public void ParseRoundTrips()
    {
        CubeState state = Scrambled(6);
        Assert.Equal(state, CubeState.Parse(state.ToString()));
        Assert.Equal(state, CubeState.Parse($"  {state.ToString().ToUpperInvariant()}  "));
    }

    [Theory]
    [InlineData("", "24 colour letters")]
    [InlineData("yyyyrrrrwwwwppppbbbbggg", "24 colour letters")]
    [InlineData("yyyyrrrrwwwwppppbbbbggggg", "24 colour letters")]
    [InlineData("xyyyrrrrwwwwppppbbbbgggg", "invalid colour letter")]
    [InlineData("yyyyyrrrwwwwppppbbbbgggg", "appears")]
    public void ParseRejectsMalformedStates(string text, string reason)
    {
        FormatException error = Assert.Throws<FormatException>(() => CubeState.Parse(text));
        Assert.Contains(reason, error.Message, StringComparison.Ordinal);
        Assert.False(CubeState.TryParse(text, out _, out _));
    }

    [Fact]
    public void ScrambleIsReproducibleAndReachable()
    {
        CubeState first = CubeState.Solved.Scramble(14, new Random(42));
        CubeState second = CubeState.Solved.Scramble(14, new Random(42));
        Assert.Equal(first, second);
        Assert.False(first.IsSolved);
        Assert.InRange(TestCubes.Table.Distance(first), 1, 14);
    }
}
