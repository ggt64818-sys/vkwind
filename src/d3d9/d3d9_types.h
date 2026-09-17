#pragma once

// Minimal D3D9 type definitions for the translation layer
// These replace the Windows SDK d3d9.h headers

#include <cstdint>
#include <cstring>

// Basic types (must be before any enum that uses BOOL)
using BOOL = int;
#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

// MAKEFOURCC (must be before D3DFORMAT enum which uses it)
#ifndef MAKEFOURCC
#define MAKEFOURCC(ch0, ch1, ch2, ch3) \
  ((uint32_t)(uint8_t)(ch0) | ((uint32_t)(uint8_t)(ch1) << 8) | \
   ((uint32_t)(uint8_t)(ch2) << 16) | ((uint32_t)(uint8_t)(ch3) << 24))
#endif

// Forward declarations of all interfaces
struct IDirect3D9;
struct IDirect3D9Ex;
struct IDirect3DDevice9;
struct IDirect3DDevice9Ex;
struct IDirect3DResource9;
struct IDirect3DBaseTexture9;
struct IDirect3DTexture9;
struct IDirect3DVolumeTexture9;
struct IDirect3DCubeTexture9;
struct IDirect3DSurface9;
struct IDirect3DVolume9;
struct IDirect3DVertexBuffer9;
struct IDirect3DIndexBuffer9;
struct IDirect3DShader9;
struct IDirect3DPixelShader9;
struct IDirect3DVertexShader9;
struct IDirect3DSwapChain9;
struct IDirect3DQuery9;
struct IDirect3DStateBlock9;

// D3DRESULT — values 0x8876xxxx exceed INT32_MAX, use uint32_t underlying type
enum D3DRESULT : uint32_t {
  D3D_OK = 0,
  D3DERR_WRONGTEXTUREFORMAT = 0x88760818,
  D3DERR_DEVICELOST = 0x88760868,
  D3DERR_DEVICENOTRESET = 0x88760869,
  D3DERR_NOTFOUND = 0x88760866,
  D3DERR_MOREDATA = 0x88760867,
  D3DERR_DEVICEHUNG = 0x88760822,
  D3DERR_UNSUPPORTEDOVERLAY = 0x88760829,
  D3DERR_NOTPALETTIZED = 0x88760830,
  D3DERR_DRIVERINTERNALERROR = 0x88760827,
  D3DERR_INVALIDCALL = 0x8876086C,
  D3DERR_DRIVERINVALIDCALL = 0x88760870,
  D3DERR_NOTAVAILABLE = 0x8876086A,
  D3DERR_OUTOFVIDEOMEMORY = 0x8876017C,
  D3DERR_INVALIDDEVICE = 0x8876086B,
  D3DERR_INVALIDOPERATION = 0x8876086D,
};

#undef FAILED
#undef SUCCEEDED
#define FAILED(hr)  (static_cast<int32_t>(hr) < 0)
#define SUCCEEDED(hr) (static_cast<int32_t>(hr) >= 0)

enum D3DRESOURCETYPE {
  D3DRTYPE_SURFACE = 1,
  D3DRTYPE_VOLUME = 2,
  D3DRTYPE_TEXTURE = 3,
  D3DRTYPE_VOLUMETEXTURE = 4,
  D3DRTYPE_CUBETEXTURE = 5,
  D3DRTYPE_VERTEXBUFFER = 6,
  D3DRTYPE_INDEXBUFFER = 7,
};

enum D3DPOOL {
  D3DPOOL_DEFAULT = 0,
  D3DPOOL_MANAGED = 1,
  D3DPOOL_SYSTEMMEM = 2,
  D3DPOOL_SCRATCH = 3,
  D3DPOOL_FORCE_DWORD = 0x7fffffff,
};

enum D3DUSAGE {
  D3DUSAGE_RENDERTARGET = 0x00000001,
  D3DUSAGE_DEPTHSTENCIL = 0x00000002,
  D3DUSAGE_WRITEONLY = 0x00000008,
  D3DUSAGE_SOFTWAREPROCESSING = 0x00000010,
  D3DUSAGE_DONOTCLIP = 0x00000020,
  D3DUSAGE_POINTS = 0x00000040,
  D3DUSAGE_RTPATCHES = 0x00000080,
  D3DUSAGE_NPATCHES = 0x00000100,
  D3DUSAGE_DYNAMIC = 0x00000200,
  D3DUSAGE_AUTOGENMIPMAP = 0x00000400,
  D3DUSAGE_DMAP = 0x00000800,
};

enum D3DFORMAT {
  D3DFMT_UNKNOWN = 0,
  D3DFMT_R8G8B8 = 20,
  D3DFMT_A8R8G8B8 = 21,
  D3DFMT_X8R8G8B8 = 22,
  D3DFMT_R5G6B5 = 23,
  D3DFMT_X1R5G5B5 = 24,
  D3DFMT_A1R5G5B5 = 25,
  D3DFMT_A4R4G4B4 = 26,
  D3DFMT_R3G3B2 = 27,
  D3DFMT_A8 = 28,
  D3DFMT_A8R3G3B2 = 29,
  D3DFMT_X4R4G4B4 = 30,
  D3DFMT_A2B10G10R10 = 31,
  D3DFMT_G16R16 = 34,
  D3DFMT_A8B8G8R8 = 32,
  D3DFMT_X8B8G8R8 = 33,
  D3DFMT_A2R10G10B10 = 35,
  D3DFMT_Q16W16V16U16 = 110,
  D3DFMT_V8U8 = 60,
  D3DFMT_Q8W8V8U8 = 63,
  D3DFMT_X8L8V8U8 = 62,
  D3DFMT_V16U16 = 64,
  D3DFMT_W11V11U10 = 65,
  D3DFMT_A2W10V10U10 = 66,

  // Float formats
  D3DFMT_R16F = 111,
  D3DFMT_G16R16F = 112,
  D3DFMT_A16B16G16R16F = 113,
  D3DFMT_R32F = 114,
  D3DFMT_G32R32F = 115,
  D3DFMT_A32B32G32R32F = 116,

  // Depth formats
  D3DFMT_D16_LOCKABLE = 70,
  D3DFMT_D32 = 71,
  D3DFMT_D15S1 = 73,
  D3DFMT_D24S8 = 75,
  D3DFMT_D16 = 80,
  D3DFMT_D24X8 = 77,
  D3DFMT_D24X4S4 = 79,

  // Compressed
  D3DFMT_DXT1 = MAKEFOURCC('D','X','T','1'),
  D3DFMT_DXT2 = MAKEFOURCC('D','X','T','2'),
  D3DFMT_DXT3 = MAKEFOURCC('D','X','T','3'),
  D3DFMT_DXT4 = MAKEFOURCC('D','X','T','4'),
  D3DFMT_DXT5 = MAKEFOURCC('D','X','T','5'),

  // Vertex formats
  D3DFMT_VERTEXDATA = 100,
  D3DFMT_INDEX16 = 101,
  D3DFMT_INDEX32 = 102,
};

