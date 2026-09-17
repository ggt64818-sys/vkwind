#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <map>
#include <stack>

namespace vkwind {

// D3D9 SM2/SM3 register types (D3DSPR_*)
enum SM3RegisterType : uint32_t {
  SM3_REG_TEMP = 0,
  SM3_REG_INPUT = 1,
  SM3_REG_CONST = 2,
  SM3_REG_ADDR = 3,
  SM3_REG_TEXTURE = 4,
  SM3_REG_CONST2 = 5,
  SM3_REG_CONST3 = 6,
  SM3_REG_CONST4 = 7,
  SM3_REG_CONST_BOOL = 8,
  SM3_REG_CONST_INT = 9,
  SM3_REG_OUTPUT = 10,
  SM3_REG_PREDICATE = 11,
};

// D3D9 SM2/SM3 opcodes (D3DSIO_*)
enum SM3Opcode : uint32_t {
  SM3_OP_NOP = 0,
  SM3_OP_MOV = 1,
  SM3_OP_ADD = 2,
  SM3_OP_SUB = 3,
  SM3_OP_MAD = 4,
  SM3_OP_MUL = 5,
  SM3_OP_RCP = 6,
  SM3_OP_RSQ = 7,
  SM3_OP_DP3 = 8,
  SM3_OP_DP4 = 9,
  SM3_OP_MIN = 10,
  SM3_OP_MAX = 11,
  SM3_OP_SLT = 12,
  SM3_OP_SGE = 13,
  SM3_OP_EXP = 14,
  SM3_OP_LOG = 15,
  SM3_OP_LIT = 16,
  SM3_OP_DST = 17,
  SM3_OP_LRP = 18,
  SM3_OP_FRC = 19,
  SM3_OP_M4x4 = 20,
  SM3_OP_M4x3 = 21,
  SM3_OP_M3x4 = 22,
  SM3_OP_M3x3 = 23,
  SM3_OP_M3x2 = 24,
  SM3_OP_CALL = 25,
  SM3_OP_CALLNZ = 26,
  SM3_OP_LOOP = 27,
  SM3_OP_RET = 28,
  SM3_OP_ENDLOOP = 29,
  SM3_OP_LABEL = 30,
  SM3_OP_DCL = 31,
  SM3_OP_POW = 32,
  SM3_OP_CRS = 33,
  SM3_OP_SGN = 34,
  SM3_OP_ABS = 35,
  SM3_OP_NRM = 36,
  SM3_OP_SINCOS = 37,
  SM3_OP_REP = 38,
  SM3_OP_ENDREP = 39,
  SM3_OP_IF = 40,
  SM3_OP_IFC = 41,
  SM3_OP_ELSE = 42,
  SM3_OP_ENDIF = 43,
  SM3_OP_BREAK = 44,
  SM3_OP_BREAKC = 45,
  SM3_OP_MOVA = 46,
  SM3_OP_DEFB = 47,
  SM3_OP_DEFI = 48,
  SM3_OP_TEXCOORD = 49,
  SM3_OP_TEXKILL = 50,
  SM3_OP_TEX = 51,
  SM3_OP_TEXBEM = 52,
  SM3_OP_TEXBEML = 53,
  SM3_OP_TEXREG2AR = 54,
  SM3_OP_TEXREG2GB = 55,
  SM3_OP_TEXM3x2PAD = 56,
  SM3_OP_TEXM3x2TEX = 57,
  SM3_OP_TEXM3x3PAD = 58,
  SM3_OP_TEXM3x3TEX = 59,
  SM3_OP_RESERVED0 = 60,
  SM3_OP_TEXM3x3SPEC = 61,
  SM3_OP_TEXM3x3VSPEC = 62,
  SM3_OP_EXPP = 63,
  SM3_OP_LOGP = 64,
  SM3_OP_CND = 65,
  SM3_OP_DEF = 66,
  SM3_OP_TEXREG2RGB = 67,
  SM3_OP_TEXDP3TEX = 68,
  SM3_OP_TEXM3x2DEPTH = 69,
  SM3_OP_TEXDP3 = 70,
  SM3_OP_TEXM3x3 = 71,
  SM3_OP_TEXDEPTH = 72,
  SM3_OP_CMP = 73,
  SM3_OP_BEM = 74,
  SM3_OP_DP2ADD = 75,
  SM3_OP_DSX = 76,
  SM3_OP_DSY = 77,
  SM3_OP_TEXLDD = 78,
  SM3_OP_SETP = 79,
  SM3_OP_TEXLDL = 80,
  SM3_OP_BREAKP = 81,
  SM3_OP_PHASE = 82,
  SM3_OP_COMMENT = 0xFFFE,
  SM3_OP_END = 0xFFFF,
};

struct SM3Register {
  SM3RegisterType type = SM3_REG_TEMP;
  uint32_t index = 0;
  uint32_t swizzle[4] = {0, 1, 2, 3};
  uint32_t writeMask = 0xF;
  bool absolute = false;
  bool negate = false;
  bool relativeAddressing = false;
};

struct SM3Instruction {
  SM3Opcode opcode = SM3_OP_NOP;
  uint32_t length = 0;
  SM3Register dest;
  std::vector<SM3Register> src;
  int32_t intConstants[4] = {};
  float floatConstants[4] = {};
  uint32_t label = 0;
  uint32_t comparisonOpcode = 0;
  bool predicated = false;
  bool predicate = false;
};

struct SM3ConstantDef {
  uint32_t regIndex = 0;
  float values[4] = {};
};

struct SM3IntConstantDef {
  uint32_t regIndex = 0;
  int32_t values[4] = {};
};

struct SM3BoolConstantDef {
  uint32_t regIndex = 0;
  bool value = false;
};

struct SM3Declaration {
  enum Usage : uint32_t {
    USAGE_POSITION = 0,
    USAGE_BLENDWEIGHT = 1,
    USAGE_BLENDINDICES = 2,
    USAGE_NORMAL = 3,
    USAGE_PSIZE = 4,
    USAGE_TEXCOORD = 5,
    USAGE_TANGENT = 6,
    USAGE_BINORMAL = 7,
    USAGE_TESSFACTOR = 8,
    USAGE_COLOR = 10,
    USAGE_FOG = 11,
    USAGE_DEPTH = 12,
    USAGE_SAMPLE = 14,
  };

