#include "d3d9_sm3_translator.h"
#include "../util/util_log.h"
#include <cstring>
#include <algorithm>

namespace vkwind {

static const char* kTag = "SM3Translator";

// DXBC constants (for detecting DXBC vs raw SM3)
static constexpr uint32_t DXBC_MAGIC = 0x43425844;
static constexpr uint32_t VS_TOKEN_PREFIX = 0xFFFE;
static constexpr uint32_t PS_TOKEN_PREFIX = 0xFFFF;
static constexpr uint32_t END_TOKEN = 0x0000FFFF;
static constexpr uint32_t COMMENT_TOKEN = 0x0000FFFE;

// D3D9 register type encoding: bits[30:28] and bits[12:11]
static SM3RegisterType decode_reg_type(uint32_t token) {
  uint32_t hi = (token >> 28) & 0x7;
  uint32_t lo = (token >> 11) & 0x3;
  uint32_t combined = (hi << 2) | lo;
  return static_cast<SM3RegisterType>(combined);
}

// D3D9 swizzle: bits[23:16] for source registers
// Each pair of bits selects a component: 0=x, 1=y, 2=z, 3=w
static void decode_swizzle(uint32_t token, uint32_t outSwizzle[4]) {
  uint32_t swiz = (token >> 16) & 0xFF;
  outSwizzle[0] = (swiz >> 0) & 0x3;
  outSwizzle[1] = (swiz >> 2) & 0x3;
  outSwizzle[2] = (swiz >> 4) & 0x3;
  outSwizzle[3] = (swiz >> 6) & 0x3;
}

// D3D9 write mask: bits[19:16] for destination registers
static uint32_t decode_writemask(uint32_t token) {
  return (token >> 16) & 0xF;
}

// D3D9 register number: bits[10:0]
static uint32_t decode_reg_num(uint32_t token) {
  return token & 0x7FF;
}

// D3D9 result modifiers: bits[23:20]
// 0=none, 1=clamp (sat), 2=partial precision, 3=clamp+partial
static uint32_t decode_result_mod(uint32_t token) {
  return (token >> 20) & 0x3;
}

// D3D9 result shift: bits[27:24] for DEF instruction immediate encoding
static uint32_t decode_def_shift(uint32_t token) {
  return (token >> 24) & 0x7;
}

// D3D9 negate flag: bit[31] of destination token
static bool decode_dest_absolute(uint32_t token) {
  return (token & (1u << 30)) != 0;
}

// D3D9 source negate: bit[31] of source register
static bool decode_src_negate(uint32_t token) {
  return (token & (1u << 31)) != 0;
}

// D3D9 source absolute: bit[30] of source register
static bool decode_src_absolute(uint32_t token) {
  return (token & (1u << 30)) != 0;
}

// D3D9 relative addressing: bit[13] of source register
static bool decode_relative(uint32_t token) {
  return (token & (1u << 13)) != 0;
}

// Number of components in a write mask
static uint32_t writemask_count(uint32_t mask) {
  uint32_t count = 0;
  if (mask & 1) count++;
  if (mask & 2) count++;
  if (mask & 4) count++;
  if (mask & 8) count++;
  return count;
}

// Helper: is this a scalar instruction (result goes to .x only)
static bool is_scalar_op(SM3Opcode op) {
  switch (op) {
    case SM3_OP_RCP:
    case SM3_OP_RSQ:
    case SM3_OP_EXP:
    case SM3_OP_LOG:
    case SM3_OP_LIT:
    case SM3_OP_EXPP:
    case SM3_OP_LOGP:
      return true;
    default:
      return false;
  }
}

// Helper: number of source operands
static uint32_t src_count(SM3Opcode op) {
  switch (op) {
    case SM3_OP_NOP: return 0;
    case SM3_OP_MOV:
    case SM3_OP_RCP:
    case SM3_OP_RSQ:
    case SM3_OP_EXP:
    case SM3_OP_LOG:
    case SM3_OP_LIT:
    case SM3_OP_ABS:
    case SM3_OP_NRM:
    case SM3_OP_FRC:
    case SM3_OP_EXPP:
    case SM3_OP_LOGP:
    case SM3_OP_MOVA:
    case SM3_OP_TEX:
    case SM3_OP_TEXCOORD:
    case SM3_OP_TEXKILL:
    case SM3_OP_TEXBEM:
    case SM3_OP_TEXBEML:
    case SM3_OP_TEXREG2AR:
    case SM3_OP_TEXREG2GB:
    case SM3_OP_TEXREG2RGB:
    case SM3_OP_TEXDP3:
    case SM3_OP_TEXDP3TEX:
    case SM3_OP_TEXDEPTH:
    case SM3_OP_TEXM3x2PAD:
    case SM3_OP_TEXM3x3PAD:
    case SM3_OP_TEXM3x2TEX:
    case SM3_OP_TEXM3x3TEX:
    case SM3_OP_TEXM3x3SPEC:
    case SM3_OP_TEXM3x3VSPEC:
    case SM3_OP_TEXM3x3:
    case SM3_OP_TEXM3x2DEPTH:
    case SM3_OP_TEXLDD:
    case SM3_OP_TEXLDL:
    case SM3_OP_BREAKP:
    case SM3_OP_IF:
    case SM3_OP_IFC:
    case SM3_OP_BREAKC:
    case SM3_OP_LOOP:
    case SM3_OP_SINCOS:
      return 1;
    case SM3_OP_ADD:
    case SM3_OP_SUB:
    case SM3_OP_MUL:
    case SM3_OP_DP3:
    case SM3_OP_DP4:
    case SM3_OP_MIN:
    case SM3_OP_MAX:
    case SM3_OP_SLT:
    case SM3_OP_SGE:
    case SM3_OP_CRS:
    case SM3_OP_POW:
    case SM3_OP_BEM:
    case SM3_OP_DSX:
    case SM3_OP_DSY:
    case SM3_OP_M4x4:
    case SM3_OP_M4x3:
    case SM3_OP_M3x4:
    case SM3_OP_M3x3:
    case SM3_OP_M3x2:
    case SM3_OP_SGN:
    case SM3_OP_DST:
    case SM3_OP_REP:
      return 2;
    case SM3_OP_LRP:
    case SM3_OP_CMP:
    case SM3_OP_MAD:
    case SM3_OP_DP2ADD:
    case SM3_OP_CND:
      return 3;
    default:
      return 0;
  }
}

static bool is_tex_op(SM3Opcode op) {
  switch (op) {
    case SM3_OP_TEX:
    case SM3_OP_TEXLDD:
    case SM3_OP_TEXLDL:
    case SM3_OP_TEXKILL:
    case SM3_OP_TEXBEM:
    case SM3_OP_TEXBEML:
    case SM3_OP_TEXREG2AR:
    case SM3_OP_TEXREG2GB:
    case SM3_OP_TEXREG2RGB:
    case SM3_OP_TEXDP3:
    case SM3_OP_TEXDP3TEX:
    case SM3_OP_TEXDEPTH:
    case SM3_OP_TEXM3x2PAD:
    case SM3_OP_TEXM3x3PAD:
    case SM3_OP_TEXM3x2TEX:
    case SM3_OP_TEXM3x3TEX:
    case SM3_OP_TEXM3x3SPEC:
    case SM3_OP_TEXM3x3VSPEC:
    case SM3_OP_TEXM3x3:
    case SM3_OP_TEXM3x2DEPTH:
    case SM3_OP_TEXCOORD:
      return true;
    default:
      return false;
  }
}

// --- SM3Translator ---

SM3Translator::SM3Translator() {}
SM3Translator::~SM3Translator() {}

SM3Register SM3Translator::decode_register(const uint32_t* token, bool isDest) {
  SM3Register reg;
  uint32_t t = *token;
  reg.type = decode_reg_type(t);
  reg.index = decode_reg_num(t);
  reg.relativeAddressing = decode_relative(t);

  if (isDest) {
    reg.writeMask = decode_writemask(t);
    reg.absolute = decode_dest_absolute(t);
    reg.negate = false;
    for (uint32_t i = 0; i < 4; i++) reg.swizzle[i] = i;
  } else {
    decode_swizzle(t, reg.swizzle);
    reg.writeMask = 0xF;
    reg.negate = decode_src_negate(t);
    reg.absolute = decode_src_absolute(t);
  }

  return reg;
}

bool SM3Translator::decode_instruction(const uint32_t* data, uint32_t remaining, SM3Instruction& out) {
  if (remaining < 1) return false;

  uint32_t token = data[0];
  out.opcode = static_cast<SM3Opcode>(token & 0xFFFF);
  out.length = (token >> 24) & 0xF;
  out.predicated = (token & (1u << 31)) != 0;

  if (out.length < 1 || out.length > remaining) return false;

  if (out.opcode == SM3_OP_END || out.opcode == SM3_OP_PHASE) return true;

  uint32_t pos = 1;

  // Skip destination token for instructions that have one
  bool hasDst = (out.opcode != SM3_OP_NOP) &&
                (out.opcode != SM3_OP_END) &&
                (out.opcode != SM3_OP_RET) &&
                (out.opcode != SM3_OP_ELSE) &&
                (out.opcode != SM3_OP_ENDIF) &&
                (out.opcode != SM3_OP_ENDLOOP) &&
                (out.opcode != SM3_OP_LABEL) &&
                (out.opcode != SM3_OP_CALL) &&
                (out.opcode != SM3_OP_LOOP) &&
                (out.opcode != SM3_OP_REP) &&
                (out.opcode != SM3_OP_ENDREP) &&
                (out.opcode != SM3_OP_BREAK) &&
                (out.opcode != SM3_OP_IF) &&
                (out.opcode != SM3_OP_IFC) &&
                (out.opcode != SM3_OP_BREAKC) &&
                (out.opcode != SM3_OP_COMMENT);

  // Special handling for DCL
  if (out.opcode == SM3_OP_DCL) {
    if (pos + 2 <= out.length) {
      // DCL has: usage token, dest register
      uint32_t usageToken = data[pos++];
      SM3Declaration decl;
      decl.usage = static_cast<SM3Declaration::Usage>(usageToken & 0x1F);
      decl.usageIndex = (usageToken >> 16) & 0xF;
      out.dest = decode_register(data + pos, true);
      decl.regType = out.dest.type;
      decl.regIndex = out.dest.index;
      m_declarations.push_back(decl);
    }
    return true;
  }

  // Special handling for DEF
  if (out.opcode == SM3_OP_DEF) {
    if (pos + 5 <= out.length) {
      out.dest = decode_register(data + pos, true);
      pos++;
      out.floatConstants[0] = reinterpret_cast<const float&>(data[pos++]);
      out.floatConstants[1] = reinterpret_cast<const float&>(data[pos++]);
      out.floatConstants[2] = reinterpret_cast<const float&>(data[pos++]);
      out.floatConstants[3] = reinterpret_cast<const float&>(data[pos++]);

      SM3ConstantDef def;
      def.regIndex = out.dest.index;
      def.values[0] = out.floatConstants[0];
      def.values[1] = out.floatConstants[1];
      def.values[2] = out.floatConstants[2];
      def.values[3] = out.floatConstants[3];
      m_floatConstants.push_back(def);
    }
    return true;
  }

  // Special handling for DEFI
  if (out.opcode == SM3_OP_DEFI) {
    if (pos + 5 <= out.length) {
      out.dest = decode_register(data + pos, true);
      pos++;
      out.intConstants[0] = static_cast<int32_t>(data[pos++]);
      out.intConstants[1] = static_cast<int32_t>(data[pos++]);
      out.intConstants[2] = static_cast<int32_t>(data[pos++]);
      out.intConstants[3] = static_cast<int32_t>(data[pos++]);

      SM3IntConstantDef def;
      def.regIndex = out.dest.index;
      def.values[0] = out.intConstants[0];
      def.values[1] = out.intConstants[1];
      def.values[2] = out.intConstants[2];
      def.values[3] = out.intConstants[3];
      m_intConstants.push_back(def);
    }
    return true;
  }

  // Special handling for DEFB
  if (out.opcode == SM3_OP_DEFB) {
    if (pos + 2 <= out.length) {
      out.dest = decode_register(data + pos, true);
      pos++;
      out.predicate = (data[pos++] != 0);

      SM3BoolConstantDef def;
      def.regIndex = out.dest.index;
      def.value = out.predicate != 0;
      m_boolConstants.push_back(def);
    }
    return true;
  }

  // Special handling for SETP (set predicate)
  if (out.opcode == SM3_OP_SETP) {
    if (pos + 4 <= out.length) {
      out.dest = decode_register(data + pos, true);
      pos++;
      out.comparisonOpcode = (data[pos] >> 24) & 0xFF;
      pos++;
      out.src.push_back(decode_register(data + pos, false));
      pos++;
      out.src.push_back(decode_register(data + pos, false));
      pos++;
    }
    return true;
  }

  // Decode destination (if present)
  if (hasDst && pos < out.length) {
    out.dest = decode_register(data + pos, true);
    pos++;
  }

  // Decode source registers
  uint32_t nSrc = src_count(out.opcode);
  for (uint32_t i = 0; i < nSrc && pos < out.length; i++) {
    out.src.push_back(decode_register(data + pos, false));
    pos++;
  }

  // Extract label and comparison opcode for flow control instructions
  // These tokens are NOT registers — they contain label numbers and comparison types
  pos = 1; // reset to after header — flow control tokens follow header directly
  switch (out.opcode) {
    case SM3_OP_LOOP:    // length=2: header + label
      if (pos < out.length) { out.label = data[pos] & 0x7FFF; pos++; }
      break;
    case SM3_OP_CALL:    // length=2: header + label
      if (pos < out.length) { out.label = data[pos] & 0x7FFF; pos++; }
      break;
    case SM3_OP_IFC:     // length=3: header + src0 + (comparison|label)
      pos++; // skip src0 (already decoded above)
      if (pos < out.length) {
        out.comparisonOpcode = (data[pos] >> 24) & 0xFF;
        out.label = data[pos] & 0x7FFF;
        pos++;
      }
      break;
    case SM3_OP_BREAKC:  // length=3: header + src0 + (comparison|label)
      pos++; // skip src0
      if (pos < out.length) {
        out.comparisonOpcode = (data[pos] >> 24) & 0xFF;
        out.label = data[pos] & 0x7FFF;
        pos++;
      }
      break;
    case SM3_OP_CALLNZ:  // length=3: header + label + src0(bool)
      if (pos < out.length) { out.label = data[pos] & 0x7FFF; pos++; }
      break;
    case SM3_OP_LABEL:   // length=1: header only, label in header bits[23:16]
      // Label is in bits[23:16] (8 bits). Bits[27:24] hold the length field (1),
      // so masking with 0x7FFF after shifting would include the length bit.
      out.label = (data[0] >> 16) & 0xFF;
      break;
    default:
      break;
  }

  return true;
}

bool SM3Translator::parse_bytecode(const uint32_t* bytecode, uint32_t dwordCount) {
  if (dwordCount < 1) return false;

  uint32_t versionToken = bytecode[0];
  uint32_t prefix = (versionToken >> 16) & 0xFFFF;
  uint32_t minor = (versionToken >> 8) & 0xFF;
  uint32_t major = versionToken & 0xFF;

  if (prefix == VS_TOKEN_PREFIX) {
    m_isVertexShader = true;
    m_isPixelShader = false;
    VKWIND_INFO(kTag, "SM3 VS %u.%u detected (%u DWORDs)", major, minor, dwordCount);
  } else if (prefix == PS_TOKEN_PREFIX) {
    m_isVertexShader = false;
    m_isPixelShader = true;
    VKWIND_INFO(kTag, "SM3 PS %u.%u detected (%u DWORDs)", major, minor, dwordCount);
  } else if (bytecode[0] == DXBC_MAGIC) {
    VKWIND_ERR(kTag, "DXBC bytecode passed to SM3 translator — use ShaderTranslator instead");
    return false;
  } else {
    VKWIND_ERR(kTag, "Unknown shader version token: 0x%08X", versionToken);
    return false;
  }

  uint32_t pos = 1;
  while (pos < dwordCount) {
    uint32_t token = bytecode[pos];
    uint32_t opcode = token & 0xFFFF;

    if (opcode == END_TOKEN) break;

    if (opcode == COMMENT_TOKEN) {
      uint32_t commentLen = (token >> 16) & 0x7FFF;
      pos += 1 + commentLen;
      continue;
    }

    SM3Instruction inst;
    uint32_t instLen = (token >> 24) & 0xF;
    if (instLen < 1) {
      VKWIND_ERR(kTag, "Invalid instruction length at pos %u", pos);
      pos++;
      continue;
    }

    if (!decode_instruction(bytecode + pos, dwordCount - pos, inst)) {
      VKWIND_ERR(kTag, "Failed to decode instruction at pos %u", pos);
      pos += instLen;
      continue;
    }

    if (inst.opcode != SM3_OP_END && inst.opcode != SM3_OP_PHASE &&
        inst.opcode != SM3_OP_DEF && inst.opcode != SM3_OP_DEFI && inst.opcode != SM3_OP_DEFB &&
        inst.opcode != SM3_OP_DCL) {
      m_instructions.push_back(inst);
    }

    pos += instLen;
  }

  // Count temps
  for (auto& decl : m_declarations) {
    if (decl.regType == SM3_REG_TEMP) {
      m_tempCount = std::max(m_tempCount, decl.regIndex + 1);
    }
  }

  // Also count temps referenced in instructions
  for (auto& inst : m_instructions) {
    if (inst.dest.type == SM3_REG_TEMP) {
      m_tempCount = std::max(m_tempCount, inst.dest.index + 1);
    }
    for (auto& s : inst.src) {
      if (s.type == SM3_REG_TEMP) {
        m_tempCount = std::max(m_tempCount, s.index + 1);
      }
    }
  }

  if (m_tempCount == 0) m_tempCount = 1;

  // Count samplers
  for (auto& decl : m_declarations) {
    if (decl.regType == SM3_REG_TEXTURE || decl.usage == SM3Declaration::USAGE_TEXCOORD) {
      m_samplerCount = std::max(m_samplerCount, decl.regIndex + 1);
    }
  }

  VKWIND_INFO(kTag, "Parsed %u instructions, %u temps, %u float consts, %u samplers",
    (uint32_t)m_instructions.size(), m_tempCount, (uint32_t)m_floatConstants.size(), m_samplerCount);

  return true;
}

// --- SPIR-V generation ---

void SM3Translator::SpirvEmitter::name(uint32_t id, const char* n) {
  uint32_t len = static_cast<uint32_t>(strlen(n)) + 1;
  uint32_t words = (len + 3) / 4;
  op(5, 2 + words);
  emit(id);
  auto& buf = currentBuffer();
  size_t start = buf.size();
  buf.resize(start + words);
  memcpy(&buf[start], n, len);
}

void SM3Translator::SpirvEmitter::decorate(uint32_t id, uint32_t dec, uint32_t val) {
  // Decorations WITH extra operands: SpecId(1), BuiltIn(11), Stream(29), Location(30),
  // Component(31), Binding(33), DescriptorSet(34), Offset(35), XfbBuffer(36),
  // XfbStride(37), FuncParamAttr(38), FPRoundingMode(39), InputAttachmentIndex(43),
  // Alignment(44), ArrayStride(6)
  bool hasVal = (dec == 1 || dec == 6 || dec == 11 || dec == 29 || dec == 30 || dec == 31 || dec == 33 ||
                 dec == 34 || dec == 35 || dec == 36 || dec == 37 || dec == 38 ||
                 dec == 39 || dec == 43 || dec == 44);
  op(71, hasVal ? 4 : 3);
  emit(id); emit(dec); if (hasVal) emit(val);
}

void SM3Translator::SpirvEmitter::memberDecorate(uint32_t id, uint32_t member, uint32_t dec, uint32_t val) {
  bool hasVal = (dec == 11 || dec == 29 || dec == 30 || dec == 31 || dec == 33 ||
                 dec == 34 || dec == 35 || dec == 36 || dec == 37 || dec == 38 ||
                 dec == 39 || dec == 43 || dec == 44 || dec == 1);
  op(72, hasVal ? 5 : 4);
  emit(id); emit(member); emit(dec); if (hasVal) emit(val);
}

void SM3Translator::SpirvEmitter::typeVoid(uint32_t id) { op(19, 2); emit(id); }
void SM3Translator::SpirvEmitter::typeFloat(uint32_t id) { op(22, 3); emit(id); emit(32); }
void SM3Translator::SpirvEmitter::typeInt(uint32_t id) { op(21, 4); emit(id); emit(32); emit(1); }
void SM3Translator::SpirvEmitter::typeBool(uint32_t id) { op(20, 2); emit(id); }

void SM3Translator::SpirvEmitter::typeVec(uint32_t id, uint32_t compType, uint32_t compCount) {
  op(23, 4); emit(id); emit(compType); emit(compCount);
}

void SM3Translator::SpirvEmitter::typeMat(uint32_t id, uint32_t vecType, uint32_t colCount) {
  op(24, 4); emit(id); emit(vecType); emit(colCount);
}

void SM3Translator::SpirvEmitter::typePointer(uint32_t id, uint32_t sc, uint32_t typeId) {
  op(32, 4); emit(id); emit(sc); emit(typeId);
}

void SM3Translator::SpirvEmitter::typeArray(uint32_t id, uint32_t typeId, uint32_t lengthId) {
  op(28, 4); emit(id); emit(typeId); emit(lengthId);
}

void SM3Translator::SpirvEmitter::typeStruct(uint32_t id, const std::vector<uint32_t>& members) {
  op(30, 2 + members.size()); emit(id);
  for (auto m : members) emit(m);
}

void SM3Translator::SpirvEmitter::typeImage(uint32_t id, uint32_t dim, uint32_t depth, uint32_t arrayed, uint32_t sampled, uint32_t format) {
  op(25, 9); emit(id); emit(floatType); emit(dim); emit(depth); emit(arrayed); emit(0); emit(sampled); emit(format);
}

void SM3Translator::SpirvEmitter::typeSampler(uint32_t id) { op(26, 2); emit(id); }
void SM3Translator::SpirvEmitter::typeSampledImage(uint32_t id, uint32_t imgType) { op(27, 3); emit(id); emit(imgType); }
void SM3Translator::SpirvEmitter::typeFunction(uint32_t id, uint32_t retType) { op(33, 3); emit(id); emit(retType); }

uint32_t SM3Translator::SpirvEmitter::variable(uint32_t typeId, uint32_t sc) {
  uint32_t id = alloc(); op(59, 4); emit(typeId); emit(id); emit(sc); return id;
}

uint32_t SM3Translator::SpirvEmitter::constant(uint32_t type, uint32_t value) {
  // OpConstant must be in type/constant section, not functions
  auto prev = currentSection;
  setSection(SEC_TYPES);
  uint32_t id = alloc(); op(43, 4); emit(type); emit(id); emit(value);
  currentSection = prev;
  return id;
}

uint32_t SM3Translator::SpirvEmitter::constant64(uint32_t type, uint32_t lo, uint32_t hi) {
  auto prev = currentSection;
  setSection(SEC_TYPES);
  uint32_t id = alloc(); op(43, 5); emit(type); emit(id); emit(lo); emit(hi);
  currentSection = prev;
  return id;
}

uint32_t SM3Translator::SpirvEmitter::specConstant(uint32_t type, uint32_t value) {
  auto prev = currentSection;
  setSection(SEC_TYPES);
  uint32_t id = alloc(); op(50, 4); emit(type); emit(id); emit(value);
  currentSection = prev;
  return id;
}

void SM3Translator::SpirvEmitter::function(uint32_t retType, uint32_t funcId, uint32_t control, uint32_t funcTypeId) {
  op(54, 5); emit(retType); emit(funcId); emit(control); emit(funcTypeId);
}

void SM3Translator::SpirvEmitter::functionEnd() { op(56, 1); }
void SM3Translator::SpirvEmitter::returnOp() { op(253, 1); }

void SM3Translator::SpirvEmitter::label(uint32_t labelId) {
  op(248, 2); emit(labelId);
}

uint32_t SM3Translator::SpirvEmitter::load(uint32_t type, uint32_t ptr) {
  uint32_t id = alloc(); op(61, 4); emit(type); emit(id); emit(ptr); return id;
}

void SM3Translator::SpirvEmitter::store(uint32_t ptr, uint32_t value) {
  op(62, 3); emit(ptr); emit(value);
}

uint32_t SM3Translator::SpirvEmitter::accessChain(uint32_t type, uint32_t base, const std::vector<uint32_t>& indices) {
  uint32_t id = alloc(); op(65, 4 + indices.size()); emit(type); emit(id); emit(base);
  for (auto i : indices) emit(i);
  return id;
}

uint32_t SM3Translator::SpirvEmitter::imageSampleImplicitLod(uint32_t rt, uint32_t si, uint32_t coord) {
  uint32_t id = alloc(); op(87, 5); emit(rt); emit(id); emit(si); emit(coord); return id;
}

uint32_t SM3Translator::SpirvEmitter::imageSampleExplicitLod(uint32_t rt, uint32_t si, uint32_t coord, uint32_t lod) {
  uint32_t id = alloc(); op(88, 7); emit(rt); emit(id); emit(si); emit(coord); emit(0x2); emit(lod); return id;
  // Operand: 0x2 = Lod operand kind (literal number)
}

uint32_t SM3Translator::SpirvEmitter::compositeConstruct(uint32_t type, const std::vector<uint32_t>& comps) {
  uint32_t id = alloc(); op(80, 3 + comps.size()); emit(type); emit(id);
  for (auto c : comps) emit(c); return id;
}

uint32_t SM3Translator::SpirvEmitter::constantComposite(uint32_t type, const std::vector<uint32_t>& consts) {
  uint32_t id = alloc(); op(51, 3 + consts.size()); emit(type); emit(id);
  for (auto c : consts) emit(c); return id;
}

uint32_t SM3Translator::SpirvEmitter::vectorShuffle(uint32_t type, uint32_t v1, uint32_t v2, const std::vector<uint32_t>& comps) {
  uint32_t id = alloc(); op(79, 5 + comps.size()); emit(type); emit(id); emit(v1); emit(v2);
  for (auto c : comps) emit(c); return id;
}

uint32_t SM3Translator::SpirvEmitter::compositeExtract(uint32_t type, uint32_t composite, uint32_t index) {
  uint32_t id = alloc(); op(81, 5); emit(type); emit(id); emit(composite); emit(index); return id;
}

uint32_t SM3Translator::SpirvEmitter::fadd(uint32_t t, uint32_t a, uint32_t b) {
  uint32_t id = alloc(); op(129, 5); emit(t); emit(id); emit(a); emit(b); return id;
}

uint32_t SM3Translator::SpirvEmitter::fsub(uint32_t t, uint32_t a, uint32_t b) {
  uint32_t id = alloc(); op(131, 5); emit(t); emit(id); emit(a); emit(b); return id;
}

uint32_t SM3Translator::SpirvEmitter::fmul(uint32_t t, uint32_t a, uint32_t b) {
  uint32_t id = alloc(); op(133, 5); emit(t); emit(id); emit(a); emit(b); return id;
}

uint32_t SM3Translator::SpirvEmitter::fdiv(uint32_t t, uint32_t a, uint32_t b) {
  uint32_t id = alloc(); op(136, 5); emit(t); emit(id); emit(a); emit(b); return id;
}

uint32_t SM3Translator::SpirvEmitter::fmin(uint32_t t, uint32_t a, uint32_t b) {
  return extInst(t, 37, {a, b}); // GLSLstd450FMin
}

uint32_t SM3Translator::SpirvEmitter::fmax(uint32_t t, uint32_t a, uint32_t b) {
  return extInst(t, 40, {a, b}); // GLSLstd450FMax
}

uint32_t SM3Translator::SpirvEmitter::fclamp(uint32_t t, uint32_t x, uint32_t lo, uint32_t hi) {
  return extInst(t, 43, {x, lo, hi}); // GLSLstd450FClamp
}

uint32_t SM3Translator::SpirvEmitter::fma(uint32_t t, uint32_t a, uint32_t b, uint32_t c) {
  return extInst(t, 50, {a, b, c}); // GLSLstd450Fma
}

uint32_t SM3Translator::SpirvEmitter::fdot(uint32_t t, uint32_t a, uint32_t b) {
  uint32_t id = alloc(); op(148, 5); emit(t); emit(id); emit(a); emit(b); return id;
}

uint32_t SM3Translator::SpirvEmitter::ffract(uint32_t t, uint32_t x) {
  return extInst(t, 10, {x}); // GLSLstd450Fract
}

uint32_t SM3Translator::SpirvEmitter::fcross(uint32_t t, uint32_t a, uint32_t b) {
  return extInst(t, 68, {a, b}); // GLSLstd450Cross
}

uint32_t SM3Translator::SpirvEmitter::fnormalize(uint32_t t, uint32_t x) {
  return extInst(t, 69, {x}); // GLSLstd450Normalize
}

uint32_t SM3Translator::SpirvEmitter::flength(uint32_t t, uint32_t x) {
  return extInst(t, 66, {x}); // GLSLstd450Length
}

uint32_t SM3Translator::SpirvEmitter::fmatrixTimesVector(uint32_t t, uint32_t mat, uint32_t vec) {
  uint32_t id = alloc(); op(145, 5); emit(t); emit(id); emit(mat); emit(vec); return id;
}

uint32_t SM3Translator::SpirvEmitter::fvectorTimesMatrix(uint32_t t, uint32_t vec, uint32_t mat) {
  uint32_t id = alloc(); op(144, 5); emit(t); emit(id); emit(vec); emit(mat); return id;
}

uint32_t SM3Translator::SpirvEmitter::fvectorTimesScalar(uint32_t t, uint32_t vec, uint32_t sc) {
  uint32_t id = alloc(); op(142, 5); emit(t); emit(id); emit(vec); emit(sc); return id;
}

uint32_t SM3Translator::SpirvEmitter::iequal(uint32_t t, uint32_t a, uint32_t b) {
  uint32_t id = alloc(); op(170, 5); emit(t); emit(id); emit(a); emit(b); return id;
}

uint32_t SM3Translator::SpirvEmitter::slessthan(uint32_t t, uint32_t a, uint32_t b) {
  uint32_t id = alloc(); op(177, 5); emit(t); emit(id); emit(a); emit(b); return id;
}
uint32_t SM3Translator::SpirvEmitter::iadd(uint32_t t, uint32_t a, uint32_t b)
{
  uint32_t id = alloc(); op(128, 5); emit(t); emit(id); emit(a); emit(b); return id;
}

uint32_t SM3Translator::SpirvEmitter::sgreaterqual(uint32_t t, uint32_t a, uint32_t b) {
  uint32_t id = alloc(); op(175, 5); emit(t); emit(id); emit(a); emit(b); return id;
}

uint32_t SM3Translator::SpirvEmitter::fordgreaterequal(uint32_t t, uint32_t a, uint32_t b) {
  uint32_t id = alloc(); op(190, 5); emit(t); emit(id); emit(a); emit(b); return id;
}

uint32_t SM3Translator::SpirvEmitter::fordlessthan(uint32_t t, uint32_t a, uint32_t b) {
  uint32_t id = alloc(); op(184, 5); emit(t); emit(id); emit(a); emit(b); return id;
}

uint32_t SM3Translator::SpirvEmitter::logicalNot(uint32_t t, uint32_t x) {
  uint32_t id = alloc(); op(168, 4); emit(t); emit(id); emit(x); return id;
}

uint32_t SM3Translator::SpirvEmitter::select(uint32_t t, uint32_t cond, uint32_t tv, uint32_t fv) {
  uint32_t id = alloc(); op(169, 6); emit(t); emit(id); emit(cond); emit(tv); emit(fv); return id;
}

uint32_t SM3Translator::SpirvEmitter::extInst(uint32_t rt, uint32_t inst, const std::vector<uint32_t>& ops) {
  uint32_t id = alloc(); op(12, 5 + ops.size()); emit(rt); emit(id); emit(extInstImport); emit(inst);
  for (auto o : ops) emit(o); return id;
}

uint32_t SM3Translator::SpirvEmitter::snegate(uint32_t t, uint32_t x) {
  uint32_t id = alloc(); op(126, 4); emit(t); emit(id); emit(x); return id;
}

uint32_t SM3Translator::SpirvEmitter::fnegate(uint32_t t, uint32_t x) {
  uint32_t id = alloc(); op(127, 4); emit(t); emit(id); emit(x); return id;
}

uint32_t SM3Translator::SpirvEmitter::fabs(uint32_t t, uint32_t x) {
  return extInst(t, 4, {x}); // GLSLstd450FAbs
}

uint32_t SM3Translator::SpirvEmitter::sconvToS(uint32_t rt, uint32_t v) {
  uint32_t id = alloc(); op(114, 4); emit(rt); emit(id); emit(v); return id; // OpSConvert
}

uint32_t SM3Translator::SpirvEmitter::sconvToF(uint32_t rt, uint32_t v) {
  uint32_t id = alloc(); op(111, 4); emit(rt); emit(id); emit(v); return id; // OpConvertSToF
}

uint32_t SM3Translator::SpirvEmitter::fconvToS(uint32_t rt, uint32_t v) {
  uint32_t id = alloc(); op(110, 4); emit(rt); emit(id); emit(v); return id;
}

uint32_t SM3Translator::SpirvEmitter::isign(uint32_t t, uint32_t x) {
  return extInst(t, 7, {x}); // GLSLstd450SSign
}

uint32_t SM3Translator::SpirvEmitter::fsign(uint32_t t, uint32_t x) {
  return extInst(t, 8, {x}); // GLSLstd450FSign
}

uint32_t SM3Translator::SpirvEmitter::ffloor(uint32_t t, uint32_t x) {
  return extInst(t, 27, {x}); // GLSLstd450Floor
}

uint32_t SM3Translator::SpirvEmitter::fsin(uint32_t t, uint32_t x) {
  return extInst(t, 13, {x}); // GLSLstd450Sin
}

uint32_t SM3Translator::SpirvEmitter::fcos(uint32_t t, uint32_t x) {
  return extInst(t, 14, {x}); // GLSLstd450Cos
}

uint32_t SM3Translator::SpirvEmitter::fexp2(uint32_t t, uint32_t x) {
  return extInst(t, 29, {x}); // GLSLstd450Exp2
}

uint32_t SM3Translator::SpirvEmitter::flog2(uint32_t t, uint32_t x) {
  return extInst(t, 30, {x}); // GLSLstd450Log2
}

uint32_t SM3Translator::SpirvEmitter::fdot2(uint32_t t, uint32_t a, uint32_t b) {
  uint32_t id = alloc();
  op(128, 5); emit(t); emit(id); emit(a); emit(b); return id; // OpDot
}

uint32_t SM3Translator::SpirvEmitter::dfdx(uint32_t t, uint32_t x) {
  return extInst(t, 79, {x}); // GLSLstd450dFdx
}

uint32_t SM3Translator::SpirvEmitter::dfdy(uint32_t t, uint32_t x) {
  return extInst(t, 80, {x}); // GLSLstd450dFdy
}

// --- Flow control ---

void SM3Translator::SpirvEmitter::branch(uint32_t targetLabel) {
  op(249, 2); emit(targetLabel);
}

void SM3Translator::SpirvEmitter::branchConditional(uint32_t cond, uint32_t trueLabel, uint32_t falseLabel) {
  op(250, 4); emit(cond); emit(trueLabel); emit(falseLabel);
}

void SM3Translator::SpirvEmitter::selectionMerge(uint32_t mergeLabel, uint32_t control) {
  op(247, 3); emit(mergeLabel); emit(control);
}

void SM3Translator::SpirvEmitter::loopMerge(uint32_t mergeLabel, uint32_t continueLabel, uint32_t control) {
  op(246, 4); emit(mergeLabel); emit(continueLabel); emit(control);
}

void SM3Translator::SpirvEmitter::kill() {
  op(252, 1);
}

void SM3Translator::SpirvEmitter::unreachable() {
  op(255, 1);
}

uint32_t SM3Translator::SpirvEmitter::fordnotequal(uint32_t t, uint32_t a, uint32_t b) {
  uint32_t id = alloc(); op(182, 5); emit(t); emit(id); emit(a); emit(b); return id;
}

uint32_t SM3Translator::SpirvEmitter::fordequal(uint32_t t, uint32_t a, uint32_t b) {
  uint32_t id = alloc(); op(181, 5); emit(t); emit(id); emit(a); emit(b); return id;
}

uint32_t SM3Translator::SpirvEmitter::fordgreaterthan(uint32_t t, uint32_t a, uint32_t b) {
  uint32_t id = alloc(); op(187, 5); emit(t); emit(id); emit(a); emit(b); return id;
}

uint32_t SM3Translator::SpirvEmitter::fordlessthanequal(uint32_t t, uint32_t a, uint32_t b) {
  uint32_t id = alloc(); op(188, 5); emit(t); emit(id); emit(a); emit(b); return id;
}

uint32_t SM3Translator::SpirvEmitter::logicalany(uint32_t t, uint32_t v) {
  uint32_t id = alloc(); op(154, 4); emit(t); emit(id); emit(v); return id;
}

uint32_t SM3Translator::SpirvEmitter::logicaland(uint32_t t, uint32_t a, uint32_t b) {
  uint32_t id = alloc(); op(165, 5); emit(t); emit(id); emit(a); emit(b); return id;
}

uint32_t SM3Translator::SpirvEmitter::logicalor(uint32_t t, uint32_t a, uint32_t b) {
  uint32_t id = alloc(); op(166, 5); emit(t); emit(id); emit(a); emit(b); return id;
}

void SM3Translator::SpirvEmitter::finalize(uint32_t bound) {
  // Concatenate all sections into code in correct SPIR-V logical layout:
  // EntryPoint → Debug → Annotations → Types → Variables → Functions
  code->insert(code->end(), secEntryPoint.begin(), secEntryPoint.end());
  code->insert(code->end(), secDebug.begin(), secDebug.end());
  code->insert(code->end(), secAnnotations.begin(), secAnnotations.end());
  code->insert(code->end(), secTypes.begin(), secTypes.end());
  code->insert(code->end(), secVariables.begin(), secVariables.end());
  code->insert(code->end(), secFunctions.begin(), secFunctions.end());
  // Fix bound
  (*code)[3] = bound;
}

// --- Register access helpers ---

uint32_t SM3Translator::get_or_create_temp(uint32_t index, uint32_t compCount) {
  auto it = m_tempVars.find(index);
  if (it != m_tempVars.end()) return it->second;

  uint32_t vecType = (compCount == 2) ? m_vec2 : m_vec4;

  auto prevSection = m_spirv->currentSection;
  m_spirv->setSection(SpirvEmitter::SEC_TYPES);
  uint32_t ptrType = m_spirv->alloc();
  m_spirv->typePointer(ptrType, 6, vecType); // Private storage class
  m_spirv->setSection(SpirvEmitter::SEC_VARIABLES);
  uint32_t id = m_spirv->variable(ptrType, 6);
  m_spirv->setSection(SpirvEmitter::SEC_DEBUG);
  char name[32];
  snprintf(name, sizeof(name), "r%u", index);
  m_spirv->name(id, name);
  m_spirv->currentSection = prevSection;

  m_tempVars[index] = id;
  return id;
}

uint32_t SM3Translator::get_or_create_input(uint32_t index, uint32_t compCount) {
  auto it = m_inputVars.find(index);
  if (it != m_inputVars.end()) return it->second;

  uint32_t vecType = (compCount == 2) ? m_vec2 : m_vec4;

  auto prevSection = m_spirv->currentSection;
  m_spirv->setSection(SpirvEmitter::SEC_TYPES);
  uint32_t ptrType = m_spirv->alloc();
  m_spirv->typePointer(ptrType, 1, vecType); // Input storage class
  m_spirv->setSection(SpirvEmitter::SEC_VARIABLES);
  uint32_t id = m_spirv->variable(ptrType, 1);
  m_spirv->setSection(SpirvEmitter::SEC_ANNOTATIONS);
  m_spirv->decorate(id, 30, index); // Location
  m_spirv->setSection(SpirvEmitter::SEC_DEBUG);
  char name[32];
  snprintf(name, sizeof(name), "v%u", index);
  m_spirv->name(id, name);
  m_spirv->currentSection = prevSection;

  m_inputVars[index] = id;
  return id;
}

uint32_t SM3Translator::get_or_create_output(uint32_t index, uint32_t compCount) {
  auto it = m_outputVars.find(index);
  if (it != m_outputVars.end()) return it->second;

  uint32_t vecType = (compCount == 2) ? m_vec2 : m_vec4;

  auto prevSection = m_spirv->currentSection;
  m_spirv->setSection(SpirvEmitter::SEC_TYPES);
  uint32_t ptrType = m_spirv->alloc();
  m_spirv->typePointer(ptrType, 3, vecType); // Output storage class
  m_spirv->setSection(SpirvEmitter::SEC_VARIABLES);
  uint32_t id = m_spirv->variable(ptrType, 3);
  m_spirv->setSection(SpirvEmitter::SEC_ANNOTATIONS);
  m_spirv->decorate(id, 30, index); // Location
  m_spirv->setSection(SpirvEmitter::SEC_DEBUG);
  char name[32];
  snprintf(name, sizeof(name), "o%u", index);
  m_spirv->name(id, name);
  m_spirv->currentSection = prevSection;

  m_outputVars[index] = id;
  return id;
}

uint32_t SM3Translator::get_or_create_const_float(uint32_t index) {
  // During emission phase: generate UBO load (OpAccessChain + OpLoad)
  if (m_inEmitPhase && m_constBufferVar && m_spirv) {
    uint32_t idx = m_spirv->constant(m_intType, index); // constant() auto-places in SEC_TYPES
    uint32_t zero = m_spirv->constant(m_intType, 0);    // member index 0 into the struct
    uint32_t ptr = m_spirv->accessChain(m_constBufferPtrType, m_constBufferVar, {zero, idx});
    uint32_t loaded = m_spirv->load(m_vec4, ptr);
    return loaded;
  }

  // Pre-scan phase: return 0 (placeholder)
  return 0;
}

uint32_t SM3Translator::get_or_create_const_int(uint32_t index) {
  auto it = m_constIntVars.find(index);
  if (it != m_constIntVars.end()) return it->second;

  int val = 0;
  for (auto& def : m_intConstants) {
    if (def.regIndex == index) {
      val = def.values[0];
      break;
    }
  }

  auto prevSection = m_spirv->currentSection;
  m_spirv->setSection(SpirvEmitter::SEC_TYPES);
  uint32_t id = m_spirv->constant(m_intType, (uint32_t)val);
  m_spirv->setSection(SpirvEmitter::SEC_DEBUG);
  char name[32];
  snprintf(name, sizeof(name), "i%u", index);
  m_spirv->name(id, name);
  m_spirv->currentSection = prevSection;

  m_constIntVars[index] = id;
  return id;
}

uint32_t SM3Translator::get_or_create_const_bool(uint32_t index) {
  auto it = m_constBoolVars.find(index);
  if (it != m_constBoolVars.end()) return it->second;

  bool val = false;
  for (auto& def : m_boolConstants) {
    if (def.regIndex == index) {
      val = def.value;
      break;
    }
  }

  auto prevSection = m_spirv->currentSection;
  m_spirv->setSection(SpirvEmitter::SEC_TYPES);
  uint32_t id;
  if (val) {
    id = m_spirv->alloc();
    m_spirv->op(41, 3); // OpConstantTrue
    m_spirv->emit(m_boolType);
    m_spirv->emit(id);
  } else {
    id = m_spirv->constant(m_boolType, 0);
  }
  m_spirv->setSection(SpirvEmitter::SEC_DEBUG);
  char name[32];
  snprintf(name, sizeof(name), "b%u", index);
  m_spirv->name(id, name);
  m_spirv->currentSection = prevSection;

  m_constBoolVars[index] = id;
  return id;
}

uint32_t SM3Translator::get_or_create_predicate(uint32_t index) {
  auto it = m_predicateVars.find(index);
  if (it != m_predicateVars.end()) return it->second;

  auto prevSection = m_spirv->currentSection;
  m_spirv->setSection(SpirvEmitter::SEC_TYPES);
  uint32_t ptrType = m_spirv->alloc();
  m_spirv->typePointer(ptrType, 6, m_boolType); // Private storage class
  uint32_t var = m_spirv->variable(ptrType, 6); // Private storage class
  m_spirv->setSection(SpirvEmitter::SEC_DEBUG);
  char name[32];
  snprintf(name, sizeof(name), "p%u", index);
  m_spirv->name(var, name);
  m_spirv->currentSection = prevSection;

  m_predicateVars[index] = var;
  return var;
}

uint32_t SM3Translator::get_or_create_private_int(uint32_t index) {
  auto it = m_privateIntVars.find(index);
  if (it != m_privateIntVars.end()) return it->second;

  auto prevSection = m_spirv->currentSection;
  m_spirv->setSection(SpirvEmitter::SEC_TYPES);
  uint32_t ptrType = m_spirv->alloc();
  m_spirv->typePointer(ptrType, 6, m_intType); // Private storage class
  uint32_t var = m_spirv->variable(ptrType, 6); // Private storage class
  m_spirv->setSection(SpirvEmitter::SEC_DEBUG);
  char name[32];
  snprintf(name, sizeof(name), "repCounter%u", index);
  m_spirv->name(var, name);
  m_spirv->currentSection = prevSection;

  m_privateIntVars[index] = var;
  return var;
}

uint32_t SM3Translator::get_or_create_sampler(uint32_t index) {
  auto it = m_samplerVars.find(index);
  if (it != m_samplerVars.end()) return it->second;

  // Create a combined image sampler: OpTypeSampledImage variable
  // matching Vulkan descriptor layout binding 1+index (COMBINED_IMAGE_SAMPLER)
  auto prevSection = m_spirv->currentSection;

  m_spirv->setSection(SpirvEmitter::SEC_TYPES);

  // Create shared types only once
  if (!m_texImageType) {
    m_texImageType = m_spirv->alloc();
    m_spirv->typeImage(m_texImageType, 1, 0, 0, 1, 0); // Dim=2D, depth=0, arrayed=0, sampled=1, format=Unknown
  }
  if (!m_texSampledImageType) {
    m_texSampledImageType = m_spirv->alloc();
    m_spirv->typeSampledImage(m_texSampledImageType, m_texImageType);
  }

  // OpTypePointer UniformConstant -> SampledImage
  uint32_t ptrType = m_spirv->alloc();
  m_spirv->typePointer(ptrType, 0, m_texSampledImageType);

  m_spirv->setSection(SpirvEmitter::SEC_VARIABLES);
  uint32_t samplerId = m_spirv->variable(ptrType, 0);

  m_spirv->setSection(SpirvEmitter::SEC_ANNOTATIONS);
  uint32_t binding = kSamplerBaseBinding + index;
  m_spirv->decorate(samplerId, 34, kSamplerSet); // DescriptorSet
  m_spirv->decorate(samplerId, 33, binding);      // Binding

  m_spirv->setSection(SpirvEmitter::SEC_DEBUG);
  char name[32];
  snprintf(name, sizeof(name), "tex%u", index);
  m_spirv->name(samplerId, name);
  m_spirv->currentSection = prevSection;

  m_samplerVars[index] = samplerId;
  return samplerId;
}

uint32_t SM3Translator::get_or_create_sampler_image(uint32_t index) {
  // Delegates to get_or_create_sampler — combined image sampler IS the sampler
  return get_or_create_sampler(index);
}

uint32_t SM3Translator::getOrCreateLabel(uint32_t sm3Label) {
  auto it = m_labelIds.find(sm3Label);
  if (it != m_labelIds.end()) return it->second;
  uint32_t id = m_spirv->alloc();
  m_labelIds[sm3Label] = id;
  return id;
}

// Swizzle a vec4 to match D3D9 source register swizzle
uint32_t SM3Translator::emit_swizzle(uint32_t vec, const SM3Register& reg, uint32_t componentCount) {
  bool needsSwizzle = false;
  for (uint32_t i = 0; i < componentCount; i++) {
    if (reg.swizzle[i] != i) { needsSwizzle = true; break; }
  }

  if (!needsSwizzle && !reg.negate && !reg.absolute) return vec;

  // Check if it's a full replicate (.xxxx)
  bool isReplicate = (reg.swizzle[0] == reg.swizzle[1] && reg.swizzle[1] == reg.swizzle[2] && reg.swizzle[2] == reg.swizzle[3]);

  if (isReplicate && componentCount == 4) {
    uint32_t comp = m_spirv->compositeExtract(m_floatType, vec, reg.swizzle[0]);
    // Replicate to vec4
    uint32_t result = m_spirv->compositeConstruct(m_vec4, {comp, comp, comp, comp});
    if (reg.negate) {
      uint32_t zero = m_spirv->constant(m_floatType, 0);
      uint32_t zeroVec = m_spirv->compositeConstruct(m_vec4, {zero, zero, zero, zero});
      result = m_spirv->fsub(m_vec4, zeroVec, result);
    }
    if (reg.absolute) {
      result = m_spirv->fabs(m_vec4, result);
    }
    return result;
  }

  if (reg.swizzle[0] == 0 && reg.swizzle[1] == 1 && reg.swizzle[2] == 2 && reg.swizzle[3] == 3) {
    if (reg.negate) {
      uint32_t zero = m_spirv->constant(m_floatType, 0);
      uint32_t zeroVec = m_spirv->compositeConstruct(m_vec4, {zero, zero, zero, zero});
      vec = m_spirv->fsub(m_vec4, zeroVec, vec);
    }
    if (reg.absolute) vec = m_spirv->fabs(m_vec4, vec);
    return vec;
  }

  // General case: vectorShuffle
  uint32_t result = m_spirv->vectorShuffle(m_vec4, vec, vec, {reg.swizzle[0], reg.swizzle[1], reg.swizzle[2], reg.swizzle[3]});

  if (reg.negate) {
    uint32_t zero = m_spirv->constant(m_floatType, 0);
    uint32_t zeroVec = m_spirv->compositeConstruct(m_vec4, {zero, zero, zero, zero});
    result = m_spirv->fsub(m_vec4, zeroVec, result);
  }
  if (reg.absolute) result = m_spirv->fabs(m_vec4, result);
  return result;
}

uint32_t SM3Translator::emit_replicate(uint32_t vec, uint32_t swizzleComp) {
  uint32_t comp = m_spirv->compositeExtract(m_floatType, vec, swizzleComp);
  return m_spirv->compositeConstruct(m_vec4, {comp, comp, comp, comp});
}

uint32_t SM3Translator::emit_load_register(const SM3Register& reg) {
  switch (reg.type) {
    case SM3_REG_TEMP: {
      uint32_t var = get_or_create_temp(reg.index);
      uint32_t loaded = m_spirv->load(m_vec4, var);
      return emit_swizzle(loaded, reg);
    }
    case SM3_REG_INPUT:
    case SM3_REG_TEXTURE: {
      // t0, t1 etc. map to texcoord inputs v0, v1 in the same index space
      uint32_t var = get_or_create_input(reg.index);
      uint32_t loaded = m_spirv->load(m_vec4, var);
      return emit_swizzle(loaded, reg);
    }
    case SM3_REG_CONST: {
      uint32_t cval = get_or_create_const_float(reg.index);
      return emit_swizzle(cval, reg);
    }
    case SM3_REG_CONST_BOOL: {
      uint32_t boolId = get_or_create_const_bool(reg.index);
      // Boolean scalar → promote to vec4 for uniform handling
      return m_spirv->compositeConstruct(m_vec4, {boolId, boolId, boolId, boolId});
    }
    case SM3_REG_CONST_INT: {
      uint32_t ival = get_or_create_const_int(reg.index);
      return emit_swizzle(ival, reg);
    }
    case SM3_REG_OUTPUT: {
      uint32_t var = get_or_create_output(reg.index);
      uint32_t loaded = m_spirv->load(m_vec4, var);
      return emit_swizzle(loaded, reg);
    }
    case SM3_REG_PREDICATE: {
      uint32_t predVar = get_or_create_predicate(reg.index);
      uint32_t loaded = m_spirv->load(m_boolType, predVar);
      // Promote bool to vec4
      return m_spirv->compositeConstruct(m_vec4, {loaded, loaded, loaded, loaded});
    }
    default:
      VKWIND_WARN(kTag, "Unsupported register type %u for load", reg.type);
      return m_spirv->constant(m_floatType, 0);
  }
}

void SM3Translator::emit_store_register(const SM3Register& reg, uint32_t value) {
  switch (reg.type) {
    case SM3_REG_TEMP: {
      uint32_t var = get_or_create_temp(reg.index);
      if (reg.writeMask != 0xF) {
        uint32_t old = m_spirv->load(m_vec4, var);
        std::vector<uint32_t> components;
        uint32_t srcIdx = 0;
        for (uint32_t i = 0; i < 4; i++) {
          if (reg.writeMask & (1 << i)) {
            components.push_back(m_spirv->compositeExtract(m_floatType, value, srcIdx++));
          } else {
            components.push_back(m_spirv->compositeExtract(m_floatType, old, i));
          }
        }
        value = m_spirv->compositeConstruct(m_vec4, components);
      }
      m_spirv->store(var, value);
      break;
    }
    case SM3_REG_OUTPUT:
    case SM3_REG_TEXTURE: {
      // TEXCOORD writes to texture registers which are outputs in VS, inputs in PS
      // Treat as output for store (get_or_create_output handles both)
      uint32_t var = get_or_create_output(reg.index);
      if (reg.writeMask != 0xF) {
        uint32_t old = m_spirv->load(m_vec4, var);
        std::vector<uint32_t> components;
        uint32_t srcIdx = 0;
        for (uint32_t i = 0; i < 4; i++) {
          if (reg.writeMask & (1 << i)) {
            components.push_back(m_spirv->compositeExtract(m_floatType, value, srcIdx++));
          } else {
            components.push_back(m_spirv->compositeExtract(m_floatType, old, i));
          }
        }
        value = m_spirv->compositeConstruct(m_vec4, components);
      }
      m_spirv->store(var, value);
      break;
    }
    default:
      VKWIND_WARN(kTag, "Unsupported register type %u for store", reg.type);
      break;
  }
}

// --- Main instruction emission ---

void SM3Translator::emit_instruction(const SM3Instruction& inst) {
  auto& S = *m_spirv;

  // Buffer instructions between CALL and its target LABEL.
  // These are continuation code that must execute AFTER the subroutine returns,
  // so we emit them at RET time, not now.
  if (m_inCallContinuation && inst.opcode != SM3_OP_LABEL) {
    m_continuationBuffer.push_back(inst);
    return;
  }

  switch (inst.opcode) {
    case SM3_OP_MOV: {
      uint32_t src = emit_load_register(inst.src[0]);
      emit_store_register(inst.dest, src);
      break;
    }

    case SM3_OP_ADD: {
      uint32_t a = emit_load_register(inst.src[0]);
      uint32_t b = emit_load_register(inst.src[1]);
      uint32_t r = S.fadd(m_vec4, a, b);
      emit_store_register(inst.dest, r);
      break;
    }

    case SM3_OP_SUB: {
      uint32_t a = emit_load_register(inst.src[0]);
      uint32_t b = emit_load_register(inst.src[1]);
      uint32_t r = S.fsub(m_vec4, a, b);
      emit_store_register(inst.dest, r);
      break;
    }

    case SM3_OP_MUL: {
      uint32_t a = emit_load_register(inst.src[0]);
      uint32_t b = emit_load_register(inst.src[1]);
      uint32_t r = S.fmul(m_vec4, a, b);
      emit_store_register(inst.dest, r);
      break;
    }

    case SM3_OP_MAD: {
      uint32_t a = emit_load_register(inst.src[0]);
      uint32_t b = emit_load_register(inst.src[1]);
      uint32_t c = emit_load_register(inst.src[2]);
      uint32_t ab = S.fmul(m_vec4, a, b);
      uint32_t r = S.fadd(m_vec4, ab, c);
      emit_store_register(inst.dest, r);
      break;
    }

    case SM3_OP_RCP: {
      uint32_t src = emit_load_register(inst.src[0]);
      uint32_t comp = S.compositeExtract(m_floatType, src, 0);
      uint32_t one = S.constant(m_floatType, 0x3F800000); // 1.0f
      uint32_t rcp = S.fdiv(m_floatType, one, comp);
      uint32_t r = S.compositeConstruct(m_vec4, {rcp, rcp, rcp, rcp});
      emit_store_register(inst.dest, r);
      break;
    }

    case SM3_OP_RSQ: {
      uint32_t src = emit_load_register(inst.src[0]);
      uint32_t comp = S.compositeExtract(m_floatType, src, 0);
      uint32_t abs_val = S.fabs(m_floatType, comp);
      uint32_t rsq = S.extInst(m_floatType, 32, {abs_val}); // GLSLstd450InverseSqrt
      uint32_t r = S.compositeConstruct(m_vec4, {rsq, rsq, rsq, rsq});
      emit_store_register(inst.dest, r);
      break;
    }

    case SM3_OP_DP3: {
      uint32_t a = emit_load_register(inst.src[0]);
      uint32_t b = emit_load_register(inst.src[1]);
      uint32_t dot = S.fdot(m_floatType, a, b);
      uint32_t r = S.compositeConstruct(m_vec4, {dot, dot, dot, dot});
      emit_store_register(inst.dest, r);
      break;
    }

    case SM3_OP_DP4: {
      uint32_t a = emit_load_register(inst.src[0]);
      uint32_t b = emit_load_register(inst.src[1]);
      uint32_t dot = S.fdot(m_floatType, a, b);
      uint32_t r = S.compositeConstruct(m_vec4, {dot, dot, dot, dot});
      emit_store_register(inst.dest, r);
      break;
    }

    case SM3_OP_MIN: {
      uint32_t a = emit_load_register(inst.src[0]);
      uint32_t b = emit_load_register(inst.src[1]);
      uint32_t r = S.fmin(m_vec4, a, b);
      emit_store_register(inst.dest, r);
      break;
    }

    case SM3_OP_MAX: {
      uint32_t a = emit_load_register(inst.src[0]);
      uint32_t b = emit_load_register(inst.src[1]);
      uint32_t r = S.fmax(m_vec4, a, b);
      emit_store_register(inst.dest, r);
      break;
    }

    case SM3_OP_SLT: {
      uint32_t a = emit_load_register(inst.src[0]);
      uint32_t b = emit_load_register(inst.src[1]);
      uint32_t cmp = S.fordlessthan(m_bvec4, a, b);
      uint32_t trueVal = S.constant(m_floatType, 0x3F800000);
      uint32_t falseVal = S.constant(m_floatType, 0);
      uint32_t tv4 = S.compositeConstruct(m_vec4, {trueVal, trueVal, trueVal, trueVal});
      uint32_t fv4 = S.compositeConstruct(m_vec4, {falseVal, falseVal, falseVal, falseVal});
      uint32_t r = S.select(m_vec4, cmp, tv4, fv4);
      emit_store_register(inst.dest, r);
      break;
    }

    case SM3_OP_SGE: {
      uint32_t a = emit_load_register(inst.src[0]);
      uint32_t b = emit_load_register(inst.src[1]);
      uint32_t cmp = S.fordgreaterequal(m_bvec4, a, b);
      uint32_t trueVal = S.constant(m_floatType, 0x3F800000);
      uint32_t falseVal = S.constant(m_floatType, 0);
      uint32_t tv4 = S.compositeConstruct(m_vec4, {trueVal, trueVal, trueVal, trueVal});
      uint32_t fv4 = S.compositeConstruct(m_vec4, {falseVal, falseVal, falseVal, falseVal});
      uint32_t r = S.select(m_vec4, cmp, tv4, fv4);
      emit_store_register(inst.dest, r);
      break;
    }

    case SM3_OP_FRC: {
      uint32_t src = emit_load_register(inst.src[0]);
      uint32_t r = S.extInst(m_vec4, 12, {src}); // Fract
      emit_store_register(inst.dest, r);
      break;
    }

    case SM3_OP_ABS: {
      uint32_t src = emit_load_register(inst.src[0]);
      uint32_t r = S.fabs(m_vec4, src);
      emit_store_register(inst.dest, r);
      break;
    }

    case SM3_OP_NRM: {
      uint32_t src = emit_load_register(inst.src[0]);
      uint32_t r = S.fnormalize(m_vec4, src);
      emit_store_register(inst.dest, r);
      break;
    }

    case SM3_OP_EXP: {
      uint32_t src = emit_load_register(inst.src[0]);
      uint32_t comp = S.compositeExtract(m_floatType, src, 0);
      // exp2(x)
      uint32_t exp_val = S.extInst(m_floatType, 29, {comp}); // Exp2
      uint32_t r = S.compositeConstruct(m_vec4, {exp_val, exp_val, exp_val, exp_val});
      emit_store_register(inst.dest, r);
      break;
    }

    case SM3_OP_LOG: {
      uint32_t src = emit_load_register(inst.src[0]);
      uint32_t comp = S.compositeExtract(m_floatType, src, 0);
      // log2(x)
      uint32_t abs_val = S.fabs(m_floatType, comp);
      uint32_t log_val = S.extInst(m_floatType, 30, {abs_val}); // Log2
      uint32_t r = S.compositeConstruct(m_vec4, {log_val, log_val, log_val, log_val});
      emit_store_register(inst.dest, r);
      break;
    }

    case SM3_OP_LIT: {
      uint32_t src = emit_load_register(inst.src[0]);
      uint32_t x = S.compositeExtract(m_floatType, src, 0);
      uint32_t y = S.compositeExtract(m_floatType, src, 1);
      uint32_t w = S.compositeExtract(m_floatType, src, 3);
      uint32_t one = S.constant(m_floatType, 0x3F800000);
      uint32_t zero = S.constant(m_floatType, 0);
      uint32_t pow_val = S.extInst(m_floatType, 26, {y, w}); // Pow
      uint32_t result_x = S.fmax(m_floatType, x, zero);
      uint32_t result_y = one;
      uint32_t result_z = S.fclamp(m_floatType, pow_val, zero, one);
      uint32_t result_w = one;
      uint32_t r = S.compositeConstruct(m_vec4, {result_x, result_y, result_z, result_w});
      emit_store_register(inst.dest, r);
      break;
    }

    case SM3_OP_LRP: {
      uint32_t a = emit_load_register(inst.src[0]);
      uint32_t b = emit_load_register(inst.src[1]);
      uint32_t c = emit_load_register(inst.src[2]);
      // lerp = a * b + (1 - a) * c = a * b + c - a * c
      uint32_t ab = S.fmul(m_vec4, a, b);
      uint32_t ac = S.fmul(m_vec4, a, c);
      uint32_t r = S.fadd(m_vec4, S.fsub(m_vec4, c, ac), ab);
      emit_store_register(inst.dest, r);
      break;
    }

    case SM3_OP_CMP: {
      uint32_t a = emit_load_register(inst.src[0]);
      uint32_t b = emit_load_register(inst.src[1]);
      uint32_t c = emit_load_register(inst.src[2]);
      // CMP: for each component, if src0 >= 0 then src1 else src2
      uint32_t zero = S.constant(m_floatType, 0);
      uint32_t zero4 = S.compositeConstruct(m_vec4, {zero, zero, zero, zero});
      uint32_t cmp = S.fordgreaterequal(m_bvec4, a, zero4);
      uint32_t r = S.select(m_vec4, cmp, b, c);
      emit_store_register(inst.dest, r);
      break;
    }

    case SM3_OP_CRS: {
      uint32_t a = emit_load_register(inst.src[0]);
      uint32_t b = emit_load_register(inst.src[1]);
      uint32_t r = S.fcross(m_vec4, a, b);
      emit_store_register(inst.dest, r);
      break;
    }

    case SM3_OP_DST: {
      uint32_t a = emit_load_register(inst.src[0]);
      uint32_t b = emit_load_register(inst.src[1]);
      // DST: dest = (1, a.y * b.y, a.z, b.w)
      uint32_t aY = S.compositeExtract(m_floatType, a, 1);
      uint32_t bY = S.compositeExtract(m_floatType, b, 1);
      uint32_t aZ = S.compositeExtract(m_floatType, a, 2);
      uint32_t bW = S.compositeExtract(m_floatType, b, 3);
      uint32_t one = S.constant(m_floatType, 0x3F800000);
      uint32_t y = S.fmul(m_floatType, aY, bY);
      uint32_t r = S.compositeConstruct(m_vec4, {one, y, aZ, bW});
      emit_store_register(inst.dest, r);
      break;
    }

    case SM3_OP_POW: {
      uint32_t a = emit_load_register(inst.src[0]);
      uint32_t b = emit_load_register(inst.src[1]);
      uint32_t ax = S.compositeExtract(m_floatType, a, 0);
      uint32_t bx = S.compositeExtract(m_floatType, b, 0);
      uint32_t abs_val = S.fabs(m_floatType, ax);
      uint32_t r = S.extInst(m_floatType, 26, {abs_val, bx}); // Pow
      uint32_t rv = S.compositeConstruct(m_vec4, {r, r, r, r});
      emit_store_register(inst.dest, rv);
      break;
    }

    case SM3_OP_M4x4: {
      uint32_t src = emit_load_register(inst.src[0]);
      uint32_t baseRegIndex = inst.src[1].index;
      // M4x4: result[i] = dot(src, c[base+i]) for i=0..3
      uint32_t results[4];
      for (uint32_t col = 0; col < 4; col++) {
        uint32_t cReg = get_or_create_const_float(baseRegIndex + col);
        uint32_t dot = S.fdot(m_floatType, src, cReg);
        results[col] = dot;
      }
      uint32_t r = S.compositeConstruct(m_vec4, {results[0], results[1], results[2], results[3]});
      emit_store_register(inst.dest, r);
      break;
    }

    case SM3_OP_M4x3: {
      uint32_t src = emit_load_register(inst.src[0]);
      uint32_t baseRegIndex = inst.src[1].index;
      uint32_t results[3];
      for (uint32_t col = 0; col < 3; col++) {
        uint32_t cReg = get_or_create_const_float(baseRegIndex + col);
        results[col] = S.fdot(m_vec4, src, cReg);
      }
      uint32_t r = S.compositeConstruct(m_vec4, {results[0], results[1], results[2], results[2]});
      emit_store_register(inst.dest, r);
      break;
    }

    case SM3_OP_M3x4: {
      uint32_t src = emit_load_register(inst.src[0]);
      uint32_t baseRegIndex = inst.src[1].index;
      // M3x4: 3 input components, 4 output components
      uint32_t x = S.compositeExtract(m_floatType, src, 0);
      uint32_t y = S.compositeExtract(m_floatType, src, 1);
      uint32_t z = S.compositeExtract(m_floatType, src, 2);
      uint32_t results[4];
      for (uint32_t col = 0; col < 4; col++) {
        uint32_t cReg = get_or_create_const_float(baseRegIndex + col);
        uint32_t cX = S.compositeExtract(m_floatType, cReg, 0);
        uint32_t cY = S.compositeExtract(m_floatType, cReg, 1);
        uint32_t cZ = S.compositeExtract(m_floatType, cReg, 2);
        uint32_t xy = S.fmul(m_floatType, x, cX);
        uint32_t yy = S.fmul(m_floatType, y, cY);
        uint32_t zy = S.fmul(m_floatType, z, cZ);
        uint32_t t1 = S.fadd(m_floatType, xy, yy);
        results[col] = S.fadd(m_floatType, t1, zy);
      }
      uint32_t r = S.compositeConstruct(m_vec4, {results[0], results[1], results[2], results[3]});
      emit_store_register(inst.dest, r);
      break;
    }

    case SM3_OP_M3x3: {
      uint32_t src = emit_load_register(inst.src[0]);
      uint32_t baseRegIndex = inst.src[1].index;
      uint32_t x = S.compositeExtract(m_floatType, src, 0);
      uint32_t y = S.compositeExtract(m_floatType, src, 1);
      uint32_t z = S.compositeExtract(m_floatType, src, 2);
      uint32_t results[3];
      for (uint32_t col = 0; col < 3; col++) {
        uint32_t cReg = get_or_create_const_float(baseRegIndex + col);
        uint32_t cX = S.compositeExtract(m_floatType, cReg, 0);
        uint32_t cY = S.compositeExtract(m_floatType, cReg, 1);
        uint32_t cZ = S.compositeExtract(m_floatType, cReg, 2);
        uint32_t xy = S.fmul(m_floatType, x, cX);
        uint32_t yy = S.fmul(m_floatType, y, cY);
        uint32_t zy = S.fmul(m_floatType, z, cZ);
        uint32_t t1 = S.fadd(m_floatType, xy, yy);
        results[col] = S.fadd(m_floatType, t1, zy);
      }
      uint32_t r = S.compositeConstruct(m_vec4, {results[0], results[1], results[2], results[2]});
      emit_store_register(inst.dest, r);
      break;
    }

    case SM3_OP_M3x2: {
      uint32_t src = emit_load_register(inst.src[0]);
      uint32_t baseRegIndex = inst.src[1].index;
      uint32_t x = S.compositeExtract(m_floatType, src, 0);
      uint32_t y = S.compositeExtract(m_floatType, src, 1);
      uint32_t z = S.compositeExtract(m_floatType, src, 2);
      uint32_t results[2];
      for (uint32_t col = 0; col < 2; col++) {
        uint32_t cReg = get_or_create_const_float(baseRegIndex + col);
        uint32_t cX = S.compositeExtract(m_floatType, cReg, 0);
        uint32_t cY = S.compositeExtract(m_floatType, cReg, 1);
        uint32_t cZ = S.compositeExtract(m_floatType, cReg, 2);
        uint32_t xy = S.fmul(m_floatType, x, cX);
        uint32_t yy = S.fmul(m_floatType, y, cY);
        uint32_t zy = S.fmul(m_floatType, z, cZ);
        uint32_t t1 = S.fadd(m_floatType, xy, yy);
        results[col] = S.fadd(m_floatType, t1, zy);
      }
      uint32_t r = S.compositeConstruct(m_vec4, {results[0], results[1], results[1], results[1]});
      emit_store_register(inst.dest, r);
      break;
    }

    case SM3_OP_SINCOS: {
      uint32_t src = emit_load_register(inst.src[0]);
      uint32_t comp = S.compositeExtract(m_floatType, src, 0);
      uint32_t sin_val = S.fsin(m_floatType, comp);
      uint32_t cos_val = S.fcos(m_floatType, comp);
      uint32_t r = S.compositeConstruct(m_vec4, {sin_val, sin_val, cos_val, cos_val});
      emit_store_register(inst.dest, r);
      break;
    }

    case SM3_OP_SGN: {
      // SGN: sign of each component, clamped to -1/0/+1
      // src[0] = value, src[1] = range min (unused in SM3), src[2] = range max (unused in SM3)
      uint32_t src = emit_load_register(inst.src[0]);
      uint32_t r = S.fsign(m_vec4, src);
      emit_store_register(inst.dest, r);
      break;
    }

    case SM3_OP_EXPP: {
      // EXPP: partial precision exp2 — result in .x, replicated to all components
      uint32_t src = emit_load_register(inst.src[0]);
      uint32_t comp = S.compositeExtract(m_floatType, src, 0);
      uint32_t floor_val = S.ffloor(m_floatType, comp);
      uint32_t exp_val = S.fexp2(m_floatType, floor_val);
      uint32_t r = S.compositeConstruct(m_vec4, {exp_val, exp_val, exp_val, exp_val});
      emit_store_register(inst.dest, r);
      break;
    }

    case SM3_OP_LOGP: {
      // LOGP: partial precision log2 — result in .x, replicated to all components
      uint32_t src = emit_load_register(inst.src[0]);
      uint32_t comp = S.compositeExtract(m_floatType, src, 0);
      uint32_t abs_val = S.fabs(m_floatType, comp);
      uint32_t log_val = S.flog2(m_floatType, abs_val);
      uint32_t r = S.compositeConstruct(m_vec4, {log_val, log_val, log_val, log_val});
      emit_store_register(inst.dest, r);
      break;
    }

    case SM3_OP_CND: {
      // CND: dest = (src0.x > 0.5) ? src1 : src2
      uint32_t src0 = emit_load_register(inst.src[0]);
      uint32_t src1 = emit_load_register(inst.src[1]);
      uint32_t src2 = emit_load_register(inst.src[2]);
      uint32_t src0x = S.compositeExtract(m_floatType, src0, 0);
      uint32_t half = S.constant(m_floatType, 0x3F000000); // 0.5
      uint32_t cond = S.fordgreaterthan(m_boolType, src0x, half);
      uint32_t cond4 = S.compositeConstruct(m_bvec4, {cond, cond, cond, cond});
      uint32_t r = S.select(m_vec4, cond4, src1, src2);
      emit_store_register(inst.dest, r);
      break;
    }

    case SM3_OP_DP2ADD: {
      // DP2ADD: dest.x = dot(src0.xy, src1.xy) + src2.x, broadcast
      uint32_t a = emit_load_register(inst.src[0]);
      uint32_t b = emit_load_register(inst.src[1]);
      uint32_t c = emit_load_register(inst.src[2]);
      uint32_t aX = S.compositeExtract(m_floatType, a, 0);
      uint32_t aY = S.compositeExtract(m_floatType, a, 1);
      uint32_t bX = S.compositeExtract(m_floatType, b, 0);
      uint32_t bY = S.compositeExtract(m_floatType, b, 1);
      uint32_t cX = S.compositeExtract(m_floatType, c, 0);
      uint32_t abX = S.fmul(m_floatType, aX, bX);
      uint32_t abY = S.fmul(m_floatType, aY, bY);
      uint32_t dot = S.fadd(m_floatType, abX, abY);
      uint32_t result = S.fadd(m_floatType, dot, cX);
      uint32_t r = S.compositeConstruct(m_vec4, {result, result, result, result});
      emit_store_register(inst.dest, r);
      break;
    }

    case SM3_OP_DSX: {
      // DSX: screen-space partial derivative in X
      uint32_t src = emit_load_register(inst.src[0]);
      uint32_t r = S.dfdx(m_vec4, src);
      emit_store_register(inst.dest, r);
      break;
    }

    case SM3_OP_DSY: {
      // DSY: screen-space partial derivative in Y
      uint32_t src = emit_load_register(inst.src[0]);
      uint32_t r = S.dfdy(m_vec4, src);
      emit_store_register(inst.dest, r);
      break;
    }

    case SM3_OP_BEM: {
      // BEM: dest = (src0 - constBias) * fetchHeight + 1.0
      // src[0] = texture coordinate, src[1] = bump env map sample
      uint32_t coord = emit_load_register(inst.src[0]);
      uint32_t bump = emit_load_register(inst.src[1]);
      uint32_t one = S.constant(m_floatType, 0x3F800000);
      uint32_t one4 = S.compositeConstruct(m_vec4, {one, one, one, one});
      uint32_t diff = S.fsub(m_vec4, coord, bump);
      uint32_t r = S.fadd(m_vec4, diff, one4);
      emit_store_register(inst.dest, r);
      break;
    }

    case SM3_OP_TEX: {
      if (inst.src.size() < 1) break;
      uint32_t samplerIndex = inst.src[0].index;

      // Load texture coordinate (vec4) and extract .xy for 2D sampling
      uint32_t coord = emit_load_register(inst.src[0]);
      uint32_t coord_x = S.compositeExtract(m_floatType, coord, 0);
      uint32_t coord_y = S.compositeExtract(m_floatType, coord, 1);
      uint32_t coordVec2 = S.compositeConstruct(m_vec2, {coord_x, coord_y});

      // Ensure the combined image sampler variable exists
      uint32_t combinedSampler = get_or_create_sampler(samplerIndex);

      // Load the SampledImage from the combined variable using shared types
      uint32_t sampledImg = S.load(m_texSampledImageType, combinedSampler);
      uint32_t texColor = S.imageSampleImplicitLod(m_vec4, sampledImg, coordVec2);

      emit_store_register(inst.dest, texColor);
      break;
    }

    case SM3_OP_TEXCOORD: {
      // TEXCOORD: in pixel shader, declare interpolated texture coordinate
      // In vertex shader, just pass through input
      if (m_isVertexShader) {
        if (!inst.src.empty()) {
          uint32_t src = emit_load_register(inst.src[0]);
          emit_store_register(inst.dest, src);
        }
      } else {
        // In PS, TEXCOORD reads the interpolated t register
        // For now, just pass through to dest
        if (!inst.src.empty()) {
          uint32_t src = emit_load_register(inst.src[0]);
          emit_store_register(inst.dest, src);
        }
      }
      break;
    }

    case SM3_OP_TEXKILL: {
      // Discard fragment if any component < 0
      uint32_t src = emit_load_register(inst.dest);
      uint32_t x = S.compositeExtract(m_floatType, src, 0);
      uint32_t y = S.compositeExtract(m_floatType, src, 1);
      uint32_t z = S.compositeExtract(m_floatType, src, 2);
      uint32_t w = S.compositeExtract(m_floatType, src, 3);
      uint32_t zero = S.constant(m_floatType, 0);

      uint32_t cmpX = S.fordlessthan(m_boolType, x, zero);
      uint32_t cmpY = S.fordlessthan(m_boolType, y, zero);
      uint32_t cmpZ = S.fordlessthan(m_boolType, z, zero);
      uint32_t cmpW = S.fordlessthan(m_boolType, w, zero);
      uint32_t anyXY = S.logicalor(m_boolType, cmpX, cmpY);
      uint32_t anyZW = S.logicalor(m_boolType, cmpZ, cmpW);
      uint32_t anyNeg = S.logicalor(m_boolType, anyXY, anyZW);

      uint32_t mergeLabel = S.alloc();
      uint32_t killLabel = S.alloc();
      uint32_t contLabel = S.alloc();

      S.selectionMerge(mergeLabel);
      S.branchConditional(anyNeg, killLabel, contLabel);
      S.label(killLabel);
      S.kill();
      S.label(contLabel);
      S.branch(mergeLabel);
      S.label(mergeLabel);
      break;
    }

    case SM3_OP_TEXLDD: {
      // TEXLDD: texture sample with explicit gradients (ddx, ddy)
      // src[0] = sampler, src[1] = coord, src[2] = ddx, src[3] = ddy
      if (inst.src.size() < 4) break;
      uint32_t samplerIndex = inst.src[0].index;
      uint32_t coord = emit_load_register(inst.src[1]);
      uint32_t ddx = emit_load_register(inst.src[2]);
      uint32_t ddy = emit_load_register(inst.src[3]);
      uint32_t coord_x = S.compositeExtract(m_floatType, coord, 0);
      uint32_t coord_y = S.compositeExtract(m_floatType, coord, 1);
      uint32_t coordVec2 = S.compositeConstruct(m_vec2, {coord_x, coord_y});
      uint32_t combinedSampler = get_or_create_sampler(samplerIndex);
      uint32_t sampledImg = S.load(m_texSampledImageType, combinedSampler);

      // Extract gradient components for explicit LOD sampling
      uint32_t ddx_x = S.compositeExtract(m_floatType, ddx, 0);
      uint32_t ddx_y = S.compositeExtract(m_floatType, ddx, 1);
      uint32_t ddy_x = S.compositeExtract(m_floatType, ddy, 0);
      uint32_t ddy_y = S.compositeExtract(m_floatType, ddy, 1);
      uint32_t gradVecDx = S.compositeConstruct(m_vec2, {ddx_x, ddx_y});
      uint32_t gradVecDy = S.compositeConstruct(m_vec2, {ddy_x, ddy_y});

      // OpImageSampleExplicitLod with Grad operand (0x4)
      uint32_t id = S.alloc();
      // Word count: 4 (result type + id + sampledimage + coord) + 1 (operand kind) + 4 (dx + dy) = 9
      S.op(88, 9); // OpImageSampleExplicitLod
      S.emit(m_vec4);  // result type
      S.emit(id);       // result id
      S.emit(sampledImg); // sampled image
      S.emit(coordVec2);  // coordinate
      S.emit(0x4);        // Grad operand kind
      S.emit(gradVecDx);  // dx
      S.emit(gradVecDy);  // dy
      emit_store_register(inst.dest, id);
      break;
    }

    case SM3_OP_TEXLDL: {
      // TEXLDL: texture sample with explicit LOD level
      // src[0] = sampler, src[1] = coord, src[2] = lod (float)
      if (inst.src.size() < 3) break;
      uint32_t samplerIndex = inst.src[0].index;
      uint32_t coord = emit_load_register(inst.src[1]);
      uint32_t lodSrc = emit_load_register(inst.src[2]);
      uint32_t lodComp = S.compositeExtract(m_floatType, lodSrc, 0);
      uint32_t coord_x = S.compositeExtract(m_floatType, coord, 0);
      uint32_t coord_y = S.compositeExtract(m_floatType, coord, 1);
      uint32_t coordVec2 = S.compositeConstruct(m_vec2, {coord_x, coord_y});
      uint32_t combinedSampler = get_or_create_sampler(samplerIndex);
      uint32_t sampledImg = S.load(m_texSampledImageType, combinedSampler);

      // Convert float LOD to int
      uint32_t lodInt = S.fconvToS(m_intType, lodComp);
      uint32_t texColor = S.imageSampleExplicitLod(m_vec4, sampledImg, coordVec2, lodInt);
      emit_store_register(inst.dest, texColor);
      break;
    }

    case SM3_OP_TEXBEM: {
      // TEXBEM: texture with bump environment mapping offset
      // src[0] = sampler, src[1] = bump env map sample
      if (inst.src.size() < 2) break;
      uint32_t samplerIndex = inst.src[0].index;
      uint32_t bumpSample = emit_load_register(inst.src[1]);
      uint32_t combinedSampler = get_or_create_sampler(samplerIndex);
      uint32_t sampledImg = S.load(m_texSampledImageType, combinedSampler);

      // Use bump sample .xy as coordinates for now
      uint32_t coord_x = S.compositeExtract(m_floatType, bumpSample, 0);
      uint32_t coord_y = S.compositeExtract(m_floatType, bumpSample, 1);
      uint32_t coordVec2 = S.compositeConstruct(m_vec2, {coord_x, coord_y});
      uint32_t texColor = S.imageSampleImplicitLod(m_vec4, sampledImg, coordVec2);
      emit_store_register(inst.dest, texColor);
      break;
    }

    case SM3_OP_TEXBEML: {
      // TEXBEML: TEXBEM + luminance compensation
      if (inst.src.size() < 2) break;
      uint32_t samplerIndex = inst.src[0].index;
      uint32_t bumpSample = emit_load_register(inst.src[1]);
      uint32_t combinedSampler = get_or_create_sampler(samplerIndex);
      uint32_t sampledImg = S.load(m_texSampledImageType, combinedSampler);

      uint32_t coord_x = S.compositeExtract(m_floatType, bumpSample, 0);
      uint32_t coord_y = S.compositeExtract(m_floatType, bumpSample, 1);
      uint32_t coordVec2 = S.compositeConstruct(m_vec2, {coord_x, coord_y});
      uint32_t texColor = S.imageSampleImplicitLod(m_vec4, sampledImg, coordVec2);
      emit_store_register(inst.dest, texColor);
      break;
    }

    case SM3_OP_TEXREG2AR: {
      // TEXREG2AR: use .a and .r as 2D tex coords
      if (inst.src.empty()) break;
      uint32_t samplerIndex = inst.src[0].index;
      uint32_t src = emit_load_register(inst.src[0]);
      uint32_t coord_a = S.compositeExtract(m_floatType, src, 3); // .a
      uint32_t coord_r = S.compositeExtract(m_floatType, src, 0); // .r
      uint32_t coordVec2 = S.compositeConstruct(m_vec2, {coord_a, coord_r});
      uint32_t combinedSampler = get_or_create_sampler(samplerIndex);
      uint32_t sampledImg = S.load(m_texSampledImageType, combinedSampler);
      uint32_t texColor = S.imageSampleImplicitLod(m_vec4, sampledImg, coordVec2);
      emit_store_register(inst.dest, texColor);
      break;
    }

    case SM3_OP_TEXREG2GB: {
      // TEXREG2GB: use .g and .b as 2D tex coords
      if (inst.src.empty()) break;
      uint32_t samplerIndex = inst.src[0].index;
      uint32_t src = emit_load_register(inst.src[0]);
      uint32_t coord_g = S.compositeExtract(m_floatType, src, 1); // .g
      uint32_t coord_b = S.compositeExtract(m_floatType, src, 2); // .b
      uint32_t coordVec2 = S.compositeConstruct(m_vec2, {coord_g, coord_b});
      uint32_t combinedSampler = get_or_create_sampler(samplerIndex);
      uint32_t sampledImg = S.load(m_texSampledImageType, combinedSampler);
      uint32_t texColor = S.imageSampleImplicitLod(m_vec4, sampledImg, coordVec2);
      emit_store_register(inst.dest, texColor);
      break;
    }

    case SM3_OP_TEXREG2RGB: {
      // TEXREG2RGB: 3D texture lookup using .rgb as 3D coordinates
      // D3D9: dst = tex3D(sampler, src.rgb) — uses .r/.g/.b as (u,v,w)
      if (inst.src.empty()) break;
      uint32_t samplerIndex = inst.src[0].index;
      uint32_t src = emit_load_register(inst.src[0]);
      uint32_t coord_x = S.compositeExtract(m_floatType, src, 0); // .r
      uint32_t coord_y = S.compositeExtract(m_floatType, src, 1); // .g
      uint32_t coord_z = S.compositeExtract(m_floatType, src, 2); // .b
      uint32_t coordVec3 = S.compositeConstruct(m_vec3, {coord_x, coord_y, coord_z});
      uint32_t combinedSampler = get_or_create_sampler(samplerIndex);
      uint32_t sampledImg = S.load(m_texSampledImageType, combinedSampler);
      uint32_t texColor = S.imageSampleImplicitLod(m_vec4, sampledImg, coordVec3);
      emit_store_register(inst.dest, texColor);
      break;
    }

    case SM3_OP_TEXDP3: {
      // TEXDP3: 3-component dot product for tex coord gen
      if (inst.src.size() < 2) break;
      uint32_t a = emit_load_register(inst.src[0]);
      uint32_t b = emit_load_register(inst.src[1]);
      uint32_t dot = S.fdot(m_vec4, a, b);
      uint32_t r = S.compositeConstruct(m_vec4, {dot, dot, dot, dot});
      emit_store_register(inst.dest, r);
      break;
    }

    case SM3_OP_TEXDP3TEX: {
      // TEXDP3TEX: dot3 coord gen + texture sample
      if (inst.src.size() < 2) break;
      uint32_t samplerIndex = inst.src[0].index;
      uint32_t a = emit_load_register(inst.src[0]);
      uint32_t b = emit_load_register(inst.src[1]);
      uint32_t dot = S.fdot(m_vec4, a, b);
      uint32_t coordVec2 = S.compositeConstruct(m_vec2, {dot, dot});
      uint32_t combinedSampler = get_or_create_sampler(samplerIndex);
      uint32_t sampledImg = S.load(m_texSampledImageType, combinedSampler);
      uint32_t texColor = S.imageSampleImplicitLod(m_vec4, sampledImg, coordVec2);
      emit_store_register(inst.dest, texColor);
      break;
    }

    case SM3_OP_TEXDEPTH: {
      // TEXDEPTH: writes r.x to fragment depth (gl_FragDepth)
      uint32_t src = emit_load_register(inst.dest);
      uint32_t depthX = S.compositeExtract(m_floatType, src, 0);
      // Clamp to [0, 1]
      uint32_t zero = S.constant(m_floatType, 0);
      uint32_t one = S.constant(m_floatType, 0x3F800000);
      uint32_t clampedDepth = S.fclamp(m_floatType, depthX, zero, one);

      // Create or reuse FragDepth output variable
      if (!m_fragDepthVar) {
        auto prevSection = S.currentSection;
        S.setSection(SpirvEmitter::SEC_TYPES);
        uint32_t ptrType = S.alloc();
        S.typePointer(ptrType, 3, m_floatType); // Output storage class, float
        S.setSection(SpirvEmitter::SEC_VARIABLES);
        m_fragDepthVar = S.variable(ptrType, 3);
        S.setSection(SpirvEmitter::SEC_ANNOTATIONS);
        S.decorate(m_fragDepthVar, 11, 52); // BuiltIn FragDepth
        S.setSection(SpirvEmitter::SEC_DEBUG);
        S.name(m_fragDepthVar, "gl_FragDepth");
        S.currentSection = prevSection;
      }
      S.store(m_fragDepthVar, clampedDepth);
      break;
    }

    case SM3_OP_TEXM3x2PAD:
    case SM3_OP_TEXM3x3PAD: {
      // PAD: intermediate step in matrix texture coord generation
      // Just pass through - the actual matrix multiply happens in the final TEXM3x* op
      if (!inst.src.empty()) {
        uint32_t src = emit_load_register(inst.src[0]);
        emit_store_register(inst.dest, src);
      }
      break;
    }

    case SM3_OP_TEXM3x2TEX: {
      // TEXM3x2TEX: dst = tex2D(s, M3x2 * src.xy)
      // D3D9: src[0] = texcoord, src[1] = const base for M3x2 (cN..cN+2)
      // M3x2 stored in c[base].xy, c[base+1].xy, c[base+2].xy
      // Result = (src.x * c[base].xy) + (src.y * c[base+1].xy) + c[base+2].xy
      if (inst.src.size() < 2) break;
      uint32_t samplerIndex = inst.src[0].index;
      uint32_t coordSrc = emit_load_register(inst.src[0]);
      uint32_t baseRegIndex = inst.src[1].index;

      // Load 3 constant register rows
      uint32_t row0 = get_or_create_const_float(baseRegIndex + 0);
      uint32_t row1 = get_or_create_const_float(baseRegIndex + 1);
      uint32_t row2 = get_or_create_const_float(baseRegIndex + 2);

      // Extract xy from each row
      uint32_t row0x = S.compositeExtract(m_floatType, row0, 0);
      uint32_t row0y = S.compositeExtract(m_floatType, row0, 1);
      uint32_t row1x = S.compositeExtract(m_floatType, row1, 0);
      uint32_t row1y = S.compositeExtract(m_floatType, row1, 1);
      uint32_t row2x = S.compositeExtract(m_floatType, row2, 0);
      uint32_t row2y = S.compositeExtract(m_floatType, row2, 1);
      uint32_t row0xy = S.compositeConstruct(m_vec2, {row0x, row0y});
      uint32_t row1xy = S.compositeConstruct(m_vec2, {row1x, row1y});
      uint32_t row2xy = S.compositeConstruct(m_vec2, {row2x, row2y});

      // Compute matrix multiply
      uint32_t srcX = S.compositeExtract(m_floatType, coordSrc, 0);
      uint32_t srcY = S.compositeExtract(m_floatType, coordSrc, 1);
      uint32_t term0 = S.fvectorTimesScalar(m_vec2, row0xy, srcX);
      uint32_t term1 = S.fvectorTimesScalar(m_vec2, row1xy, srcY);
      uint32_t texCoord = S.fadd(m_vec2, S.fadd(m_vec2, term0, term1), row2xy);

      // Sample 2D texture
      uint32_t combinedSampler = get_or_create_sampler(samplerIndex);
      uint32_t sampledImg = S.load(m_texSampledImageType, combinedSampler);
      uint32_t texColor = S.imageSampleImplicitLod(m_vec4, sampledImg, texCoord);
      emit_store_register(inst.dest, texColor);
      break;
    }

    case SM3_OP_TEXM3x3TEX: {
      // TEXM3x3TEX: dst = tex2D(s, M3x3 * src.xyz)
      // D3D9: src[0] = texcoord, src[1] = const base for M3x3 (cN..cN+2)
      // M3x3 stored in c[base].xyz, c[base+1].xyz, c[base+2].xyz
      if (inst.src.size() < 2) break;
      uint32_t samplerIndex = inst.src[0].index;
      uint32_t coordSrc = emit_load_register(inst.src[0]);
      uint32_t baseRegIndex = inst.src[1].index;

      uint32_t row0 = get_or_create_const_float(baseRegIndex + 0);
      uint32_t row1 = get_or_create_const_float(baseRegIndex + 1);
      uint32_t row2 = get_or_create_const_float(baseRegIndex + 2);

      // Extract xyz from source and each row
      uint32_t srcX = S.compositeExtract(m_floatType, coordSrc, 0);
      uint32_t srcY = S.compositeExtract(m_floatType, coordSrc, 1);
      uint32_t srcZ = S.compositeExtract(m_floatType, coordSrc, 2);

      uint32_t results[3];
      for (uint32_t col = 0; col < 3; col++) {
        uint32_t r0c = S.compositeExtract(m_floatType, row0, col);
        uint32_t r1c = S.compositeExtract(m_floatType, row1, col);
        uint32_t r2c = S.compositeExtract(m_floatType, row2, col);
        uint32_t t0 = S.fmul(m_floatType, srcX, r0c);
        uint32_t t1 = S.fmul(m_floatType, srcY, r1c);
        uint32_t t2 = S.fmul(m_floatType, srcZ, r2c);
        uint32_t sum = S.fadd(m_floatType, S.fadd(m_floatType, t0, t1), t2);
        results[col] = sum;
      }
      uint32_t texCoord = S.compositeConstruct(m_vec3, {results[0], results[1], results[2]});

      uint32_t combinedSampler = get_or_create_sampler(samplerIndex);
      uint32_t sampledImg = S.load(m_texSampledImageType, combinedSampler);
      uint32_t texColor = S.imageSampleImplicitLod(m_vec4, sampledImg, texCoord);
      emit_store_register(inst.dest, texColor);
      break;
    }

    case SM3_OP_TEXM3x3SPEC: {
      // TEXM3x3SPEC: dst = texCUBE(s, reflect(-eye, M3x3 * src.xyz))
      // D3D9: src[0] = texcoord, src[1] = const base for M3x3, src[2] = eye vector
      // 1. Compute texCoord = M3x3 * src.xyz
      // 2. Compute reflection: R = 2 * dot(N, E) * N - E (where N = normalize(texCoord), E = eye)
      if (inst.src.size() < 3) break;
      uint32_t samplerIndex = inst.src[0].index;
      uint32_t coordSrc = emit_load_register(inst.src[0]);
      uint32_t baseRegIndex = inst.src[1].index;
      uint32_t eyeSrc = emit_load_register(inst.src[2]);

      uint32_t row0 = get_or_create_const_float(baseRegIndex + 0);
      uint32_t row1 = get_or_create_const_float(baseRegIndex + 1);
      uint32_t row2 = get_or_create_const_float(baseRegIndex + 2);

      uint32_t srcX = S.compositeExtract(m_floatType, coordSrc, 0);
      uint32_t srcY = S.compositeExtract(m_floatType, coordSrc, 1);
      uint32_t srcZ = S.compositeExtract(m_floatType, coordSrc, 2);

      // M3x3 * src.xyz
      uint32_t results[3];
      for (uint32_t col = 0; col < 3; col++) {
        uint32_t r0c = S.compositeExtract(m_floatType, row0, col);
        uint32_t r1c = S.compositeExtract(m_floatType, row1, col);
        uint32_t r2c = S.compositeExtract(m_floatType, row2, col);
        uint32_t t0 = S.fmul(m_floatType, srcX, r0c);
        uint32_t t1 = S.fmul(m_floatType, srcY, r1c);
        uint32_t t2 = S.fmul(m_floatType, srcZ, r2c);
        results[col] = S.fadd(m_floatType, S.fadd(m_floatType, t0, t1), t2);
      }
      uint32_t N = S.compositeConstruct(m_vec3, {results[0], results[1], results[2]});

      // Normalize N
      uint32_t nLen = S.extInst(m_floatType, 34, {N}); // Length
      uint32_t nNorm = S.fdiv(m_vec3, N, nLen);        // Normalize

      // Extract eye.xyz
      uint32_t eyeX = S.compositeExtract(m_floatType, eyeSrc, 0);
      uint32_t eyeY = S.compositeExtract(m_floatType, eyeSrc, 1);
      uint32_t eyeZ = S.compositeExtract(m_floatType, eyeSrc, 2);
      uint32_t E = S.compositeConstruct(m_vec3, {eyeX, eyeY, eyeZ});

      // Reflection: R = 2 * dot(N, E) * N - E
      uint32_t dotNE = S.fdot(m_floatType, nNorm, E);
      uint32_t two = S.constant(m_floatType, 0x40000000); // 2.0
      uint32_t twoDot = S.fmul(m_floatType, dotNE, two);
      uint32_t scaled = S.fvectorTimesScalar(m_vec3, nNorm, twoDot);
      uint32_t R = S.fsub(m_vec3, scaled, E);

      // Sample cube map with reflection vector
      uint32_t combinedSampler = get_or_create_sampler(samplerIndex);
      uint32_t sampledImg = S.load(m_texSampledImageType, combinedSampler);
      uint32_t texColor = S.imageSampleImplicitLod(m_vec4, sampledImg, R);
      emit_store_register(inst.dest, texColor);
      break;
    }

    case SM3_OP_TEXM3x3VSPEC: {
      // TEXM3x3VSPEC: dst = texCUBE(s, reflect(-eye, M3x3 * src.xyz))
      // Same as TEXM3x3SPEC but src[2] = eye vector in tangent space
      // D3D9: src[0] = texcoord, src[1] = const base for M3x3, src[2] = eye vector
      if (inst.src.size() < 3) break;
      uint32_t samplerIndex = inst.src[0].index;
      uint32_t coordSrc = emit_load_register(inst.src[0]);
      uint32_t baseRegIndex = inst.src[1].index;
      uint32_t eyeSrc = emit_load_register(inst.src[2]);

      uint32_t row0 = get_or_create_const_float(baseRegIndex + 0);
      uint32_t row1 = get_or_create_const_float(baseRegIndex + 1);
      uint32_t row2 = get_or_create_const_float(baseRegIndex + 2);

      uint32_t srcX = S.compositeExtract(m_floatType, coordSrc, 0);
      uint32_t srcY = S.compositeExtract(m_floatType, coordSrc, 1);
      uint32_t srcZ = S.compositeExtract(m_floatType, coordSrc, 2);

      // M3x3 * src.xyz
      uint32_t results[3];
      for (uint32_t col = 0; col < 3; col++) {
        uint32_t r0c = S.compositeExtract(m_floatType, row0, col);
        uint32_t r1c = S.compositeExtract(m_floatType, row1, col);
        uint32_t r2c = S.compositeExtract(m_floatType, row2, col);
        uint32_t t0 = S.fmul(m_floatType, srcX, r0c);
        uint32_t t1 = S.fmul(m_floatType, srcY, r1c);
        uint32_t t2 = S.fmul(m_floatType, srcZ, r2c);
        results[col] = S.fadd(m_floatType, S.fadd(m_floatType, t0, t1), t2);
      }
      uint32_t N = S.compositeConstruct(m_vec3, {results[0], results[1], results[2]});

      // Normalize N
      uint32_t nLen = S.extInst(m_floatType, 34, {N}); // Length
      uint32_t nNorm = S.fdiv(m_vec3, N, nLen);

      // Extract eye.xyz
      uint32_t eyeX = S.compositeExtract(m_floatType, eyeSrc, 0);
      uint32_t eyeY = S.compositeExtract(m_floatType, eyeSrc, 1);
      uint32_t eyeZ = S.compositeExtract(m_floatType, eyeSrc, 2);
      uint32_t E = S.compositeConstruct(m_vec3, {eyeX, eyeY, eyeZ});

      // Reflection: R = 2 * dot(N, E) * N - E
      uint32_t dotNE = S.fdot(m_floatType, nNorm, E);
      uint32_t two = S.constant(m_floatType, 0x40000000);
      uint32_t twoDot = S.fmul(m_floatType, dotNE, two);
      uint32_t scaled = S.fvectorTimesScalar(m_vec3, nNorm, twoDot);
      uint32_t R = S.fsub(m_vec3, scaled, E);

      // Sample cube map
      uint32_t combinedSampler = get_or_create_sampler(samplerIndex);
      uint32_t sampledImg = S.load(m_texSampledImageType, combinedSampler);
      uint32_t texColor = S.imageSampleImplicitLod(m_vec4, sampledImg, R);
      emit_store_register(inst.dest, texColor);
      break;
    }

    case SM3_OP_TEXM3x3: {
      // TEXM3x3: M3x3 matrix multiply for tex coord (no sample)
      if (inst.src.size() < 2) break;
      uint32_t baseRegIndex = inst.src[1].index;
      uint32_t src = emit_load_register(inst.src[0]);
      uint32_t results[3];
      for (uint32_t col = 0; col < 3; col++) {
        uint32_t cReg = get_or_create_const_float(baseRegIndex + col);
        results[col] = S.fdot(m_vec4, src, cReg);
      }
      uint32_t r = S.compositeConstruct(m_vec4, {results[0], results[1], results[2], results[2]});
      emit_store_register(inst.dest, r);
      break;
    }

    case SM3_OP_TEXM3x2DEPTH: {
      // TEXM3x2DEPTH: writes depth = (M3x2 * src.xy).x
      // D3D9: src[0] = texcoord, src[1] = const base for M3x2 (cN..cN+2)
      // M3x2: c[0].xy, c[1].xy, c[2].xy
      // Result.xy = src.x * c[0].xy + src.y * c[1].xy + c[2].xy
      // Depth = result.x
      if (inst.src.size() < 2) break;
      uint32_t coordSrc = emit_load_register(inst.src[0]);
      uint32_t baseRegIndex = inst.src[1].index;

      uint32_t row0 = get_or_create_const_float(baseRegIndex + 0);
      uint32_t row1 = get_or_create_const_float(baseRegIndex + 1);
      uint32_t row2 = get_or_create_const_float(baseRegIndex + 2);

      uint32_t row0x = S.compositeExtract(m_floatType, row0, 0);
      uint32_t row0y = S.compositeExtract(m_floatType, row0, 1);
      uint32_t row1x = S.compositeExtract(m_floatType, row1, 0);
      uint32_t row1y = S.compositeExtract(m_floatType, row1, 1);
      uint32_t row2x = S.compositeExtract(m_floatType, row2, 0);
      uint32_t row2y = S.compositeExtract(m_floatType, row2, 1);
      uint32_t row0xy = S.compositeConstruct(m_vec2, {row0x, row0y});
      uint32_t row1xy = S.compositeConstruct(m_vec2, {row1x, row1y});
      uint32_t row2xy = S.compositeConstruct(m_vec2, {row2x, row2y});

      uint32_t srcX = S.compositeExtract(m_floatType, coordSrc, 0);
      uint32_t srcY = S.compositeExtract(m_floatType, coordSrc, 1);
      uint32_t term0 = S.fvectorTimesScalar(m_vec2, row0xy, srcX);
      uint32_t term1 = S.fvectorTimesScalar(m_vec2, row1xy, srcY);
      uint32_t texCoord = S.fadd(m_vec2, S.fadd(m_vec2, term0, term1), row2xy);

      // Store depth value (x component) in temp register
      uint32_t depthX = S.compositeExtract(m_floatType, texCoord, 0);
      uint32_t zero = S.constant(m_floatType, 0);
      uint32_t one = S.constant(m_floatType, 0x3F800000);
      uint32_t clampedDepth = S.fclamp(m_floatType, depthX, zero, one);
      uint32_t depthVec = S.compositeConstruct(m_vec4, {clampedDepth, zero, zero, one});
      emit_store_register(inst.dest, depthVec);
      break;
    }

    case SM3_OP_MOVA: {
      // MOVA: Move to address register (a0.x / a0.xy)
      // Stores float-to-int converted value(s) for relative addressing.
      // a0.x = floor(src.x), a0.y = floor(src.y) when write mask has .y
      uint32_t src = emit_load_register(inst.src[0]);
      uint32_t addrIndex = inst.dest.index;
      uint32_t addrVar = get_or_create_private_int(addrIndex);

      // Extract .x component, floor, convert to int
      uint32_t srcX = S.compositeExtract(m_floatType, src, 0);
      uint32_t floorX = S.ffloor(m_floatType, srcX);
      uint32_t intX = S.fconvToS(m_intType, floorX);
      S.store(addrVar, intX);

      // If .y write mask, store a0.y in addrIndex+1
      if (inst.dest.writeMask & 0x2) {
        uint32_t srcY = S.compositeExtract(m_floatType, src, 1);
        uint32_t floorY = S.ffloor(m_floatType, srcY);
        uint32_t intY = S.fconvToS(m_intType, floorY);
        uint32_t addrVarY = get_or_create_private_int(addrIndex + 1);
        S.store(addrVarY, intY);
      }
      break;
    }

    case SM3_OP_IF: {
      // IF's src[0] is a boolean constant register (b#).
      // The merge/false labels are synthesized — not in bytecode.
      uint32_t boolIndex = inst.src.empty() ? 0 : inst.src[0].index;
      uint32_t condBool = get_or_create_const_bool(boolIndex);

      uint32_t mergeLabel = S.alloc();
      uint32_t trueLabel = S.alloc();
      uint32_t falseLabel = S.alloc();

      m_flowStack.push({mergeLabel, falseLabel, false});

      S.selectionMerge(mergeLabel);
      S.branchConditional(condBool, trueLabel, falseLabel);
      S.label(trueLabel);
      break;
    }

    case SM3_OP_IFC: {
      // IFC: compare src0 against 0.0 using comparison opcode.
      // The label from bytecode is the merge/skip target.
      uint32_t mergeLabel = getOrCreateLabel(inst.label);
      uint32_t trueLabel = S.alloc();
      uint32_t falseLabel = S.alloc();

      m_flowStack.push({mergeLabel, falseLabel, false});

      // Load src0 (vec4) and compare against zero
      uint32_t src0 = emit_load_register(inst.src[0]);
      uint32_t zero = S.constant(m_floatType, 0);
      uint32_t zeroVec = S.compositeConstruct(m_vec4, {zero, zero, zero, zero});

      // Perform component-wise comparison, reduce with logicalany
      uint32_t cmpResult;
      switch (inst.comparisonOpcode) {
        case 1: cmpResult = S.fordgreaterthan(m_bvec4, src0, zeroVec); break;  // GT
        case 2: cmpResult = S.fordequal(m_bvec4, src0, zeroVec); break;        // EQ
        case 3: cmpResult = S.fordgreaterequal(m_bvec4, src0, zeroVec); break; // GE
        case 4: cmpResult = S.fordlessthan(m_bvec4, src0, zeroVec); break;     // LT
        case 5: cmpResult = S.fordnotequal(m_bvec4, src0, zeroVec); break;     // NE
        case 6: cmpResult = S.fordlessthanequal(m_bvec4, src0, zeroVec); break; // LE
        default: cmpResult = S.fordgreaterequal(m_bvec4, src0, zeroVec); break;
      }
      uint32_t condBool = S.logicalany(m_boolType, cmpResult);

      S.selectionMerge(mergeLabel);
      S.branchConditional(condBool, trueLabel, falseLabel);
      S.label(trueLabel);
      break;
    }

    case SM3_OP_ELSE: {
      auto& frame = m_flowStack.top();
      frame.hasElse = true;
      // End the if-true block → jump to merge
      S.branch(frame.mergeLabel);
      // Start the else block
      S.label(frame.falseLabel);
      break;
    }

    case SM3_OP_ENDIF: {
      auto frame = m_flowStack.top();
      m_flowStack.pop();
      // End current block (if-true or else) → jump to merge
      S.branch(frame.mergeLabel);
      if (!frame.hasElse) {
        // No ELSE: empty false block needed for valid SPIR-V
        S.label(frame.falseLabel);
        S.branch(frame.mergeLabel);
      }
      // Merge point — continue here
      S.label(frame.mergeLabel);
      break;
    }

    case SM3_OP_LOOP: {
      uint32_t bodyLabel = getOrCreateLabel(inst.label);
      uint32_t mergeLabel = S.alloc();
      uint32_t headerLabel = S.alloc();
      uint32_t continueTarget = S.alloc();

      m_flowStack.push({mergeLabel, 0, false});
      m_loopStartLabel = headerLabel;
      m_loopMergeLabel = mergeLabel;
      m_loopContinueTarget = continueTarget;
      m_continueLabelOpened = false;

      S.branch(headerLabel);
      S.label(headerLabel);
      S.loopMerge(mergeLabel, continueTarget);
      S.branch(bodyLabel);
      S.label(bodyLabel);
      break;
    }

    case SM3_OP_ENDLOOP: {
      auto frame = m_flowStack.top();
      m_flowStack.pop();
      if (!m_continueLabelOpened) {
        S.branch(m_loopContinueTarget);
        S.label(m_loopContinueTarget);
      }
      S.branch(m_loopStartLabel);
      S.label(frame.mergeLabel);
      break;
    }

    case SM3_OP_REP: {
      // REP: counted loop (do-while style: body executes first, then check)
      // src[0] = address register (loop counter, i0)
      // src[1] = const int register (iteration count)
      uint32_t mergeLabel = S.alloc();
      uint32_t headerLabel = S.alloc();
      uint32_t bodyLabel = S.alloc();
      uint32_t continueTarget = S.alloc();

      // Get address register for counter
      uint32_t addrIndex = inst.src[0].index;
      m_repCounterVar = get_or_create_private_int(addrIndex);

      // Initialize counter to 0
      S.store(m_repCounterVar, S.constant(m_intType, 0));

      // Load iteration count directly from const int register
      uint32_t countReg = inst.src[1].index;
      m_repCountInt = get_or_create_const_int(countReg);

      m_flowStack.push({mergeLabel, 0, false});
      m_loopStartLabel = headerLabel;
      m_loopMergeLabel = mergeLabel;
      m_loopContinueTarget = continueTarget;
      m_continueLabelOpened = false;

      // Header block: loopMerge immediately followed by branch
      S.branch(headerLabel);
      S.label(headerLabel);
      S.loopMerge(mergeLabel, continueTarget);
      S.branch(bodyLabel);

      S.label(bodyLabel);
      break;
    }

    case SM3_OP_ENDREP: {
      auto frame = m_flowStack.top();
      m_flowStack.pop();

      // Increment counter in continue target
      if (!m_continueLabelOpened) {
        S.branch(m_loopContinueTarget);
        S.label(m_loopContinueTarget);
      }
      // counterVal++ at continue target
      uint32_t counterVal = S.load(m_intType, m_repCounterVar);
      uint32_t one = S.constant(m_intType, 1);
      uint32_t newCounter = S.iadd(m_intType, counterVal, one);
      S.store(m_repCounterVar, newCounter);

      // Check counter < count, loop back or exit
      uint32_t cmpResult = S.slessthan(m_boolType, newCounter, m_repCountInt);
      S.branchConditional(cmpResult, m_loopStartLabel, frame.mergeLabel);
      S.label(frame.mergeLabel);
      break;
    }

    case SM3_OP_BREAK: {
      // Unconditional branch to the current loop's merge label
      if (!m_flowStack.empty() && m_loopMergeLabel) {
        S.branch(m_loopMergeLabel);
      }
      break;
    }

    case SM3_OP_BREAKC: {
      if (!m_flowStack.empty() && m_loopMergeLabel) {
        uint32_t mergeLabel = m_loopMergeLabel;
        uint32_t falseLabel = m_loopContinueTarget;

        uint32_t src0 = emit_load_register(inst.src[0]);
        uint32_t zero = S.constant(m_floatType, 0);
        uint32_t zeroVec = S.compositeConstruct(m_vec4, {zero, zero, zero, zero});

        uint32_t cmpResult;
        switch (inst.comparisonOpcode) {
          case 1: cmpResult = S.fordgreaterthan(m_bvec4, src0, zeroVec); break;
          case 2: cmpResult = S.fordequal(m_bvec4, src0, zeroVec); break;
          case 3: cmpResult = S.fordgreaterequal(m_bvec4, src0, zeroVec); break;
          case 4: cmpResult = S.fordlessthan(m_bvec4, src0, zeroVec); break;
          case 5: cmpResult = S.fordnotequal(m_bvec4, src0, zeroVec); break;
          case 6: cmpResult = S.fordlessthanequal(m_bvec4, src0, zeroVec); break;
          default: cmpResult = S.fordgreaterequal(m_bvec4, src0, zeroVec); break;
        }
        uint32_t condBool = S.logicalany(m_boolType, cmpResult);

        S.branchConditional(condBool, mergeLabel, falseLabel);
        S.label(falseLabel);
        m_continueLabelOpened = true;
      }
      break;
    }

    case SM3_OP_SETP: {
      // SETP writes a boolean predicate register.
      // dest is a predicate register (p#), src0/src1 are compared.
      uint32_t predIndex = inst.dest.index;
      uint32_t predVar = get_or_create_predicate(predIndex);

      // Load src0 (vec4) and src1 (vec4)
      uint32_t src0 = emit_load_register(inst.src[0]);
      uint32_t src1 = emit_load_register(inst.src[1]);

      // Perform component-wise comparison, reduce with logicalany
      uint32_t cmpResult;
      switch (inst.comparisonOpcode) {
        case 1: cmpResult = S.fordgreaterthan(m_bvec4, src0, src1); break;  // GT
        case 2: cmpResult = S.fordequal(m_bvec4, src0, src1); break;        // EQ
        case 3: cmpResult = S.fordgreaterequal(m_bvec4, src0, src1); break; // GE
        case 4: cmpResult = S.fordlessthan(m_bvec4, src0, src1); break;     // LT
        case 5: cmpResult = S.fordnotequal(m_bvec4, src0, src1); break;     // NE
        case 6: cmpResult = S.fordlessthanequal(m_bvec4, src0, src1); break; // LE
        default: cmpResult = S.fordgreaterequal(m_bvec4, src0, src1); break;
      }
      uint32_t condBool = S.logicalany(m_boolType, cmpResult);

      S.store(predVar, condBool);
      break;
    }

    case SM3_OP_BREAKP: {
      if (!m_flowStack.empty() && m_loopMergeLabel) {
        uint32_t predIndex = inst.src.empty() ? 0 : inst.src[0].index;
        uint32_t predVar = get_or_create_predicate(predIndex);
        uint32_t condBool = S.load(m_boolType, predVar);

        S.branchConditional(condBool, m_loopMergeLabel, m_loopContinueTarget);
        S.label(m_loopContinueTarget);
        m_continueLabelOpened = true;
      }
      break;
    }

    case SM3_OP_CALL: {
      // CALL: branch to target, but DON'T open continuation block yet.
      // Continuation instructions between CALL and LABEL are buffered,
      // then emitted AFTER the subroutine returns (at RET time).
      uint32_t targetLabel = getOrCreateLabel(inst.label);
      uint32_t returnLabel = S.alloc();
      m_callReturnLabels.push(returnLabel);

      S.branch(targetLabel);
      m_inCallContinuation = true;
      m_continuationBuffer.clear();
      break;
    }

    case SM3_OP_CALLNZ: {
      uint32_t targetLabel = getOrCreateLabel(inst.label);
      uint32_t continueLabel = S.alloc();
      uint32_t callBodyLabel = S.alloc();

      uint32_t predIndex = inst.src.empty() ? 0 : inst.src[0].index;
      uint32_t predVar = get_or_create_predicate(predIndex);
      uint32_t condBool = S.load(m_boolType, predVar);

      m_callReturnLabels.push(continueLabel);

      S.selectionMerge(continueLabel, 0);
      S.branchConditional(condBool, callBodyLabel, continueLabel);

      S.label(callBodyLabel);
      S.branch(targetLabel);

      S.label(continueLabel);
      break;
    }

    case SM3_OP_RET: {
      // RET: return from subroutine
      if (!m_callReturnLabels.empty()) {
        uint32_t returnLabel = m_callReturnLabels.top();
        m_callReturnLabels.pop();
        S.branch(returnLabel);

        // If there are buffered continuation instructions (from a CALL),
        // emit them now in the continuation block.
        if (!m_continuationBuffer.empty()) {
          S.label(returnLabel);
          for (auto& ci : m_continuationBuffer) {
            emit_instruction(ci);
          }
          m_continuationBuffer.clear();
  if (!m_callReturnEmitted) {
    S.returnOp();
  }
          m_callReturnEmitted = true;
        }
      } else {
        S.returnOp();
        m_callReturnEmitted = true;
      }
      break;
    }

    case SM3_OP_LABEL: {
      uint32_t labelId = getOrCreateLabel(inst.label);
      if (m_inCallContinuation) {
        S.label(labelId);
        m_inCallContinuation = false;
      } else {
        S.label(labelId);
      }
      break;
    }

    case SM3_OP_PHASE:
    case SM3_OP_NOP:
      break;

    default:
      VKWIND_WARN(kTag, "Unimplemented SM3 opcode: %u", inst.opcode);
      break;
  }
}

std::vector<uint32_t> SM3Translator::generate_spirv() {
  std::vector<uint32_t> code;
  uint32_t nextId = 1;

  SpirvEmitter S(code, nextId, m_ptrPrivate, m_extInstImport, m_floatType);
  m_spirv = &S;

  // === PREAMBLE: Header + Capabilities + Extensions + Memory Model (direct to code) ===
  code.push_back(0x07230203); // Magic
  code.push_back(0x00010000); // Version 1.0
  code.push_back(0x00080001); // Generator
  code.push_back(0);          // Bound (filled at end)
  code.push_back(0);          // Schema

  // Capability Shader
  code.push_back((2 << 16) | 17); code.push_back(1);

  // GLSL.std.450 extension
  m_extInstImport = S.alloc();
  {
    uint32_t len = strlen("GLSL.std.450") + 1;
    uint32_t strWords = (len + 3) / 4;
    code.push_back(((1 + 1 + strWords) << 16) | 11);
    code.push_back(m_extInstImport);
    size_t start = code.size();
    code.resize(start + strWords);
    memset(&code[start], 0, strWords * 4);
    memcpy(&code[start], "GLSL.std.450", len);
  }

  // Memory model
  code.push_back((3 << 16) | 14); code.push_back(0); code.push_back(1);

  // === Allocate all IDs ===
  m_voidType = S.alloc();
  m_floatType = S.alloc();
  m_intType = S.alloc();
  m_boolType = S.alloc();
  m_vec2 = S.alloc();
  m_vec3 = S.alloc();
  m_vec4 = S.alloc();
  m_bvec4 = S.alloc();
  m_ivec4 = S.alloc();
  m_funcType = S.alloc();
  m_funcId = S.alloc();

  uint32_t ptrInput = S.alloc();
  uint32_t ptrOutput = S.alloc();
  uint32_t ptrPrivateVar = S.alloc();

  // === Emit basic types FIRST (must be before get_or_create_* adds typePointers to secTypes) ===
  S.setSection(SpirvEmitter::SEC_TYPES);
  S.typeVoid(m_voidType);
  S.typeFloat(m_floatType);
  S.typeInt(m_intType);
  S.typeBool(m_boolType);
  S.typeVec(m_vec2, m_floatType, 2);
  S.typeVec(m_vec3, m_floatType, 3);
  S.typeVec(m_vec4, m_floatType, 4);
  S.typeVec(m_bvec4, m_boolType, 4);
  S.typeVec(m_ivec4, m_intType, 4);
  S.typeFunction(m_funcType, m_voidType);

  uint32_t maxConstIndex = 0;
  for (auto& c : m_floatConstants) {
    maxConstIndex = std::max(maxConstIndex, c.regIndex);
  }
  uint32_t constArrayLen = maxConstIndex + 1;
  if (constArrayLen < 1) constArrayLen = 1;
  uint32_t constArrayLenId = S.alloc();
  uint32_t constArrayType = S.alloc();
  uint32_t constStructType = S.alloc();
  uint32_t constStructPtrType = S.alloc();
  uint32_t constBufferVar = S.alloc();

  // Discover outputs/inputs - allocate IDs via get_or_create_*
  // These will emit into the correct sections because we set sections below
  bool hasPositionOutput = false;
  uint32_t posOutputVar = 0;

  // Pre-create all inputs/outputs/temps/samplers/constants
  // by scanning declarations and instructions
  for (auto& decl : m_declarations) {
    if (decl.usage == SM3Declaration::USAGE_POSITION && m_isVertexShader) {
      hasPositionOutput = true;
      posOutputVar = get_or_create_output(decl.regIndex);
    } else if (decl.usage == SM3Declaration::USAGE_COLOR && m_isPixelShader) {
      get_or_create_output(decl.regIndex);
    } else if (decl.usage == SM3Declaration::USAGE_TEXCOORD && m_isVertexShader) {
      get_or_create_output(decl.regIndex);
    } else if (decl.usage == SM3Declaration::USAGE_TEXCOORD && m_isPixelShader) {
      get_or_create_input(decl.regIndex);
    } else if (decl.usage == SM3Declaration::USAGE_COLOR && m_isVertexShader) {
      get_or_create_output(decl.regIndex);
    }
  }

  // Scan instructions for register accesses
  for (auto& inst : m_instructions) {
    if (inst.dest.type == SM3_REG_OUTPUT || inst.dest.type == SM3_REG_TEXTURE) {
      if (m_isVertexShader && !hasPositionOutput) {
        hasPositionOutput = true;
        posOutputVar = get_or_create_output(inst.dest.index);
      } else {
        get_or_create_output(inst.dest.index);
      }
    }
    for (auto& s : inst.src) {
      if (s.type == SM3_REG_OUTPUT) get_or_create_output(s.index);
      if (s.type == SM3_REG_INPUT) get_or_create_input(s.index);
      if (s.type == SM3_REG_TEXTURE) get_or_create_input(s.index);
      if (s.type == SM3_REG_CONST) get_or_create_const_float(s.index);
      if (s.type == SM3_REG_CONST_INT) get_or_create_const_int(s.index);
      if (s.type == SM3_REG_CONST_BOOL) get_or_create_const_bool(s.index);
    }
    // Samplers: last source register is the sampler
    if (is_tex_op(inst.opcode) && !inst.src.empty()) {
      uint32_t samplerIdx = inst.src.back().index;
      get_or_create_sampler(samplerIdx);
      get_or_create_sampler_image(samplerIdx);
    }
  }

  // Position output fallback
  if (!hasPositionOutput && m_isVertexShader) {
    posOutputVar = get_or_create_output(0);
    hasPositionOutput = true;
  }

  // Pixel shader output
  uint32_t fragColorVar = 0;
  if (m_isPixelShader) {
    bool hasFragOutput = false;
    for (auto& decl : m_declarations) {
      if (decl.usage == SM3Declaration::USAGE_COLOR && decl.regType == SM3_REG_OUTPUT) {
        hasFragOutput = true;
        fragColorVar = get_or_create_output(decl.regIndex);
      }
    }
    if (!hasFragOutput) {
      fragColorVar = get_or_create_output(0);
    }
  }

  m_entryLabel = S.alloc();

  // === Now emit sections in correct SPIR-V order ===

  // SECTION: Entry Point (secEntryPoint)
  S.setSection(SpirvEmitter::SEC_ENTRY);
  {
    uint32_t execModel = m_isVertexShader ? 0 : 4;
    const char* name = "main";
    uint32_t nameLen = strlen(name) + 1;
    uint32_t nameWords = (nameLen + 3) / 4;

    // Collect interface variable IDs (required for all entry points)
    // SPIR-V 1.0-1.3: only Input(1) and Output(3) storage classes allowed
    std::vector<uint32_t> interfaces;
    // All input variables
    for (auto& [idx, id] : m_inputVars) interfaces.push_back(id);
    // All output variables
    for (auto& [idx, id] : m_outputVars) interfaces.push_back(id);
    // FragDepth output (if TEXDEPTH was used)
    if (m_fragDepthVar) interfaces.push_back(m_fragDepthVar);

    uint32_t wordCount = 3 + nameWords + (uint32_t)interfaces.size();
    S.op(15, wordCount);
    S.emit(execModel);
    S.emit(m_funcId);
    size_t start = S.secEntryPoint.size();
    S.secEntryPoint.resize(start + nameWords);
    memset(&S.secEntryPoint[start], 0, nameWords * 4);
    memcpy(&S.secEntryPoint[start], name, nameLen);
    for (auto id : interfaces) S.emit(id);
  }

  // SECTION: Debug (secDebug)
  S.setSection(SpirvEmitter::SEC_DEBUG);
  S.name(m_funcId, "main");
  S.name(constBufferVar, "Constants");
  if (posOutputVar) S.name(posOutputVar, "gl_Position");
  if (fragColorVar) S.name(fragColorVar, "FragColor");

  // SECTION: Annotations (secAnnotations)
  S.setSection(SpirvEmitter::SEC_ANNOTATIONS);
  S.decorate(constStructType, 2); // Block (applied to struct, not member)
  S.memberDecorate(constStructType, 0, 35, 0); // Offset 0
  S.decorate(constArrayType, 6, 16); // ArrayStride 16 (vec4)
  S.decorate(constBufferVar, 34, 0); // DescriptorSet 0
  S.decorate(constBufferVar, 33, m_isVertexShader ? 1 : 2); // Binding 1 (VS) or 2 (PS)
  if (posOutputVar) S.decorate(posOutputVar, 11, 0); // BuiltIn Position
  // Location 0 already decorated in get_or_create_output()

  // Fragment shaders need OriginUpperLeft
  if (!m_isVertexShader) {
    S.setSection(SpirvEmitter::SEC_ENTRY);
    // OpExecutionMode %main OriginUpperLeft (7)
    S.op(16, 3); S.emit(m_funcId); S.emit(7);
  }

  // SECTION: Types (secTypes) - additional types for const buffer
  S.setSection(SpirvEmitter::SEC_TYPES);
  S.typePointer(ptrInput, 1, m_vec4);
  S.typePointer(ptrOutput, 3, m_vec4);
  S.typePointer(ptrPrivateVar, 6, m_vec4);
  constArrayLenId = S.constant(m_intType, constArrayLen); // Re-use the pre-allocated slot is not possible; capture the actual id
  S.typeArray(constArrayType, m_vec4, constArrayLenId);
  S.typeStruct(constStructType, {constArrayType});
  S.typePointer(constStructPtrType, 2, constStructType);

  // SECTION: Variables (secVariables)
  S.setSection(SpirvEmitter::SEC_VARIABLES);
  // Re-emit constBufferVar with correct 2-arg variable() call
  // constBufferVar was already allocated; we need to emit its variable instruction
  S.op(59, 4); S.emit(constStructPtrType); S.emit(constBufferVar); S.emit(2); // storage class = Uniform

  // Save UBO info for emission phase
  m_constBufferVar = constBufferVar;
  m_constBufferPtrType = S.alloc(); // pointer type for vec4 in Uniform
  S.setSection(SpirvEmitter::SEC_TYPES);
  S.typePointer(m_constBufferPtrType, 2, m_vec4); // Uniform pointer to vec4

  // SECTION: Functions (secFunctions)
  S.setSection(SpirvEmitter::SEC_FUNCTIONS);
  S.function(m_voidType, m_funcId, 0, m_funcType);
  S.label(m_entryLabel);

  // Enter emission phase — const float reads now generate UBO loads
  m_inEmitPhase = true;

  // Generate code for each instruction
  for (auto& inst : m_instructions) {
    emit_instruction(inst);
  }

  m_inEmitPhase = false;

  // Y-flip for vertex shader
  if (m_isVertexShader && posOutputVar) {
    uint32_t posVec = S.load(m_vec4, posOutputVar);
    uint32_t y = S.compositeExtract(m_floatType, posVec, 1);
    uint32_t negY = S.fnegate(m_floatType, y);
    std::vector<uint32_t> comps;
    comps.push_back(S.compositeExtract(m_floatType, posVec, 0));
    comps.push_back(negY);
    comps.push_back(S.compositeExtract(m_floatType, posVec, 2));
    comps.push_back(S.compositeExtract(m_floatType, posVec, 3));
    uint32_t flipped = S.compositeConstruct(m_vec4, comps);
    S.store(posOutputVar, flipped);
  }

  if (!m_callReturnEmitted) {
    S.returnOp();
  }
  S.functionEnd();

  // finalize: concatenate sections in correct order + fix bound
  S.finalize(nextId + 100);

  m_spirv = nullptr;
  return code;
}

SM3Translator::TranslationResult SM3Translator::translate(const uint32_t* bytecode, uint32_t dwordCount) {
  m_instructions.clear();
  m_floatConstants.clear();
  m_intConstants.clear();
  m_boolConstants.clear();
  m_declarations.clear();
  m_tempCount = 0;
  m_samplerCount = 0;
  m_tempVars.clear();
  m_inputVars.clear();
  m_outputVars.clear();
  m_constFloatVars.clear();
  m_constIntVars.clear();
  m_constBoolVars.clear();
  m_samplerVars.clear();
  m_samplerCombinedVars.clear();
  m_privateIntVars.clear();
  // Flow control state
  while (!m_flowStack.empty()) m_flowStack.pop();
  while (!m_callReturnLabels.empty()) m_callReturnLabels.pop();
  m_loopStartLabel = 0;
  m_loopMergeLabel = 0;
  m_loopContinueTarget = 0;
  m_continueLabelOpened = false;

  if (!parse_bytecode(bytecode, dwordCount)) {
    VKWIND_ERR(kTag, "Failed to parse SM3 bytecode");
    return {};
  }

  TranslationResult result;
  result.isVertexShader = m_isVertexShader;
  result.tempCount = m_tempCount;
  result.constBufferFloatCount = 0;
  result.samplerCount = m_samplerCount;
  result.spirv = generate_spirv();

  VKWIND_INFO(kTag, "Translation complete: %u SPIR-V words, %s",
    (uint32_t)result.spirv.size(), m_isVertexShader ? "vertex" : "pixel");

  return result;
}

} // namespace vkwind
