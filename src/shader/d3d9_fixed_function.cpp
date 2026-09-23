#include "d3d9_fixed_function.h"
#include "../util/util_log.h"
#include <cstring>

namespace vkwind {

static const char* kTag = "FixedFn";

// ============================================================================
// Section-based SPIR-V builder — ensures correct section ordering
// ============================================================================
//
// SPIR-V section order:
//   1. Header (magic, version, generator, bound, schema)
//   2. Capabilities
//   3. Extensions
//   4. ExtInstImports
//   5. MemoryModel
//   6. EntryPoint
//   7. ExecutionModes
//   8. Debug (names)
//   9. Annotations (decorations)
//  10. Types, Constants, Variables (declarations)
//  11. Functions (definitions)
//

namespace {

// SPIR-V opcodes
enum Op : uint32_t {
  OpNop=0, OpSource=3, OpName=5, OpMemberName=6,
  OpExtInstImport=11, OpExtInst=12, OpMemoryModel=14,
  OpEntryPoint=15, OpExecutionMode=16, OpCapability=17,
  OpTypeVoid=19, OpTypeInt=21, OpTypeFloat=22, OpTypeVector=23,
  OpTypeImage=25, OpTypeSampler=26, OpTypeSampledImage=27,
  OpTypeStruct=30, OpTypePointer=32, OpTypeFunction=33,
  OpConstant=43, OpConstantComposite=44, OpFunction=54, OpFunctionEnd=56,
  OpVariable=59, OpLoad=61, OpStore=62, OpAccessChain=65,
  OpDecorate=71, OpMemberDecorate=72,
  OpImageSampleImplicitLod=87,
  OpFAdd=129, OpFSub=131, OpFMul=133,
  OpVectorTimesScalar=142,
  OpCompositeExtract=81, OpCompositeConstruct=80,
  OpLabel=248, OpReturn=253, OpKill=252,
};

enum SC : uint32_t {
  SC_UniformConstant=0, SC_Input=1, SC_Uniform=2, SC_Output=3, SC_Function=7,
};

enum Dec : uint32_t {
  DecBlock=2, DecLocation=30, DecBinding=33, DecDescriptorSet=34, DecOffset=35,
};

// SPIR-V assembler with section tracking
class SpvBuilder {
public:
  // Sections (in order of appearance in final SPIR-V)
  std::vector<uint32_t> secCapabilities;
  std::vector<uint32_t> secExtImports;
  std::vector<uint32_t> secMemoryModel;
  std::vector<uint32_t> secEntryPoint;
  std::vector<uint32_t> secExecutionModes;
  std::vector<uint32_t> secAnnotations;
  std::vector<uint32_t> secDeclarations; // types, constants, variables
  std::vector<uint32_t> secFunctions;

  uint32_t nextId = 1;
  uint32_t id() { return nextId++; }

  void emitOp(std::vector<uint32_t>& section, uint32_t opcode, uint32_t wordCount) {
    section.push_back((wordCount << 16) | opcode);
  }

  // --- Capabilities ---
  void capability(uint32_t cap) {
    emitOp(secCapabilities, OpCapability, 2);
    secCapabilities.push_back(cap);
  }

  // --- ExtInstImport ---
  uint32_t extInstImport(const char* name) {
    uint32_t tid = id();
    uint32_t nameLen = (uint32_t)(strlen(name) + 1);
    uint32_t paddedLen = (nameLen + 3) & ~3u;
    uint32_t wordCount = 2 + paddedLen / 4;
    emitOp(secExtImports, OpExtInstImport, wordCount);
    secExtImports.push_back(tid);
    for (uint32_t i = 0; i < paddedLen / 4; i++) {
      uint32_t w = 0;
      for (int j = 0; j < 4; j++) {
        uint32_t idx = i * 4 + j;
        if (idx < nameLen) w |= ((uint32_t)(uint8_t)name[idx]) << (j * 8);
      }
      secExtImports.push_back(w);
    }
    return tid;
  }