enum D3DMULTISAMPLE_TYPE {
  D3DMULTISAMPLE_NONE = 0,
  D3DMULTISAMPLE_2_SAMPLES = 2,
  D3DMULTISAMPLE_3_SAMPLES = 3,
  D3DMULTISAMPLE_4_SAMPLES = 4,
  D3DMULTISAMPLE_5_SAMPLES = 5,
  D3DMULTISAMPLE_6_SAMPLES = 6,
  D3DMULTISAMPLE_7_SAMPLES = 7,
  D3DMULTISAMPLE_8_SAMPLES = 8,
  D3DMULTISAMPLE_9_SAMPLES = 9,
  D3DMULTISAMPLE_10_SAMPLES = 10,
  D3DMULTISAMPLE_11_SAMPLES = 11,
  D3DMULTISAMPLE_12_SAMPLES = 12,
  D3DMULTISAMPLE_13_SAMPLES = 13,
  D3DMULTISAMPLE_14_SAMPLES = 14,
  D3DMULTISAMPLE_15_SAMPLES = 15,
  D3DMULTISAMPLE_16_SAMPLES = 16,
  D3DMULTISAMPLE_FORCE_DWORD = 0x7fffffff,
};

enum D3DDEVTYPE {
  D3DDEVTYPE_HAL = 1,
  D3DDEVTYPE_REF = 2,
  D3DDEVTYPE_SW = 3,
  D3DDEVTYPE_NULLREF = 4,
};

enum D3DBACKBUFFER_TYPE {
  D3DBACKBUFFER_TYPE_MONO = 0,
  D3DBACKBUFFER_TYPE_LEFT = 1,
  D3DBACKBUFFER_TYPE_RIGHT = 2,
};

enum D3DPRIMITIVETYPE {
  D3DPT_POINTLIST = 1,
  D3DPT_LINELIST = 2,
  D3DPT_LINESTRIP = 3,
  D3DPT_TRIANGLELIST = 4,
  D3DPT_TRIANGLESTRIP = 5,
  D3DPT_TRIANGLEFAN = 6,
  D3DPT_FORCE_DWORD = 0x7fffffff,
};

enum D3DTRANSFORMSTATETYPE {
  D3DTS_VIEW = 2,
  D3DTS_PROJECTION = 3,
  D3DTS_TEXTURE0 = 16,
  D3DTS_TEXTURE1 = 17,
  D3DTS_TEXTURE2 = 18,
  D3DTS_TEXTURE3 = 19,
  D3DTS_TEXTURE4 = 20,
  D3DTS_TEXTURE5 = 21,
  D3DTS_TEXTURE6 = 22,
  D3DTS_TEXTURE7 = 23,
  D3DTS_WORLD = 256,
  D3DTS_WORLD1 = 257,
  D3DTS_WORLD2 = 258,
  D3DTS_WORLD3 = 259,
};

#define D3DTS_WORLDMATRIX(index) ((D3DTRANSFORMSTATETYPE)(256 + (index)))

enum D3DRENDERSTATETYPE {
  D3DRS_ZENABLE = 7,
  D3DRS_FILLMODE = 8,
  D3DRS_SHADEMODE = 9,
  D3DRS_LINEPATTERN = 10,
  D3DRS_ZWRITEENABLE = 14,
  D3DRS_ALPHATESTENABLE = 15,
  D3DRS_LASTPIXEL = 16,
  D3DRS_SRCBLEND = 19,
  D3DRS_DESTBLEND = 20,
  D3DRS_CULLMODE = 22,
  D3DRS_ZFUNC = 23,
  D3DRS_ALPHAREF = 24,
  D3DRS_ALPHAFUNC = 25,
  D3DRS_DITHERENABLE = 26,
  D3DRS_ALPHABLENDENABLE = 27,
  D3DRS_FOGENABLE = 28,
  D3DRS_SPECULARENABLE = 29,
  D3DRS_ZVISIBLE = 30,
  D3DRS_FOGCOLOR = 34,
  D3DRS_FOGTABLEMODE = 35,
  D3DRS_FOGSTART = 36,
  D3DRS_FOGEND = 37,
  D3DRS_FOGDENSITY = 38,
  D3DRS_EDGEANTIALIAS = 40,
  D3DRS_ZBIAS = 47,
  D3DRS_RANGEFOGENABLE = 48,
  D3DRS_STENCILENABLE = 52,
  D3DRS_STENCILFAIL = 53,
  D3DRS_STENCILZFAIL = 54,
  D3DRS_STENCILPASS = 55,
  D3DRS_STENCILFUNC = 56,
  D3DRS_STENCILREF = 57,
  D3DRS_STENCILMASK = 58,
  D3DRS_STENCILWRITEMASK = 59,
  D3DRS_TEXTUREFACTOR = 160,
  D3DRS_TWOSIDEDSTENCILMODE = 185,
  D3DRS_CCW_STENCILFAIL = 186,
  D3DRS_CCW_STENCILZFAIL = 187,
  D3DRS_CCW_STENCILPASS = 188,
  D3DRS_CCW_STENCILFUNC = 189,
  D3DRS_BLENDFACTOR = 193,
  D3DRS_WRAP0 = 128,
  D3DRS_WRAP1 = 129,
  D3DRS_WRAP2 = 130,
  D3DRS_WRAP3 = 131,
  D3DRS_WRAP4 = 132,
  D3DRS_WRAP5 = 133,
  D3DRS_WRAP6 = 134,
  D3DRS_WRAP7 = 135,
  D3DRS_LIGHTING = 137,
  D3DRS_AMBIENT = 139,
  D3DRS_FOGVERTEXMODE = 140,
  D3DRS_COLORVERTEX = 141,
  D3DRS_LOCALVIEWER = 142,
  D3DRS_NORMALIZENORMALS = 143,
  D3DRS_DIFFUSEMATERIALSOURCE = 145,
  D3DRS_SPECULARMATERIALSOURCE = 146,
  D3DRS_AMBIENTMATERIALSOURCE = 147,
  D3DRS_EMISSIVEMATERIALSOURCE = 148,
  D3DRS_VERTEXBLEND = 151,
  D3DRS_CLIPPLANEENABLE = 152,
  D3DRS_POINTSIZE_MIN = 154,
  D3DRS_POINTSPRITEENABLE = 156,
  D3DRS_POINTSCALEENABLE = 157,
  D3DRS_POINTSCALE_A = 158,
  D3DRS_POINTSCALE_B = 159,
  D3DRS_POINTSCALE_C = 160,
  D3DRS_MULTISAMPLEANTIALIAS = 161,
  D3DRS_MULTISAMPLEMASK = 162,
  D3DRS_PATCHEDGESTYLE = 163,
  D3DRS_PATCHSEGMENTS = 164,
  D3DRS_DEBUGMONITORTOKEN = 165,
  D3DRS_POINTSIZE_MAX = 166,
  D3DRS_INDEXEDVERTEXBLENDENABLE = 167,
  D3DRS_COLORWRITEENABLE = 168,
  D3DRS_TWEENFACTOR = 170,
  D3DRS_BLENDOP = 171,
  D3DRS_POSITIONORDER = 172,
  D3DRS_NORMALORDER = 173,
  D3DRS_SCISSORTESTENABLE = 174,
  D3DRS_SLOPESCALEDEPTHBIAS = 175,
  D3DRS_ANTIALIASEDLINEENABLE = 176,
  D3DRS_MINTESSELATIONLEVEL = 178,
  D3DRS_MAXTESSELATIONLEVEL = 179,
  D3DRS_ADAPTIVETESS_X = 180,
  D3DRS_ADAPTIVETESS_Y = 181,
  D3DRS_ADAPTIVETESS_Z = 182,
  D3DRS_ADAPTIVETESS_W = 183,
  D3DRS_ENABLEADAPTIVETESSELLATION = 184,
  D3DRS_DEPTHBIAS = 195,
  D3DRS_WRAP8 = 198,
  D3DRS_WRAP9 = 199,
  D3DRS_WRAP10 = 200,
  D3DRS_WRAP11 = 201,
  D3DRS_WRAP12 = 202,
  D3DRS_WRAP13 = 203,
  D3DRS_WRAP14 = 204,
  D3DRS_WRAP15 = 205,
  D3DRS_SEPARATEALPHABLENDENABLE = 206,
  D3DRS_SRCBLENDALPHA = 207,
  D3DRS_DESTBLENDALPHA = 208,
  D3DRS_BLENDOPALPHA = 209,
};

