using System.Diagnostics.CodeAnalysis;
using System.Runtime.CompilerServices;

namespace Rubik2x2;

/// <summary>Storage for the 24 sticker colour ids of a <see cref="CubeState"/>.</summary>
[InlineArray(CubeState.FaceletCount)]
internal struct StickerBuffer
{
    private byte _element0;
}

/// <summary>
/// A 2x2x2 cube as 24 sticker colour ids — the readable model, and nothing else.
/// </summary>
/// <remarks>
/// <para>
/// Facelets are numbered <c>side * 4 + row * 2 + column</c> with the sides in the
/// order UP, FRONT, DOWN, BACK, LEFT, RIGHT and each side read row-major. A move is
/// a permutation of those 24 positions, applied as <c>next[i] = current[perm[i]]</c>.
/// </para>
/// <para>
/// The state is a 24-byte value type, so turning a face allocates nothing and states
/// can live in sets and dictionaries with structural equality. Searches never use
/// this type — they run on the dense integer index of <see cref="Coordinates"/> —
/// which is exactly why the two are kept apart.
/// </para>
/// </remarks>
public readonly struct CubeState : IEquatable<CubeState>
{
    /// <summary>Number of stickers on a 2x2x2 cube.</summary>
    public const int FaceletCount = 24;

    /// <summary>Colour letters by colour id: red, white, blue, yellow, purple, green.</summary>
    public const string ColorChars = "rwbypg";

    /// <summary>The number of colours, and of sides.</summary>
    public const int ColorCount = 6;

    /// <summary>Facelet index of the first sticker of the UP side.</summary>
    public const int Up = 0 * 4;

    /// <summary>Facelet index of the first sticker of the FRONT side.</summary>
    public const int Front = 1 * 4;

    /// <summary>Facelet index of the first sticker of the DOWN side.</summary>
    public const int Down = 2 * 4;

    /// <summary>Facelet index of the first sticker of the BACK side.</summary>
    public const int Back = 3 * 4;

    /// <summary>Facelet index of the first sticker of the LEFT side.</summary>
    public const int Left = 4 * 4;

    /// <summary>Facelet index of the first sticker of the RIGHT side.</summary>
    public const int Right = 5 * 4;

    private readonly StickerBuffer _stickers;

    private CubeState(ReadOnlySpan<byte> colors) => colors.CopyTo(_stickers);

    /// <summary>The solved cube in the standard orientation: yellow up, red front.</summary>
    public static CubeState Solved { get; } = Create([
        3, 3, 3, 3, 0, 0, 0, 0, 1, 1, 1, 1, 4, 4, 4, 4, 2, 2, 2, 2, 5, 5, 5, 5,
    ]);

    /// <summary>The colour id at <paramref name="facelet"/>.</summary>
    /// <param name="facelet">A facelet index below <see cref="FaceletCount"/>.</param>
    public byte this[int facelet] => _stickers[facelet];

    /// <summary>The 24 colour ids, in facelet order.</summary>
    [UnscopedRef]
    public ReadOnlySpan<byte> Stickers => _stickers;

    /// <summary>True when every side shows a single colour.</summary>
    public bool IsSolved
    {
        get
        {
            for (int side = 0; side < FaceletCount; side += 4)
            {
                byte color = this[side];
                if (this[side + 1] != color || this[side + 2] != color || this[side + 3] != color)
                {
                    return false;
                }
            }

            return true;
        }
    }

    /// <summary>Builds a state from 24 colour ids, without checking that it is legal.</summary>
    /// <param name="colors">Exactly 24 colour ids below <see cref="ColorCount"/>.</param>
    /// <exception cref="ArgumentException">The span does not hold 24 colours.</exception>
    public static CubeState Create(ReadOnlySpan<byte> colors)
    {
        if (colors.Length != FaceletCount)
        {
            throw new ArgumentException($"expected {FaceletCount} colours, got {colors.Length}", nameof(colors));
        }

        return new CubeState(colors);
    }

    /// <summary>
    /// Reads 24 colour letters in side order UP FRONT DOWN BACK LEFT RIGHT.
    /// </summary>
    /// <param name="text">The state string, for example <c>yyyyrrrrwwwwppppbbbbgggg</c>.</param>
    /// <exception cref="FormatException">
    /// The text is not 24 letters from <see cref="ColorChars"/> with each colour appearing four times.
    /// </exception>
    public static CubeState Parse(ReadOnlySpan<char> text)
    {
        return TryParse(text, out CubeState state, out string? error)
            ? state
            : throw new FormatException(error);
    }

    /// <summary>Reads a state string, reporting why it was rejected instead of throwing.</summary>
    /// <param name="text">The state string.</param>
    /// <param name="state">The parsed state, or default when parsing failed.</param>
    /// <param name="error">Why parsing failed, or <see langword="null"/> on success.</param>
    /// <returns>True when <paramref name="text"/> is a well-formed state string.</returns>
    public static bool TryParse(ReadOnlySpan<char> text, out CubeState state, [NotNullWhen(false)] out string? error)
    {
        state = default;
        ReadOnlySpan<char> trimmed = text.Trim();
        if (trimmed.Length != FaceletCount)
        {
            error = $"expected {FaceletCount} colour letters, got {trimmed.Length}";
            return false;
        }

        Span<byte> colors = stackalloc byte[FaceletCount];
        Span<int> counts = stackalloc int[ColorCount];
        counts.Clear();
        for (int i = 0; i < FaceletCount; i++)
        {
            int color = ColorChars.IndexOf(char.ToLowerInvariant(trimmed[i]));
            if (color < 0)
            {
                error = $"invalid colour letter '{trimmed[i]}' (allowed: {ColorChars})";
                return false;
            }

            colors[i] = (byte)color;
            counts[color]++;
        }

        for (int color = 0; color < ColorCount; color++)
        {
            if (counts[color] != 4)
            {
                error = $"colour '{ColorChars[color]}' appears {counts[color]} times, expected 4";
                return false;
            }
        }

        state = new CubeState(colors);
        error = null;
        return true;
    }

    /// <summary>Turns one face.</summary>
    /// <param name="move">A move letter from <see cref="Moves.All"/>.</param>
    public CubeState Apply(char move) => Apply(Moves.Permutation(move));

    /// <summary>Applies a facelet permutation directly.</summary>
    /// <param name="permutation">24 source facelet indices; <c>next[i] = current[permutation[i]]</c>.</param>
    public CubeState Apply(ReadOnlySpan<byte> permutation)
    {
        Span<byte> next = stackalloc byte[FaceletCount];
        for (int i = 0; i < FaceletCount; i++)
        {
            next[i] = this[permutation[i]];
        }

        return new CubeState(next);
    }

    /// <summary>Turns faces in sequence, for example <c>"lFtL"</c>.</summary>
    /// <param name="moves">Move letters, applied left to right.</param>
    public CubeState Apply(ReadOnlySpan<char> moves)
    {
        CubeState state = this;
        foreach (char move in moves)
        {
            state = state.Apply(move);
        }

        return state;
    }

    /// <summary>
    /// Applies <paramref name="count"/> random moves, each reaching a state not seen before.
    /// </summary>
    /// <param name="count">How many turns to make.</param>
    /// <param name="random">The source of randomness; a seeded instance makes this reproducible.</param>
    public CubeState Scramble(int count, Random random)
    {
        ArgumentNullException.ThrowIfNull(random);
        CubeState state = this;
        HashSet<CubeState> seen = [state];
        while (count > 0)
        {
            CubeState candidate = state.Apply(Moves.All[random.Next(Moves.All.Length)]);
            if (seen.Add(candidate))
            {
                state = candidate;
                count--;
            }
        }

        return state;
    }

    /// <inheritdoc/>
    public bool Equals(CubeState other) => Stickers.SequenceEqual(other.Stickers);

    /// <inheritdoc/>
    public override bool Equals(object? obj) => obj is CubeState other && Equals(other);

    /// <inheritdoc/>
    public override int GetHashCode()
    {
        HashCode hash = default;
        hash.AddBytes(Stickers);
        return hash.ToHashCode();
    }

    /// <summary>The 24 colour letters of this state. Inverse of <see cref="Parse"/>.</summary>
    public override string ToString()
    {
        return string.Create(FaceletCount, this, static (span, state) =>
        {
            for (int i = 0; i < FaceletCount; i++)
            {
                span[i] = ColorChars[state[i]];
            }
        });
    }

    /// <summary>Compares two states for equality.</summary>
    /// <param name="left">The first state.</param>
    /// <param name="right">The second state.</param>
    public static bool operator ==(CubeState left, CubeState right) => left.Equals(right);

    /// <summary>Compares two states for inequality.</summary>
    /// <param name="left">The first state.</param>
    /// <param name="right">The second state.</param>
    public static bool operator !=(CubeState left, CubeState right) => !left.Equals(right);
}
