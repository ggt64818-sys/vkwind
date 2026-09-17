#include "shader_translator.h"
#include "../util/util_log.h"
#include <cstring>
#include <algorithm>

namespace vkwind {

static const char* kTag = "ShaderTranslator";

// DXBC constants
static constexpr uint32_t DXBC_MAGIC = 0x43425844;
static constexpr uint32_t CHUNK_ISGN = 0x4E475349;
static constexpr uint32_t CHUNK_OSGN = 0x4E47534F;
static constexpr uint32_t CHUNK_SHDR = 0x52444853;
static constexpr uint32_t CHUNK_STAT = 0x54415453;
static constexpr uint32_t CHUNK_PCSG = 0x47534350;
static constexpr uint32_t CHUNKERSHEY = 0x59524553;

// Semantic name table
static const char* kSemanticNames[] = {
  "POSITION", "BLENDWEIGHT", "BLENDINDICES", "NORMAL",
  "PSIZE", "TEXCOORD", "TANGENT", "BINORMAL",
  "TESSFACTOR", "COLOR", "FOG", "DEPTH", "SAMPLE", nullptr
};

// SM4 opcodes
enum ShaderTranslator::SM4Opcode : uint32_t {
  OP_NOP = 0,
  OP_LOAD = 1,
  OP_STORE = 2,
  OP_CALL = 3,
  OP_RET = 4,
  OP_BRANCH = 5,
  OP_CASE = 6,
  OP_SWITCH = 7,
  OP_CMP = 8,
  OP_MOV = 9,
  OP_ADD = 10,
  OP_SUB = 11,
  OP_MUL = 12,
  OP_DIV = 13,
  OP_RCP = 14,
  OP_RSQ = 15,
  OP_EXP = 16,
  OP_LOG = 17,
  OP_MIN = 18,
  OP_MAX = 19,
  OP_CLAMP = 20,
  OP_ROUND = 21,
  OP_FLOOR = 22,
  OP_CEIL = 23,
  OP_SIN = 24,
  OP_COS = 25,
  OP_SINCOS = 26,
  OP_DP2 = 27,
  OP_DP3 = 28,
  OP_DP4 = 29,
  OP_NOOP = 30,
  OP_MAD = 32,
  OP_LRCP = 33,
  OP_FRC = 34,
  OP_ABS = 35,
  OP_TEX = 36,
  OP_TEXLOD = 37,
  OP_TEXLD = 38,
  OP_TEXCOORD = 39,
  OP_TEXKILL = 40,
  OP_TEXDEPTH = 41,
  OP_TEXGRAD = 42,
  OP_TEXB = 43,
  OP_SAMPLE = 44,
  OP_SAMPLE_LOD = 45,
  OP_SAMPLE_GRAD = 46,
  OP_SAMPLE_CMP = 47,
  OP_SAMPLE_B = 48,
  OP_SAMPLE_D = 49,
  OP_DCL_CONST_BUFFER = 128,
  OP_DCL_SAMPLER = 129,
  OP_DCL_TEXTURE = 130,
  OP_DCL_INPUT = 131,
  OP_DCL_OUTPUT = 132,
  OP_DCL_INPUT_PS = 133,
  OP_DCL_INDEXABLE_TEMP = 134,
  OP_DCL_GLOBAL_FLAGS = 135,
};

// SM4 operand type encoding
enum SM4OperandType : uint32_t {
  OP_TYPE_TEMP = 0,
  OP_TYPE_INPUT = 1,
  OP_TYPE_OUTPUT = 2,
  OP_TYPE_INDEXABLE_TEMP = 3,
  OP_TYPE_IMM32 = 4,
  OP_TYPE_IMM64 = 5,
  OP_TYPE_FLOAT_IMM = 6,
  OP_TYPE_INT_IMM = 7,
  OP_TYPE_BOOL_IMM = 8,
  OP_TYPE_SAMPLER = 10,
  OP_TYPE_RESOURCE = 11,
};

static uint32_t sm4_operand_comps(const uint32_t* token) {
  uint32_t sel = (token[0] >> 4) & 0xF;
  if (sel == 0) return 4; // xyzw
  uint32_t count = 0;
  if (sel & 1) count++;
  if (sel & 2) count++;
  if (sel & 4) count++;
  if (sel & 8) count++;
  return count == 0 ? 4 : count;
}

static uint32_t sm4_swizzle(const uint32_t* token, uint32_t comp) {
  uint32_t sel = (token[0] >> 4) & 0xF;
  if (sel == 0) return comp; // xyzw passthrough
  uint32_t idx = 0;
  for (uint32_t i = 0; i < 4; i++) {
    if (sel & (1 << i)) {
      if (idx == comp) return i;
      idx++;
    }
  }
  return 0;
}

// SPIRV builder implementation
static constexpr uint32_t SPV_MAGIC = 0x07230203;
static constexpr uint32_t SPV_VERSION = 0x00010000;
static constexpr uint32_t SPV_GENERATOR = 0x00080001;

enum SpvOpcode : uint32_t {
  SPV_OP_CAPABILITY = 17,
  SPV_OP_EXT_INST_IMPORT = 11,
  SPV_OP_MEMORY_MODEL = 14,
  SPV_OP_ENTRY_POINT = 15,
  SPV_OP_NAME = 5,
  SPV_OP_MEMBER_NAME = 6,
  SPV_OP_DECORATE = 71,
  SPV_OP_MEMBER_DECORATE = 72,
  SPV_OP_TYPE_VOID = 19,
  SPV_OP_TYPE_BOOL = 20,
  SPV_OP_TYPE_INT = 21,
  SPV_OP_TYPE_FLOAT = 22,
  SPV_OP_TYPE_VECTOR = 23,
  SPV_OP_TYPE_MATRIX = 24,
  SPV_OP_TYPE_IMAGE = 25,
  SPV_OP_TYPE_SAMPLER = 26,
  SPV_OP_TYPE_SAMPLED_IMAGE = 27,
  SPV_OP_TYPE_ARRAY = 28,
  SPV_OP_TYPE_STRUCT = 30,
  SPV_OP_TYPE_POINTER = 32,
  SPV_OP_TYPE_FUNCTION = 33,
  SPV_OP_VARIABLE = 59,
  SPV_OP_LOAD = 61,
  SPV_OP_STORE = 62,
  SPV_OP_ACCESS_CHAIN = 65,
  SPV_OP_FUNCTION = 54,
  SPV_OP_FUNCTION_END = 56,
  SPV_OP_LABEL = 248,
  SPV_OP_RETURN = 253,
  SPV_OP_FADD = 129,
  SPV_OP_FSUB = 130,
  SPV_OP_FMUL = 131,
  SPV_OP_FDIV = 136,
  SPV_OP_FMIN = 137,
  SPV_OP_FMAX = 138,
  SPV_OP_FCLAMP = 141,
  SPV_OP_FRACT = 34,
  SPV_OP_FROUND = 27,
  SPV_OP_FLOOR = 28,
  SPV_OP_FCEIL = 27,
  SPV_OP_FABS = 35,
  SPV_OP_DP2 = 148,
  SPV_OP_DP3 = 149,
  SPV_OP_DP4 = 150,
  SPV_OP_CROSS = 153,
  SPV_OP_NORMALIZE = 154,
  SPV_OP_LENGTH = 66,
  SPV_OP_MATRIX_TIMES_VECTOR = 125,
  SPV_OP_VECTOR_TIMES_MATRIX = 126,
  SPV_OP_VECTOR_TIMES_SCALAR = 142,
  SPV_OP_EXT_INST = 81,
  SPV_OP_BRANCH = 250,
  SPV_OP_BRANCH_CONDITIONAL = 251,
  SPV_OP_I_EQUAL = 171,
  SPV_OP_I_NOT_EQUAL = 172,
  SPV_OP_SELECT = 174,
  SPV_OP_SCONVERT_TO_F = 143,
  SPV_OP_UCONVERT_TO_F = 144,
  SPV_OP_FCONVERT_TO_S = 145,
  SPV_OP_LOGICAL_AND = 165,
  SPV_OP_LOGICAL_OR = 166,
  SPV_OP_LOGICAL_NOT = 168,
  SPV_OP_COMPOSITE_CONSTRUCT = 80,
  SPV_OP_DOT = 150,
};

enum SpvDecoration : uint32_t {
  SPV_DEC_BUILTIN = 11,
  SPV_DEC_BINDING = 33,
  SPV_DEC_DESCRIPTOR_SET = 34,
  SPV_DEC_LOCATION = 30,
  SPV_DEC_OFFSET = 35,
  SPV_DEC_BLOCK = 2,
  SPV_DEC_COL_MAJOR = 5,
};

enum SpvStorageClass : uint32_t {
  SPV_SC_UNIFORM_CONSTANT = 0,
  SPV_SC_INPUT = 1,
  SPV_SC_UNIFORM = 2,
  SPV_SC_OUTPUT = 3,
  SPV_SC_FUNCTION = 7,
  SPV_SC_PUSH_CONSTANT = 9,
};

enum SpvBuiltin : uint32_t {
  SPV_BUILTIN_POSITION = 0,
  SPV_BUILTIN_VERTEXINDEX = 42,
  SPV_BUILTIN_INSTANCEINDEX = 43,
  SPV_BUILTIN_FRAGCOORD = 15,
  SPV_BUILTIN_FRAGCOLOR = 2,
};

enum SpvCapability : uint32_t {
  SPV_CAP_SHADER = 1,
  SPV_CAP_IMAGE_QUERY = 25,
};

enum SpvDim : uint32_t {
  SPV_DIM_1D = 0,
  SPV_DIM_2D = 1,
  SPV_DIM_3D = 2,
  SPV_DIM_CUBE = 3,
};

enum SpvImageFormat : uint32_t {
  SPV_IMG_FORMAT_UNKNOWN = 0,
  SPV_IMG_FORMAT_RGBA8 = 37,
};

void ShaderTranslator::SpirvBuilder::emit_header() {
  code.push_back(SPV_MAGIC);
  code.push_back(SPV_VERSION);
  code.push_back(SPV_GENERATOR);
  code.push_back(bound);
  code.push_back(0);
}

void ShaderTranslator::SpirvBuilder::emit_capability(uint32_t cap) {
  code.push_back((2 << 16) | SPV_OP_CAPABILITY);
  code.push_back(cap);
}

void ShaderTranslator::SpirvBuilder::emit_ext_inst_import() {
  uint32_t id = alloc_id();
  code.push_back((3 + 18) << 16 | SPV_OP_EXT_INST_IMPORT);
  code.push_back(id);
  const char* ext = "GLSL.std.450";
  size_t len = strlen(ext) + 1;
  size_t words = (len + 3) / 4;
  size_t start = code.size();
  code.resize(start + words);
  memcpy(&code[start], ext, len);
}

void ShaderTranslator::SpirvBuilder::emit_memory_model() {
  code.push_back((3 << 16) | SPV_OP_MEMORY_MODEL);
  code.push_back(0); // Logical GLSL450
  code.push_back(1); // Logical
}

void ShaderTranslator::SpirvBuilder::emit_name(uint32_t id, const char* name) {
  code.push_back((3 << 16) | SPV_OP_NAME);
  code.push_back(id);
  size_t len = strlen(name) + 1;
  size_t words = (len + 3) / 4;
  size_t start = code.size();
  code.resize(start + words);
  memcpy(&code[start], name, len);
}

void ShaderTranslator::SpirvBuilder::emit_decorate(uint32_t id, uint32_t decoration, uint32_t value) {
  code.push_back((4 << 16) | SPV_OP_DECORATE);
  code.push_back(id);
  code.push_back(decoration);
  if (decoration == SPV_DEC_BINDING || decoration == SPV_DEC_DESCRIPTOR_SET || decoration == SPV_DEC_LOCATION ||
      decoration == SPV_DEC_OFFSET || decoration == SPV_DEC_BUILTIN || decoration == SPV_DEC_COL_MAJOR) {
    code.push_back(value);
  }
}

void ShaderTranslator::SpirvBuilder::emit_member_decorate(uint32_t id, uint32_t member, uint32_t decoration, uint32_t value) {
  code.push_back((5 << 16) | SPV_OP_MEMBER_DECORATE);
  code.push_back(id);
  code.push_back(member);
  code.push_back(decoration);
  code.push_back(value);
}

uint32_t ShaderTranslator::SpirvBuilder::type_void() {
  uint32_t id = alloc_id();
  code.push_back((2 << 16) | SPV_OP_TYPE_VOID);
  code.push_back(id);
  return id;
}

uint32_t ShaderTranslator::SpirvBuilder::type_float() {
  uint32_t id = alloc_id();
  code.push_back((3 << 16) | SPV_OP_TYPE_FLOAT);
  code.push_back(id);
  code.push_back(32);
  return id;
}

uint32_t ShaderTranslator::SpirvBuilder::type_int() {
  uint32_t id = alloc_id();
  code.push_back((4 << 16) | SPV_OP_TYPE_INT);
  code.push_back(id);
  code.push_back(32);
  code.push_back(1);
  return id;
}

uint32_t ShaderTranslator::SpirvBuilder::type_vec(uint32_t compType, uint32_t compCount) {
  uint32_t id = alloc_id();
  code.push_back((4 << 16) | SPV_OP_TYPE_VECTOR);
  code.push_back(id);
  code.push_back(compType);
  code.push_back(compCount);
  return id;
}

uint32_t ShaderTranslator::SpirvBuilder::type_mat(uint32_t vecType, uint32_t colCount) {
  uint32_t id = alloc_id();
  code.push_back((4 << 16) | SPV_OP_TYPE_MATRIX);
  code.push_back(id);
  code.push_back(vecType);
  code.push_back(colCount);
  return id;
}

uint32_t ShaderTranslator::SpirvBuilder::type_pointer(uint32_t type, uint32_t sc) {
  uint32_t id = alloc_id();
  code.push_back((4 << 16) | SPV_OP_TYPE_POINTER);
  code.push_back(id);
  code.push_back(sc);
  code.push_back(type);
  return id;
}

uint32_t ShaderTranslator::SpirvBuilder::type_struct(const std::vector<uint32_t>& members) {
  uint32_t id = alloc_id();
  code.push_back((2 + members.size()) << 16 | SPV_OP_TYPE_STRUCT);
  code.push_back(id);
  for (auto m : members) code.push_back(m);
  return id;
}

uint32_t ShaderTranslator::SpirvBuilder::type_array(uint32_t type, uint32_t length) {
  uint32_t id = alloc_id();
  code.push_back((4 << 16) | SPV_OP_TYPE_ARRAY);
  code.push_back(id);
  code.push_back(type);
  code.push_back(length);
  return id;
}

uint32_t ShaderTranslator::SpirvBuilder::type_image(uint32_t sampledType, uint32_t dim, uint32_t depth, uint32_t arrayed, uint32_t multisampled, uint32_t sampled, uint32_t format) {
  uint32_t id = alloc_id();
  code.push_back((9 << 16) | SPV_OP_TYPE_IMAGE);
  code.push_back(id);
  code.push_back(sampledType);
  code.push_back(dim);
  code.push_back(depth);
  code.push_back(arrayed);
  code.push_back(multisampled);
  code.push_back(sampled);
  code.push_back(format);
  return id;
}

uint32_t ShaderTranslator::SpirvBuilder::type_sampler() {
  uint32_t id = alloc_id();
  code.push_back((2 << 16) | SPV_OP_TYPE_SAMPLER);
  code.push_back(id);
  return id;
}

uint32_t ShaderTranslator::SpirvBuilder::type_sampled_image(uint32_t imageType) {
  uint32_t id = alloc_id();
  code.push_back((3 << 16) | SPV_OP_TYPE_SAMPLED_IMAGE);
  code.push_back(id);
  code.push_back(imageType);
  return id;
}

uint32_t ShaderTranslator::SpirvBuilder::type_function(uint32_t retType) {
  uint32_t id = alloc_id();
  code.push_back((3 << 16) | SPV_OP_TYPE_FUNCTION);
  code.push_back(id);
  code.push_back(retType);
  return id;
}

uint32_t ShaderTranslator::SpirvBuilder::emit_variable(uint32_t type, uint32_t sc) {
  uint32_t id = alloc_id();
  code.push_back((4 << 16) | SPV_OP_VARIABLE);
  code.push_back(type);
  code.push_back(id);
  code.push_back(sc);
  return id;
}

uint32_t ShaderTranslator::SpirvBuilder::emit_function(uint32_t retType, uint32_t control, uint32_t funcType) {
  uint32_t id = alloc_id();
  code.push_back((5 << 16) | SPV_OP_FUNCTION);
  code.push_back(retType);
  code.push_back(id);
  code.push_back(control);
  code.push_back(funcType);
  return id;
}

void ShaderTranslator::SpirvBuilder::emit_function_end() {
  code.push_back((1 << 16) | SPV_OP_FUNCTION_END);
}

void ShaderTranslator::SpirvBuilder::emit_return() {
  code.push_back((1 << 16) | SPV_OP_RETURN);
}

void ShaderTranslator::SpirvBuilder::emit_label(uint32_t labelId) {
  code.push_back((2 << 16) | SPV_OP_LABEL);
  code.push_back(labelId);
}

uint32_t ShaderTranslator::SpirvBuilder::emit_load(uint32_t type, uint32_t ptr) {
  uint32_t id = alloc_id();
  code.push_back((4 << 16) | SPV_OP_LOAD);
  code.push_back(type);
  code.push_back(id);
  code.push_back(ptr);
  return id;
}

void ShaderTranslator::SpirvBuilder::emit_store(uint32_t ptr, uint32_t val) {
  code.push_back((3 << 16) | SPV_OP_STORE);
  code.push_back(ptr);
  code.push_back(val);
}

uint32_t ShaderTranslator::SpirvBuilder::emit_access_chain(uint32_t type, uint32_t base, const std::vector<uint32_t>& indices) {
  uint32_t id = alloc_id();
  code.push_back((4 + indices.size()) << 16 | SPV_OP_ACCESS_CHAIN);
  code.push_back(type);
  code.push_back(id);
  code.push_back(base);
  for (auto idx : indices) code.push_back(idx);
  return id;
}

uint32_t ShaderTranslator::SpirvBuilder::emit_image_sample_implicit_lod(uint32_t resultType, uint32_t sampledImg, uint32_t coord) {
  uint32_t id = alloc_id();
  code.push_back((5 << 16) | SPV_OP_EXT_INST);
  code.push_back(resultType);
  code.push_back(id);
  code.push_back(1); // GLSL.std.450
  code.push_back(87); // texture
  code.push_back(sampledImg);
  code.push_back(coord);
  return id;
}

uint32_t ShaderTranslator::SpirvBuilder::emit_image_sample_explicit_lod(uint32_t resultType, uint32_t sampledImg, uint32_t coord, uint32_t lod) {
  uint32_t id = alloc_id();
  code.push_back((6 << 16) | SPV_OP_EXT_INST);
  code.push_back(resultType);
  code.push_back(id);
  code.push_back(1);
  code.push_back(88); // textureLod
  code.push_back(sampledImg);
  code.push_back(coord);
  code.push_back(lod);
  return id;
}

uint32_t ShaderTranslator::SpirvBuilder::emit_image_fetch(uint32_t resultType, uint32_t image, uint32_t coord, uint32_t lod) {
  uint32_t id = alloc_id();
  code.push_back((6 << 16) | SPV_OP_EXT_INST);
  code.push_back(resultType);
  code.push_back(id);
  code.push_back(1);
  code.push_back(101); // texelFetch
  code.push_back(image);
  code.push_back(coord);
  code.push_back(lod);
  return id;
}

uint32_t ShaderTranslator::SpirvBuilder::emit_composite_construct(uint32_t type, const std::vector<uint32_t>& comps) {
  uint32_t id = alloc_id();
  code.push_back((3 + comps.size()) << 16 | SPV_OP_COMPOSITE_CONSTRUCT);
  code.push_back(type);
  code.push_back(id);
  for (auto c : comps) code.push_back(c);
  return id;
}

uint32_t ShaderTranslator::SpirvBuilder::emit_f_add(uint32_t type, uint32_t a, uint32_t b) {
  uint32_t id = alloc_id();
  code.push_back((5 << 16) | SPV_OP_FADD);
  code.push_back(type);
  code.push_back(id);
  code.push_back(a);
  code.push_back(b);
  return id;
}

uint32_t ShaderTranslator::SpirvBuilder::emit_f_sub(uint32_t type, uint32_t a, uint32_t b) {
  uint32_t id = alloc_id();
  code.push_back((5 << 16) | SPV_OP_FSUB);
  code.push_back(type);
  code.push_back(id);
  code.push_back(a);
  code.push_back(b);
  return id;
}

uint32_t ShaderTranslator::SpirvBuilder::emit_f_mul(uint32_t type, uint32_t a, uint32_t b) {
  uint32_t id = alloc_id();
  code.push_back((5 << 16) | SPV_OP_FMUL);
  code.push_back(type);
  code.push_back(id);
  code.push_back(a);
  code.push_back(b);
  return id;
}

uint32_t ShaderTranslator::SpirvBuilder::emit_f_div(uint32_t type, uint32_t a, uint32_t b) {
  uint32_t id = alloc_id();
  code.push_back((5 << 16) | SPV_OP_FDIV);
  code.push_back(type);
  code.push_back(id);
  code.push_back(a);
  code.push_back(b);
  return id;
}

uint32_t ShaderTranslator::SpirvBuilder::emit_f_min(uint32_t type, uint32_t a, uint32_t b) {
  uint32_t id = alloc_id();
  code.push_back((5 << 16) | SPV_OP_FMIN);
  code.push_back(type);
  code.push_back(id);
  code.push_back(a);
  code.push_back(b);
  return id;
}

uint32_t ShaderTranslator::SpirvBuilder::emit_f_max(uint32_t type, uint32_t a, uint32_t b) {
  uint32_t id = alloc_id();
  code.push_back((5 << 16) | SPV_OP_FMAX);
  code.push_back(type);
  code.push_back(id);
  code.push_back(a);
  code.push_back(b);
  return id;
}

uint32_t ShaderTranslator::SpirvBuilder::emit_f_clamp(uint32_t type, uint32_t x, uint32_t minVal, uint32_t maxVal) {
  uint32_t id = alloc_id();
  code.push_back((6 << 16) | SPV_OP_FCLAMP);
  code.push_back(type);
  code.push_back(id);
  code.push_back(x);
  code.push_back(minVal);
  code.push_back(maxVal);
  return id;
}

uint32_t ShaderTranslator::SpirvBuilder::emit_dp2(uint32_t type, uint32_t a, uint32_t b) {
  uint32_t id = alloc_id();
  code.push_back((5 << 16) | SPV_OP_DP2);
  code.push_back(type);
  code.push_back(id);
  code.push_back(a);
  code.push_back(b);
  return id;
}

uint32_t ShaderTranslator::SpirvBuilder::emit_dp3(uint32_t type, uint32_t a, uint32_t b) {
  uint32_t id = alloc_id();
  code.push_back((5 << 16) | SPV_OP_DP3);
  code.push_back(type);
  code.push_back(id);
  code.push_back(a);
  code.push_back(b);
  return id;
}

uint32_t ShaderTranslator::SpirvBuilder::emit_dp4(uint32_t type, uint32_t a, uint32_t b) {
  uint32_t id = alloc_id();
  code.push_back((5 << 16) | SPV_OP_DP4);
  code.push_back(type);
  code.push_back(id);
  code.push_back(a);
  code.push_back(b);
  return id;
}

uint32_t ShaderTranslator::SpirvBuilder::emit_cross(uint32_t type, uint32_t a, uint32_t b) {
  uint32_t id = alloc_id();
  code.push_back((5 << 16) | SPV_OP_CROSS);
  code.push_back(type);
  code.push_back(id);
  code.push_back(a);
  code.push_back(b);
  return id;
}

uint32_t ShaderTranslator::SpirvBuilder::emit_normalize(uint32_t type, uint32_t x) {
  uint32_t id = alloc_id();
  code.push_back((4 << 16) | SPV_OP_NORMALIZE);
  code.push_back(type);
  code.push_back(id);
  code.push_back(x);
  return id;
}

uint32_t ShaderTranslator::SpirvBuilder::emit_length(uint32_t type, uint32_t x) {
  uint32_t id = alloc_id();
  code.push_back((4 << 16) | SPV_OP_LENGTH);
  code.push_back(type);
  code.push_back(id);
  code.push_back(x);
  return id;
}

uint32_t ShaderTranslator::SpirvBuilder::emit_dot(uint32_t type, uint32_t a, uint32_t b) {
  uint32_t id = alloc_id();
  code.push_back((5 << 16) | SPV_OP_DOT);
  code.push_back(type);
  code.push_back(id);
  code.push_back(a);
  code.push_back(b);
  return id;
}

uint32_t ShaderTranslator::SpirvBuilder::emit_matrix_times_vector(uint32_t type, uint32_t mat, uint32_t vec) {
  uint32_t id = alloc_id();
  code.push_back((5 << 16) | SPV_OP_MATRIX_TIMES_VECTOR);
  code.push_back(type);
  code.push_back(id);
  code.push_back(mat);
  code.push_back(vec);
  return id;
}

uint32_t ShaderTranslator::SpirvBuilder::emit_vector_times_matrix(uint32_t type, uint32_t vec, uint32_t mat) {
  uint32_t id = alloc_id();
  code.push_back((5 << 16) | SPV_OP_VECTOR_TIMES_MATRIX);
  code.push_back(type);
  code.push_back(id);
  code.push_back(vec);
  code.push_back(mat);
  return id;
}

uint32_t ShaderTranslator::SpirvBuilder::emit_vector_times_scalar(uint32_t type, uint32_t vec, uint32_t scalar) {
  uint32_t id = alloc_id();
  code.push_back((5 << 16) | SPV_OP_VECTOR_TIMES_SCALAR);
  code.push_back(type);
  code.push_back(id);
  code.push_back(vec);
  code.push_back(scalar);
  return id;
}

uint32_t ShaderTranslator::SpirvBuilder::emit_ext_inst(uint32_t resultType, uint32_t set, uint32_t inst, const std::vector<uint32_t>& operands) {
  uint32_t id = alloc_id();
  code.push_back((4 + operands.size()) << 16 | SPV_OP_EXT_INST);
  code.push_back(resultType);
  code.push_back(id);
  code.push_back(set);
  code.push_back(inst);
  for (auto op : operands) code.push_back(op);
  return id;
}

void ShaderTranslator::SpirvBuilder::emit_branch(uint32_t label) {
  code.push_back((2 << 16) | SPV_OP_BRANCH);
  code.push_back(label);
}

void ShaderTranslator::SpirvBuilder::emit_branch_conditional(uint32_t cond, uint32_t trueLabel, uint32_t falseLabel) {
  code.push_back((4 << 16) | SPV_OP_BRANCH_CONDITIONAL);
  code.push_back(cond);
  code.push_back(trueLabel);
  code.push_back(falseLabel);
}

uint32_t ShaderTranslator::SpirvBuilder::emit_select(uint32_t type, uint32_t cond, uint32_t trueVal, uint32_t falseVal) {
  uint32_t id = alloc_id();
  code.push_back((6 << 16) | SPV_OP_SELECT);
  code.push_back(type);
  code.push_back(id);
  code.push_back(cond);
  code.push_back(trueVal);
  code.push_back(falseVal);
  return id;
}

uint32_t ShaderTranslator::SpirvBuilder::emit_s_conv_to_f(uint32_t resultType, uint32_t value) {
  uint32_t id = alloc_id();
  code.push_back((4 << 16) | SPV_OP_SCONVERT_TO_F);
  code.push_back(resultType);
  code.push_back(id);
  code.push_back(value);
  return id;
}

void ShaderTranslator::SpirvBuilder::finalize() {
  bound = nextId + 100;
  code[4] = bound;
}

// --- Parser ---

ShaderTranslator::ShaderTranslator() {}
ShaderTranslator::~ShaderTranslator() {}

ShaderModel ShaderTranslator::detect_shader_model(const uint32_t* bytecode, uint32_t size) {
  if (size < 4) return ShaderModel::SM_3_0;
  if (bytecode[0] == DXBC_MAGIC) {
    if (size >= 28) {
      uint32_t minor = bytecode[5] & 0xFFFF;
      if (minor >= 0) return ShaderModel::SM_4_0;
    }
    return ShaderModel::SM_4_0;
  }
  uint32_t token = bytecode[0];
  if ((token & 0xFFFF0000) == 0x00030000) {
    uint32_t minor = (token >> 8) & 0xFF;
    if (minor >= 30) return ShaderModel::SM_3_0;
    if (minor >= 20) return ShaderModel::SM_2_X;
    return ShaderModel::SM_2_0;
  }
  return ShaderModel::SM_3_0;
}

bool ShaderTranslator::parse_chunks(const uint32_t* bytecode, uint32_t size, ParsedShader& out) {
  if (size < 8 || bytecode[0] != DXBC_MAGIC) {
    VKWIND_ERR(kTag, "Not DXBC bytecode");
    return false;
  }

  uint32_t chunkCount = bytecode[2];
  if (size < 8 + chunkCount * 8) {
    VKWIND_ERR(kTag, "DXBC truncated: chunkCount=%u size=%u", chunkCount, size);
    return false;
  }

  for (uint32_t i = 0; i < chunkCount; i++) {
    uint32_t chunkId = bytecode[8 + i * 2];
    uint32_t chunkOffset = bytecode[8 + i * 2 + 1];

    if (chunkOffset + 4 > size * 4) continue;
    uint32_t chunkSize = bytecode[chunkOffset / 4];
    const uint32_t* chunkData = bytecode + chunkOffset / 4 + 1;
    uint32_t chunkDataWords = (chunkSize - 4) / 4;

    switch (chunkId) {
      case CHUNK_ISGN: {
        if (chunkDataWords < 2) break;
        uint32_t count = chunkData[0];
        for (uint32_t j = 0; j < count && (2 + j * 6) < chunkDataWords; j++) {
          uint32_t offset = 1 + j * 6;
          if (offset + 5 >= chunkDataWords) break;
          uint32_t namePtr = chunkData[offset];
          uint32_t semanticIdx = chunkData[offset + 1];
          uint32_t sysValue = chunkData[offset + 2];
          uint32_t regComp = chunkData[offset + 3];
          uint32_t regIndex = chunkData[offset + 4];
          uint32_t mask = chunkData[offset + 5];

          ParsedShader::IODecl decl;
          if (namePtr < 12) decl.semantic = kSemanticNames[namePtr];
          else decl.semantic = "UNKNOWN";
          decl.index = semanticIdx;
          decl.registerIndex = regIndex;
          decl.componentMask = mask;
          out.inputs.push_back(decl);
        }
        break;
      }

      case CHUNK_OSGN: {
        if (chunkDataWords < 2) break;
        uint32_t count = chunkData[0];
        for (uint32_t j = 0; j < count && (2 + j * 6) < chunkDataWords; j++) {
          uint32_t offset = 1 + j * 6;
          if (offset + 5 >= chunkDataWords) break;
          uint32_t namePtr = chunkData[offset];
          uint32_t semanticIdx = chunkData[offset + 1];
          uint32_t sysValue = chunkData[offset + 2];
          uint32_t regComp = chunkData[offset + 3];
          uint32_t regIndex = chunkData[offset + 4];
          uint32_t mask = chunkData[offset + 5];

          ParsedShader::IODecl decl;
          if (namePtr < 12) decl.semantic = kSemanticNames[namePtr];
          else decl.semantic = "UNKNOWN";
          decl.index = semanticIdx;
          decl.registerIndex = regIndex;
          decl.componentMask = mask;
          out.outputs.push_back(decl);
        }
        break;
      }

      case CHUNK_SHDR: {
        out.bytecode.assign(chunkData, chunkData + chunkDataWords);
        break;
      }

      case CHUNK_STAT: {
        break;
      }
    }
  }

  return !out.bytecode.empty();
}

bool ShaderTranslator::decode_sm4_instruction(const uint32_t* data, uint32_t maxSize, SM4Instruction& out) {
  if (maxSize < 1) return false;
  out.opcode = data[0] & 0xFF;
  out.length = (data[0] >> 24) & 0xFF;
  if (out.length < 1 || out.length > maxSize) return false;

  out.operands.clear();
  for (uint32_t i = 1; i < out.length && i < maxSize; i++) {
    out.operands.push_back(data[i]);
  }
  return true;
}

void ShaderTranslator::analyze_sm4_instructions(const uint32_t* bytecode, uint32_t size, ParsedShader& out) {
  uint32_t pos = 0;
  while (pos < size) {
    SM4Instruction inst;
    if (!decode_sm4_instruction(bytecode + pos, size - pos, inst)) break;

    switch (inst.opcode) {
      case OP_DCL_CONST_BUFFER: {
        if (inst.operands.size() >= 3) {
          uint32_t regIdx = inst.operands[0] & 0xFFFF;
          uint32_t count = inst.operands[2];
          ConstantEntry entry;
          entry.name = "cbuffer_" + std::to_string(regIdx);
          entry.startRegister = regIdx;
          entry.registerCount = count;
          entry.bytes = count * 16;
          out.constants.push_back(entry);
        }
        break;
      }
      case OP_DCL_SAMPLER: {
        if (inst.operands.size() >= 1) {
          uint32_t regIdx = inst.operands[0] & 0xFFFF;
          ParsedShader::SamplerDecl sampler;
          sampler.name = "sampler_" + std::to_string(regIdx);
          sampler.registerIndex = regIdx;
          out.samplers.push_back(sampler);
        }
        break;
      }
      case OP_DCL_TEXTURE: {
        if (inst.operands.size() >= 1) {
          // texture declaration
        }
        break;
      }
      case OP_DCL_INPUT: {
        if (inst.operands.size() >= 1) {
          uint32_t regIdx = inst.operands[0] & 0xFFFF;
          // Check if already in inputs list
          bool found = false;
          for (auto& inp : out.inputs) {
            if (inp.registerIndex == regIdx) { found = true; break; }
          }
          if (!found) {
            ParsedShader::IODecl decl;
            decl.semantic = "INPUT";
            decl.registerIndex = regIdx;
            decl.componentMask = 0xF;
            out.inputs.push_back(decl);
          }
        }
        break;
      }
      case OP_DCL_OUTPUT: {
        if (inst.operands.size() >= 1) {
          uint32_t regIdx = inst.operands[0] & 0xFFFF;
          bool found = false;
          for (auto& outp : out.outputs) {
            if (outp.registerIndex == regIdx) { found = true; break; }
          }
          if (!found) {
            ParsedShader::IODecl decl;
            decl.semantic = "OUTPUT";
            decl.registerIndex = regIdx;
            decl.componentMask = 0xF;
            out.outputs.push_back(decl);
          }
        }
        break;
      }
      default:
        break;
    }
    pos += inst.length;
  }
}

ParsedShader ShaderTranslator::parse_dxbc(const uint32_t* bytecode, uint32_t size) {
  ParsedShader parsed;
  parsed.model = detect_shader_model(bytecode, size);

  if (parsed.model == ShaderModel::SM_4_0 || parsed.model == ShaderModel::SM_5_0) {
    parse_chunks(bytecode, size, parsed);
    if (!parsed.bytecode.empty()) {
      analyze_sm4_instructions(parsed.bytecode.data(),
                                static_cast<uint32_t>(parsed.bytecode.size()), parsed);
    }
  } else {
    parsed.bytecode.assign(bytecode, bytecode + size / 4);
  }

  return parsed;
}

// --- SPIR-V Generation ---

std::vector<uint32_t> ShaderTranslator::generate_spirv_vertex(const ParsedShader& parsed) {
  SpirvBuilder b;
  b.emit_header();
  b.emit_capability(SPV_CAP_SHADER);
  b.emit_ext_inst_import();
  b.emit_memory_model();

  // Predeclare types
  uint32_t voidType = b.type_void();
  uint32_t floatType = b.type_float();
  uint32_t intType = b.type_int();
  uint32_t vec2 = b.type_vec(floatType, 2);
  uint32_t vec3 = b.type_vec(floatType, 3);
  uint32_t vec4 = b.type_vec(floatType, 4);
  uint32_t ivec4 = b.type_vec(intType, 4);

  uint32_t ptrInput = b.type_pointer(vec4, SPV_SC_INPUT);
  uint32_t ptrOutput = b.type_pointer(vec4, SPV_SC_OUTPUT);
  uint32_t ptrUniform = b.type_pointer(vec4, SPV_SC_UNIFORM);

  // Position output builtin
  uint32_t posId = b.alloc_id();
  uint32_t ptrPosOutput = b.type_pointer(vec4, SPV_SC_OUTPUT);

  // Create input/output variables
  std::vector<uint32_t> inputIds;
  for (size_t i = 0; i < parsed.inputs.size(); i++) {
    uint32_t id = b.emit_variable(ptrInput, SPV_SC_INPUT);
    b.emit_decorate(id, SPV_DEC_LOCATION, static_cast<uint32_t>(i));
    inputIds.push_back(id);
  }

  std::vector<uint32_t> outputIds;
  for (size_t i = 0; i < parsed.outputs.size(); i++) {
    uint32_t id = b.emit_variable(ptrOutput, SPV_SC_OUTPUT);
    b.emit_decorate(id, SPV_DEC_LOCATION, static_cast<uint32_t>(i));
    outputIds.push_back(id);
  }

  // gl_Position
  bool hasPositionOutput = false;
  for (auto& o : parsed.outputs) {
    if (o.semantic == "POSITION") { hasPositionOutput = true; break; }
  }

  if (!hasPositionOutput) {
    uint32_t posVar = b.emit_variable(ptrPosOutput, SPV_SC_OUTPUT);
    b.emit_decorate(posVar, SPV_DEC_BUILTIN, SPV_BUILTIN_POSITION);
    posId = posVar;
  }

  // EntryPoint
  uint32_t funcType = b.type_function(voidType);
  uint32_t funcId = b.emit_function(voidType, 0, funcType);
  b.emit_name(funcId, "main");

  uint32_t labelId = b.alloc_id();
  b.emit_label(labelId);

  // If no declared outputs, do passthrough
  if (parsed.outputs.empty() && !parsed.inputs.empty()) {
    // Pass all inputs to outputs or position
    for (size_t i = 0; i < parsed.inputs.size(); i++) {
      uint32_t loaded = b.emit_load(vec4, inputIds[i]);
      if (i < outputIds.size()) {
        b.emit_store(outputIds[i], loaded);
      } else if (!hasPositionOutput && i == 0) {
        b.emit_store(posId, loaded);
      }
    }
  } else {
    // Copy inputs to matching outputs
    for (size_t i = 0; i < parsed.inputs.size() && i < parsed.outputs.size(); i++) {
      uint32_t loaded = b.emit_load(vec4, inputIds[i]);
      b.emit_store(outputIds[i], loaded);
    }

    // Position output
    if (!hasPositionOutput && !parsed.inputs.empty()) {
      uint32_t loaded = b.emit_load(vec4, inputIds[0]);
      b.emit_store(posId, loaded);
    }
  }

  b.emit_return();
  b.emit_function_end();
  b.finalize();

  VKWIND_INFO(kTag, "VS generated: %zu inputs, %zu outputs, %zu SPIR-V words",
    parsed.inputs.size(), parsed.outputs.size(), b.code.size());
  return b.code;
}

std::vector<uint32_t> ShaderTranslator::generate_spirv_pixel(const ParsedShader& parsed) {
  SpirvBuilder b;
  b.emit_header();
  b.emit_capability(SPV_CAP_SHADER);
  b.emit_ext_inst_import();
  b.emit_memory_model();

  uint32_t voidType = b.type_void();
  uint32_t floatType = b.type_float();
  uint32_t intType = b.type_int();
  uint32_t vec2 = b.type_vec(floatType, 2);
  uint32_t vec3 = b.type_vec(floatType, 3);
  uint32_t vec4 = b.type_vec(floatType, 4);

  uint32_t ptrInput = b.type_pointer(vec4, SPV_SC_INPUT);
  uint32_t ptrOutput = b.type_pointer(vec4, SPV_SC_OUTPUT);

  // Fragment output
  uint32_t fragColor = b.emit_variable(ptrOutput, SPV_SC_OUTPUT);
  b.emit_decorate(fragColor, SPV_DEC_LOCATION, 0);

  // Input variables
  std::vector<uint32_t> inputIds;
  for (size_t i = 0; i < parsed.inputs.size(); i++) {
    uint32_t id = b.emit_variable(ptrInput, SPV_SC_INPUT);
    b.emit_decorate(id, SPV_DEC_LOCATION, static_cast<uint32_t>(i));
    inputIds.push_back(id);
  }

  uint32_t funcType = b.type_function(voidType);
  uint32_t funcId = b.emit_function(voidType, 0, funcType);
  b.emit_name(funcId, "main");

  uint32_t labelId = b.alloc_id();
  b.emit_label(labelId);

  // Passthrough: first input to fragColor
  if (!parsed.inputs.empty()) {
    uint32_t loaded = b.emit_load(vec4, inputIds[0]);
    b.emit_store(fragColor, loaded);
  }

  b.emit_return();
  b.emit_function_end();
  b.finalize();

  VKWIND_INFO(kTag, "PS generated: %zu inputs, %zu SPIR-V words",
    parsed.inputs.size(), b.code.size());
  return b.code;
}

TranslatedShader ShaderTranslator::translate_vertex_shader(const uint32_t* bytecode, uint32_t size) {
  m_nextTempRegister = 0;
  m_nextCBVBinding = 0;
  m_nextTextureBinding = 0;

  ParsedShader parsed = parse_dxbc(bytecode, size);
  TranslatedShader result;
  result.entryPoint = "main";
  result.spirv = generate_spirv_vertex(parsed);

  for (auto& inp : parsed.inputs) {
    result.inputs.push_back({inp.semantic, inp.registerIndex, inp.componentMask});
  }
  for (auto& outp : parsed.outputs) {
    result.outputs.push_back({outp.semantic, outp.registerIndex, outp.componentMask});
  }

  VKWIND_INFO(kTag, "VS translated: %zu in, %zu out, %zu constants, %zu SPIR-V words",
    parsed.inputs.size(), parsed.outputs.size(), parsed.constants.size(), result.spirv.size());
  return result;
}

TranslatedShader ShaderTranslator::translate_pixel_shader(const uint32_t* bytecode, uint32_t size) {
  m_nextTempRegister = 0;
  m_nextCBVBinding = 0;
  m_nextTextureBinding = 0;

  ParsedShader parsed = parse_dxbc(bytecode, size);
  TranslatedShader result;
  result.entryPoint = "main";
  result.spirv = generate_spirv_pixel(parsed);

  for (auto& inp : parsed.inputs) {
    result.inputs.push_back({inp.semantic, inp.registerIndex, inp.componentMask});
  }
  for (auto& outp : parsed.outputs) {
    result.outputs.push_back({outp.semantic, outp.registerIndex, outp.componentMask});
  }

  VKWIND_INFO(kTag, "PS translated: %zu in, %zu out, %zu SPIR-V words",
    parsed.inputs.size(), parsed.outputs.size(), result.spirv.size());
  return result;
}

} // namespace vkwind
