#include "script.hpp"
#include <engine/unicode.h>
#include "string32.hpp"

namespace arctic {

double ScriptVariable::Calculate(ScriptVirtualMachine &vm) {
  return vm.variables[name];
}

void ScriptVariable::Let(ScriptVirtualMachine &vm, double value) {
  vm.variables[name] = value;
  vm.OnVariableChange(name, value);
};




bool BeginsWith(const Ui32* data, const Ui32* prefix) {
  if (!data) {
    if (!prefix) {
      return true;
    }
    return false;
  }
  if (!prefix) {
    return true;
  }
  while (true) {
    if (*prefix == 0) {
      return true;
    }
    if (*data == 0) {
      return false;
    }
    if (*data != *prefix) {
      return false;
    }
    ++data;
    ++prefix;
  }
}

const Ui32* SkipPrefix(const Ui32* data, const Ui32* prefix) {
  const Ui32* begin = data;
  if (!data) {
    return data;
  }
  if (!prefix) {
    return data;
  }
  while (true) {
    if (*prefix == 0) {
      return data;
    }
    if (*data == 0) {
      return begin;
    }
    if (*data != *prefix) {
      return begin;
    }
    ++data;
    ++prefix;
  }
}


const Ui32* RemovePrefix(const Ui32* data, const Ui32 prefix) {
  if (!data) {
    return data;
  }
  if (prefix == 0) {
    return data;
  }
  if (*data == prefix) {
    return data + 1;
  }
  return data;
}

const Ui32* SkipWhitespaceAndNewline(const Ui32* data) {
  const Ui32* p = data;
  do {
    data = p;
    p = RemovePrefix(p, ' ');
    p = RemovePrefix(p, '\t');
    p = RemovePrefix(p, '\n');
    p = RemovePrefix(p, '\r');
  } while (p != data);
  return p;
}

const Ui32* SkipNumber(const Ui32* data, double *out_number) {
  if (!data) {
    return data;
  }
  double sign = 1.0;
  double value = 0.0;
  double multiplier = 0.1;
  bool has_digits = false;
  bool has_dot = false;
  const Ui32* p = data;
  const Ui32* p1 = RemovePrefix(p, '-');
  if (p1 != p) {
    sign = -1.0;
  }
  p = p1;
  do {
    p1 = p;
    if (*p == '\0') {
      if (has_digits) {
        if (out_number) {
          *out_number = sign * value;
        }
        return p;
      } else {
        return data;
      }
    }
    if (*p >= '0' && *p <= '9') {
      if (has_dot) {
        value = value + multiplier * static_cast<double>(*p - '0');
        multiplier = multiplier * 0.1;
      } else {
        value = value * 10.0 + static_cast<double>(*p - '0');
      }
      has_digits = true;
      ++p;
    }
    if (*p == '.') {
      if (has_dot) {
        return data;
      } else {
        has_dot = true;
        ++p;
      }
    }
  } while (p != p1);
  if (has_digits) {
    if (out_number) {
      *out_number = sign * value;
    }
    return p;
  } else {
    return data;
  }
}

Ui32 Utf32FromUtf8(const char* utf8) {
  Utf32Reader reader;
  reader.Reset(reinterpret_cast<const Ui8*>(utf8));
  return reader.ReadOne();
}

bool IsLetter(Ui32 ch) {
  static const Ui32 ch_a = Utf32FromUtf8(u8"a");
  static const Ui32 ch_z = Utf32FromUtf8(u8"z");
  static const Ui32 ch_A = Utf32FromUtf8(u8"A");
  static const Ui32 ch_Z = Utf32FromUtf8(u8"Z");
  static const Ui32 ch_ra = Utf32FromUtf8(u8"а");
  static const Ui32 ch_rz = Utf32FromUtf8(u8"я");
  static const Ui32 ch_rA = Utf32FromUtf8(u8"А");
  static const Ui32 ch_rZ = Utf32FromUtf8(u8"Я");
  if ((ch >= ch_a && ch <= ch_z) ||
      (ch >= ch_A && ch <= ch_Z) ||
      (ch >= ch_ra && ch <= ch_rz) ||
      (ch >= ch_rA && ch <= ch_rZ)) {
    return true;
  } else {
    return false;
  }
}

bool IsDigit(Ui32 ch) {
  static const Ui32 ch_0 = Utf32FromUtf8(u8"0");
  static const Ui32 ch_9 = Utf32FromUtf8(u8"9");
  if (ch >= ch_0 && ch <= ch_9) {
    return true;
  } else {
    return false;
  }
}

bool IsUnderscore(Ui32 ch) {
  static const Ui32 ch_u = Utf32FromUtf8(u8"_");
  if (ch == ch_u) {
    return true;
  } else {
    return false;
  }
}

bool IsMinus(Ui32 ch) {
  static const Ui32 ch_u = Utf32FromUtf8(u8"-");
  if (ch == ch_u) {
    return true;
  } else {
    return false;
  }
}

const Ui32* SkipVariableName(const Ui32* data) {
  if (!data) {
    return data;
  }
  bool has_letter = false;
  while (true) {
    if (*data == 0) {
      return data;
    }
    if (IsDigit(*data)) {
      if (!has_letter) {
        return data;
      }
    } else if (IsLetter(*data)) {
      has_letter = true;
    } else if (IsUnderscore(*data)) {
      has_letter = true;
    } else {
      return data;
    }
    ++data;
  }
}

Ui32 g_word_node[16] = {0};
Ui32 g_word_choice[16] = {0};
Ui32 g_word_divert[16] = {0};
Ui32 g_word_let[16] = {0};
Ui32 g_word_condition[16] = {0};

void InitScriptWord(Ui32 *p, const char *value) {
  Utf32Reader reader;
  reader.Reset(reinterpret_cast<const Ui8*>(value));
  Ui32 ch = 0;
  do {
    ch = reader.ReadOne();
    *p = ch;
    ++p;
  } while (ch);
}

void InitScriptWords() {
  InitScriptWord(g_word_node, u8"НОТА");
  InitScriptWord(g_word_choice, u8"ЧОЙС");
  InitScriptWord(g_word_divert, u8"ДИВЕРТ");
  InitScriptWord(g_word_let, u8"ЛЕТ");
  InitScriptWord(g_word_condition, u8"КОНДИШН");
}


ParseResult ParseValue(const Ui32* p, ScriptValue *value) {
  const Ui32* p1 = p;
  if (IsDigit(*p) || IsMinus(*p)) {
    //number
    double number = 0.0;
    p1 = SkipNumber(p, &number);
    if (p1 == p) {
      return ParseResult(u8"Script does not have a number name after \"=\" in LET, but it should to.");
    }
    value->type = ScriptValue::kTypeImmediate;
    value->immediate = number;
    p = p1;
  } else {
    //variable
    p1 = SkipVariableName(p);
    if (p1 == p) {
      return ParseResult(u8"Script does not have a variable name after \"=\" in LET, but it should to.");
    }
    String32 name32_a(p, p1);
    std::string name = Utf32ToUtf8(name32_a.data.data());
    value->type = ScriptValue::kTypeVariable;
    value->variable.name = name;
    p = p1;
  }
  p1 = SkipWhitespaceAndNewline(p1);
  p = p1;
  return ParseResult(p);
}

const Ui32* RemoveOperation(const Ui32* p, ScriptExpression::Operation *operation) {
  if (!p) {
    return p;
  }
  ScriptExpression::Operation op = ScriptExpression::kOpAdd;
  switch (*p) {
    case '+': op = ScriptExpression::kOpAdd; p++; break;
    case '-': op = ScriptExpression::kOpSub; p++; break;
    case '*': op = ScriptExpression::kOpMul; p++; break;
    case '/': op = ScriptExpression::kOpDiv; p++; break;
    case '<':
      if (p[1] == '=') {
        p++;
        op = ScriptExpression::kOpLe; p++; break;
      } else {
        op = ScriptExpression::kOpL; p++; break;
      }
    case '=':
      if (p[1] == '=') {
        p++;
        op = ScriptExpression::kOpE; p++; break;
      } else {
        return p;
      }
    case '>':
      if (p[1] == '=') {
        p++;
        op = ScriptExpression::kOpGe; p++; break;
      } else {
        op = ScriptExpression::kOpG; p++; break;
      }
    case '!':
      if (p[1] == '=') {
        p++;
        op = ScriptExpression::kOpNe; p++; break;
      } else {
        return p;
      }
    case '&':
      if (p[1] == '&') {
        p++;
        op = ScriptExpression::kOpAnd; p++; break;
      } else {
        return p;
      }
    case '|':
      if (p[1] == '|') {
        p++;
        op = ScriptExpression::kOpOr; p++; break;
      } else {
        return p;
      }
    case '^': op = ScriptExpression::kOpXor; p++; break;
    default:
      return p;
  }
  if (operation) {
    *operation = op;
  }
  return p;
}

ParseResult ParseExpression(const Ui32* p, ScriptExpression *expression) {
  ScriptExpression expr;
  // OPERAND A
  ParseResult res = ParseValue(p, &expression->operand_a);
  if (!res.is_ok) {
    return res;
  }
  p = SkipWhitespaceAndNewline(res.p);
  const Ui32* p1 = p;
  // OPERATION
  p1 = RemoveOperation(p, &expression->operation);
  if (p1 == p) {
    // no operation, just a single assignment
    expression->operation = ScriptExpression::kOpAdd;
    expression->operand_b.type = ScriptValue::kTypeImmediate;
    expression->operand_b.immediate = 0.0;
  } else {
    p = SkipWhitespaceAndNewline(p1);
    p1 = p;
    // OPERAND B
    res = ParseValue(p, &expression->operand_b);
    if (!res.is_ok) {
      return res;
    }
    p = res.p;
    p1 = p;
  }

  return ParseResult(p);
}

ParseResult ParseStatement(const Ui32* p, ScriptStatement *statement) {
  const Ui32 *p1 = SkipPrefix(p, g_word_let);
  if (p1 == p) {
    return ParseResult(u8"Error skipping LET");
  }
  p = p1;
  p1 = SkipWhitespaceAndNewline(p);
  if (p1 == p) {
    return ParseResult(u8"Script does not have a whitespace character after LET, but it should to.");
  }
  p = p1;
  p1 = SkipVariableName(p);
  if (p1 == p) {
    return ParseResult(u8"Script does not have a variable name after LET, but it should to.");
  }
  String32 name32(p, p1);
  std::string name = Utf32ToUtf8(name32.data.data());
  statement->result.name = name;
  p = p1;
  p1 = SkipWhitespaceAndNewline(p);
  p = p1;
  p1 = RemovePrefix(p, '=');
  if (p1 == p) {
    return ParseResult(u8"Script does not have a \"=\" after variable name in LET, but it should to.");
  }
  p1 = SkipWhitespaceAndNewline(p1);
  p = p1;

  ParseResult res = ParseExpression(p, &statement->expression);
  if (!res.is_ok) {
    return res;
  }
  p = SkipWhitespaceAndNewline(res.p);
  return ParseResult(p);
}



ParseResult ParseChoice(const Ui32* p, ScriptNode *node) {
  ScriptChoice choice;
  choice.condition.operation = ScriptExpression::kOpE;

  const Ui32 *p1 = SkipPrefix(p, g_word_condition);
  if (p1 != p) {
    p = SkipWhitespaceAndNewline(p1);
    if (p1 == p) {
      return ParseResult(u8"Script does not have a whitespace character after CONDITION, but it should to.");
    }
    p1 = p;
    ParseResult res = ParseExpression(p, &choice.condition);
    if (!res.is_ok) {
      return res;
    }
    p = SkipWhitespaceAndNewline(res.p);
    p1 = p;
  }

  p1 = SkipPrefix(p, g_word_choice);
  if (p1 == p) {
    return ParseResult(u8"Error skipping CHOICE");
  }
  p = p1;
  p1 = SkipWhitespaceAndNewline(p);
  if (p1 == p) {
    return ParseResult(u8"Script does not have a whitespace character after CHOICE, but it should to.");
  }
  p = p1;

  while (*p && !BeginsWith(p, g_word_node) &&
         !BeginsWith(p, g_word_choice) &&
         !BeginsWith(p, g_word_let) &&
         !BeginsWith(p, g_word_divert)) {
    ++p;
  }
  String32 text(p1, p);
  choice.text = Utf32ToUtf8(text.data.data());
  p1 = p;

  while (BeginsWith(p, g_word_let)) {
    ScriptStatement statement;
    ParseResult res = ParseStatement(p, &statement);
    if (!res.is_ok) {
      return res;
    }
    p = res.p;
    p1 = p;
    choice.code.statements.push_back(statement);
  }

  if (BeginsWith(p, g_word_divert)) {
    const Ui32 *p1 = SkipPrefix(p, g_word_divert);
    if (p1 == p) {
      return ParseResult(u8"Error skipping DIVERT");
    }
    p = p1;
    p1 = SkipWhitespaceAndNewline(p);
    if (p1 == p) {
      return ParseResult(u8"Script does not have a whitespace character after DIVERT, but it should to.");
    }
    p = p1;
    p1 = SkipVariableName(p);
    if (p1 == p) {
      return ParseResult(u8"Script does not have a node name after DIVERT, but it should to.");
    }
    String32 divert = String32(p, p1);
    choice.divert = Utf32ToUtf8(divert.data.data());
    p = p1;
  }
  // choice is over, return
  node->choices.push_back(choice);
  return ParseResult(p);
}


ParseResult ParseNode(Script &script, const Ui32* p) {
  const Ui32 *p1 = SkipPrefix(p, g_word_node);
  if (p1 == p) {
    return ParseResult(u8"Error skipping НОТА");
  }
  p = p1;
  p1 = SkipWhitespaceAndNewline(p);
  if (p1 == p) {
    return ParseResult(u8"Script does not have a whitespace character after NODE, but it should to.");
  }
  p = p1;
  p1 = SkipVariableName(p);
  if (p1 == p) {
    return ParseResult(u8"Script does not have a node name after NODE, but it should to.");
  }
  String32 name32(p, p1);
  std::string name = Utf32ToUtf8(name32.data.data());
  p = p1;
  p1 = SkipWhitespaceAndNewline(p);
  if (p1 == p) {
    return ParseResult(u8"Script does not have a whitespace character after NODE name, but it should to.");
  }
  p = p1;

  ScriptNode &node = script.nodes[name];
  node.name = name;
  bool has_text = false;
  while (*p && !BeginsWith(p, g_word_node)) {
    if (BeginsWith(p, g_word_condition) || BeginsWith(p, g_word_choice) ||
        BeginsWith(p, g_word_let)) {
      if (!has_text) {
        String32 text(p1, p);
        node.text = Utf32ToUtf8(text.data.data());
        has_text = true;
      }
      // parse let
      while (BeginsWith(p, g_word_let)) {
        ScriptStatement statement;
        ParseResult res = ParseStatement(p, &statement);
        if (!res.is_ok) {
          return res;
        }
        p = res.p;
        p1 = p;
        node.code.statements.push_back(statement);
      }
      // parse choice
      ParseResult res = ParseChoice(p, &node);
      if (!res.is_ok) {
        return res;
      }
      p = res.p;
    } else {
      ++p;
    }
  }
  if (!has_text) {
    String32 text(p1, p);
    node.text = Utf32ToUtf8(text.data.data());
    has_text = true;
  }
  // node is over, return
  return ParseResult(p);
}

ParseResult ParseScript(Script &script, const Ui32* script_str32) {
  InitScriptWords();
  if (!script_str32) {
    return ParseResult();
  }
  const Ui32 *p = script_str32;
  while (true) {
    p = SkipWhitespaceAndNewline(p);
    if (*p == 0) {
      return ParseResult();
    }
    bool is_ok = BeginsWith(p, g_word_node);
    if (!is_ok) {
      return ParseResult(u8"Script does not start with НОТА, but it should to.");
    }
    ParseResult res = ParseNode(script, p);
    if (!res.is_ok) {
      return res;
    }
    p = res.p;
  }
}

ParseResult ParseScript(Script &script, const Ui8* script_str) {
  Utf32Reader reader;
  reader.Reset(script_str);
  Ui64 size = 1;
  while (reader.ReadOne()) {
    ++size;
  }
  reader.Rewind();
  std::vector<Ui32> script_str32;
  script_str32.reserve((size_t)size);
  while (Ui32 ch = reader.ReadOne()) {
    script_str32.push_back(ch);
  }
  script_str32.push_back(0);
  return ParseScript(script, script_str32.data());
}

} // namespace arctic