  SM3RegisterType regType = SM3_REG_TEMP;
  uint32_t regIndex = 0;
  Usage usage = USAGE_POSITION;
  uint32_t usageIndex = 0;
  uint32_t texType = 0;
};

class SM3Translator {
public:
  SM3Translator();
  ~SM3Translator();

  struct TranslationResult {
    std::vector<uint32_t> spirv;
    bool isVertexShader = false;
    uint32_t tempCount = 0;
    uint32_t constBufferFloatCount = 0;
    uint32_t constBufferIntCount = 0;
    uint32_t constBufferBoolCount = 0;
    uint32_t samplerCount = 0;
  };

  TranslationResult translate(const uint32_t* bytecode, uint32_t dwordCount);

private:
  bool parse_bytecode(const uint32_t* bytecode, uint32_t dwordCount);
  bool decode_instruction(const uint32_t* data, uint32_t remaining, SM3Instruction& out);
  SM3Register decode_register(const uint32_t* token, bool isDest);

  std::vector<uint32_t> generate_spirv();

  uint32_t get_or_create_temp(uint32_t index, uint32_t componentCount = 4);
  uint32_t get_or_create_input(uint32_t index, uint32_t componentCount = 4);
  uint32_t get_or_create_output(uint32_t index, uint32_t componentCount = 4);
  uint32_t get_or_create_const_float(uint32_t index);
  uint32_t get_or_create_const_int(uint32_t index);
  uint32_t get_or_create_const_bool(uint32_t index);
  uint32_t get_or_create_sampler(uint32_t index);
  uint32_t get_or_create_sampler_image(uint32_t index);
  uint32_t get_or_create_sampler_combined(uint32_t index);
  uint32_t get_or_create_predicate(uint32_t index);
  uint32_t get_or_create_private_int(uint32_t index);

  uint32_t emit_load_register(const SM3Register& reg);
  void emit_store_register(const SM3Register& reg, uint32_t value);
  uint32_t emit_swizzle(uint32_t vec, const SM3Register& reg, uint32_t componentCount = 4);
  uint32_t emit_replicate(uint32_t vec, uint32_t swizzleComp);

  uint32_t ensure_type(uint32_t typeId, uint32_t width = 32);

  void emit_instruction(const SM3Instruction& inst);

  bool m_isVertexShader = false;
  bool m_isPixelShader = false;

  std::vector<SM3Instruction> m_instructions;
  std::vector<SM3ConstantDef> m_floatConstants;
  std::vector<SM3IntConstantDef> m_intConstants;
  std::vector<SM3BoolConstantDef> m_boolConstants;
  std::vector<SM3Declaration> m_declarations;
  uint32_t m_tempCount = 0;
  uint32_t m_samplerCount = 0;

