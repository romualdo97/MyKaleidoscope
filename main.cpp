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

#include <llvm/Bitcode/BitcodeWriter.h>
#include <llvm/Bitcode/BitcodeReader.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Support/Error.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Value.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/PassManager.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Passes/StandardInstrumentations.h>
#include <llvm/Transforms/InstCombine/InstCombine.h>
#include <llvm/Transforms/Scalar/Reassociate.h>
#include <llvm/Transforms/Scalar/GVN.h>
#include "llvm/Transforms/Utils/Cloning.h"
#include <llvm/Transforms/Scalar/SimplifyCFG.h>
#include <llvm/Analysis/LoopAnalysisManager.h>
#include <llvm/Analysis/CGSCCPassManager.h>
#include <iostream>
#include <cassert>
#include <map>
#include <memory>
#include <variant>
#include <vector>
#include <filesystem>
#include <fstream>
#include "KaleidoscopeJIT.h"

/////////////////////////////////////////////
////////////// LLVM related //////////////
/////////////////////////////////////////////

static std::ifstream SourceCode;
static std::unique_ptr<llvm::LLVMContext> TheContext;
static std::unique_ptr<llvm::IRBuilder<>> Builder;
static std::unique_ptr<llvm::Module> TheModule;
static std::map<std::string, llvm::Value*> NamedValues;

static std::unique_ptr<llvm::LoopAnalysisManager> TheLoopAnalysisManager;
static std::unique_ptr<llvm::FunctionAnalysisManager> TheFunctionAnalysisManager;
static std::unique_ptr<llvm::CGSCCAnalysisManager> TheCallGraphAnalysisManager;
static std::unique_ptr<llvm::ModuleAnalysisManager> TheModuleAnalysisManager;
static std::unique_ptr<llvm::FunctionPassManager> TheFunctionPassManager;

// For logging
static std::unique_ptr<llvm::PassInstrumentationCallbacks> ThePassInstrumentationCallback;
static std::unique_ptr<llvm::StandardInstrumentations> TheStandardInstrumentation;

// Define a global wrapper instance
static llvm::ExitOnError ExitOnErr;

static std::unique_ptr<llvm::orc::KaleidoscopeJIT> TheJIT;
static std::map<std::string, std::unique_ptr<class PrototypeAST>> FunctionProtos; // This was intended for having multiple re-definitions of the function but is no longer valid technique as per the tutorial, removing as it just ocmplicates the cod

// This holds the precedence for each binary operator that is pre-defined by the language.
static std::map<char, int> BinaryOperatorPrecedence
{
    { '<', 10 },
    { '+', 20 },
    { '-', 20 },
    { '*', 30 },
    { '/', 30 }
};

bool bWaitForNewExpression = false;

/////////////////////////////////////////////
////////////// Lexer (Scanner) //////////////
/////////////////////////////////////////////

/// The lexer returns tokens [0-255] if it is an unknown character, otherwise one
/// of these for known things.
enum class TokenType : char
{
    EndOfFile = 0,

    // Command
    Definition,
    Extern,

    // Primary
    Identifier,
    Number,

    // Conditional
    If,
    Then,
    Else,

    // Loop
    For,
    In,

    // User defined operators
    UnaryOperator,
    BinaryOperator,
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
    static size_t Index = -1;
    static std::string Line;

    // Get the full line instantly
    if (Index == -1)
    {
        std::getline(std::cin, Line);
        Index = 0;
    }

    // Ask for the next line
    if (Index >= Line.size())
    {
        Index = -1;
        Line.clear();
        return EOF;
    }

    return Line[Index++];

    // Romu> Not using this from tutorial as I don't want to explicitly pass the EOF character, this in order to behave more like a REPL
    //const int Character = getchar();
    //assert(Character >= 0 && Character < 128 && "Provided character is not an ascii character");
    //return static_cast<char>(Character);
}

