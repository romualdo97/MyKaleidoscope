// Following Kaleidoscope tutorial from https://llvm.org/docs/tutorial/

// Glossary of words ideas that I want to roughly remember after one week of writing this:
// - Grammar: An academic formalism to express/generate valid sentences that can be formed in a language
// (Introduced by Noam Chomsky a guy studying linguistics, remember that he was mentioned in Epstein files as mnemonic)
//      - Formally is defined as a 4-tuple G = (N, E, P, S)
//          - N: Finite set of 'Non Terminals' symbols, e.g. for english {Noun, Verb, Pronouns, Adverbs}.
//          - E: Finite set of 'Terminals' symbols, e.g. the actual words used in a sentence.
//          - P: A finite set of production rules that define how symbols can be substituted.
//          - S: A special Non-Terminal symbol where sentence generation begins.
//      - Chomsky proposed 4 types
//          - Type 3 (Regular Grammars), 2 (Context Free Grammars), 1 (Context Sensitive Grammars) and 0 (Unrestricted grammars)
// - Automata Theory: Is the study of "abstract machines" known as automata, these are classified depending on the types of formal grammars (defined above)
// that they can recognize.
//      - Can be classified in 4 types:
//          - Finite Automaton, Pushdown Automaton, Linear Bounded Automaton, Turing Machine
// - Parser: Parsers are the "engineering" implementation of an Automaton (Automaton belongs to the "infinite" and "magical" world of maths),
// they are classified depending on the Automaton they "inspire from"  
// - BNF: A syntax invented in the fifties to define grammars in programming languages, first used by Algol60
// - LL and LR parsers:
//      - First letter says read direction of the parser, so both cases read left-to-right
//      - Second letter says how the AST is built (Leftmost derivation) or (Rightmost derivation)
//          - Leftmost: Replace first the Non-Terminal that is closest to the left
//              - "Easy" to implement by hand, e.g. Recursive Descent Parser done in this tutorial
//          - Rightmost: Replace first the Non-Terminal that is closest to the right
//              - These are "hard" to program, see Yacc and Bison

#include <llvm/IR/Value.h>
#include <iostream>
#include <cassert>
#include <map>
#include <memory>
#include <variant>
#include <vector>

/////////////////////////////////////////////
////////////// Lexer (Scanner) //////////////
/////////////////////////////////////////////

// The lexer returns tokens [0-255] if it is an unknown character, otherwise one
// of these for known things.
enum class TokenType : char
{
    EndOfFile = 0,

    // Command
    Definition,
    Extern,

    // Primary
    Identifier,
    Number
};

static std::string IdentifierStr; // Filled in if Token::Identifier
static double NumVal; // // Filled in if Token::Number
using TokenOrAsciiCharacter = std::variant<TokenType/* Token */, char /* Character as ascii value, used for example if char is a + or - */>;

static bool IsTokenChar(const TokenOrAsciiCharacter& Token)
{
    return std::holds_alternative<char>(Token);
}

static bool IsTokenType(const TokenOrAsciiCharacter& Token)
{
    return std::holds_alternative<TokenType>(Token);
}

static bool IsToken(const TokenOrAsciiCharacter& Token, const char Character)
{
    const char* Found = std::get_if<char>(&Token);
    return Found && *Found == Character;
}

static bool IsToken(const TokenOrAsciiCharacter& Token, const TokenType InTokenType)
{
    const TokenType* Found = std::get_if<TokenType>(&Token);
    return Found && *Found == InTokenType;
}

static char GetNextChar()
{
    const int Character = getchar();
    //assert(Character >= 0 && Character < 128 && "Provided character is not an ascii character");
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
            return TokenType::Definition;
        }

        if (IdentifierStr == "extern")
        {
            return TokenType::Extern;    
        }

        return TokenType::Identifier;
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
        return TokenType::Number;
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
        return TokenType::EndOfFile;
    }

    // Otherwise, just return the character as its ascii value.
    char ThisChar = LastChar;
    LastChar = GetNextChar();
    return ThisChar;
}