  // Map D3D9 register slots to SPIR-V IDs
  std::unordered_map<uint32_t, uint32_t> m_tempVars;
  std::unordered_map<uint32_t, uint32_t> m_inputVars;
  std::unordered_map<uint32_t, uint32_t> m_outputVars;
  std::unordered_map<uint32_t, uint32_t> m_constFloatVars;
  std::unordered_map<uint32_t, uint32_t> m_constIntVars;
  std::unordered_map<uint32_t, uint32_t> m_constBoolVars;
  std::unordered_map<uint32_t, uint32_t> m_samplerVars;
  std::unordered_map<uint32_t, uint32_t> m_samplerImageVars;
  std::unordered_map<uint32_t, uint32_t> m_samplerCombinedVars;

  // Const buffer array
  uint32_t m_constArrayFloatVar = 0;
  uint32_t m_constArrayIntVar = 0;
  uint32_t m_constArrayBoolVar = 0;

  // Helper types
  uint32_t m_voidType = 0;
  uint32_t m_floatType = 0;
  uint32_t m_intType = 0;
  uint32_t m_boolType = 0;
  uint32_t m_vec2 = 0;
  uint32_t m_vec3 = 0;
  uint32_t m_vec4 = 0;
  uint32_t m_bvec4 = 0;
  uint32_t m_ivec4 = 0;
  uint32_t m_mat4 = 0;
  uint32_t m_ptrInput = 0;
  uint32_t m_ptrOutput = 0;
  uint32_t m_ptrUniform = 0;
  uint32_t m_ptrPrivate = 0;
  uint32_t m_ptrFunction = 0;
  uint32_t m_extInstImport = 0;
  uint32_t m_funcType = 0;
  uint32_t m_funcId = 0;
  uint32_t m_entryLabel = 0;

  // Sampler/image types
  std::unordered_map<uint32_t, uint32_t> m_imageTypes;
  std::unordered_map<uint32_t, uint32_t> m_sampledImageTypes;

  // Shared sampler types (all samplers use same 2D image+sampledImage types)
  uint32_t m_texImageType = 0;
  uint32_t m_texSampledImageType = 0;

  // FragDepth output (for TEXDEPTH opcode)
  uint32_t m_fragDepthVar = 0;

  // SPIR-V IDs
  uint32_t m_nextId = 1;

  // Constant buffer descriptor set/binding
  static constexpr uint32_t kConstBufferSet = 0;
  static constexpr uint32_t kConstBufferBindingVS = 1;
  static constexpr uint32_t kConstBufferBindingPS = 2;
  static constexpr uint32_t kSamplerSet = 0;
  static constexpr uint32_t kSamplerBaseBinding = 3;  // bindings 3-10 for samplers

  // SPIR-V builder helpers
  struct SpirvEmitter {
    std::vector<uint32_t>* code;
    uint32_t& nextId;
    uint32_t& ptrPrivate;
    uint32_t& extInstImport;
    uint32_t& floatType;

    // Section-based output for correct SPIR-V ordering
    std::vector<uint32_t> secDebug;       // OpName, OpMemberName
    std::vector<uint32_t> secAnnotations; // OpDecorate, OpMemberDecorate
    std::vector<uint32_t> secTypes;       // OpType*, OpConstant
    std::vector<uint32_t> secVariables;   // OpVariable (global)
    std::vector<uint32_t> secEntryPoint;  // OpEntryPoint
    std::vector<uint32_t> secFunctions;   // OpFunction body

    enum Section { SEC_DEBUG, SEC_ANNOTATIONS, SEC_TYPES, SEC_VARIABLES, SEC_ENTRY, SEC_FUNCTIONS };
    Section currentSection = SEC_FUNCTIONS;

    SpirvEmitter(std::vector<uint32_t>& c, uint32_t& n, uint32_t& pp, uint32_t& ei, uint32_t& ft)
      : code(&c), nextId(n), ptrPrivate(pp), extInstImport(ei), floatType(ft) {}

    uint32_t alloc() { return nextId++; }

    void setSection(Section s) { currentSection = s; }

    std::vector<uint32_t>& currentBuffer() {
      switch (currentSection) {
        case SEC_DEBUG: return secDebug;
        case SEC_ANNOTATIONS: return secAnnotations;
        case SEC_TYPES: return secTypes;
        case SEC_VARIABLES: return secVariables;
        case SEC_ENTRY: return secEntryPoint;
        case SEC_FUNCTIONS: return secFunctions;
        default: return secFunctions;
      }
    }