/// Return the next token from standard input.
static TokenOrAsciiCharacter GetNextToken() // Lexer
{
    // Find next not empty space
    static char LastChar = ' ';
    while (isspace(LastChar) || bWaitForNewExpression)
    {
        bWaitForNewExpression = false;
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

        if (IdentifierStr == "if")
        {
            return TokenType::If;
        }

        if (IdentifierStr == "then")
        {
            return TokenType::Then;
        }

        if (IdentifierStr == "else")
        {
            return TokenType::Else;
        }

        if (IdentifierStr == "for")
        {
            return TokenType::For;
        }

        if (IdentifierStr == "in")
        {
            return TokenType::In;
        }

        if (IdentifierStr == "unary")
        {
            return TokenType::UnaryOperator;
        }

        if (IdentifierStr == "binary")
        {
            return TokenType::BinaryOperator;
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

/// Provide a simple token buffer.  CurrentToken is the current
/// token the parser is looking at.  AdvanceToken reads another token from the
/// lexer and updates CurrentToken with its results.
static TokenOrAsciiCharacter CurrentToken;
static TokenOrAsciiCharacter AdvanceToNextToken()
{
    return CurrentToken = GetNextToken();
}

/////////////////////////////////////////////
//////// Abstract Syntax Tree tpes //////////
/////////////////////////////////////////////

llvm::Value* LogErrorV(const char* Str);

/// Base class for all expression nodes.
class ExpressionAST
{
public:
    virtual ~ExpressionAST() = default;
    virtual llvm::Value* CodeGen() = 0; // Generates IR for LLVM, see: https://llvm.org/doxygen/classllvm_1_1Value.html
};

/// Expression class for numeric literals like "1.0".
class NumberExpressionAST final : public ExpressionAST
{
    double Value;
public:
    explicit NumberExpressionAST(const double InValue)
        : Value(InValue)
    {}

    llvm::Value* CodeGen() override
    {
        return llvm::ConstantFP::get(*TheContext, llvm::APFloat(Value));;
    }
};

/// Expression class for referencing a variable, like "a".
class VariableExpressionAST final : public ExpressionAST
{
    std::string Name;

    public:
        explicit VariableExpressionAST(std::string&& InValue)
            : Name(std::move(InValue))
    {}

    llvm::Value* CodeGen() override
    {
        // Look this variable up in the function.
        if (const auto Found = NamedValues.find(Name);
            Found != NamedValues.end())
        {
            return Found->second;
        }
        return LogErrorV("Unknown variable name");
    }
};

llvm::Function* GetFunction(const std::string& Name);

/// Expression class representing a binary operation like a + b
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
    
    llvm::Value* CodeGen() override
    {
        llvm::Value* Left = LeftHandSide->CodeGen();
        llvm::Value* Right = RightHandSide->CodeGen();
        if (!Left || !Right)
        {
            return nullptr;
        }

        switch (Operator) {
        case '+':
            return Builder->CreateFAdd(Left, Right, "AddTmp");
        case '-':
            return Builder->CreateFSub(Left, Right, "SubTmp");
        case '*':
            return Builder->CreateFMul(Left, Right, "MulTmp");
        case '/':
            return Builder->CreateFDiv(Left, Right, "DivTmp");
        case '<':
            Left = Builder->CreateFCmpULT(Left, Right, "CmpTmp");
            // Convert bool 0/1 to double 0.0 or 1.0
            return Builder->CreateUIToFP(
                Left,
                llvm::Type::getDoubleTy(*TheContext),
                "BoolTmp");
        default:
            break;
        }

        // If it wasn't a builtin binary operator, it must be a user defined one. Emit
        // a call to it.
        llvm::Function *Function = GetFunction(std::string("UserDefined_BinaryOperator_") + Operator);
        assert(Function && "binary operator not found!");

        llvm::Value* Operands[2] = { Left, Right };
        return Builder->CreateCall(Function, Operands, "CallUserBinaryOperator");
    }
};

//  Expression class for a unary operator.
class UnaryExpressionAST final : public ExpressionAST
{
    char Operator;
    std::unique_ptr<ExpressionAST> Operand;

public:
    UnaryExpressionAST(
        const char InOperator,
        std::unique_ptr<ExpressionAST> InOperand) :
        Operator(InOperator),
        Operand(std::move(InOperand)) {}

    llvm::Value *CodeGen() override
    {
        llvm::Value *OperandV = Operand->CodeGen();
        if (!OperandV)
        {
            return nullptr;
        }

        llvm::Function* Function = GetFunction(std::string("UserDefined_UnaryOperator_") + Operator);
        if (!Function)
        {
            return LogErrorV("Unknown unary operator");
        }

        return Builder->CreateCall(Function, OperandV, "CallUserUnaryOperator");
    }
};

/// Expression class representing a function invocation 
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
    
    llvm::Value* CodeGen() override
    {
        // Each function it's in their own module, find last module where a fun was defined
        llvm::Function* CalleeF = GetFunction(Callee);
        
        // Look up the name in the global module table.
        // llvm::Function *CalleeF = TheModule->getFunction(Callee);

        if (!CalleeF)
        {
            return LogErrorV("Unknown function referenced");
        }

        // If argument mismatch error.
        if (CalleeF->arg_size() != Arguments.size())
        {
            return LogErrorV("Incorrect # arguments passed");
        }

        std::vector<llvm::Value*> ArgsV;
        for (const auto& Argument : Arguments)
        {
            ArgsV.push_back(Argument->CodeGen());
            if (!ArgsV.back())
            {
                return nullptr;
            }
        }

        return Builder->CreateCall(CalleeF, ArgsV, "CallTmp");
    }
};

/// Expression class for if/then/else.
class IfExprAST : public ExpressionAST {
    std::unique_ptr<ExpressionAST> Cond, Then, Else;

public:
    IfExprAST(
        std::unique_ptr<ExpressionAST> Cond,
        std::unique_ptr<ExpressionAST> Then,
        std::unique_ptr<ExpressionAST> Else) :
        Cond(std::move(Cond)),
        Then(std::move(Then)),
        Else(std::move(Else))
    {}

    llvm::Value* CodeGen() override
    {
        llvm::Value *ConditionValue = Cond->CodeGen();
        if (!ConditionValue)
        {
            return nullptr;
        }

        // Convert condition to a bool by comparing non-equal to 0.0.
        ConditionValue = Builder->CreateFCmpONE(
            /*LHS*/ ConditionValue,
            /*RHS*/ llvm::ConstantFP::get(
                *TheContext,
                llvm::APFloat(0.0)),
                "IfCondition");

        llvm::Function* TheFunction = Builder->GetInsertBlock()->getParent();

        // Create blocks for the then and else cases.  Insert the 'then' block at the
        // end of the function.
        llvm::BasicBlock *ThenBlock = llvm::BasicBlock::Create(*TheContext, "Then", TheFunction);
        llvm::BasicBlock *ElseBlock = llvm::BasicBlock::Create(*TheContext, "Else");
        llvm::BasicBlock *MergeBlock = llvm::BasicBlock::Create(*TheContext, "IfContinuation");

        Builder->CreateCondBr(ConditionValue, ThenBlock, ElseBlock);

        // Emit then value.
        Builder->SetInsertPoint(ThenBlock);

        llvm::Value* ThenValue = Then->CodeGen();
        if (!ThenValue)
        {
            return nullptr;
        }

        Builder->CreateBr(MergeBlock);
        
        // Codegen of 'Then' can change the current block, update ThenBlock for the PHI.
        ThenBlock = Builder->GetInsertBlock();

        // Emit else block.
        TheFunction->insert(TheFunction->end(), ElseBlock);
        Builder->SetInsertPoint(ElseBlock);

        llvm::Value* ElseValue = Else->CodeGen();
        if (!ElseValue)
        {
            return nullptr;
        }

        Builder->CreateBr(MergeBlock);

        // Codegen of 'Else' can change the current block, update ElseBB for the PHI.
        ElseBlock = Builder->GetInsertBlock();
        
        // Emit merge block.
        TheFunction->insert(TheFunction->end(), MergeBlock);
        Builder->SetInsertPoint(MergeBlock);
        llvm::PHINode* PhiBlock =
          Builder->CreatePHI(
              llvm::Type::getDoubleTy(
                  *TheContext),
                  2,
                  "IfTmp");

        PhiBlock->addIncoming(ThenValue, ThenBlock);
        PhiBlock->addIncoming(ElseValue, ElseBlock);
        return PhiBlock;
    }
};

/**
 * Expression class representing for/in loops
 */
class ForExprAST final : public ExpressionAST {
    std::string VarName;
    std::unique_ptr<ExpressionAST> Start, End, Step, Body;

public:
    ForExprAST(
        std::string&& VarName,
        std::unique_ptr<ExpressionAST> Start,
        std::unique_ptr<ExpressionAST> End,
        std::unique_ptr<ExpressionAST> Step,
        std::unique_ptr<ExpressionAST> Body) :
        VarName(std::move(VarName)),
        Start(std::move(Start)),
        End(std::move(End)),
        Step(std::move(Step)),
        Body(std::move(Body))
    {}

    llvm::Value *CodeGen() override
    {
        // Emit the start code first, without 'variable' in scope.
        llvm::Value* StartVal = Start->CodeGen();
        if (!StartVal)
        {
            return nullptr;
        }

        // Make the new basic block for the loop header, inserting after current
        // block.
        llvm::Function* TheFunction = Builder->GetInsertBlock()->getParent();
        llvm::BasicBlock* PreheaderBlock = Builder->GetInsertBlock();
        llvm::BasicBlock* LoopBlock = llvm::BasicBlock::Create(
            *TheContext,
            "Loop",
            TheFunction);

        // Insert an explicit fall through from the current block to the LoopBB.
        Builder->CreateBr(LoopBlock);

        // Start insertion in LoopBB.
        Builder->SetInsertPoint(LoopBlock);

        // Start the PHI node with an entry for Start.
        llvm::PHINode* Variable = Builder->CreatePHI(
            llvm::Type::getDoubleTy(*TheContext),
            2,
            VarName);
        Variable->addIncoming(StartVal, PreheaderBlock);

        // Within the loop, the variable is defined equal to the PHI node.  If it
        // shadows an existing variable, we have to restore it, so save it now.
        llvm::Value* OldVal = NamedValues[VarName];
        NamedValues[VarName] = Variable;

        // Emit the body of the loop. This, like any other expr, can change the
        // current BB.  Note that we ignore the value computed by the body, but don't
        // allow an error.
        if (!Body->CodeGen())
        {
            return nullptr;
        }
        
        // Emit the step value.
        llvm::Value* StepVal = nullptr;
        if (Step)
        {
            StepVal = Step->CodeGen();
            if (!StepVal)
            {
                return nullptr;
            }
        }
        else
        {
            // If not specified, use 1.0.
            StepVal = llvm::ConstantFP::get(
                *TheContext,
                llvm::APFloat(1.0));
        }

        llvm::Value* NextVar = Builder->CreateFAdd(
            Variable,
            StepVal,
            "NextVar");

        // Compute the end condition.
        llvm::Value* EndCond = End->CodeGen();
        if (!EndCond)
        {
            return nullptr;
        }

        // Convert condition to a bool by comparing non-equal to 0.0.
        EndCond = Builder->CreateFCmpONE(
            EndCond,
            llvm::ConstantFP::get(
                *TheContext,
                llvm::APFloat(0.0)),
            "CoopCond");

        // Create the "after loop" block and insert it.
        llvm::BasicBlock* LoopEndBlock = Builder->GetInsertBlock();
        llvm::BasicBlock* AfterBlock =
            llvm::BasicBlock::Create(
                *TheContext,
                "AfterLoop",
                TheFunction);

        // Insert the conditional branch into the end of LoopEndBB.
        Builder->CreateCondBr(EndCond, LoopBlock, AfterBlock);

        // Any new code will be inserted in AfterBB.
        Builder->SetInsertPoint(AfterBlock);

        // Add a new entry to the PHI node for the back-edge.
        Variable->addIncoming(NextVar, LoopEndBlock);

        // Restore the unshadowed variable.
        if (OldVal)
        {
            NamedValues[VarName] = OldVal;
        }
        else
        {
            NamedValues.erase(VarName);
        }

        // for expr always returns 0.0.
        return llvm::Constant::getNullValue(llvm::Type::getDoubleTy(*TheContext));
    }
};

/**
 * This class represents the "prototype" for a function,
 * which captures its name, and its argument names (thus implicitly the number
 * of arguments the function takes).
 *
 * as well as if it is an operator.
 */
class PrototypeAST
{
    std::string Name;
    std::vector<std::string> Args;
    bool IsOperator;
    unsigned Precedence;  // Precedence if a binary op.
    
public:
    PrototypeAST(
        std::string&& InName,
        std::vector<std::string> InArgs,
        bool InIsOperator,
        unsigned InPrecedence)
        : Name(std::move(InName))
        , Args(std::move(InArgs))
        , IsOperator(InIsOperator)
        , Precedence(InPrecedence) {}

    [[nodiscard]] const std::string& GetName() const { return Name; }

    [[nodiscard]] bool IsUnaryOp() const { return IsOperator && Args.size() == 1; }
    [[nodiscard]] bool IsBinaryOp() const { return IsOperator && Args.size() == 2; }

    [[nodiscard]] unsigned GetBinaryOperatorPrecedence() const
    {
        assert(IsBinaryOp());
        return Precedence;
    }
    
    [[nodiscard]] char GetOperatorName() const
    {
        assert(IsUnaryOp() || IsBinaryOp());
        return Name[Name.size() - 1];
    }
    
    [[nodiscard]] llvm::Function* CodeGen() const
    {
        // Make the function type:  double(double,double) etc.
        std::vector<llvm::Type*> Doubles(
            Args.size(),
            llvm::Type::getDoubleTy(*TheContext));
        
        llvm::FunctionType* FunctionType =
            llvm::FunctionType::get(
                llvm::Type::getDoubleTy(*TheContext),
                Doubles,
                false);

        llvm::Function* Function =
            llvm::Function::Create(
                FunctionType,
                llvm::Function::ExternalLinkage,
                Name,
                TheModule.get());

        // Set names for all arguments.
        unsigned Idx = 0;
        for (auto& Arg : Function->args())
        {
            Arg.setName(Args[Idx++]);
        }

        return Function;
    }
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

    [[nodiscard]] llvm::Function* CodeGen() const
    {        
        // First, check for an existing function from a previous 'extern' declaration.
        llvm::Function* TheFunction = TheModule->getFunction(Proto->GetName());
        if (!TheFunction)
        {
            TheFunction = Proto->CodeGen();
        }

        if (!TheFunction->empty())
        {
            fprintf(stdout, "%s\n", "Function previously defined, re-defining....");
            fflush(stdout);
            TheFunction->eraseFromParent();
            // Re-generating so we allow parameter change, if we only invoked TheFunction->deleteBody()
            // then function redefinition should keep the same parameter count and respective names  (aka function prototype)
            TheFunction = Proto->CodeGen();  
        }

        // If this is an operator, install it.
        if (Proto->IsBinaryOp())
        {
            BinaryOperatorPrecedence[Proto->GetOperatorName()] = Proto->GetBinaryOperatorPrecedence();
        }

        // Create a new basic block to start insertion into.
        llvm::BasicBlock* BasicBlock = llvm::BasicBlock::Create(
            *TheContext,
            "Entry",
            TheFunction);
        Builder->SetInsertPoint(BasicBlock);

        // Record the function arguments in the NamedValues map.
        NamedValues.clear();
        for (auto& Arg : TheFunction->args())
        {
            NamedValues[std::string(Arg.getName())] = &Arg;
        }

        if (llvm::Value* RetVal = Body->CodeGen())
        {
            // Finish off the function.
            Builder->CreateRet(RetVal);

            // Validate the generated code, checking for consistency.
            // VerifyFunction(*TheFunction);

            // Optimize the generated IL code
            TheFunctionPassManager->run(*TheFunction, *TheFunctionAnalysisManager);

            return TheFunction;
        }

        // Error reading body, remove function.
        TheFunction->eraseFromParent();
        return nullptr;
    }
};

llvm::Function* GetFunction(const std::string& Name)
{
    // First, see if the function has already been added to the current module.
    if (auto* Function = TheModule->getFunction(Name))
        return Function;

    // If not, check whether we can codegen the declaration from some existing
    // prototype.
    if (const auto FunctionPrototype = FunctionProtos.find(Name);
        FunctionPrototype != FunctionProtos.end())
        return FunctionPrototype->second->CodeGen();

    // If no existing prototype exists, return null.
    return nullptr;
}

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

llvm::Value* LogErrorV(const char* Str)
{
    LogError(Str);
    return nullptr;
}

/////////////////////////////////////////////
////////////////// Parser ///////////////////
/////////////////////////////////////////////

// These are little helper functions for error handling.
// NonTerminal follow a PascalCase notation
// Terminals are camelCase

static std::unique_ptr<ExpressionAST> ParsePrimaryExpression();
static std::unique_ptr<ExpressionAST> ParseUnaryExpression();
static std::unique_ptr<ExpressionAST> ParseExpression();
static std::unique_ptr<ExpressionAST> ParseBinaryOperationRHS(int ExpressionPrecedence, std::unique_ptr<ExpressionAST> LeftHandSide);

// NumberExpr ::= number

/**
 * Romu> Extract the number of the current token
 * NumberExpr ::= number
 */
static std::unique_ptr<ExpressionAST> ParseNumberExpr()
{
    // Parse number, assumes that the current token from the lexer is a number
    auto Node = std::make_unique<NumberExpressionAST>(NumVal);
    AdvanceToNextToken(); // consume the number
    return std::move(Node);
}

/**
 * Romu> Represents an Expression wrapped by parenthesis, for example `(GetFooValue() + 7 * 2)`
 * ParenthesisExpr ::= '(' Expression ')'
 */
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

/**
 * Romu> Represents the name of a variable or a function call
 * IdentifierExpr
 *     ::= identifier
 *     ::= identifier '(' Expression* ')'
 */
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

/**
 * Romu> Represents an if/then/else expression
 * IfExpr ::= 'if' expression 'then' expression 'else' expression
 */
static std::unique_ptr<ExpressionAST> ParseIfExpr()
{
    AdvanceToNextToken(); // Eat the `if`
    auto Condition = ParseExpression();
    if (!Condition)
    {
        return nullptr;
    }

    if (!IsToken(CurrentToken, TokenType::Then))
    {
        return LogError("Expected then");
    }
    AdvanceToNextToken(); // Eat the `then`

    auto Then = ParseExpression(); 
    if (!Then)
    {
        return nullptr;        
    }
    
    if (!IsToken(CurrentToken, TokenType::Else))
    {
        return LogError("Expected then");
    }
    AdvanceToNextToken(); // Eat the `else`

    auto Else = ParseExpression();
    if (!Else)
    {
        return nullptr;
    }
    
    return std::make_unique<IfExprAST>(std::move(Condition), std::move(Then), std::move(Else));
}

/**
 * Romu> A for loop formed by three parts `StartBlock; StepBlock in EndBlock`, the formal grammar is defined as:
 * ForExpr ::= 'for' identifier '=' Expression ';' Expression (',' Expression)? 'in' Expression
 */
static std::unique_ptr<ExpressionAST> ParseForExpr()
{
    AdvanceToNextToken(); // Eat the for.

    if (!IsToken(CurrentToken, TokenType::Identifier))
    {
        return LogError("Expected identifier after `for`");
    }

    std::string IdName = IdentifierStr;
    AdvanceToNextToken();  // Eat identifier.

    if (!IsToken(CurrentToken, '='))
    {
        return LogError("Expected `=` after `identifier` in `for` loop");
    }
    AdvanceToNextToken(); // Eat `=`

    std::unique_ptr<ExpressionAST> StartExpression = ParseExpression();
    if (!StartExpression)
    {
        return nullptr;
    }

    if (!IsToken(CurrentToken, ';'))
    {
        return LogError("Expected ';' after `for Identifier = Expression` loop");
    }
    AdvanceToNextToken(); // Eat the `;`
    
    std::unique_ptr<ExpressionAST> EndExpression = ParseExpression();
    if (!EndExpression)
    {
        return nullptr;
    }

    // The step value is optional.
    std::unique_ptr<ExpressionAST> StepExpression;
    if (IsToken(CurrentToken, ';'))
    {
        AdvanceToNextToken(); // Eat the ';'
        StepExpression = ParseExpression();
        if (!StepExpression)
        {
            return nullptr;
        }
    }

    if (!IsToken(CurrentToken, TokenType::In))
    {
        return LogError("Expected `in` after `for`");
    }
    AdvanceToNextToken(); // Eat the 'in'

    std::unique_ptr<ExpressionAST> BodyExpression = ParseExpression();
    if (!BodyExpression)
    {
        return nullptr;
    }

    return std::make_unique<ForExprAST>(
        std::move(IdName),
        std::move(StartExpression),
        std::move(EndExpression),
        std::move(StepExpression),
        std::move(BodyExpression));
}

// Operator precedence parsing

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

/**
 * Romu> Parses a UnaryExpression (which is a token that can decay into a PrimaryExpression) optionally followed by a BinaryOperationRHS
 * Expression
 *     ::= UnaryExpression BinaryOperationRHS
 */
static std::unique_ptr<ExpressionAST> ParseExpression()
{
    auto LeftHandSide = ParseUnaryExpression();

    // No expression, we just return null
    if (!LeftHandSide)
    {
        return nullptr;
    }

    return ParseBinaryOperationRHS(0, std::move(LeftHandSide));
}

/**
 * Romu> Receives the left hand side operand of a binary operation and check if a binary operator and an Expression is followed, if yes then return the AST node for the binary expression
 * BinaryOperationRHS
 *   ::= ('+' UnaryExpression)*
 */
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
        auto RightHandSide = ParseUnaryExpression(); // UnaryExpression can decay into a PrimaryExpression
        if (!RightHandSide)
        {
            return nullptr;
        }

        // If BinaryOperator binds less tightly with RHS than the operator after RHS, let
        // the pending operator take RHS as its LHS.
        if (const int NextPrecedence = GetTokenPrecedence();
            TokenPrecedence < NextPrecedence)
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

/**
 * Romu> Name of variable or function, a number or an expression wrapped by parenthesis 
 * PrimaryExpression
 *   ::= IdentifierExpr
 *   ::= NumberExpr
 *   ::= ParenthesisExpr
 *   ::= IfExpr
 *   ::= ForExpr
 */
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

    if (IsToken(CurrentToken, TokenType::If))
    {
        return ParseIfExpr();
    }

    if (IsToken(CurrentToken, TokenType::For))
    {
        return ParseForExpr();
    }

    return LogError("Unknown token when expecting an expression");
}