  // --- MemoryModel ---
  void memoryModel(uint32_t addrModel, uint32_t memModel) {
    emitOp(secMemoryModel, OpMemoryModel, 3);
    secMemoryModel.push_back(addrModel);
    secMemoryModel.push_back(memModel);
  }

  // --- EntryPoint ---
  void entryPoint(uint32_t model, uint32_t funcId, const char* name,
                  const std::vector<uint32_t>& interfaces) {
    uint32_t nameLen = (uint32_t)(strlen(name) + 1);
    uint32_t paddedLen = (nameLen + 3) & ~3u;
    uint32_t wordCount = 3 + paddedLen / 4 + (uint32_t)interfaces.size();
    emitOp(secEntryPoint, OpEntryPoint, wordCount);
    secEntryPoint.push_back(model);
    secEntryPoint.push_back(funcId);
    for (uint32_t i = 0; i < paddedLen / 4; i++) {
      uint32_t w = 0;
      for (int j = 0; j < 4; j++) {
        uint32_t idx = i * 4 + j;
        if (idx < nameLen) w |= ((uint32_t)(uint8_t)name[idx]) << (j * 8);
      }
      secEntryPoint.push_back(w);
    }
    for (auto iface : interfaces) secEntryPoint.push_back(iface);
  }

  // --- ExecutionMode ---
  void executionMode(uint32_t funcId, uint32_t mode) {
    emitOp(secExecutionModes, OpExecutionMode, 3);
    secExecutionModes.push_back(funcId);
    secExecutionModes.push_back(mode);
  }

  // --- Decorations ---
  void decorate(uint32_t target, uint32_t dec, uint32_t value = 0) {
    emitOp(secAnnotations, OpDecorate, 4);
    secAnnotations.push_back(target);
    secAnnotations.push_back(dec);
    secAnnotations.push_back(value);
  }

  void memberDecorate(uint32_t type, uint32_t member, uint32_t dec, uint32_t value = 0) {
    emitOp(secAnnotations, OpMemberDecorate, 5);
    secAnnotations.push_back(type);
    secAnnotations.push_back(member);
    secAnnotations.push_back(dec);
    secAnnotations.push_back(value);
  }

  // --- Types (in declarations section) ---
  uint32_t typeVoid() {
    uint32_t t = id();
    emitOp(secDeclarations, OpTypeVoid, 2);
    secDeclarations.push_back(t);
    return t;
  }

  uint32_t typeFloat(uint32_t width = 32) {
    uint32_t t = id();
    emitOp(secDeclarations, OpTypeFloat, 3);
    secDeclarations.push_back(t);
    secDeclarations.push_back(width);
    return t;
  }

  uint32_t typeInt(uint32_t width = 32) {
    uint32_t t = id();
    emitOp(secDeclarations, OpTypeInt, 4);
    secDeclarations.push_back(t);
    secDeclarations.push_back(width);
    secDeclarations.push_back(1); // signed
    return t;
  }

  uint32_t typeVec(uint32_t compType, uint32_t count) {
    uint32_t t = id();
    emitOp(secDeclarations, OpTypeVector, 4);
    secDeclarations.push_back(t);
    secDeclarations.push_back(compType);
    secDeclarations.push_back(count);
    return t;
  }

  uint32_t typePointer(uint32_t sc, uint32_t typeId) {
    uint32_t t = id();
    emitOp(secDeclarations, OpTypePointer, 4);
    secDeclarations.push_back(t);
    secDeclarations.push_back(sc);
    secDeclarations.push_back(typeId);
    return t;
  }

  uint32_t typeFunction(uint32_t returnType) {
    uint32_t t = id();
    emitOp(secDeclarations, OpTypeFunction, 3);
    secDeclarations.push_back(t);
    secDeclarations.push_back(returnType);
    return t;
  }

  uint32_t typeStruct(const std::vector<uint32_t>& members) {
    uint32_t t = id();
    emitOp(secDeclarations, OpTypeStruct, 2 + (uint32_t)members.size());
    secDeclarations.push_back(t);
    for (auto m : members) secDeclarations.push_back(m);
    return t;
  }