    void emit(uint32_t word) { currentBuffer().push_back(word); }
    void emit2(uint32_t w1, uint32_t w2) { currentBuffer().push_back(w1); currentBuffer().push_back(w2); }
    void emit3(uint32_t w1, uint32_t w2, uint32_t w3) { currentBuffer().push_back(w1); currentBuffer().push_back(w2); currentBuffer().push_back(w3); }
    void emit4(uint32_t w1, uint32_t w2, uint32_t w3, uint32_t w4) { currentBuffer().push_back(w1); currentBuffer().push_back(w2); currentBuffer().push_back(w3); currentBuffer().push_back(w4); }

    void op(uint32_t opcode, uint32_t wordCount) {
      currentBuffer().push_back((wordCount << 16) | opcode);
    }

    void name(uint32_t id, const char* name);
    void decorate(uint32_t id, uint32_t decoration, uint32_t value = 0);
    void memberDecorate(uint32_t id, uint32_t member, uint32_t decoration, uint32_t value = 0);
    void typeVoid(uint32_t id);
    void typeFloat(uint32_t id);
    void typeInt(uint32_t id);
    void typeBool(uint32_t id);
    void typeVec(uint32_t id, uint32_t compType, uint32_t compCount);
    void typeMat(uint32_t id, uint32_t vecType, uint32_t colCount);
    void typePointer(uint32_t id, uint32_t storageClass, uint32_t typeId);
    void typeArray(uint32_t id, uint32_t typeId, uint32_t lengthId);
    void typeStruct(uint32_t id, const std::vector<uint32_t>& members);
    void typeImage(uint32_t id, uint32_t dim, uint32_t depth, uint32_t arrayed, uint32_t sampled, uint32_t format);
    void typeSampler(uint32_t id);
    void typeSampledImage(uint32_t id, uint32_t imageType);
    void typeFunction(uint32_t id, uint32_t returnType);
    uint32_t variable(uint32_t typeId, uint32_t storageClass);
    uint32_t constant(uint32_t type, uint32_t value);
    uint32_t constant64(uint32_t type, uint32_t lo, uint32_t hi);
    uint32_t specConstant(uint32_t type, uint32_t value);
    void function(uint32_t returnType, uint32_t funcId, uint32_t control, uint32_t funcTypeId);
    void functionEnd();
    void returnOp();
    void label(uint32_t labelId);
    uint32_t load(uint32_t type, uint32_t ptr);
    void store(uint32_t ptr, uint32_t value);
    uint32_t accessChain(uint32_t type, uint32_t base, const std::vector<uint32_t>& indices);
    uint32_t imageSampleImplicitLod(uint32_t resultType, uint32_t sampledImage, uint32_t coord);
    uint32_t imageSampleExplicitLod(uint32_t resultType, uint32_t sampledImage, uint32_t coord, uint32_t lod);
    uint32_t compositeConstruct(uint32_t type, const std::vector<uint32_t>& components);
    uint32_t constantComposite(uint32_t type, const std::vector<uint32_t>& consts);
    uint32_t vectorShuffle(uint32_t type, uint32_t vec1, uint32_t vec2, const std::vector<uint32_t>& components);
    uint32_t compositeExtract(uint32_t type, uint32_t composite, uint32_t index);
    uint32_t fadd(uint32_t type, uint32_t a, uint32_t b);
    uint32_t fsub(uint32_t type, uint32_t a, uint32_t b);
    uint32_t fmul(uint32_t type, uint32_t a, uint32_t b);
    uint32_t fdiv(uint32_t type, uint32_t a, uint32_t b);
    uint32_t fmin(uint32_t type, uint32_t a, uint32_t b);
    uint32_t fmax(uint32_t type, uint32_t a, uint32_t b);
    uint32_t fclamp(uint32_t type, uint32_t x, uint32_t minVal, uint32_t maxVal);
    uint32_t fma(uint32_t type, uint32_t a, uint32_t b, uint32_t c);
    uint32_t fdot(uint32_t type, uint32_t a, uint32_t b);
    uint32_t ffract(uint32_t type, uint32_t x);
    uint32_t fcross(uint32_t type, uint32_t a, uint32_t b);
    uint32_t fnormalize(uint32_t type, uint32_t x);
    uint32_t flength(uint32_t type, uint32_t x);
    uint32_t fmatrixTimesVector(uint32_t type, uint32_t matrix, uint32_t vector);
    uint32_t fvectorTimesMatrix(uint32_t type, uint32_t vector, uint32_t matrix);
    uint32_t fvectorTimesScalar(uint32_t type, uint32_t vector, uint32_t scalar);
    uint32_t iequal(uint32_t type, uint32_t a, uint32_t b);
    uint32_t inotqual(uint32_t type, uint32_t a, uint32_t b);
    uint32_t slessthan(uint32_t type, uint32_t a, uint32_t b);
    uint32_t sgreaterqual(uint32_t type, uint32_t a, uint32_t b);
    uint32_t iadd(uint32_t type, uint32_t a, uint32_t b);
    uint32_t logicalNot(uint32_t type, uint32_t x);
    uint32_t select(uint32_t type, uint32_t cond, uint32_t trueVal, uint32_t falseVal);
    uint32_t extInst(uint32_t resultType, uint32_t inst, const std::vector<uint32_t>& operands);
    uint32_t snegate(uint32_t type, uint32_t x);
    uint32_t fnegate(uint32_t type, uint32_t x);
    uint32_t fabs(uint32_t type, uint32_t x);
    uint32_t sconvToS(uint32_t resultType, uint32_t value);
    uint32_t sconvToF(uint32_t resultType, uint32_t value);
    uint32_t fconvToS(uint32_t resultType, uint32_t value);
    uint32_t isign(uint32_t type, uint32_t x);
    uint32_t fsign(uint32_t type, uint32_t x);
    uint32_t ffloor(uint32_t type, uint32_t x);
    uint32_t fsin(uint32_t type, uint32_t x);
    uint32_t fcos(uint32_t type, uint32_t x);
    uint32_t fexp2(uint32_t type, uint32_t x);
    uint32_t flog2(uint32_t type, uint32_t x);
    uint32_t fdot2(uint32_t type, uint32_t a, uint32_t b);
    uint32_t dfdx(uint32_t type, uint32_t x);
    uint32_t dfdy(uint32_t type, uint32_t x);