/**
 * Romu> Parses grammar that defines unary operators like ~a where ~ is the operator
 *
 * UnaryExpression
 *
 *      ::= PrimaryExpression
 *
 *      ::= '!' unary
 */
static std::unique_ptr<ExpressionAST> ParseUnaryExpression()
{
    // If the current token is not an operator, it must be a primary expr.
    if (!IsTokenChar(CurrentToken) || IsToken(CurrentToken, '(') || IsToken(CurrentToken, ','))
    {
        return ParsePrimaryExpression(); // This breaks the UnaryExpression recursion
    }

    // If this is a unary operator, read it.
    char Operator = std::get<char>(CurrentToken);
    AdvanceToNextToken();

    // Parse the expression following the unary operator
    if (auto Operand = ParseUnaryExpression())
    {
        return std::make_unique<UnaryExpressionAST>(Operator, std::move(Operand));
    }

    return nullptr;
}

/**
 * Romu> Declares the prototype of the function, this indicates the function name and the name of the parameters
 * 
 * Prototype
 * 
 *     ::= id '(' id* ')'
 *
 *     ::= unary LETTER (id)
 *     
 *     ::= binary LETTER number? (id, id)
 *
 */
static std::unique_ptr<PrototypeAST> ParsePrototype()
{
    std::string FnName;
    unsigned Kind = 0; // 0 = identifier, 1 = unary, 2 = binary.
    unsigned BinaryPrecedence = 30;

    if (IsToken(CurrentToken, TokenType::Identifier))
    {
        FnName = IdentifierStr;
        Kind = 0;
        AdvanceToNextToken();
    }
    else if (IsToken(CurrentToken, TokenType::UnaryOperator))
    {
        AdvanceToNextToken();
        if (!IsTokenChar(CurrentToken))
        {
            return LogErrorP("Expected unary operator");
        }
        
        FnName = "UserDefined_UnaryOperator_";
        FnName += std::get<char>(CurrentToken);
        Kind = 1;
        AdvanceToNextToken();
    }
    else if (IsToken(CurrentToken, TokenType::BinaryOperator))
    {
        AdvanceToNextToken();
        if (!IsTokenChar(CurrentToken))
        {
            return LogErrorP("Expected binary operator");
        }

        FnName = "UserDefined_BinaryOperator_";
        FnName += std::get<char>(CurrentToken);
        Kind = 2;
        AdvanceToNextToken();

        // Read the precedence if present.
        if (IsToken(CurrentToken, TokenType::Number))
        {
            if (NumVal < 1 || NumVal > 100)
            {
                return LogErrorP("Invalid precedence: must be 1..100");
            }

            BinaryPrecedence = static_cast<unsigned>(NumVal);
            AdvanceToNextToken();
        }
    }
    else
    {
        return LogErrorP("Expected function name in prototype");
    }

    if (!IsToken(CurrentToken, '('))
    {
        return LogErrorP("Expected '(' in prototype");        
    }
    
    // Read the list of argument names.
    std::vector<std::string> ArgNames;
    while (IsToken(AdvanceToNextToken(), TokenType::Identifier))
    {
        ArgNames.push_back(std::move(IdentifierStr));

        if (const TokenOrAsciiCharacter NextToken = AdvanceToNextToken();
            IsToken(NextToken, ',') || IsToken(NextToken, ')'))
        {
            if (IsToken(NextToken, ')'))
            {
                break;
            }
        }
        else
        {
            return LogErrorP("Unexpected token after parameter name, expected ',' or ')'");
        }
    }

    // Success: Eat ')'
    AdvanceToNextToken();

    if (Kind && ArgNames.size() != Kind)
    {
        return LogErrorP("Incorrect number of arguments for user defined operator");
    }

    return std::make_unique<PrototypeAST>(std::move(FnName), std::move(ArgNames), /*bInIsOperator*/ Kind != 0, BinaryPrecedence);
}