enum D3DTEXTUREFILTERTYPE {
  D3DTEXF_NONE = 0,
  D3DTEXF_POINT = 1,
  D3DTEXF_LINEAR = 2,
  D3DTEXF_ANISOTROPIC = 3,
  D3DTEXF_PYRAMIDALQUAD = 6,
  D3DTEXF_GAUSSIANQUAD = 7,
};

enum D3DTEXTUREADDRESS {
  D3DTADDRESS_WRAP = 1,
  D3DTADDRESS_MIRROR = 2,
  D3DTADDRESS_CLAMP = 3,
  D3DTADDRESS_BORDER = 4,
  D3DTADDRESS_MIRRORONCE = 5,
};

enum D3DSAMPLERSTATETYPE {
  D3DSAMP_ADDRESSU = 1,
  D3DSAMP_ADDRESSV = 2,
  D3DSAMP_ADDRESSW = 3,
  D3DSAMP_BORDERCOLOR = 4,
  D3DSAMP_MAGFILTER = 5,
  D3DSAMP_MINFILTER = 6,
  D3DSAMP_MIPFILTER = 7,
  D3DSAMP_MIPMAPLODBIAS = 8,
  D3DSAMP_MAXMIPLEVEL = 9,
  D3DSAMP_MAXANISOTROPY = 10,
  D3DSAMP_SRGBTEXTURE = 11,
  D3DSAMP_ELEMENTINDEX = 12,
  D3DSAMP_DMAPOFFSET = 13,
};

enum D3DCMPFUNC {
  D3DCMP_NEVER = 1,
  D3DCMP_LESS = 2,
  D3DCMP_EQUAL = 3,
  D3DCMP_LESSEQUAL = 4,
  D3DCMP_GREATER = 5,
  D3DCMP_NOTEQUAL = 6,
  D3DCMP_GREATEREQUAL = 7,
  D3DCMP_ALWAYS = 8,
};

enum D3DBLEND {
  D3DBLEND_ZERO = 1,
  D3DBLEND_ONE = 2,
  D3DBLEND_SRCCOLOR = 3,
  D3DBLEND_INVSRCCOLOR = 4,
  D3DBLEND_SRCALPHA = 5,
  D3DBLEND_INVSRCALPHA = 6,
  D3DBLEND_DESTALPHA = 7,
  D3DBLEND_INVDESTALPHA = 8,
  D3DBLEND_DESTCOLOR = 9,
  D3DBLEND_INVDESTCOLOR = 10,
  D3DBLEND_SRCALPHASAT = 11,
  D3DBLEND_BOTHSRCALPHA = 12,
  D3DBLEND_BOTHINVSRCALPHA = 13,
  D3DBLEND_BLENDFACTOR = 14,
  D3DBLEND_INVBLENDFACTOR = 15,
  D3DBLEND_SRCCOLOR2 = 16,
  D3DBLEND_INVSRCCOLOR2 = 17,
};

enum D3DSTENCILOP {
  D3DSTENCILOP_KEEP = 1,
  D3DSTENCILOP_ZERO = 2,
  D3DSTENCILOP_REPLACE = 3,
  D3DSTENCILOP_INCRSAT = 4,
  D3DSTENCILOP_DECRSAT = 5,
  D3DSTENCILOP_INVERT = 6,
  D3DSTENCILOP_INCR = 7,
  D3DSTENCILOP_DECR = 8,
};

enum D3DCULL {
  D3DCULL_NONE = 1,
  D3DCULL_CW = 2,
  D3DCULL_CCW = 3,
};

enum D3DFILLMODE {
  D3DFILL_POINT = 1,
  D3DFILL_WIREFRAME = 2,
  D3DFILL_SOLID = 3,
};

enum D3DSHADEMODE {
  D3DSHADE_FLAT = 1,
  D3DSHADE_GOURAUD = 2,
  D3DSHADE_PHONG = 3,
};

enum D3DFOGMODE {
  D3DFOG_NONE = 0,
  D3DFOG_EXP = 1,
  D3DFOG_EXP2 = 2,
  D3DFOG_LINEAR = 3,
};

enum D3DQUERYTYPE {
  D3DQUERYTYPE_VCACHE = 4,
  D3DQUERYTYPE_RESOURCEMANAGER = 5,
  D3DQUERYTYPE_VERTEXSTATS = 6,
  D3DQUERYTYPE_EVENT = 8,
  D3DQUERYTYPE_OCCLUSION = 9,
};

// --- Enums used by structures ---

enum D3DSWAPEFFECT {
  D3DSWAPEFFECT_DISCARD   = 1,
  D3DSWAPEFFECT_FLIP      = 2,
  D3DSWAPEFFECT_COPY      = 3,
  D3DSWAPEFFECT_OVERLAY   = 4,
  D3DSWAPEFFECT_FLIPEX    = 5,
};

// GUID: defined by Windows SDK (guiddef.h) when windows.h is included.
// For files that haven't included windows.h yet, we need a definition.
#if defined(__MINGW32__) || defined(_MSC_VER)
  // Windows build — GUID comes from Windows SDK headers
  #include <guiddef.h>
#else
  // Non-Windows build
  struct GUID {
    uint32_t Data1;
    uint16_t Data2;
    uint16_t Data3;
    uint8_t  Data4[8];
  };
#endif

// --- Structures ---

struct D3DPRESENT_PARAMETERS {
  uint32_t BackBufferWidth;
  uint32_t BackBufferHeight;
  D3DFORMAT BackBufferFormat;
  uint32_t BackBufferCount;
  D3DMULTISAMPLE_TYPE MultiSampleType;
  uint32_t MultiSampleQuality;
  D3DSWAPEFFECT SwapEffect;
  void* hDeviceWindow;
  bool Windowed;
  bool EnableAutoDepthStencil;
  D3DFORMAT AutoDepthStencilFormat;
  uint32_t Flags;
  uint32_t FullScreen_RefreshRateInHz;
  uint32_t PresentationInterval;
  bool ForceDeviceFullscreenEx;
};

