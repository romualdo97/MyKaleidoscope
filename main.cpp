#include <iostream>
#include <cassert>
#include <memory>
#include <variant>
#include <vector>

// ====================================================================================
// ====================================================================================
// ====================================================================================

// The lexer returns tokens [0-255] if it is an unknown character, otherwise one
// of these for known things.
enum class Token : char
{
    EndOfFile = 0,

    // Command
    Definition,
    Extern,

    // Primary
    Identifier,
    Number,

    Unknown
};

static std::string IdentifierStr; // Filled in if Token::Identifier
static double NumVal; // // Filled in if Token::Number
using TokenOrAsciiCharacter = std::variant<Token/* Token */, char /* Character as ascii value, used for example if char is a + or - */>;

static char GetNextChar()
{
    const int Character = getchar();
    assert(Character >= 0 && Character < 128 && "Provided character is not an ascii character");
    return static_cast<char>(Character);
}

// Return the next token from standard input.
static TokenOrAsciiCharacter GetNextToken() // Lexer
{
    // Find next not empty space
    static char LastChar = ' ';
    while (isspace(LastChar))
    {
        LastChar = GetNextChar();
    }
    
    // identifier: [a-zA-Z][a-zA-Z0-9]*
    if (isalpha(LastChar))
    {
        IdentifierStr = static_cast<char>(LastChar); // Assume input if ASCI
        while (isalnum(LastChar = GetNextChar()))
        {
            IdentifierStr += static_cast<char>(LastChar); 
        }

        if (IdentifierStr == "def")
        {
            return Token::Definition;
        }

        if (IdentifierStr == "extern")
        {
            return Token::Extern;    
        }

        return Token::Identifier;
    }

    // Number: [0-9.]+
    if (isdigit(LastChar) || LastChar == '.')
    {
        std::string NumberStr;
        do
        {
            NumberStr += LastChar;
            LastChar = GetNextChar();
        }
        while (isdigit(LastChar) || LastChar == '.');

        NumVal = strtod(NumberStr.c_str(), nullptr);
        return Token::Number;
    }

    // Comments: #
    if (LastChar == '#')
    {
        do
        {
            LastChar = GetNextChar();
        }
        while (LastChar != EOF && LastChar != '\n' && LastChar != '\r');

        if (LastChar != EOF)
        {
            return GetNextToken();
        }
    }

    // Check for end of file.  Don't eat the EOF.
    if (LastChar == EOF)
    {
        return Token::EndOfFile;
    }

    // Otherwise, just return the character as its ascii value.
    char ThisChar = LastChar;
    LastChar = GetNextChar();
    return ThisChar;
}

// ====================================================================================
// ====================================================================================
// ====================================================================================

// Base class for all expression nodes.
class ExpressionAST
{
public:
    virtual ~ExpressionAST() = default;
};

// Expression class for numeric literals like "1.0".
class NumberExpressionAST final : public ExpressionAST
{
    double Value;
public:
    explicit NumberExpressionAST(const double InValue)
        : Value(InValue)
    {}
};

// Expression class for referencing a variable, like "a".
class VariableExpressionAST final : public ExpressionAST
{
    std::string Name;
    public:
        explicit VariableExpressionAST(std::string&& InValue)
            : Name(std::move(InValue))
    {}
};

class BinaryExpressionAST final : public ExpressionAST
{
    char Operator;
    std::unique_ptr<ExpressionAST> LHS, RHS;

public:
    explicit BinaryExpressionAST(
        const char InOperator,
        std::unique_ptr<ExpressionAST>&& InLHS,
        std::unique_ptr<ExpressionAST>&& InRHS)
        : Operator(InOperator)
        , LHS(std::move(InLHS))
        , RHS(std::move(InRHS))
    {}
};

// ====================================================================================
// ====================================================================================
// ====================================================================================

/**
 * This class represents the "prototype" for a function,
 * which captures its name, and its argument names (thus implicitly the number
 * of arguments the function takes).
 */
class PrototypeAST
{
    std::string Name;
    std::vector<std::string> Args;

public:
    PrototypeAST(
        const std::string& InName,
        std::vector<std::string> InArgs)
        : Name(InName)
        , Args(std::move(InArgs)) {}

    [[nodiscard]] const std::string& getName() const { return Name; }
};

/**
 * This class represents a function definition itself.
 */
class FunctionAST
{
    std::unique_ptr<PrototypeAST> Proto;
    std::unique_ptr<ExpressionAST> Body;

public:
    FunctionAST(
        std::unique_ptr<PrototypeAST> InProto,
        std::unique_ptr<ExpressionAST> InBody)
        : Proto(std::move(InProto))
        , Body(std::move(InBody))
    {}
};

// ====================================================================================
// ====================================================================================
// ====================================================================================

// Provide a simple token buffer.  CurrentToken is the current
// token the parser is looking at.  AdvanceToken reads another token from the
// lexer and updates CurrentToken with its results.
static TokenOrAsciiCharacter CurrentToken;
static TokenOrAsciiCharacter AdvanceToNextToken()
{
    return CurrentToken = GetNextToken();
}

// These are little helper functions for error handling.

std::unique_ptr<ExpressionAST> LogError(const char* Str)
{
    fprintf(stderr, "Error: %s\n", Str);
    return nullptr;
}

std::unique_ptr<PrototypeAST> LogErrorP(const char* Str)
{
    LogError(Str);
    return nullptr;
}

// NumberExpr ::= number
std::unique_ptr<ExpressionAST> ParseNumberExpr()
{
    // Parse number, assumes that the current token from the lexer is a number
    auto Node = std::make_unique<NumberExpressionAST>(NumVal);
    AdvanceToNextToken(); // consume the number
    return std::move(Node);
}

// ParenthesisExpr ::= '(' expression ')'
static std::unique_ptr<ExpressionAST> ParseParenthesisExpr()
{
    AdvanceToNextToken(); // eat (.
    auto V = ParseExpression();
    if (!V)
    {
        return nullptr;
    }

    if (std::get<char>(CurrentToken) != ')')
    {
        return LogError("expected ')'");
    }

    AdvanceToNextToken(); // eat ).
    return V;
}

int main()
{

}