  uint32_t typeImage(uint32_t sampledType) {
    uint32_t t = id();
    // OpTypeImage sampledType Dim=1(2D) Depth=0 Arrayed=0 MS=0 Sampled=1 Format=0
    emitOp(secDeclarations, OpTypeImage, 9);
    secDeclarations.push_back(t);
    secDeclarations.push_back(sampledType);
    secDeclarations.push_back(1); // Dim = 2D
    secDeclarations.push_back(0); // Depth
    secDeclarations.push_back(0); // Arrayed
    secDeclarations.push_back(0); // MS
    secDeclarations.push_back(1); // Sampled
    secDeclarations.push_back(0); // Format = Unknown
    return t;
  }

  uint32_t typeSampler() {
    uint32_t t = id();
    emitOp(secDeclarations, OpTypeSampler, 2);
    secDeclarations.push_back(t);
    return t;
  }

  uint32_t typeSampledImage(uint32_t imageType) {
    uint32_t t = id();
    emitOp(secDeclarations, OpTypeSampledImage, 3);
    secDeclarations.push_back(t);
    secDeclarations.push_back(imageType);
    return t;
  }

  // --- Constants ---
  uint32_t constantFloat(uint32_t floatType, float value) {
    uint32_t t = id();
    uint32_t bits;
    memcpy(&bits, &value, 4);
    emitOp(secDeclarations, OpConstant, 4);
    secDeclarations.push_back(floatType);
    secDeclarations.push_back(t);
    secDeclarations.push_back(bits);
    return t;
  }

  uint32_t constantInt(uint32_t intType, int32_t value) {
    uint32_t t = id();
    emitOp(secDeclarations, OpConstant, 4);
    secDeclarations.push_back(intType);
    secDeclarations.push_back(t);
    secDeclarations.push_back((uint32_t)value);
    return t;
  }

  uint32_t constantComposite(uint32_t type, const std::vector<uint32_t>& constituents) {
    uint32_t t = id();
    emitOp(secDeclarations, OpConstantComposite, 2 + (uint32_t)constituents.size());
    secDeclarations.push_back(type);
    secDeclarations.push_back(t);
    for (auto c : constituents) secDeclarations.push_back(c);
    return t;
  }

  // --- Variables (declarations section) ---
  uint32_t variable(uint32_t ptrType, uint32_t sc) {
    uint32_t t = id();
    emitOp(secDeclarations, OpVariable, 4);
    secDeclarations.push_back(ptrType);
    secDeclarations.push_back(t);
    secDeclarations.push_back(sc);
    return t;
  }

  // --- Function body (functions section) ---
  void function(uint32_t retType, uint32_t funcId, uint32_t funcTypeId) {
    emitOp(secFunctions, OpFunction, 5);
    secFunctions.push_back(retType);
    secFunctions.push_back(funcId);
    secFunctions.push_back(0); // No function control
    secFunctions.push_back(funcTypeId);
  }

  void functionEnd() { emitOp(secFunctions, OpFunctionEnd, 1); }
  void ret() { emitOp(secFunctions, OpReturn, 1); }
  void kill() { emitOp(secFunctions, OpKill, 1); }

  void label(uint32_t id) {
    emitOp(secFunctions, OpLabel, 2);
    secFunctions.push_back(id);
  }

  uint32_t load(uint32_t type, uint32_t ptr) {
    uint32_t t = id();
    emitOp(secFunctions, OpLoad, 4);
    secFunctions.push_back(type);
    secFunctions.push_back(t);
    secFunctions.push_back(ptr);
    return t;
  }

  void store(uint32_t ptr, uint32_t value) {
    emitOp(secFunctions, OpStore, 3);
    secFunctions.push_back(ptr);
    secFunctions.push_back(value);
  }

