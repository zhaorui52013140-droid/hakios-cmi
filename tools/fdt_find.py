#!/usr/bin/env python3
"""Minimal flattened-device-tree reader: list nodes whose name or properties match."""
import struct, sys

FDT_MAGIC = 0xd00dfeed


def cells(data, off, n):
    return struct.unpack('>%dI' % n, data[off:off + 4 * n]) if n else ()


def parse(path):
    data = open(path, 'rb').read()
    magic, totalsize, off_struct, off_strings, off_rsv, version, last_comp, boot_cpu, size_strings, size_struct = \
        struct.unpack('>10I', data[:40])
    if magic != FDT_MAGIC:
        raise SystemExit('not a dtb: %#x' % magic)
    strings = data[off_strings:off_strings + size_strings]
    pos = off_struct
    path_stack = []
    nodes = []          # (full_path, {prop: value})
    while pos < off_struct + size_struct:
        tok = struct.unpack('>I', data[pos:pos + 4])[0]
        pos += 4
        if tok == 1:    # BEGIN_NODE
            end = data.index(b'\0', pos)
            name = data[pos:end].decode('ascii', 'replace')
            pos = (end + 4) & ~3
            path_stack.append(name)
            nodes.append(('/' + '/'.join(p for p in path_stack if p), {}))
        elif tok == 2:  # END_NODE
            if path_stack:
                path_stack.pop()
        elif tok == 3:  # PROP
            length, nameoff = struct.unpack('>II', data[pos:pos + 8])
            pos += 8
            end = strings.index(b'\0', nameoff)
            name = strings[nameoff:end].decode('ascii', 'replace')
            value = data[pos:pos + length]
            pos = (pos + length + 3) & ~3
            if nodes:
                nodes[-1][1][name] = value
        elif tok == 4:  # NOP
            continue
        elif tok == 9:  # END
            break
        else:
            raise SystemExit('bad token %#x at %#x' % (tok, pos - 4))
    return nodes


def show(value):
    if len(value) and value[-1:] == b'\0' and all(32 <= c < 127 or c == 0 for c in value):
        return value.rstrip(b'\0').decode('ascii', 'replace')
    if len(value) % 4 == 0 and 0 < len(value) <= 64:
        return ' '.join('%#x' % c for c in cells(value, 0, len(value) // 4))
    return value.hex()


def main():
    path = sys.argv[1]
    needles = [n.lower() for n in sys.argv[2:]] or ['haptic', 'aw869', 'vibrat']
    nodes = parse(path)
    hits = []
    for full, props in nodes:
        blob = (full + ' ' + ' '.join(
            '%s=%s' % (k, show(v)) for k, v in props.items())).lower()
        if any(n in blob for n in needles):
            hits.append((full, props))
    for full, props in hits:
        print('=== %s' % full)
        for k, v in props.items():
            print('    %-24s %s' % (k, show(v)))
    print('nodes: %d, matching: %d' % (len(nodes), len(hits)))


if __name__ == '__main__':
    main()