// Provide a simple token buffer.  CurrentToken is the current
// token the parser is looking at.  AdvanceToken reads another token from the
// lexer and updates CurrentToken with its results.
static TokenOrAsciiCharacter CurrentToken;
static TokenOrAsciiCharacter AdvanceToNextToken()
{
    return CurrentToken = GetNextToken();
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

// Expression class representing a binary operation like a + b
class BinaryExpressionAST final : public ExpressionAST
{
    char Operator;
    std::unique_ptr<ExpressionAST> LeftHandSide, RightHandSide;

public:
    explicit BinaryExpressionAST(
        const char InOperator,
        std::unique_ptr<ExpressionAST>&& InLeftHandSide,
        std::unique_ptr<ExpressionAST>&& InRightHandSide)
        : Operator(InOperator)
        , LeftHandSide(std::move(InLeftHandSide))
        , RightHandSide(std::move(InRightHandSide))
    {}
};

// Expression class representing a function invocation 
class CallExpressionAST final : public ExpressionAST
{
    std::string Callee; // Name of the function being called / invoked
    std::vector<std::unique_ptr<ExpressionAST>> Arguments;

public:
    CallExpressionAST(
        std::string&& InCallee,
        std::vector<std::unique_ptr<ExpressionAST>>&& InArguments)
        : Callee(std::move(InCallee))
        , Arguments(std::move(InArguments))
    {}
};


/////////////////////////////////////////////
//////// Abstract Syntax Tree types /////////
/////////////////////////////////////////////
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
        std::string&& InName,
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

/////////////////////////////////////////////
////////////////// Parser ///////////////////
/////////////////////////////////////////////

// These are little helper functions for error handling.
// NonTerminal follow a PascalCase notation
// Terminals are camelCase

static std::unique_ptr<ExpressionAST> ParsePrimaryExpression();
static std::unique_ptr<ExpressionAST> ParseExpression();
static std::unique_ptr<ExpressionAST> ParsePrimaryExpression();
static std::unique_ptr<ExpressionAST> ParseBinaryOperationRHS(int ExpressionPrecedence, std::unique_ptr<ExpressionAST> LeftHandSide);

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
static std::unique_ptr<ExpressionAST> ParseNumberExpr()
{
    // Parse number, assumes that the current token from the lexer is a number
    auto Node = std::make_unique<NumberExpressionAST>(NumVal);
    AdvanceToNextToken(); // consume the number
    return std::move(Node);
}

// ParenthesisExpr ::= '(' Expression ')'
static std::unique_ptr<ExpressionAST> ParseParenthesisExpr()
{
    AdvanceToNextToken(); // eat (.
    auto Value = ParseExpression();
    if (!Value)
    {
        return nullptr;
    }

    if (!IsToken(CurrentToken, ')'))
    {
        return LogError("expected ')'");
    }

    AdvanceToNextToken(); // eat ).
    return Value;
}

// IdentifierExpr
//   ::= identifier
//   ::= identifier '(' Expression* ')'
static std::unique_ptr<ExpressionAST> ParseIdentifierExpr()
{
    std::string NameId = IdentifierStr;
    AdvanceToNextToken(); // Eat the identifier
    if (!IsToken(CurrentToken, '(')) // Simple variable reference
    {
        return std::make_unique<VariableExpressionAST>(std::move(NameId));
    }

    // Parenthesis after identifier, this is a function call
    AdvanceToNextToken(); // eat the '('

    // Process the parenthesis
    std::vector<std::unique_ptr<ExpressionAST>> Args;
    if (!IsToken(CurrentToken, ')'))
    {
        while (true)
        {
            if (auto Arg = ParseExpression())
            {
                Args.push_back(std::move(Arg));
            }
            else
            {
                return nullptr;            
            }

            // Found the final parenthesis
            if (IsToken(CurrentToken, ')'))
            {
                break;
            }

            if (!IsToken(CurrentToken, ','))
            {
                return LogError("Expected ')' or ',' in argument list");
            }

            AdvanceToNextToken();
        }
    }

    // Eat the ')'
    AdvanceToNextToken();
    return std::make_unique<CallExpressionAST>(std::move(NameId), std::move(Args));
}

// BinaryOpPrecedence - This holds the precedence for each binary operator that is  defined.
static const std::map<char, int> BinaryOperatorPrecedence
{
    { '<', 10 },
    { '+', 20 },
    { '-', 20 },
    { '*', 30 },
    { '/', 30 }
};

static int GetTokenPrecedence()
{
    if (!IsTokenChar(CurrentToken)) // When token is a known TokenType, precedence is ignored
    {
        return -1;
    }

    const auto Found = BinaryOperatorPrecedence.find(std::get<char>(CurrentToken));
    return Found == BinaryOperatorPrecedence.end()
        ? -1
        : Found->second;
}

// Operator precedence parsing

// Expression
//   ::= PrimaryExpression BinaryOperationRHS
static std::unique_ptr<ExpressionAST> ParseExpression()
{
    auto LeftHandSide = ParsePrimaryExpression();

    // No expression, we just return null
    if (!LeftHandSide)
    {
        return nullptr;
    }

    return ParseBinaryOperationRHS(0, std::move(LeftHandSide));
}

// BinaryOperationRHS
//   ::= ('+' Primary)*
static std::unique_ptr<ExpressionAST> ParseBinaryOperationRHS(int ExpressionPrecedence, std::unique_ptr<ExpressionAST> LeftHandSide)
{
    // If this is a BinaryOperator, find its precedence.
    while (true)
    {
        const int TokenPrecedence = GetTokenPrecedence();

        // If this is a BinaryOperator that binds at least as tightly as the current BinaryOperator,
        // consume it, otherwise we are done.
        if (TokenPrecedence < ExpressionPrecedence)
        {
            return LeftHandSide;
        }

        if (!IsTokenChar(CurrentToken))
        {
            return nullptr;
        }
        
        // Okay, we know this is a BinaryOperator.
        char BinaryOp = std::get<char>(CurrentToken);
        AdvanceToNextToken();  // eat BinaryOperator

        // Parse the primary Expression after the BinaryOperator.
        auto RightHandSide = ParsePrimaryExpression();
        if (!RightHandSide)
        {
            return nullptr;
        }

        // If BinaryOperator binds less tightly with RHS than the operator after RHS, let
        // the pending operator take RHS as its LHS.
        int NextPrecedence = GetTokenPrecedence();
        if (TokenPrecedence < NextPrecedence)
        {
            RightHandSide = ParseBinaryOperationRHS(
                TokenPrecedence + 1,
                std::move(RightHandSide));
            if (!RightHandSide)
            {
                return nullptr;
            }
        }

        // Merge LHS/RHS.
        LeftHandSide = std::make_unique<BinaryExpressionAST>(
            BinaryOp,
            std::move(LeftHandSide),
            std::move(RightHandSide));
    }
}

// PrimaryExpression
//   ::= IdentifierExpr
//   ::= NumberExpr
//   ::= ParenthesisExpr
static std::unique_ptr<ExpressionAST> ParsePrimaryExpression()
{
    if (IsToken(CurrentToken, TokenType::Identifier))
    {
        return ParseIdentifierExpr();
    }
    
    if (IsToken(CurrentToken, TokenType::Number))
    {
        return ParseNumberExpr();
    }
    
    if (IsToken(CurrentToken, '('))
    {
        return ParseParenthesisExpr();
    }

    return LogError("unknown token when expecting an expression");
}

// Prototype
//   ::= id '(' id* ')'
static std::unique_ptr<PrototypeAST> ParsePrototype()
{
    if (IsToken(CurrentToken, TokenType::Identifier))
    {
        return LogErrorP("Expected function name in prototype");
    }

    std::string FnName = std::move(IdentifierStr);
    AdvanceToNextToken();

    if (!IsToken(CurrentToken, '('))
    {
        return LogErrorP("Expected '(' in prototype");
    }

    // Read the list of argument names.
    std::vector<std::string> ArgNames;
    while (IsToken(AdvanceToNextToken(), TokenType::Identifier))
    {
        ArgNames.push_back(std::move(IdentifierStr));
    }
    
    if (!IsToken(CurrentToken, ')'))
    {
        return LogErrorP("Expected ')' in prototype");
    }

    // Success.
    AdvanceToNextToken();  // eat ')'.

    return std::make_unique<PrototypeAST>(std::move(FnName), std::move(ArgNames));
}

/// Definition ::= 'def' prototype expression
static std::unique_ptr<FunctionAST> ParseDefinition()
{
    AdvanceToNextToken(); // eat def.

    auto Proto = ParsePrototype();
    if (!Proto)
    {
        return nullptr;
    }

    if (auto Expression = ParseExpression())
    {
        return std::make_unique<FunctionAST>(std::move(Proto), std::move(Expression));
    }

    return nullptr;
}

// External ::= 'extern' prototype
static std::unique_ptr<PrototypeAST> ParseExtern()
{
    AdvanceToNextToken();  // eat extern.
    return ParsePrototype();
}

// TopLevelExpr ::= Expression
static std::unique_ptr<FunctionAST> ParseTopLevelExpr()
{
    if (auto Expression = ParseExpression())
    {
        // Make an anonymous proto.
        auto Proto = std::make_unique<PrototypeAST>("__anon_expr", std::vector<std::string>());
        return std::make_unique<FunctionAST>(std::move(Proto), std::move(Expression));
    }
    return nullptr;
}

/// Top ::= Definition | External | Expression | ';'
static void MainLoop()
{
    while (true)
    {
        fprintf(stdout, "ready>\n");
        fflush(stdout);
    
        AdvanceToNextToken();

        if (IsToken(CurrentToken, TokenType::EndOfFile))
        {
            return;
        }

        if (IsToken(CurrentToken, ';'))
        {
            AdvanceToNextToken();
            continue;
        }

        if (IsToken(CurrentToken, TokenType::Definition))
        {
            ParseDefinition();
            continue;
        }
        
        if (IsToken(CurrentToken, TokenType::Extern))
        {
            ParseExtern();
            continue;
        }

        ParseTopLevelExpr();
    }
}

int main()
{
    MainLoop();
    return 0;
}
