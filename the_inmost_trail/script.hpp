#ifndef script_hpp
#define script_hpp

#include <deque>
#include <string>
#include <unordered_map>
#include "engine/arctic_types.h"


namespace arctic {

struct ScriptVirtualMachine;

struct ScriptVariable {
  std::string name;
  double Calculate(ScriptVirtualMachine &vm);
  void Let(ScriptVirtualMachine &vm, double value);
};

struct ScriptValue {
  enum Type {
    kTypeVariable,
    kTypeImmediate,
  };
  Type type = kTypeImmediate;
  ScriptVariable variable;
  double immediate = 0.0;
  double Calculate(ScriptVirtualMachine &vm) {
    if (type == kTypeVariable) {
      return variable.Calculate(vm);
    } else {
      return immediate;
    }
  }
};
struct ScriptExpression {
  enum Operation {
    kOpAdd,
    kOpSub,
    kOpMul,
    kOpDiv,
    kOpL,
    kOpLe,
    kOpE,
    kOpGe,
    kOpG,
    kOpNe,
    kOpAnd,
    kOpOr,
    kOpXor
  };
  ScriptValue operand_a;
  Operation operation = kOpAdd;
  ScriptValue operand_b;
  double Calculate(ScriptVirtualMachine &vm) {
    double a = operand_a.Calculate(vm);
    double b = operand_b.Calculate(vm);
    switch (operation) {
      case kOpAdd: return a + b;
      case kOpSub: return a - b;
      case kOpMul: return a * b;
      case kOpDiv: return (b == 0.0 ? 0.0 : a / b);
      case kOpL: return a < b ? 1.0 : 0.0;
      case kOpLe: return a <= b ? 1.0 : 0.0;
      case kOpE: return a == b ? 1.0 : 0.0;
      case kOpGe: return a >= b ? 1.0 : 0.0;
      case kOpG: return a > b ? 1.0 : 0.0;
      case kOpNe: return a != b ? 1.0 : 0.0;
      case kOpAnd: return ((std::abs(a) >= 0.5) && (std::abs(b) >= 0.5)) ? 1.0 : 0.0;
      case kOpOr: return ((std::abs(a) >= 0.5) || (std::abs(b) >= 0.5)) ? 1.0 : 0.0;
      case kOpXor: return ((std::abs(a) >= 0.5) ^ (std::abs(b) >= 0.5)) ? 1.0 : 0.0;
      default: return 0;
    }
  }
};
struct ScriptStatement {
  ScriptVariable result;
  ScriptExpression expression;
  void Execute(ScriptVirtualMachine &vm) {
    result.Let(vm, expression.Calculate(vm));
  }
};
struct ScriptCode {
  std::deque<ScriptStatement> statements;
  void Execute(ScriptVirtualMachine &vm) {
    for (ScriptStatement& s: statements) {
      s.Execute(vm);
    }
  }
};
struct ScriptChoice {
  ScriptExpression condition;
  std::string text;
  ScriptCode code;
  std::string divert;
};
struct ScriptNode {
  std::string name;
  std::string text;
  ScriptCode code;
  std::deque<ScriptChoice> choices;
};
struct Script {
  std::unordered_map<std::string, ScriptNode> nodes;
};
struct ScriptVirtualMachine {
  Script script;
  std::unordered_map<std::string, double> variables;

  std::function<void (std::string var_name, double value)> OnVariableChange;
};



struct ParseResult {
  bool is_ok = true;
  std::string error_message;
  const Ui32* p = nullptr;

  ParseResult() {
  }

  ParseResult(const Ui32* ptr) {
    p = ptr;
  }

  ParseResult(std::string error) {
    is_ok = false;
    error_message = error;
  }
};

bool BeginsWith(const Ui32* data, const Ui32* prefix);
const Ui32* SkipPrefix(const Ui32* data, const Ui32* prefix);
const Ui32* RemovePrefix(const Ui32* data, const Ui32 prefix);
const Ui32* SkipWhitespaceAndNewline(const Ui32* data);
const Ui32* SkipNumber(const Ui32* data, double *out_number = nullptr);
Ui32 Utf32FromUtf8(const char* utf8);
bool IsLetter(Ui32 ch);
bool IsDigit(Ui32 ch);
bool IsUnderscore(Ui32 ch);
bool IsMinus(Ui32 ch);
const Ui32* SkipVariableName(const Ui32* data);
void InitScriptWord(Ui32 *p, const char *value);
void InitScriptWords();
ParseResult ParseValue(const Ui32* p, ScriptValue *value);
const Ui32* RemoveOperation(const Ui32* p, ScriptExpression::Operation *operation);
ParseResult ParseExpression(const Ui32* p, ScriptExpression *expression);
ParseResult ParseStatement(const Ui32* p, ScriptStatement *statement);
ParseResult ParseChoice(const Ui32* p, ScriptNode *node);
ParseResult ParseNode(Script &script, const Ui32* p);
ParseResult ParseScript(Script &script, const Ui32* script_str32);
ParseResult ParseScript(Script &script, const Ui8* script_str);

} // namespace arctic

#endif /* script_hpp */
