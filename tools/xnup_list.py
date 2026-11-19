import struct, sys, os, hashlib

def parse(path):
    with open(path, 'rb') as f:
        data = f.read()
    if data[:4] != b'XNUP':
        return None
    total = struct.unpack('<I', data[4:8])[0]
    off = 8
    entries = []
    while off + 28 <= min(total, len(data)):
        name = data[off:off+16].split(b'\x00')[0].decode('ascii', 'replace')
        size, rsvd = struct.unpack('<II', data[off+16:off+24])
        doff = (off + 24 + 15) & ~15
        if doff + size > len(data):
            entries.append((name, size, None, 'TRUNCATED'))
            break
        entries.append((name, size, doff, hashlib.md5(data[doff:doff+size]).hexdigest()))
        off = (doff + size + 15) & ~15
    return total, entries

for p in sys.argv[1:]:
    r = parse(p)
    print('==', os.path.basename(p), os.path.getsize(p))
    if not r:
        print('   not XNUP'); continue
    total, entries = r
    print('   header-total=%d' % total)
    for name, size, doff, md5 in entries:
        blocks = size / 4096.0
        print('   %-16s %12d  blocks=%8.3f  md5=%s' % (name, size, blocks, md5))
