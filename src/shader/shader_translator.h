#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <unordered_map>

namespace vkwind {

enum class ShaderModel {
  SM_2_0,
  SM_2_X,
  SM_3_0,
  SM_4_0,
  SM_5_0,
};

struct ConstantEntry {
  std::string name;
  uint32_t startRegister = 0;
  uint32_t registerCount = 0;
  uint32_t bytes = 0;
};

struct ParsedShader {
  ShaderModel model = ShaderModel::SM_3_0;
  std::vector<uint32_t> bytecode;

  struct IODecl {
    std::string semantic;
    uint32_t index = 0;
    uint32_t registerIndex = 0;
    uint32_t componentMask = 0xF;
  };
  std::vector<IODecl> inputs;
  std::vector<IODecl> outputs;
  std::vector<ConstantEntry> constants;

  struct SamplerDecl {
    std::string name;
    uint32_t registerIndex = 0;
    uint32_t dimensions = 2;
  };
  std::vector<SamplerDecl> samplers;
};

struct TranslatedShader {
  std::vector<uint32_t> spirv;
  std::string entryPoint = "main";

  struct UniformBinding { uint32_t set; uint32_t binding; std::string name; uint32_t size; };
  std::vector<UniformBinding> uniformBindings;

  struct TextureBinding { uint32_t set; uint32_t binding; std::string name; };
  std::vector<TextureBinding> textureBindings;

  struct InputBinding { std::string semantic; uint32_t location; uint32_t components; };
  std::vector<InputBinding> inputs;

  struct OutputBinding { std::string semantic; uint32_t location; uint32_t components; };
  std::vector<OutputBinding> outputs;
};

class ShaderTranslator {
public:
  ShaderTranslator();
  ~ShaderTranslator();

  TranslatedShader translate_vertex_shader(const uint32_t* bytecode, uint32_t size);
  TranslatedShader translate_pixel_shader(const uint32_t* bytecode, uint32_t size);
  ParsedShader parse_dxbc(const uint32_t* bytecode, uint32_t size);
  std::vector<uint32_t> generate_spirv_vertex(const ParsedShader& parsed);
  std::vector<uint32_t> generate_spirv_pixel(const ParsedShader& parsed);
  static ShaderModel detect_shader_model(const uint32_t* bytecode, uint32_t size);

private:
  bool parse_chunks(const uint32_t* bytecode, uint32_t size, ParsedShader& out);
  void analyze_sm4_instructions(const uint32_t* bytecode, uint32_t size, ParsedShader& out);

  // SPIR-V builder
  struct SpirvBuilder {
    std::vector<uint32_t> code;
    uint32_t bound = 100;
    uint32_t nextId = 1;

    uint32_t alloc_id() { return nextId++; }

    void emit_header();
    void emit_capability(uint32_t cap);
    void emit_ext_inst_import();
    void emit_memory_model();
    void emit_name(uint32_t id, const char* name);
    void emit_decorate(uint32_t id, uint32_t decoration, uint32_t value = 0);
    void emit_member_decorate(uint32_t id, uint32_t member, uint32_t decoration, uint32_t value = 0);

    uint32_t type_void();
    uint32_t type_float();
    uint32_t type_int();
    uint32_t type_vec(uint32_t componentType, uint32_t componentCount);
    uint32_t type_mat(uint32_t vecType, uint32_t colCount);
    uint32_t type_pointer(uint32_t type, uint32_t storageClass);
    uint32_t type_struct(const std::vector<uint32_t>& members);
    uint32_t type_array(uint32_t type, uint32_t length);
    uint32_t type_image(uint32_t sampledType, uint32_t dim, uint32_t depth, uint32_t arrayed, uint32_t multisampled, uint32_t sampled, uint32_t format);
    uint32_t type_sampler();
    uint32_t type_sampled_image(uint32_t imageType);
    uint32_t type_function(uint32_t returnType);