struct D3DDISPLAYMODE {
  uint32_t Width;
  uint32_t Height;
  uint32_t RefreshRate;
  D3DFORMAT Format;
};

struct D3DCAPS9 {
  uint32_t MaxTextureWidth;
  uint32_t MaxTextureHeight;
  uint32_t MaxVolumeExtent;
  uint32_t MaxTextureRepeat;
  uint32_t MaxTextureAspectRatio;
  uint32_t MaxAnisotropy;
  float MaxVertexW;
  float GuardBandLeft;
  float GuardBandTop;
  float GuardBandRight;
  float GuardBandBottom;
  float ExtentsAdjust;
  uint32_t StencilCaps;
  uint32_t FVFCaps;
  uint32_t TextureOpCaps;
  uint32_t MaxTextureBlendStages;
  uint32_t MaxSimultaneousTextures;
  uint32_t VertexProcessingCaps;
  uint32_t MaxActiveLights;
  uint32_t MaxUserClipPlanes;
  uint32_t MaxVertexBlendMatrices;
  uint32_t MaxVertexBlendMatrixIndex;
  float MaxPointSize;
  uint32_t MaxPrimitiveCount;
  uint32_t MaxVertexIndex;
  uint32_t MaxStreams;
  uint32_t MaxVertexStride;
  uint32_t VertexShaderVersion;
  uint32_t MaxVertexShaderConst;
  uint32_t PixelShaderVersion;
  float MaxPixelShaderValue;
};

struct D3DLOCKED_RECT {
  int Pitch;
  void* pBits;
};

struct D3DVERTEXBUFFER_DESC {
  D3DFORMAT Format;
  D3DRESOURCETYPE Type;
  uint32_t Usage;
  D3DPOOL Pool;
  uint32_t Size;
  uint32_t FVF;
};

struct D3DINDEXBUFFER_DESC {
  D3DFORMAT Format;
  D3DRESOURCETYPE Type;
  uint32_t Usage;
  D3DPOOL Pool;
  uint32_t Size;
};

struct D3DSURFACE_DESC {
  D3DFORMAT Format;
  D3DRESOURCETYPE Type;
  uint32_t Usage;
  D3DPOOL Pool;
  D3DMULTISAMPLE_TYPE MultiSampleType;
  uint32_t Width;
  uint32_t Height;
};

struct D3DVIEWPORT9 {
  uint32_t X;
  uint32_t Y;
  uint32_t Width;
  uint32_t Height;
  float MinZ;
  float MaxZ;
};

struct D3DMATERIAL9 {
  float Diffuse[4];
  float Ambient[4];
  float Specular[4];
  float Emissive[4];
  float Power;
};

struct D3DLIGHT9 {
  uint32_t Type;
  float Diffuse[4];
  float Specular[4];
  float Ambient[4];
  float Position[3];
  float Direction[3];
  float Range;
  float Falloff;
  float Attenuation0;
  float Attenuation1;
  float Attenuation2;
  float Theta;
  float Phi;
};

struct D3DCLIPPLANE9 {
  float Plane[4];
  uint32_t Index;
};

struct D3DRASTER_STATUS {
  BOOL InVBlank;
  uint32_t ScanLine;
};

// Suppress pedantic warning for anonymous struct in D3DMATRIX
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
struct D3DMATRIX {
  union {
    struct {
      float _11, _12, _13, _14;
      float _21, _22, _23, _24;
      float _31, _32, _33, _34;
      float _41, _42, _43, _44;
    };
    float m[4][4];
  };
};
#pragma GCC diagnostic pop



struct D3DLOCKED_BOX {
  int RowPitch;
  int SlicePitch;
  void* pBits;
};

struct D3DBOX {
  uint32_t Left;
  uint32_t Top;
  uint32_t Right;
  uint32_t Bottom;
  uint32_t Front;
  uint32_t Back;
};

struct D3DVERTEXELEMENT9 {
  uint16_t Stream;
  uint16_t Offset;
  uint8_t Type;
  uint8_t Method;
  uint8_t Usage;
  uint8_t UsageIndex;
};

// --- Constants ---

#define D3DFVF_RESERVED0        0x001
#define D3DFVF_POSITION_MASK    0x00E
#define D3DFVF_XYZ              0x002
#define D3DFVF_XYZRHW           0x004
#define D3DFVF_XYZB1            0x006
#define D3DFVF_XYZB2            0x008
#define D3DFVF_XYZB3            0x00a
#define D3DFVF_XYZB4            0x00c
#define D3DFVF_XYZB5            0x00e
#define D3DFVF_NORMAL           0x010
#define D3DFVF_PSIZE            0x020
#define D3DFVF_DIFFUSE          0x040
#define D3DFVF_SPECULAR         0x080
#define D3DFVF_TEXCOUNT_MASK    0xf00
#define D3DFVF_TEXCOUNT_SHIFT   8
#define D3DFVF_TEX0             0x000
#define D3DFVF_TEX1             0x100
#define D3DFVF_TEX2             0x200
#define D3DFVF_TEX3             0x300
#define D3DFVF_TEX4             0x400
#define D3DFVF_TEX5             0x500
#define D3DFVF_TEX6             0x600
#define D3DFVF_TEX7             0x700
#define D3DFVF_TEX8             0x800

#define D3DLOCK_READONLY         0x0010
#define D3DLOCK_DISCARD          0x2000
#define D3DLOCK_NOOVERWRITE      0x1000
#define D3DLOCK_NOSYSLOCK        0x0800
#define D3DLOCK_DONOTWAIT        0x4000
#define D3DLOCK_NO_DIRTY_UPDATE  0x8000

#define D3DADAPTER_DEFAULT      0
#define D3D_SDK_VERSION         32

// D3D9 Declaration types (D3DDECLTYPE)
#define D3DDECLTYPE_FLOAT1      0
#define D3DDECLTYPE_FLOAT2      1
#define D3DDECLTYPE_FLOAT3      2
#define D3DDECLTYPE_FLOAT4      3
#define D3DDECLTYPE_D3DCOLOR    4
#define D3DDECLTYPE_UBYTE4      5
#define D3DDECLTYPE_SHORT2      6
#define D3DDECLTYPE_SHORT4      7
#define D3DDECLTYPE_UBYTE4N     8
#define D3DDECLTYPE_SHORT2N     9
#define D3DDECLTYPE_SHORT4N    10
#define D3DDECLTYPE_USHORT2N   11
#define D3DDECLTYPE_USHORT4N   12
#define D3DDECLTYPE_UDEC3      13
#define D3DDECLTYPE_DEC3N      14
#define D3DDECLTYPE_FLOAT16_2  15
#define D3DDECLTYPE_FLOAT16_4  16
#define D3DDECLTYPE_UNUSED      17

