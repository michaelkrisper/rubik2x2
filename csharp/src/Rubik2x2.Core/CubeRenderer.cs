using System.Text;

namespace Rubik2x2;

/// <summary>How much colour terminal output may use.</summary>
public enum ColorMode
{
    /// <summary>Plain letters, no escape sequences.</summary>
    None,

    /// <summary>The xterm 256-colour cube.</summary>
    Ansi256,

    /// <summary>24-bit colour.</summary>
    TrueColor,
}

/// <summary>
/// Draws a cube as the unfolded net a state string is written in.
/// </summary>
/// <remarks>
/// Deliberately separate from <see cref="CubeState"/>: the model is 24 bytes and
/// knows nothing about how anyone looks at it. UP sits above FRONT, the four side
/// faces form a band, DOWN sits below. BACK is drawn as seen from behind, which is
/// what makes the band read as one continuous strip around the cube. Coloured
/// facelets are two rows tall so they come out roughly square in a terminal cell.
/// </remarks>
public static class CubeRenderer
{
    private const int CellWidth = 4;
    private const int FaceWidth = CellWidth * 2;
    private const string Gap = "  ";
    private const string Reset = "\e[0m";
    private const string Dim = "\e[2m";

    /// <summary>Face colours, close to a real cube; this puzzle uses purple for orange.</summary>
    private static ReadOnlySpan<byte> Palette =>
    [
        196, 30, 58,    // r
        245, 245, 245,  // w
        0, 81, 186,     // b
        255, 213, 0,    // y
        137, 66, 175,   // p
        0, 158, 96,     // g
    ];

    private static ReadOnlySpan<int> Band => [CubeState.Left, CubeState.Front, CubeState.Right, CubeState.Back];

    private static readonly string[] BandLabels = ["LEFT", "FRONT", "RIGHT", "BACK"];

    /// <summary>
    /// What to use for the given stream, honouring <c>NO_COLOR</c>, <c>FORCE_COLOR</c>
    /// and whether output is a terminal at all.
    /// </summary>
    /// <param name="redirected">True when output is piped or redirected.</param>
    public static ColorMode Detect(bool redirected)
    {
        if (!string.IsNullOrEmpty(Environment.GetEnvironmentVariable("NO_COLOR")))
        {
            return ColorMode.None;
        }

        if (redirected && string.IsNullOrEmpty(Environment.GetEnvironmentVariable("FORCE_COLOR")))
        {
            return ColorMode.None;
        }

        return Best();
    }

    /// <summary>The richest mode this terminal understands, ignoring where output goes.</summary>
    public static ColorMode Best()
    {
        if (Environment.GetEnvironmentVariable("TERM") == "dumb")
        {
            return ColorMode.None;
        }

        string? colorTerm = Environment.GetEnvironmentVariable("COLORTERM");
        return colorTerm is "truecolor" or "24bit" ? ColorMode.TrueColor : ColorMode.Ansi256;
    }

    /// <summary>Draws the cube as an unfolded net.</summary>
    /// <param name="state">The cube to draw.</param>
    /// <param name="mode">How much colour to use.</param>
    public static string Render(CubeState state, ColorMode mode)
    {
        int cellHeight = mode is ColorMode.None ? 1 : 2;
        string indent = new(' ', FaceWidth + Gap.Length);
        StringBuilder text = new();

        text.Append(indent).AppendLine(Label("UP", mode));
        AppendFace(text, state, CubeState.Up, mode, cellHeight, indent);
        text.AppendLine();

        for (int i = 0; i < Band.Length; i++)
        {
            text.Append(i == 0 ? string.Empty : Gap).Append(Label(BandLabels[i], mode));
        }

        text.AppendLine();
        for (int row = 0; row < 2; row++)
        {
            for (int line = 0; line < cellHeight; line++)
            {
                for (int face = 0; face < Band.Length; face++)
                {
                    text.Append(face == 0 ? string.Empty : Gap).Append(FaceRow(state, Band[face], row, mode));
                }

                text.AppendLine();
            }
        }

        text.AppendLine();
        text.Append(indent).AppendLine(Label("DOWN", mode));
        AppendFace(text, state, CubeState.Down, mode, cellHeight, indent);
        return text.ToString().TrimEnd('\n', '\r');
    }

    private static void AppendFace(StringBuilder text, CubeState state, int side, ColorMode mode, int cellHeight, string indent)
    {
        for (int row = 0; row < 2; row++)
        {
            for (int line = 0; line < cellHeight; line++)
            {
                text.Append(indent).AppendLine(FaceRow(state, side, row, mode));
            }
        }
    }

    private static string FaceRow(CubeState state, int side, int row, ColorMode mode)
    {
        // BACK is seen from behind, so its stickers are read in reverse.
        (int first, int second) = side == CubeState.Back
            ? (row == 0 ? (3, 2) : (1, 0))
            : (row == 0 ? (0, 1) : (2, 3));
        return Paint(state[side + first], mode) + Paint(state[side + second], mode);
    }

    private static string Paint(byte color, ColorMode mode)
    {
        if (mode is ColorMode.None)
        {
            return Center(char.ToUpperInvariant(CubeState.ColorChars[color]).ToString(), CellWidth);
        }

        int red = Palette[color * 3];
        int green = Palette[(color * 3) + 1];
        int blue = Palette[(color * 3) + 2];
        string background = mode is ColorMode.TrueColor
            ? $"\e[48;2;{red};{green};{blue}m"
            : $"\e[48;5;{16 + (36 * (red * 5 / 255)) + (6 * (green * 5 / 255)) + (blue * 5 / 255)}m";
        return background + new string(' ', CellWidth) + Reset;
    }

    private static string Label(string text, ColorMode mode)
    {
        string centered = Center(text, FaceWidth);
        return mode is ColorMode.None ? centered : Dim + centered + Reset;
    }

    private static string Center(string text, int width)
    {
        int padding = width - text.Length;
        return new string(' ', padding / 2) + text + new string(' ', padding - (padding / 2));
    }
}