    uint32_t emit_variable(uint32_t type, uint32_t storageClass);
    uint32_t emit_function(uint32_t returnType, uint32_t control, uint32_t funcType);
    void emit_function_end();
    void emit_return();
    void emit_label(uint32_t labelId);
    uint32_t emit_load(uint32_t type, uint32_t pointer);
    void emit_store(uint32_t pointer, uint32_t value);
    uint32_t emit_access_chain(uint32_t type, uint32_t base, const std::vector<uint32_t>& indices);
    uint32_t emit_image_sample_implicit_lod(uint32_t resultType, uint32_t sampledImage, uint32_t coordinate);
    uint32_t emit_image_sample_explicit_lod(uint32_t resultType, uint32_t sampledImage, uint32_t coordinate, uint32_t lod);
    uint32_t emit_image_fetch(uint32_t resultType, uint32_t image, uint32_t coordinate, uint32_t lod);
    uint32_t emit_composite_construct(uint32_t type, const std::vector<uint32_t>& components);
    uint32_t emit_vector_extract_dynamic(uint32_t resultType, uint32_t vector, uint32_t index);
    uint32_t emit_f_add(uint32_t type, uint32_t a, uint32_t b);
    uint32_t emit_f_sub(uint32_t type, uint32_t a, uint32_t b);
    uint32_t emit_f_mul(uint32_t type, uint32_t a, uint32_t b);
    uint32_t emit_f_div(uint32_t type, uint32_t a, uint32_t b);
    uint32_t emit_f_min(uint32_t type, uint32_t a, uint32_t b);
    uint32_t emit_f_max(uint32_t type, uint32_t a, uint32_t b);
    uint32_t emit_f_clamp(uint32_t type, uint32_t x, uint32_t minVal, uint32_t maxVal);
    uint32_t emit_f_saturate(uint32_t type, uint32_t x);
    uint32_t emit_dp2(uint32_t type, uint32_t a, uint32_t b);
    uint32_t emit_dp3(uint32_t type, uint32_t a, uint32_t b);
    uint32_t emit_dp4(uint32_t type, uint32_t a, uint32_t b);
    uint32_t emit_cross(uint32_t type, uint32_t a, uint32_t b);
    uint32_t emit_normalize(uint32_t type, uint32_t x);
    uint32_t emit_length(uint32_t type, uint32_t x);
    uint32_t emit_dot(uint32_t type, uint32_t a, uint32_t b);
    uint32_t emit_matrix_times_vector(uint32_t type, uint32_t matrix, uint32_t vector);
    uint32_t emit_vector_times_matrix(uint32_t type, uint32_t vector, uint32_t matrix);
    uint32_t emit_vector_times_scalar(uint32_t type, uint32_t vector, uint32_t scalar);
    uint32_t emit_ext_inst(uint32_t resultType, uint32_t set, uint32_t inst, const std::vector<uint32_t>& operands);
    void emit_branch(uint32_t label);
    void emit_branch_conditional(uint32_t condition, uint32_t trueLabel, uint32_t falseLabel);
    uint32_t emit_i_equal(uint32_t resultType, uint32_t a, uint32_t b);
    uint32_t emit_i_not_equal(uint32_t resultType, uint32_t a, uint32_t b);
    uint32_t emit_select(uint32_t type, uint32_t condition, uint32_t trueVal, uint32_t falseVal);
    uint32_t emit_s_conv_to_f(uint32_t resultType, uint32_t value);
    uint32_t emit_u_conv_to_f(uint32_t resultType, uint32_t value);
    uint32_t emit_f_conv_to_s(uint32_t resultType, uint32_t value);
    uint32_t emit_logical_and(uint32_t resultType, uint32_t a, uint32_t b);
    uint32_t emit_logical_or(uint32_t resultType, uint32_t a, uint32_t b);
    uint32_t emit_logical_not(uint32_t resultType, uint32_t x);

    void finalize();
  };

  // SM4 instruction decoding
  enum SM4Opcode : uint32_t;
  struct SM4Instruction {
    uint32_t opcode = 0;
    uint32_t length = 0;
    std::vector<uint32_t> operands;
  };

  bool decode_sm4_instruction(const uint32_t* data, uint32_t maxSize, SM4Instruction& out);
  void emit_sm4_instruction(SpirvBuilder& spirv, const SM4Instruction& inst,
                             const ParsedShader& parsed,
                             std::unordered_map<uint32_t, uint32_t>& temps,
                             std::unordered_map<uint32_t, uint32_t>& constantBuffers,
                             std::unordered_map<uint32_t, uint32_t>& samplers,
                             std::unordered_map<uint32_t, uint32_t>& inputs,
                             std::unordered_map<uint32_t, uint32_t>& outputs,
                             uint32_t float4Type, uint32_t floatType, uint32_t vec4InputType, uint32_t vec4OutputType);

  uint32_t m_nextTempRegister = 0;
  uint32_t m_nextCBVBinding = 0;
  uint32_t m_nextTextureBinding = 0;
};

} // namespace vkwind