    void branch(uint32_t targetLabel);
    void branchConditional(uint32_t cond, uint32_t trueLabel, uint32_t falseLabel);
    void selectionMerge(uint32_t mergeLabel, uint32_t control = 0);
    void loopMerge(uint32_t mergeLabel, uint32_t continueLabel, uint32_t control = 0);
    void kill();
    void unreachable();
    uint32_t fordnotequal(uint32_t type, uint32_t a, uint32_t b);
    uint32_t fordequal(uint32_t type, uint32_t a, uint32_t b);
    uint32_t fordgreaterequal(uint32_t type, uint32_t a, uint32_t b);
    uint32_t fordgreaterthan(uint32_t type, uint32_t a, uint32_t b);
    uint32_t fordlessthan(uint32_t type, uint32_t a, uint32_t b);
    uint32_t fordlessthanequal(uint32_t type, uint32_t a, uint32_t b);
    uint32_t logicalany(uint32_t type, uint32_t vec);
    uint32_t logicaland(uint32_t type, uint32_t a, uint32_t b);
    uint32_t logicalor(uint32_t type, uint32_t a, uint32_t b);

    void finalize(uint32_t bound);
  };

  SpirvEmitter* m_spirv = nullptr;
  std::vector<uint32_t> m_code;

  // UBO constant buffer (set during generate_spirv)
  uint32_t m_constBufferVar = 0;       // OpVariable Uniform for c[] array
  uint32_t m_constBufferPtrType = 0;   // OpTypePointer Uniform -> vec4
  bool m_inEmitPhase = false;          // true when generating function body

  // Flow control state
  std::map<uint32_t, uint32_t> m_labelIds;
  std::stack<uint32_t> m_breakTargets;
  uint32_t m_loopStartLabel = 0;
  uint32_t m_loopMergeLabel = 0;
  uint32_t m_loopContinueTarget = 0;
  bool m_continueLabelOpened = false;

  // Predicate register variables (SETP writes here, BREAKP reads here)
  std::unordered_map<uint32_t, uint32_t> m_predicateVars;

  // Private int variables (REP counter, etc.)
  std::unordered_map<uint32_t, uint32_t> m_privateIntVars;

  // CALL/RET state
  std::stack<uint32_t> m_callReturnLabels;
  bool m_inCallContinuation = false;            // true between CALL and its target LABEL
  std::vector<SM3Instruction> m_continuationBuffer; // buffered instructions between CALL and LABEL
  bool m_callReturnEmitted = false;             // true if RET already emitted function return

  // REP/ENDREP counter variable tracking
  uint32_t m_repCounterVar = 0;
  uint32_t m_repCountInt = 0;

  // IF/ELSE/ENDIF flow control frame
  struct FlowControlFrame {
    uint32_t mergeLabel;
    uint32_t falseLabel;
    bool hasElse;
  };
  std::stack<FlowControlFrame> m_flowStack;

  uint32_t getOrCreateLabel(uint32_t sm3Label);
};

} // namespace vkwind