// D3D9 Declaration usage (D3DDECLUSAGE)
#define D3DDECLUSAGE_POSITION   0
#define D3DDECLUSAGE_BLENDWEIGHT 1
#define D3DDECLUSAGE_BLENDINDICES 2
#define D3DDECLUSAGE_NORMAL     3
#define D3DDECLUSAGE_PSIZE      4
#define D3DDECLUSAGE_COLOR      5
#define D3DDECLUSAGE_TEXCOORD   6
#define D3DDECLUSAGE_TANGENT     7
#define D3DDECLUSAGE_BINORMAL    8
#define D3DDECLUSAGE_TESSFACTOR  9
#define D3DDECLUSAGE_POSITIONT  10
#define D3DDECLUSAGE_SAMPLE     11

// D3D9 declaration end marker
#define D3DDECL_END() { 0xFF, 0, D3DDECLTYPE_UNUSED, 0, 0, 0 }
#define D3DCLEAR_TARGET         0x00000001
#define D3DCLEAR_ZBUFFER        0x00000002
#define D3DCLEAR_STENCIL        0x00000004
#define D3DCOLOR_ARGB(a,r,g,b)  ((uint32_t)((((a)&0xff)<<24)|(((r)&0xff)<<16)|(((g)&0xff)<<8)|((b)&0xff)))
#define D3DCOLOR_XRGB(r,g,b)   D3DCOLOR_ARGB(0xff,r,g,b)
#define D3DCREATE_HARDWARE_VERTEXPROCESSING 0x00000040
#define D3DCREATE_SOFTWARE_VERTEXPROCESSING 0x00000020
#define D3DCREATE_MIXED_VERTEXPROCESSING    0x00000080
#define D3DCREATE_FPU_PRESERVE  0x00000002
#define D3DCREATE_MULTITHREADED 0x00000004
#define D3DPRESENT_INTERVAL_DEFAULT   0x00000000
#define D3DPRESENT_INTERVAL_ONE       0x00000001
#define D3DPRESENT_INTERVAL_TWO       0x00000002
#define D3DPRESENT_INTERVAL_THREE     0x00000004
#define D3DPRESENT_INTERVAL_FOUR      0x00000008
#define D3DPRESENT_INTERVAL_IMMEDIATE 0x80000000

// Texture stage state types (D3D9 D3DTEXTURESTAGESTATETYPE)
#define D3DTSS_COLOROP        1
#define D3DTSS_COLORARG1      2
#define D3DTSS_COLORARG2      3
#define D3DTSS_ALPHAOP        4
#define D3DTSS_ALPHAARG1      5
#define D3DTSS_ALPHAARG2      6
#define D3DTSS_BUMPENVMAT00   7
#define D3DTSS_BUMPENVMAT01   8
#define D3DTSS_BUMPENVMAT10   9
#define D3DTSS_BUMPENVMAT11  10
#define D3DTSS_TEXCOORDINDEX 11
#define D3DTSS_ADDRESSU      13
#define D3DTSS_ADDRESSV      14
#define D3DTSS_BORDERCOLOR   15
#define D3DTSS_MAGFILTER     16
#define D3DTSS_MINFILTER     17
#define D3DTSS_MIPFILTER     18
#define D3DTSS_MIPMAPLODBIAS 19
#define D3DTSS_MAXMIPLEVEL   20
#define D3DTSS_MAXANISOTROPY 21
#define D3DTSS_BUMPENVLSCALE 22
#define D3DTSS_BUMPENVLOFFSET 23
#define D3DTSS_TEXTURETRANSFORMFLAGS 24

// Texture argument flags
#define D3DTA_SELECTMASK    0x0000000f
#define D3DTA_DIFFUSE       0x00000000
#define D3DTA_CURRENT       0x00000001
#define D3DTA_TEXTURE       0x00000002
#define D3DTA_TFACTOR       0x00000003
#define D3DTA_SPECULAR      0x00000004
#define D3DTA_TEMP          0x00000005
#define D3DTA_COMPLEMENT    0x00000010
#define D3DTA_ALPHAREPLICATE 0x00000020

// Texture operation types
#define D3DTOP_DISABLE       1
#define D3DTOP_SELECTARG1    2
#define D3DTOP_SELECTARG2    3
#define D3DTOP_MODULATE      4
#define D3DTOP_MODULATE2X    5
#define D3DTOP_MODULATE4X    6
#define D3DTOP_ADD           7
#define D3DTOP_ADDSIGNED     8
#define D3DTOP_ADDSIGNED2X   9
#define D3DTOP_SUBTRACT     10
#define D3DTOP_ADDSMOOTH    11
#define D3DTOP_BLENDDIFFUSEALPHA 12
#define D3DTOP_BLENDTEXTUREALPHA 13
#define D3DTOP_BLENDFACTORALPHA  14
#define D3DTOP_BLENDTEXTUREALPHAPM 15
#define D3DTOP_BLENDCURRENTALPHA 16
#define D3DTOP_PREMODULATE   17
#define D3DTOP_MODULATEALPHA_ADDCOLOR 18
#define D3DTOP_MODULATECOLOR_ADDALPHA 19
#define D3DTOP_MODULATEINVALPHA_ADDCOLOR 20
#define D3DTOP_MODULATEINVCOLOR_ADDALPHA 21
#define D3DTOP_BUMPENVMAP    22
#define D3DTOP_BUMPENVMAPLUMINANCE 23
#define D3DTOP_DOTPRODUCT3   24
#define D3DTOP_MULTIPLYADD   25
#define D3DTOP_LERP          26

struct D3DADAPTER_IDENTIFIER9 {
  char     Driver[512];
  char     Description[512];
  uint32_t DriverVersion[2];
  uint32_t VendorId;
  uint32_t DeviceId;
  uint32_t SubSysId;
  uint32_t Revision;
  GUID     DeviceIdentifier;
  uint32_t WHQLLevel;
};

// --- Interface Hierarchy ---

// IUnknown (base for all COM-like interfaces)
// Guard against Windows COM headers which define their own IUnknown
#ifndef __IUnknown_INTERFACE_DEFINED__
#define __IUnknown_INTERFACE_DEFINED__
struct IUnknown {
  virtual ~IUnknown() = default;
  virtual uint32_t AddRef() = 0;
  virtual uint32_t Release() = 0;
  virtual int QueryInterface(const void* iid, void** obj) = 0;
};
#endif

// IDirect3DResource9 (base for all GPU resources)
struct IDirect3DResource9 : IUnknown {
  virtual ~IDirect3DResource9() = default;
  virtual void GetDevice(IDirect3DDevice9** ppDevice) = 0;
  virtual void SetPrivateData(uint32_t refguid, const void* pData, uint32_t SizeOfData, uint32_t Flags) = 0;
  virtual void GetPrivateData(uint32_t refguid, void* pData, uint32_t* pSizeOfData) = 0;
  virtual void FreePrivateData(uint32_t refguid) = 0;
  virtual uint32_t SetPriority(uint32_t PriorityNew) = 0;
  virtual uint32_t GetPriority() = 0;
  virtual void PreLoad() = 0;
  virtual D3DRESOURCETYPE GetType() = 0;
};

