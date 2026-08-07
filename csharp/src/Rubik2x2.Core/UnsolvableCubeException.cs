namespace Rubik2x2;

/// <summary>A sticker arrangement no sequence of turns can reach from a solved cube.</summary>
public sealed class UnsolvableCubeException : Exception
{
    /// <summary>Creates the exception with a default message.</summary>
    public UnsolvableCubeException()
        : base("this cube cannot be reached from a solved cube")
    {
    }

    /// <summary>Creates the exception.</summary>
    /// <param name="message">Why the cube is unsolvable.</param>
    public UnsolvableCubeException(string message)
        : base(message)
    {
    }

    /// <summary>Creates the exception.</summary>
    /// <param name="message">Why the cube is unsolvable.</param>
    /// <param name="innerException">The cause.</param>
    public UnsolvableCubeException(string message, Exception innerException)
        : base(message, innerException)
    {
    }
}