/**
 * Romu> Used to declare the prototype of a function followed by the function definition (an Expression).
 * Definition
 *    ::= 'def' Prototype Expression
 */
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

/**
 * Romu> Used to declare the prototype of a function that is defined externally (in another module)
 * Definition
 *    ::= 'extern'
 */
static std::unique_ptr<PrototypeAST> ParseExtern()
{
    AdvanceToNextToken();  // eat extern.
    return ParsePrototype();
}

/**
 * Romu> Used to parse instructions that are not contained in a function, the compiler will wrap the Expression inside an anonymous function
 * TopLevelExpr
 *    ::= Expression
 */
static std::unique_ptr<FunctionAST> ParseTopLevelExpr()
{
    if (auto Expression = ParseExpression())
    {
        // Make an anonymous proto.
        auto Proto = std::make_unique<PrototypeAST>("__AnonymousExpression", std::vector<std::string>(), /*bInIsOperator*/ false, /*InPrecedence*/ 1);
        return std::make_unique<FunctionAST>(std::move(Proto), std::move(Expression));
    }
    return nullptr;
}

static void InitializeModuleAndManagers()
{
    // Open a new context and module.
    TheContext = std::make_unique<llvm::LLVMContext>();
    TheModule = std::make_unique<llvm::Module>("My cool jit", *TheContext);
    TheModule->setDataLayout(TheJIT->getDataLayout());

    // Create a new builder for the module.
    Builder = std::make_unique<llvm::IRBuilder<>>(*TheContext);

    // https://llvm.org/docs/tutorial/MyFirstLanguageFrontend/LangImpl04.html
    // https://llvm.org/docs/NewPassManager.html#id2
    // Create the analysis managers.
    // These must be declared in this order so that they are destroyed in the
    // correct order due to inter-analysis-manager references.
    TheLoopAnalysisManager = std::make_unique<llvm::LoopAnalysisManager>();
    TheFunctionAnalysisManager = std::make_unique<llvm::FunctionAnalysisManager>();
    TheCallGraphAnalysisManager = std::make_unique<llvm::CGSCCAnalysisManager>();
    TheModuleAnalysisManager = std::make_unique<llvm::ModuleAnalysisManager>();

    // For logging
    ThePassInstrumentationCallback = std::make_unique<llvm::PassInstrumentationCallbacks>();
    TheStandardInstrumentation = std::make_unique<llvm::StandardInstrumentations>(*TheContext, /*DebugLogging*/ true);
    TheStandardInstrumentation->registerCallbacks(*ThePassInstrumentationCallback, TheModuleAnalysisManager.get());
    
    // Create the new pass manager builder.
    // Take a look at the PassBuilder constructor parameters for more
    // customization, e.g. specifying a TargetMachine or various debugging options.
    llvm::PassBuilder PassBuilder(nullptr, llvm::PipelineTuningOptions()); //, std::nullopt, ThePassInstrumentationCallback.get());

    // Register all the basic analyses with the managers.
    PassBuilder.registerLoopAnalyses(*TheLoopAnalysisManager);
    PassBuilder.registerFunctionAnalyses(*TheFunctionAnalysisManager);
    PassBuilder.registerCGSCCAnalyses(*TheCallGraphAnalysisManager);
    PassBuilder.registerModuleAnalyses(*TheModuleAnalysisManager);
    PassBuilder.crossRegisterProxies(*TheLoopAnalysisManager, *TheFunctionAnalysisManager, *TheCallGraphAnalysisManager, *TheModuleAnalysisManager);
    
    
    // This FunctionPassManager pipeline could have been built from the PassBuilder above
    TheFunctionPassManager = std::make_unique<llvm::FunctionPassManager>();
    
    // Add transform passes.
    // Do simple "peephole" optimizations and bit-twiddling options.
    TheFunctionPassManager->addPass(llvm::InstCombinePass());
    // Reassociate expressions.
    TheFunctionPassManager->addPass(llvm::ReassociatePass());
    // Eliminate Common SubExpressions.
    TheFunctionPassManager->addPass(llvm::GVNPass());
    // Simplify the control flow graph (deleting unreachable blocks, etc...).
    TheFunctionPassManager->addPass(llvm::SimplifyCFGPass());
}