// IDirect3DBaseTexture9
struct IDirect3DBaseTexture9 : IDirect3DResource9 {
  virtual ~IDirect3DBaseTexture9() = default;
  virtual uint32_t SetLOD(uint32_t LODNew) = 0;
  virtual uint32_t GetLOD() = 0;
  virtual uint32_t GetLevelCount() = 0;
};

// IDirect3DTexture9
struct IDirect3DTexture9 : IDirect3DBaseTexture9 {
  virtual ~IDirect3DTexture9() = default;
  virtual uint32_t GetLevelDesc(uint32_t Level, void* pDesc) = 0;
  virtual uint32_t GetSurfaceLevel(uint32_t Level, IDirect3DSurface9** ppSurfaceLevel) = 0;
  virtual uint32_t LockRect(uint32_t Level, void* pLockedRect, const void* pRect, uint32_t Flags) = 0;
  virtual uint32_t UnlockRect(uint32_t Level) = 0;
  virtual uint32_t AddDirtyRect(const void* pDirtyRect) = 0;
};

// IDirect3DVolumeTexture9
struct IDirect3DVolumeTexture9 : IDirect3DBaseTexture9 {
  virtual ~IDirect3DVolumeTexture9() = default;
};

// IDirect3DCubeTexture9
struct IDirect3DCubeTexture9 : IDirect3DBaseTexture9 {
  virtual ~IDirect3DCubeTexture9() = default;
};

// IDirect3DSurface9
struct IDirect3DSurface9 : IDirect3DResource9 {
  virtual ~IDirect3DSurface9() = default;
  virtual int GetContainer(const void* riid, void** ppContainer) = 0;
  virtual int GetDesc(void* pDesc) = 0;
  virtual int LockRect(void* pLockedRect, const void* pRect, uint32_t Flags) = 0;
  virtual int UnlockRect() = 0;
};

// IDirect3DVolume9
struct IDirect3DVolume9 : IDirect3DResource9 {
  virtual ~IDirect3DVolume9() = default;
};

// IDirect3DVertexBuffer9
struct IDirect3DVertexBuffer9 : IDirect3DResource9 {
  virtual ~IDirect3DVertexBuffer9() = default;
  virtual int Lock(uint32_t OffsetToLock, uint32_t SizeToLock, void** ppbData, uint32_t Flags) = 0;
  virtual int Unlock() = 0;
  virtual int GetDesc(void* pDesc) = 0;
};

// IDirect3DIndexBuffer9
struct IDirect3DIndexBuffer9 : IDirect3DResource9 {
  virtual ~IDirect3DIndexBuffer9() = default;
  virtual int Lock(uint32_t OffsetToLock, uint32_t SizeToLock, void** ppbData, uint32_t Flags) = 0;
  virtual int Unlock() = 0;
  virtual int GetDesc(void* pDesc) = 0;
};

// IDirect3DPixelShader9
struct IDirect3DPixelShader9 : IDirect3DResource9 {
  virtual ~IDirect3DPixelShader9() = default;
  virtual int GetFunction(void* pFunction, uint32_t* pSizeOfData) = 0;
};

// IDirect3DVertexShader9
struct IDirect3DVertexShader9 : IDirect3DResource9 {
  virtual ~IDirect3DVertexShader9() = default;
  virtual int GetFunction(void* pFunction, uint32_t* pSizeOfData) = 0;
};

// IDirect3DSwapChain9
struct IDirect3DSwapChain9 : IUnknown {
  virtual ~IDirect3DSwapChain9() = default;
  virtual int Present(const void* pSourceRect, const void* pDestRect, void* hDestWindowOverride, void* pDirtyRegion, uint32_t Flags) = 0;
  virtual int GetFrontBuffer(IDirect3DSurface9* pDestSurface) = 0;
  virtual int GetBackBuffer(uint32_t BackBuffer, D3DBACKBUFFER_TYPE Type, IDirect3DSurface9** ppBackBuffer) = 0;
  virtual int GetRasterStatus(void* pRasterStatus) = 0;
  virtual int GetDisplayMode(void* pMode) = 0;
  virtual int GetDevice(IDirect3DDevice9** ppDevice) = 0;
  virtual int GetPresentParameters(void* pPresentationParameters) = 0;
};

// IDirect3DQuery9
struct IDirect3DQuery9 : IUnknown {
  virtual ~IDirect3DQuery9() = default;
  virtual void GetDevice(IDirect3DDevice9** ppDevice) = 0;
  virtual void SetPrivateData(uint32_t refguid, const void* pData, uint32_t SizeOfData, uint32_t Flags) = 0;
  virtual void GetPrivateData(uint32_t refguid, void* pData, uint32_t* pSizeOfData) = 0;
  virtual void FreePrivateData(uint32_t refguid) = 0;
  virtual int GetType() = 0;
  virtual uint32_t GetDataSize() = 0;
  virtual int Issue(uint32_t IssueFlags) = 0;
  virtual int GetData(void* pData, uint32_t SizeToFill, uint32_t* pGetDataResult) = 0;
};

// IDirect3DStateBlock9
struct IDirect3DStateBlock9 : IUnknown {
  virtual ~IDirect3DStateBlock9() = default;
};

// IDirect3D9
struct IDirect3D9 : IUnknown {
  virtual ~IDirect3D9() = default;
  virtual int RegisterSoftwareDevice(void* pInitializeFunction) = 0;
  virtual uint32_t GetAdapterCount() = 0;
  virtual int GetAdapterIdentifier(uint32_t Adapter, uint32_t Flags, void* pIdentifier) = 0;
  virtual uint32_t GetAdapterModeCount(uint32_t Adapter, D3DFORMAT Format) = 0;
  virtual int EnumAdapterModes(uint32_t Adapter, D3DFORMAT Format, uint32_t Mode, D3DDISPLAYMODE* pMode) = 0;
  virtual int GetAdapterDisplayMode(uint32_t Adapter, D3DDISPLAYMODE* pMode) = 0;
  virtual int CheckDeviceType(uint32_t Adapter, D3DDEVTYPE CheckType, D3DFORMAT DisplayFormat, D3DFORMAT BackBufferFormat, bool Windowed) = 0;
  virtual int CheckDeviceFormat(uint32_t Adapter, D3DDEVTYPE DeviceType, D3DFORMAT AdapterFormat, uint32_t Usage, D3DRESOURCETYPE RType, D3DFORMAT CheckFormat) = 0;
  virtual int CheckDeviceMultiSampleType(uint32_t Adapter, D3DDEVTYPE DeviceType, D3DFORMAT SurfaceFormat, bool Windowed, D3DMULTISAMPLE_TYPE MultiSampleType, uint32_t* pQualityLevels) = 0;
  virtual int CheckDepthStencilMatch(uint32_t Adapter, D3DDEVTYPE DeviceType, D3DFORMAT AdapterFormat, D3DFORMAT RenderTargetFormat, D3DFORMAT DepthStencilFormat) = 0;
  virtual int CheckDeviceFormatConversion(uint32_t Adapter, D3DDEVTYPE DeviceType, D3DFORMAT SourceFormat, D3DFORMAT TargetFormat) = 0;
  virtual int GetDeviceCaps(uint32_t Adapter, D3DDEVTYPE DeviceType, D3DCAPS9* pCaps) = 0;
  virtual int GetAdapterMonitor(uint32_t Adapter) = 0;
  virtual int CreateDevice(uint32_t Adapter, D3DDEVTYPE DeviceType, void* hFocusWindow, uint32_t BehaviorFlags, D3DPRESENT_PARAMETERS* pPresentationParameters, IDirect3DDevice9** ppReturnedDeviceInterface) = 0;
};