  uint32_t accessChain(uint32_t resultType, uint32_t base, const std::vector<uint32_t>& indices) {
    uint32_t t = id();
    emitOp(secFunctions, OpAccessChain, 3 + (uint32_t)indices.size());
    secFunctions.push_back(resultType);
    secFunctions.push_back(t);
    secFunctions.push_back(base);
    for (auto i : indices) secFunctions.push_back(i);
    return t;
  }

  uint32_t fadd(uint32_t type, uint32_t a, uint32_t b) {
    uint32_t t = id();
    emitOp(secFunctions, OpFAdd, 5);
    secFunctions.push_back(type); secFunctions.push_back(t);
    secFunctions.push_back(a); secFunctions.push_back(b);
    return t;
  }

  uint32_t fsub(uint32_t type, uint32_t a, uint32_t b) {
    uint32_t t = id();
    emitOp(secFunctions, OpFSub, 5);
    secFunctions.push_back(type); secFunctions.push_back(t);
    secFunctions.push_back(a); secFunctions.push_back(b);
    return t;
  }

  uint32_t fmul(uint32_t type, uint32_t a, uint32_t b) {
    uint32_t t = id();
    emitOp(secFunctions, OpFMul, 5);
    secFunctions.push_back(type); secFunctions.push_back(t);
    secFunctions.push_back(a); secFunctions.push_back(b);
    return t;
  }

  uint32_t vectorTimesScalar(uint32_t type, uint32_t vec, uint32_t scalar) {
    uint32_t t = id();
    emitOp(secFunctions, OpVectorTimesScalar, 5);
    secFunctions.push_back(type); secFunctions.push_back(t);
    secFunctions.push_back(vec); secFunctions.push_back(scalar);
    return t;
  }

  uint32_t compositeExtract(uint32_t type, uint32_t composite, uint32_t index) {
    uint32_t t = id();
    emitOp(secFunctions, OpCompositeExtract, 5);
    secFunctions.push_back(type); secFunctions.push_back(t);
    secFunctions.push_back(composite); secFunctions.push_back(index);
    return t;
  }

  uint32_t compositeConstruct(uint32_t type, const std::vector<uint32_t>& constituents) {
    uint32_t t = id();
    emitOp(secFunctions, OpCompositeConstruct, 2 + (uint32_t)constituents.size());
    secFunctions.push_back(type); secFunctions.push_back(t);
    for (auto c : constituents) secFunctions.push_back(c);
    return t;
  }

  uint32_t imageSample(uint32_t resultType, uint32_t sampledImage, uint32_t coord) {
    uint32_t t = id();
    emitOp(secFunctions, OpImageSampleImplicitLod, 5);
    secFunctions.push_back(resultType); secFunctions.push_back(t);
    secFunctions.push_back(sampledImage); secFunctions.push_back(coord);
    return t;
  }