llvm::orc::ThreadSafeModule CreateSafeModuleClone()
{
    // Serialize the module so we can de-serialize it later and generate a new context
    std::string BitcodeBuffer;
    llvm::raw_string_ostream OutputStream(BitcodeBuffer);
    llvm::WriteBitcodeToFile(*TheModule, OutputStream);
    OutputStream.flush(); // Not really needed as raw_string_ostream is just an adapter and memory is managed by std::string

    // Load the serialized buffer
    auto BufferPtr = llvm::MemoryBuffer::getMemBuffer(BitcodeBuffer);
    
    // Parse the loaded buffer and store the new context data into the NewContext
    std::unique_ptr<llvm::LLVMContext> NewContext = std::make_unique<llvm::LLVMContext>();
    std::unique_ptr<llvm::Module> NewModule = ExitOnErr(llvm::parseBitcodeFile(*BufferPtr, *NewContext));    

    return {std::move(NewModule), std::move(NewContext)};
}

/**
 * Romu> The starting symbol of the grammar
 * 
 * Top
 *     ::= Definition | Extern | TopLevelExpr | ';'
 */
static void MainLoop()
{
    while (true)
    {
        fprintf(stdout, "ready>\n");
        fflush(stdout);
    
        AdvanceToNextToken();

        if (IsToken(CurrentToken, TokenType::EndOfFile))
        {
            bWaitForNewExpression = true;
            AdvanceToNextToken();
        }

        if (IsToken(CurrentToken, ';'))
        {
            AdvanceToNextToken();
            continue;
        }

        if (IsToken(CurrentToken, TokenType::Definition))
        {
            if (const auto FunctionDefinitionAST = ParseDefinition())
            {
                FunctionDefinitionAST->CodeGen()->print( llvm::outs());
                llvm::outs().flush();
            }
            continue;
        }
        
        if (IsToken(CurrentToken, TokenType::Extern))
        {
            if (const auto ExternFunctionAST = ParseExtern())
            {
                ExternFunctionAST->CodeGen()->print( llvm::outs());
                llvm::outs().flush();
            }
            continue;
        }

        if (const auto TopLevelExpressionAST = ParseTopLevelExpr())
        {
            if (llvm::Function* Function = TopLevelExpressionAST->CodeGen())
            {
                Function->print( llvm::outs());
                llvm::outs().flush();

                // Create a ResourceTracker to track JIT'd memory allocated to our
                // anonymous expression -- that way we can free it after executing.
                auto ResourceTracker = TheJIT->getMainJITDylib().createResourceTracker();
                ExitOnErr(TheJIT->addModule(CreateSafeModuleClone(), ResourceTracker));

                // Search the JIT for the __anon_expr symbol.
                auto ExprSymbol = ExitOnErr(TheJIT->lookup("__AnonymousExpression"));

                // Get the symbol's address and cast it to the right type (takes no
                // arguments, returns a double) so we can call it as a native function.
                double (*FP)() = ExprSymbol.toPtr<double (*)()>();
                fprintf(stdout, "Evaluated to %f\n", FP());
                fflush(stdout);

                // Delete the anonymous expression module from the JIT.
                ExitOnErr(ResourceTracker->remove());
            }
            
        }
    }
}

#ifdef _WIN32
#define DLLEXPORT __declspec(dllexport)
#else
#define DLLEXPORT
#endif

// that takes a double and returns 0.
/// putchard - putchar that takes a double and returns 0.
extern "C" DLLEXPORT double putchard(double X)
{
    fputc(static_cast<char>(X), stderr);
    return 0;
}

/// printd - printf that takes a double prints it as "%f\n", returning 0.
extern "C" DLLEXPORT double printd(double X)
{
    fprintf(stderr, "%f\n", X);
    return 0;
}

int main()
{
    // Initialize the target registry and code generation components for the host
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();
    llvm::InitializeNativeTargetAsmParser(); // Optional: if you parse inline/target asm
    TheJIT = ExitOnErr(llvm::orc::KaleidoscopeJIT::Create());
    InitializeModuleAndManagers();
    MainLoop();
    return 0;
}
