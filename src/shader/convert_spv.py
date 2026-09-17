import struct

def spv_to_c_array(path, name):
    with open(path, 'rb') as f:
        data = f.read()
    words = struct.unpack('<' + 'I' * (len(data) // 4), data)
    lines = [f'static const uint32_t {name}[] = {{']
    for i in range(0, len(words), 8):
        chunk = words[i:i+8]
        line = '  ' + ', '.join(f'0x{w:08X}' for w in chunk)
        if i + 8 < len(words):
            line += ','
        lines.append(line)
    lines.append('};')
    lines.append(f'static const size_t {name}_size = {len(data)};')
    return '\n'.join(lines)

base = 'E:/MARAT/Projects/vkwind/src/shader/'
print(spv_to_c_array(base + 'default_shaders.vert.spv', 'kDefaultVS'))
print()
print(spv_to_c_array(base + 'default_shaders.frag.spv', 'kDefaultFS'))
