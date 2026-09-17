import struct
import sys

def convert(name, filename):
    with open(filename, 'rb') as f:
        data = f.read()
    words = struct.unpack('<%dI' % (len(data)//4), data)
    lines = []
    for i in range(0, len(words), 8):
        chunk = ', '.join('0x%08X' % w for w in words[i:i+8])
        lines.append('  ' + chunk + ',')
    print('static const uint32_t k%s[] = {' % name)
    for l in lines:
        print(l)
    print('};')
    print('static const size_t k%s_size = %d;' % (name, len(data)))

convert('DefaultVS', 'default_vs.spv')
print()
convert('DefaultFS', 'default_fs.spv')