  // --- Finalize: assemble all sections in correct order ---
  std::vector<uint32_t> finalize() {
    std::vector<uint32_t> result;

    // Header
    result.push_back(0x07230203); // SPIR-V magic
    result.push_back(0x00010000); // Version 1.0
    result.push_back(0x00000000); // Generator
    result.push_back(nextId);     // Bound
    result.push_back(0);          // Schema

    // Sections in order
    auto append = [&](const std::vector<uint32_t>& s) {
      result.insert(result.end(), s.begin(), s.end());
    };
    append(secCapabilities);
    append(secExtImports);
    append(secMemoryModel);
    append(secEntryPoint);
    append(secExecutionModes);
    append(secAnnotations);
    append(secDeclarations);
    append(secFunctions);

    return result;
  }
};

// ============================================================================
// D3D9 texture stage helper types
// ============================================================================

struct StageState {
  uint32_t colorOp, colorArg1, colorArg2;
  uint32_t alphaOp, alphaArg1, alphaArg2;
  uint32_t resultArg, texCoordIndex;
};

// Resolve a D3DTA argument (handles COMPLEMENT and ALPHAREPLICATE modifiers)
uint32_t resolveArg(SpvBuilder& spv, uint32_t arg, uint32_t stage,
                    uint32_t vec4Type, uint32_t floatType,
                    uint32_t diffuse, uint32_t current, uint32_t temp,
                    uint32_t specular, uint32_t tfactor,
                    const uint32_t* texSample) {
  uint32_t base = arg & 0x0F;
  bool complement = (arg & 0x10) != 0;
  bool alphaRep = (arg & 0x20) != 0;

  uint32_t value = 0;
  switch (base) {
    case 0: value = diffuse; break;    // DIFFUSE
    case 1: value = current; break;    // CURRENT
    case 2: value = texSample[stage]; break; // TEXTURE
    case 3: value = tfactor; break;    // TFACTOR
    case 4: value = specular; break;   // SPECULAR
    case 5: value = temp; break;       // TEMP
    default: value = diffuse; break;
  }

  if (complement) {
    uint32_t one = spv.constantFloat(floatType, 1.0f);
    uint32_t ones = spv.constantComposite(vec4Type, {one, one, one, one});
    value = spv.fsub(vec4Type, ones, value);
  }

  if (alphaRep) {
    uint32_t a = spv.compositeExtract(floatType, value, 3);
    value = spv.compositeConstruct(vec4Type, {a, a, a, a});
  }

  return value;
}

// Apply color operation
uint32_t applyColorOp(SpvBuilder& spv, uint32_t op, uint32_t a1, uint32_t a2,
                      uint32_t current, uint32_t diffuse, uint32_t stage,
                      uint32_t vec4Type, uint32_t floatType,
                      const uint32_t* texSample) {
  uint32_t V = vec4Type, F = floatType;

  switch (op) {
    case 2: return a1; // SELECTARG1
    case 3: return a2; // SELECTARG2
    case 4: return spv.fmul(V, a1, a2); // MODULATE
    case 5: { // MODULATE2X
      uint32_t two = spv.constantFloat(F, 2.0f);
      uint32_t twos = spv.constantComposite(V, {two, two, two, two});
      return spv.fmul(V, a1, spv.fmul(V, a2, twos));
    }
    case 6: { // MODULATE4X
      uint32_t four = spv.constantFloat(F, 4.0f);
      uint32_t fours = spv.constantComposite(V, {four, four, four, four});
      return spv.fmul(V, a1, spv.fmul(V, a2, fours));
    }
    case 7: return spv.fadd(V, a1, a2); // ADD
    case 8: { // ADDSIGNED
      uint32_t half = spv.constantFloat(F, 0.5f);
      uint32_t halves = spv.constantComposite(V, {half, half, half, half});
      return spv.fsub(V, spv.fadd(V, a1, a2), halves);
    }
    case 9: { // ADDSIGNED2X
      uint32_t half = spv.constantFloat(F, 0.5f);
      uint32_t halves = spv.constantComposite(V, {half, half, half, half});
      uint32_t sum = spv.fsub(V, spv.fadd(V, a1, a2), halves);
      uint32_t two = spv.constantFloat(F, 2.0f);
      return spv.fmul(V, sum, spv.constantComposite(V, {two, two, two, two}));
    }
    case 10: return spv.fsub(V, a1, a2); // SUBTRACT
    case 11: return spv.fsub(V, spv.fadd(V, a1, a2), spv.fmul(V, a1, a2)); // ADDSMOOTH
    case 12: { // BLENDDIFFUSEALPHA
      uint32_t da = spv.compositeExtract(F, diffuse, 3);
      uint32_t invDa = spv.fsub(F, spv.constantFloat(F, 1.0f), da);
      return spv.fadd(V, spv.vectorTimesScalar(V, a1, da), spv.vectorTimesScalar(V, a2, invDa));
    }
    case 13: { // BLENDTEXTUREALPHA
      uint32_t ta = spv.compositeExtract(F, texSample[stage], 3);
      uint32_t invTa = spv.fsub(F, spv.constantFloat(F, 1.0f), ta);
      return spv.fadd(V, spv.vectorTimesScalar(V, a1, ta), spv.vectorTimesScalar(V, a2, invTa));
    }
    case 16: { // BLENDCURRENTALPHA
      uint32_t ca = spv.compositeExtract(F, current, 3);
      uint32_t invCa = spv.fsub(F, spv.constantFloat(F, 1.0f), ca);
      return spv.fadd(V, spv.vectorTimesScalar(V, a1, ca), spv.vectorTimesScalar(V, a2, invCa));
    }
    case 18: { // MODULATEALPHA_ADDCOLOR
      uint32_t a1a = spv.compositeExtract(F, a1, 3);
      uint32_t scaled = spv.vectorTimesScalar(V, a1, a1a);
      return spv.fadd(V, scaled, a2);
    }
    case 19: return spv.fmul(V, a1, a2); // MODULATECOLOR_ADDALPHA (approx)
    case 20: { // MODULATEINVALPHA_ADDCOLOR
      uint32_t a1a = spv.compositeExtract(F, a1, 3);
      uint32_t inv = spv.fsub(F, spv.constantFloat(F, 1.0f), a1a);
      return spv.fadd(V, spv.vectorTimesScalar(V, a1, inv), a2);
    }
    case 21: { // MODULATEINVCOLOR_ADDALPHA
      uint32_t one = spv.constantFloat(F, 1.0f);
      uint32_t ones = spv.constantComposite(V, {one, one, one, one});
      return spv.fmul(V, spv.fsub(V, ones, a1), a2);
    }
    case 24: { // DOTPRODUCT3
      uint32_t r1 = spv.compositeExtract(F, a1, 0);
      uint32_t g1 = spv.compositeExtract(F, a1, 1);
      uint32_t b1 = spv.compositeExtract(F, a1, 2);
      uint32_t r2 = spv.compositeExtract(F, a2, 0);
      uint32_t g2 = spv.compositeExtract(F, a2, 1);
      uint32_t b2 = spv.compositeExtract(F, a2, 2);
      uint32_t d = spv.fadd(F, spv.fadd(F, spv.fmul(F, r1, r2), spv.fmul(F, g1, g2)), spv.fmul(F, b1, b2));
      return spv.compositeConstruct(V, {d, d, d, d});
    }
    case 25: return spv.fadd(V, spv.fmul(V, a1, a2), current); // MULTIPLYADD
    case 26: { // LERP
      uint32_t ca = spv.compositeExtract(F, current, 3);
      uint32_t invCa = spv.fsub(F, spv.constantFloat(F, 1.0f), ca);
      return spv.fadd(V, spv.vectorTimesScalar(V, a1, ca), spv.vectorTimesScalar(V, a2, invCa));
    }
    default:
      return a1; // fallback: SELECTARG1
  }
}

// Apply alpha operation (same as color but result alpha is used)
uint32_t applyAlphaOp(SpvBuilder& spv, uint32_t op, uint32_t a1, uint32_t a2,
                      uint32_t current, uint32_t diffuse, uint32_t stage,
                      uint32_t vec4Type, uint32_t floatType,
                      const uint32_t* texSample) {
  // For alpha ops, D3D9 semantics are the same as color ops
  // The result's alpha channel is what matters
  return applyColorOp(spv, op, a1, a2, current, diffuse, stage, vec4Type, floatType, texSample);
}

} // anonymous namespace

// ============================================================================
// Main entry point
// ============================================================================

std::vector<uint32_t> generate_fixed_function_pixel_shader(
    const TextureStageConfig& config) {

  VKWIND_INFO(kTag, "Generating FF shader: %u active textures, tfactor=0x%08x",
    config.activeTextureCount, config.textureFactor);

  SpvBuilder spv;

  // --- Section 1: Capabilities ---
  spv.capability(1); // Shader

  // --- Section 2: ExtInstImports ---
  spv.extInstImport("GLSL.std.450");

  // --- Section 3: MemoryModel ---
  spv.memoryModel(0, 2); // Logical, GLSL450

  // --- Types ---
  uint32_t F = spv.typeFloat(32);
  uint32_t I = spv.typeInt(32);
  uint32_t V2 = spv.typeVec(F, 2);
  uint32_t V4 = spv.typeVec(F, 4);
  uint32_t voidT = spv.typeVoid();
  uint32_t funcT = spv.typeFunction(voidT);

  // Push constant struct: { vec4 mvp[4], vec4 extras }
  // extras = (alphaRef, alphaFunc, tfactorPacked_unused, 0)
  uint32_t pcStruct = spv.typeStruct({V4, V4, V4, V4, V4});
  spv.decorate(pcStruct, DecBlock);
  spv.memberDecorate(pcStruct, 0, DecOffset, 0);
  spv.memberDecorate(pcStruct, 1, DecOffset, 16);
  spv.memberDecorate(pcStruct, 2, DecOffset, 32);
  spv.memberDecorate(pcStruct, 3, DecOffset, 48);
  spv.memberDecorate(pcStruct, 4, DecOffset, 64);

  uint32_t imageT = spv.typeImage(F);
  uint32_t samplerT = spv.typeSampler();
  uint32_t sampledImageT = spv.typeSampledImage(imageT);

  uint32_t pcPtr = spv.typePointer(SC_Uniform, pcStruct);
  uint32_t samplerPtr = spv.typePointer(SC_UniformConstant, sampledImageT);
  uint32_t inColorPtr = spv.typePointer(SC_Input, V4);
  uint32_t inTexCoordPtr = spv.typePointer(SC_Input, V2);
  uint32_t outColorPtr = spv.typePointer(SC_Output, V4);

  // --- Constants ---
  uint32_t f1 = spv.constantFloat(F, 1.0f);
  uint32_t f0 = spv.constantFloat(F, 0.0f);
  uint32_t ones4 = spv.constantComposite(V4, {f1, f1, f1, f1});
  uint32_t zeros4 = spv.constantComposite(V4, {f0, f0, f0, f0});

  uint32_t tfR = spv.constantFloat(F, (float)((config.textureFactor >> 0) & 0xFF) / 255.0f);
  uint32_t tfG = spv.constantFloat(F, (float)((config.textureFactor >> 8) & 0xFF) / 255.0f);
  uint32_t tfB = spv.constantFloat(F, (float)((config.textureFactor >> 16) & 0xFF) / 255.0f);
  uint32_t tfA = spv.constantFloat(F, (float)((config.textureFactor >> 24) & 0xFF) / 255.0f);
  uint32_t tfactorVec = spv.constantComposite(V4, {tfR, tfG, tfB, tfA});

  // --- Variables ---
  uint32_t pcVar = spv.variable(pcPtr, SC_Uniform);
  spv.decorate(pcVar, DecDescriptorSet, 0);
  spv.decorate(pcVar, DecBinding, 0);

  uint32_t activeTex = config.activeTextureCount > 8 ? 8 : config.activeTextureCount;
  if (activeTex == 0) activeTex = 1;

  uint32_t samplerVars[8] = {};
  for (uint32_t i = 0; i < activeTex; i++) {
    samplerVars[i] = spv.variable(samplerPtr, SC_UniformConstant);
    spv.decorate(samplerVars[i], DecDescriptorSet, 0);
    spv.decorate(samplerVars[i], DecBinding, 3 + i);
  }

  uint32_t inColor = spv.variable(inColorPtr, SC_Input);
  spv.decorate(inColor, DecLocation, 0);
  uint32_t inTexCoord = spv.variable(inTexCoordPtr, SC_Input);
  spv.decorate(inTexCoord, DecLocation, 1);
  uint32_t outColor = spv.variable(outColorPtr, SC_Output);
  spv.decorate(outColor, DecLocation, 0);

  // --- Function ---
  uint32_t mainId = spv.id();
  spv.function(voidT, mainId, funcT);
  uint32_t entryLabel = spv.id();
  spv.label(entryLabel);

  // Load inputs
  uint32_t diffuse = spv.load(V4, inColor);
  uint32_t texCoord = spv.load(V2, inTexCoord);
  uint32_t current = diffuse;
  uint32_t temp = diffuse;
  uint32_t specular = diffuse;

  // Sample textures
  uint32_t texSample[8];
  for (uint32_t i = 0; i < activeTex; i++) {
    uint32_t sampledImg = spv.load(sampledImageT, samplerVars[i]);
    texSample[i] = spv.imageSample(V4, sampledImg, texCoord);
  }
  for (uint32_t i = activeTex; i < 8; i++) {
    texSample[i] = ones4;
  }

  // Load alpha test from push constants
  // extras = (alphaRef, alphaFunc, ?, ?)
  uint32_t extrasPtr = spv.accessChain(spv.typePointer(SC_Uniform, V4), pcVar, {spv.constantInt(I, 4)});
  uint32_t extras = spv.load(V4, extrasPtr);
  uint32_t alphaRef = spv.compositeExtract(F, extras, 0);
  uint32_t alphaFunc = spv.compositeExtract(F, extras, 1); // stored as float for simplicity

  // === Texture combiner chain ===
  current = diffuse;

  for (uint32_t stage = 0; stage < activeTex; stage++) {
    uint32_t cop = config.colorOp[stage];
    uint32_t aop = config.alphaOp[stage];

    // Handle uninitialized/disabled
    if (cop == 0) {
      // Uninitialized: if first stage with a texture, assume MODULATE (tex * current)
      if (stage == 0) cop = 4;
      else break;
    }
    if (cop == 1) continue; // DISABLE

    // Resolve args
    uint32_t c1 = resolveArg(spv, config.colorArg1[stage], stage, V4, F,
                             diffuse, current, temp, specular, tfactorVec, texSample);
    uint32_t c2 = resolveArg(spv, config.colorArg2[stage], stage, V4, F,
                             diffuse, current, temp, specular, tfactorVec, texSample);
    uint32_t a1 = resolveArg(spv, config.alphaArg1[stage], stage, V4, F,
                             diffuse, current, temp, specular, tfactorVec, texSample);
    uint32_t a2 = resolveArg(spv, config.alphaArg2[stage], stage, V4, F,
                             diffuse, current, temp, specular, tfactorVec, texSample);

    // Apply color op
    uint32_t colorResult = applyColorOp(spv, cop, c1, c2, current, diffuse, stage, V4, F, texSample);

    // Apply alpha op
    uint32_t alphaResult = current;
    if (aop != 0 && aop != 1) {
      alphaResult = applyAlphaOp(spv, aop, a1, a2, current, diffuse, stage, V4, F, texSample);
    }

    // Combine: RGB from color result, A from alpha result
    uint32_t r = spv.compositeExtract(F, colorResult, 0);
    uint32_t g = spv.compositeExtract(F, colorResult, 1);
    uint32_t b = spv.compositeExtract(F, colorResult, 2);
    uint32_t a = spv.compositeExtract(F, alphaResult, 3);
    current = spv.compositeConstruct(V4, {r, g, b, a});

    // RESULTARG handling
    if (config.resultArg[stage] == 0) {
      diffuse = current; // Write back to DIFFUSE
    }
  }

  // Store output
  spv.store(outColor, current);

  spv.ret();
  spv.functionEnd();

  // --- EntryPoint (must be in section 4 of SPIR-V, after MemoryModel) ---
  std::vector<uint32_t> interfaces = { pcVar, inColor, inTexCoord, outColor };
  for (uint32_t i = 0; i < activeTex; i++) interfaces.push_back(samplerVars[i]);
  spv.entryPoint(4 /* Fragment */, mainId, "main", interfaces);
  spv.executionMode(mainId, 7); // OriginUpperLeft

  // --- Finalize ---
  auto result = spv.finalize();

  VKWIND_INFO(kTag, "FF shader generated: %zu words, bound=%u", result.size(), spv.nextId);
  return result;
}

} // namespace vkwind
