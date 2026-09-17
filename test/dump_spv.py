import struct, sys

with open(sys.argv[1], 'rb') as f:
    data = f.read()

words = struct.unpack('<%dI' % (len(data)//4), data)
print(f'File: {sys.argv[1]}  Size: {len(data)} bytes  Words: {len(words)}')

i = 0
# Skip header
print(f'=== Header ===')
print(f'  Magic:   0x{words[0]:08X}')
print(f'  Version: 0x{words[1]:08X}')
print(f'  Generator: 0x{words[2]:08X}')
print(f'  Bound:   {words[3]}')
print(f'  Schema:  {words[4]}')
i = 5

while i < len(words):
    wc = (words[i] >> 16) & 0xFFFF
    op = words[i] & 0xFFFF
    start = i
    if wc == 0:
        print(f'  [{i:3d}] ERROR: wordcount=0 at offset 0x{i*4:04X}')
        break
    end = min(i + wc, len(words))
    raw_hex = ' '.join(f'{words[j]:08X}' for j in range(start, end))
    print(f'  [{i:3d}] wc={wc} op=0x{op:04X} ({op:3d})  {raw_hex}')
    i = end