// IDirect3D9Ex
struct IDirect3D9Ex : IDirect3D9 {
  virtual ~IDirect3D9Ex() = default;
};

// IDirect3DDevice9
struct IDirect3DDevice9 : IUnknown {
  virtual ~IDirect3DDevice9() = default;

  // Device management
  virtual int TestCooperativeLevel() = 0;
  virtual int GetAvailablePoolMem(uint32_t Usage) = 0;
  virtual int EvictManagedResources() = 0;
  virtual int GetDirect3D(IDirect3D9** ppD3D9) = 0;
  virtual int GetDeviceCaps(D3DCAPS9* pCaps) = 0;
  virtual int GetDisplayMode(uint32_t iSwapChain, D3DDISPLAYMODE* pMode) = 0;
  virtual int GetCreationParameters(void* pParameters) = 0;
  virtual int SetCursorProperties(uint32_t XHotSpot, uint32_t YHotSpot, IDirect3DSurface9* pCursorBitmap) = 0;
  virtual void SetCursorPosition(int X, int Y, uint32_t Flags) = 0;
  virtual int ShowCursor(bool bShow) = 0;
  virtual int CreateAdditionalSwapChain(D3DPRESENT_PARAMETERS* pPresentationParameters, IDirect3DSwapChain9** pSwapChain) = 0;
  virtual int GetSwapChain(uint32_t iSwapChain, IDirect3DSwapChain9** pSwapChain) = 0;
  virtual uint32_t GetNumberOfSwapChains() = 0;
  virtual int Reset(D3DPRESENT_PARAMETERS* pPresentationParameters) = 0;
  virtual int Present(const void* pSourceRect, const void* pDestRect, void* hDestWindowOverride, const void* pDirtyRegion) = 0;
  virtual int GetBackBuffer(uint32_t iSwapChain, uint32_t iBackBuffer, uint32_t Type, IDirect3DSurface9** ppBackBuffer) = 0;
  virtual int GetRasterStatus(uint32_t iSwapChain, void* pRasterStatus) = 0;
  virtual int SetDialogBoxMode(bool bEnableDialogs) = 0;
  virtual void SetGammaRamp(uint32_t iSwapChain, uint32_t Flags, const void* pRamp) = 0;
  virtual void GetGammaRamp(uint32_t iSwapChain, void* pRamp) = 0;
  virtual int CreateTexture(uint32_t Width, uint32_t Height, uint32_t Levels, uint32_t Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DTexture9** ppTexture, void* pSharedHandle) = 0;
  virtual int CreateVolumeTexture(uint32_t Width, uint32_t Height, uint32_t Depth, uint32_t Levels, uint32_t Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DVolumeTexture9** ppVolumeTexture, void* pSharedHandle) = 0;
  virtual int CreateCubeTexture(uint32_t EdgeLength, uint32_t Levels, uint32_t Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DCubeTexture9** ppCubeTexture, void* pSharedHandle) = 0;
  virtual int CreateVertexBuffer(uint32_t Length, uint32_t Usage, uint32_t FVF, D3DPOOL Pool, IDirect3DVertexBuffer9** ppVertexBuffer, void* pSharedHandle) = 0;
  virtual int CreateIndexBuffer(uint32_t Length, uint32_t Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DIndexBuffer9** ppIndexBuffer, void* pSharedHandle) = 0;
  virtual int CreateRenderTarget(uint32_t Width, uint32_t Height, D3DFORMAT Format, D3DMULTISAMPLE_TYPE MultiSample, uint32_t MultisampleQuality, bool Lockable, IDirect3DSurface9** ppSurface, void* pSharedHandle) = 0;
  virtual int CreateDepthStencilSurface(uint32_t Width, uint32_t Height, D3DFORMAT Format, D3DMULTISAMPLE_TYPE MultiSample, uint32_t MultisampleQuality, bool Discard, IDirect3DSurface9** ppSurface, void* pSharedHandle) = 0;
  virtual int CreateImageSurface(uint32_t Width, uint32_t Height, D3DFORMAT Format, IDirect3DSurface9** ppSurface) = 0;
  virtual int CopyRect(IDirect3DSurface9* pSourceSurface, const void* pSourceRect, IDirect3DSurface9* pDestSurface, void* pDestPoint) = 0;
  virtual int UpdateSurface(IDirect3DSurface9* pSourceSurface, const void* pSourceRect, IDirect3DSurface9* pDestSurface, void* pDestPoint) = 0;
  virtual int UpdateTexture(IDirect3DBaseTexture9* pSourceTexture, IDirect3DBaseTexture9* pDestinationTexture) = 0;
  virtual int GetRenderTargetData(IDirect3DSurface9* pRenderTarget, IDirect3DSurface9* pDestSurface) = 0;
  virtual int GetFrontBufferData(uint32_t iSwapChain, IDirect3DSurface9* pDestSurface) = 0;
  virtual int StretchRect(IDirect3DSurface9* pSourceSurface, const void* pSourceRect, IDirect3DSurface9* pDestSurface, const void* pDestRect, uint32_t Filter) = 0;
  virtual int ColorFill(IDirect3DSurface9* pSurface, const void* pRect, uint32_t Color) = 0;
  virtual int CreateOffscreenPlainSurface(uint32_t Width, uint32_t Height, D3DFORMAT Format, D3DPOOL Pool, IDirect3DSurface9** ppSurface, void* pSharedHandle) = 0;
  virtual void SetRenderTarget(uint32_t RenderTargetIndex, IDirect3DSurface9* pRenderTarget) = 0;
  virtual void GetRenderTarget(uint32_t RenderTargetIndex, IDirect3DSurface9** ppRenderTarget) = 0;
  virtual void SetDepthStencilSurface(IDirect3DSurface9* pZStencilSurface) = 0;
  virtual void GetDepthStencilSurface(IDirect3DSurface9** ppZStencilSurface) = 0;
  virtual int BeginScene() = 0;
  virtual int EndScene() = 0;
  virtual int Clear(uint32_t Count, const void* pRects, uint32_t Flags, uint32_t Color, float Z, uint32_t Stencil) = 0;
  virtual int SetTransform(uint32_t State, const D3DMATRIX* pMatrix) = 0;
  virtual int GetTransform(uint32_t State, D3DMATRIX* pMatrix) = 0;
  virtual int MultiplyTransform(uint32_t State, const D3DMATRIX* pMatrix) = 0;
  virtual void SetViewport(const D3DVIEWPORT9* pViewport) = 0;
  virtual void GetViewport(D3DVIEWPORT9* pViewport) = 0;
  virtual void SetMaterial(const D3DMATERIAL9* pMaterial) = 0;
  virtual void GetMaterial(D3DMATERIAL9* pMaterial) = 0;
  virtual void SetLight(uint32_t Index, const D3DLIGHT9* pLight) = 0;
  virtual void GetLight(uint32_t Index, D3DLIGHT9* pLight) = 0;
  virtual int LightEnable(uint32_t Index, bool Enable) = 0;
  virtual void GetLightEnable(uint32_t Index, bool* pEnable) = 0;
  virtual int SetClipPlane(uint32_t Index, const float* pPlane) = 0;
  virtual void GetClipPlane(uint32_t Index, float* pPlane) = 0;
  virtual int SetRenderState(uint32_t State, uint32_t Value) = 0;
  virtual int GetRenderState(uint32_t State, uint32_t* pValue) = 0;
  virtual int BeginStateBlock() = 0;
  virtual int EndStateBlock(uint32_t* pToken) = 0;
  virtual int CreateClipStatus(void** ppClipStatus) = 0;
  virtual int GetClipStatus(void* pClipStatus) = 0;
  virtual int GetTexture(uint32_t Stage, IDirect3DBaseTexture9** ppTexture) = 0;
  virtual int SetTexture(uint32_t Stage, IDirect3DBaseTexture9* pTexture) = 0;
  virtual int GetTextureStageState(uint32_t Stage, uint32_t Type, uint32_t* pValue) = 0;
  virtual int SetTextureStageState(uint32_t Stage, uint32_t Type, uint32_t Value) = 0;
  virtual int GetSamplerState(uint32_t Sampler, uint32_t Type, uint32_t* pValue) = 0;
  virtual int SetSamplerState(uint32_t Sampler, uint32_t Type, uint32_t Value) = 0;
  virtual int ValidateDevice(uint32_t* pNumPasses) = 0;
  virtual int SetPaletteEntries(uint32_t PaletteNumber, const void* pEntries) = 0;
  virtual int GetPaletteEntries(uint32_t PaletteNumber, void* pEntries) = 0;
  virtual int SetCurrentTexturePalette(uint32_t PaletteNumber) = 0;
  virtual int GetCurrentTexturePalette(uint32_t* PaletteNumber) = 0;
  virtual int SetScissorRect(const void* pRect) = 0;
  virtual void GetScissorRect(void* pRect) = 0;
  virtual int SetSoftwareVertexProcessing(bool bSoftware) = 0;
  virtual bool GetSoftwareVertexProcessing() = 0;
  virtual int SetNPatchMode(float nSegments) = 0;
  virtual float GetNPatchMode() = 0;
  virtual int DrawPrimitive(uint32_t PrimitiveType, uint32_t StartVertex, uint32_t PrimitiveCount) = 0;
  virtual int DrawIndexedPrimitive(uint32_t PrimitiveType, int BaseVertexIndex, uint32_t MinVertexIndex, uint32_t NumVertexIndices, uint32_t StartIndex, uint32_t PrimitiveCount) = 0;
  virtual int DrawPrimitiveUP(uint32_t PrimitiveType, uint32_t PrimitiveCount, const void* pVertexStreamZeroData, uint32_t VertexStreamZeroStride) = 0;
  virtual int DrawIndexedPrimitiveUP(uint32_t PrimitiveType, uint32_t MinVertexIndex, uint32_t NumVertexIndices, uint32_t PrimitiveCount, const void* pIndexData, D3DFORMAT IndexDataFormat, const void* pVertexStreamZeroData, uint32_t VertexStreamZeroStride) = 0;
  virtual int ProcessVertices(uint32_t SrcStartIndex, uint32_t DestIndex, uint32_t VertexCount, IDirect3DVertexBuffer9* pDestBuffer, void* pVertexDecl, uint32_t Flags) = 0;
  virtual int CreateVertexShader(const uint32_t* pDeclaration, const uint32_t* pFunction, void** ppVertexShader, uint32_t Flags) = 0;
  virtual int SetVertexShader(void* pShader) = 0;
  virtual void* GetVertexShader() = 0;
  virtual int SetVertexShaderConstant(uint32_t StartRegister, const float* pConstantData, uint32_t Vector4fCount) = 0;
  virtual int GetVertexShaderConstant(uint32_t StartRegister, float* pConstantData, uint32_t Vector4fCount) = 0;
  virtual int SetVertexShaderDecl(void* pDecl) = 0;
  virtual int SetVertexShaderFunction(const uint32_t* pFunction) = 0;
  virtual int SetFVF(uint32_t FVF) = 0;
  virtual int GetFVF(uint32_t* pFVF) = 0;
  virtual int CreateVertexDeclaration(const D3DVERTEXELEMENT9* pVertexElements, void** ppDecl) = 0;
  virtual int SetVertexDeclaration(void* pDecl) = 0;
  virtual void GetVertexDeclaration(void** ppDecl) = 0;
  virtual int SetStreamSource(uint32_t StreamNumber, IDirect3DVertexBuffer9* pStreamData, uint32_t OffsetInBytes, uint32_t Stride) = 0;
  virtual void GetStreamSource(uint32_t StreamNumber, IDirect3DVertexBuffer9** ppStreamData, uint32_t* pOffsetInBytes, uint32_t* pStride) = 0;
  virtual int SetStreamSourceFreq(uint32_t StreamNumber, uint32_t Setting) = 0;
  virtual void GetStreamSourceFreq(uint32_t StreamNumber, uint32_t* pSetting) = 0;
  virtual int SetIndices(IDirect3DIndexBuffer9* pIndexData) = 0;
  virtual void GetIndices(IDirect3DIndexBuffer9** ppIndexData) = 0;
  virtual int CreatePixelShader(const uint32_t* pFunction, IDirect3DPixelShader9** ppShader) = 0;
  virtual int SetPixelShader(IDirect3DPixelShader9* pShader) = 0;
  virtual IDirect3DPixelShader9* GetPixelShader() = 0;
  virtual int SetPixelShaderConstant(uint32_t StartRegister, const float* pConstantData, uint32_t Vector4fCount) = 0;
  virtual int GetPixelShaderConstant(uint32_t StartRegister, float* pConstantData, uint32_t Vector4fCount) = 0;
  virtual int SetPixelShaderFunction(const uint32_t* pFunction) = 0;
  virtual int DrawRectPatch(uint32_t Handle, const float* pNumSegs, const void* pRectPatchInfo) = 0;
  virtual int DrawTriPatch(uint32_t Handle, const float* pNumSegs, const void* pTriPatchInfo) = 0;
  virtual int DeletePatch(uint32_t Handle) = 0;
  virtual int CreateQuery(uint32_t Type, IDirect3DQuery9** ppQuery) = 0;
};

// IDirect3DDevice9Ex
struct IDirect3DDevice9Ex : IDirect3DDevice9 {
  virtual ~IDirect3DDevice9Ex() = default;
};

