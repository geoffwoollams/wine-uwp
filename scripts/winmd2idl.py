#!/usr/bin/env python3
"""Generate widl IDL for WinRT types from Windows metadata (.winmd), so IIDs and vtable order are exact.

Usage: winmd2idl.py WINDOWS.WINMD [--have WINE_INCLUDE_DIR ...] TYPE [TYPE ...]
  TYPE        full name of an interface, delegate or runtime class to define completely
              (for a runtime class: the class plus all of its interfaces, statics and factories)
  --have DIR  Wine's include directory: types already defined in its *.idl files are not emitted
Everything the requested types reference is emitted too: enums and structs in full, other interfaces and
delegates as forward declarations, other runtime classes with their default interface only. Output goes to stdout.
Windows.winmd can be taken from github.com/microsoft/windows-rs (crates/libs/default/Windows.winmd).

patches/wine/0014 was made with --have pointing at a wine-11.0 + 0001-0013 include/ directory and the types
Windows.UI.Xaml.Application Windows.UI.Xaml.ApplicationInitializationCallbackParams Windows.UI.Xaml.Window
Windows.UI.Xaml.UIElement Windows.UI.Xaml.FrameworkElement Windows.UI.Xaml.Controls.TextBlock
Windows.UI.Xaml.Media.Brush Windows.UI.Xaml.Media.SolidColorBrush; the output's first lines list the imports it needs.
"""
import struct, sys, collections

# ---------------------------------------------------------------- ECMA-335 metadata reader

TABLES = {  # id: (name, columns); column kinds: 2/4 fixed, 's' string, 'g' guid, 'b' blob, ('t', table), ('c', coded)
    0x00: ('Module', [2, 's', 'g', 'g', 'g']),
    0x01: ('TypeRef', [('c', 'ResolutionScope'), 's', 's']),
    0x02: ('TypeDef', [4, 's', 's', ('c', 'TypeDefOrRef'), ('t', 0x04), ('t', 0x06)]),
    0x03: ('FieldPtr', [('t', 0x04)]),
    0x04: ('Field', [2, 's', 'b']),
    0x05: ('MethodPtr', [('t', 0x06)]),
    0x06: ('MethodDef', [4, 2, 2, 's', 'b', ('t', 0x08)]),
    0x07: ('ParamPtr', [('t', 0x08)]),
    0x08: ('Param', [2, 2, 's']),
    0x09: ('InterfaceImpl', [('t', 0x02), ('c', 'TypeDefOrRef')]),
    0x0A: ('MemberRef', [('c', 'MemberRefParent'), 's', 'b']),
    0x0B: ('Constant', [2, ('c', 'HasConstant'), 'b']),
    0x0C: ('CustomAttribute', [('c', 'HasCustomAttribute'), ('c', 'CustomAttributeType'), 'b']),
    0x0D: ('FieldMarshal', [('c', 'HasFieldMarshal'), 'b']),
    0x0E: ('DeclSecurity', [2, ('c', 'HasDeclSecurity'), 'b']),
    0x0F: ('ClassLayout', [2, 4, ('t', 0x02)]),
    0x10: ('FieldLayout', [4, ('t', 0x04)]),
    0x11: ('StandAloneSig', ['b']),
    0x12: ('EventMap', [('t', 0x02), ('t', 0x14)]),
    0x13: ('EventPtr', [('t', 0x14)]),
    0x14: ('Event', [2, 's', ('c', 'TypeDefOrRef')]),
    0x15: ('PropertyMap', [('t', 0x02), ('t', 0x17)]),
    0x16: ('PropertyPtr', [('t', 0x17)]),
    0x17: ('Property', [2, 's', 'b']),
    0x18: ('MethodSemantics', [2, ('t', 0x06), ('c', 'HasSemantics')]),
    0x19: ('MethodImpl', [('t', 0x02), ('c', 'MethodDefOrRef'), ('c', 'MethodDefOrRef')]),
    0x1A: ('ModuleRef', ['s']),
    0x1B: ('TypeSpec', ['b']),
    0x1C: ('ImplMap', [2, ('c', 'MemberForwarded'), 's', ('t', 0x1A)]),
    0x1D: ('FieldRVA', [4, ('t', 0x04)]),
    0x1E: ('ENCLog', [4, 4]),
    0x1F: ('ENCMap', [4]),
    0x20: ('Assembly', [4, 2, 2, 2, 2, 4, 'b', 's', 's']),
    0x21: ('AssemblyProcessor', [4]),
    0x22: ('AssemblyOS', [4, 4, 4]),
    0x23: ('AssemblyRef', [2, 2, 2, 2, 4, 'b', 's', 's', 'b']),
    0x24: ('AssemblyRefProcessor', [4, ('t', 0x23)]),
    0x25: ('AssemblyRefOS', [4, 4, 4, ('t', 0x23)]),
    0x26: ('File', [4, 's', 'b']),
    0x27: ('ExportedType', [4, 4, 's', 's', ('c', 'Implementation')]),
    0x28: ('ManifestResource', [4, 4, 's', ('c', 'Implementation')]),
    0x29: ('NestedClass', [('t', 0x02), ('t', 0x02)]),
    0x2A: ('GenericParam', [2, 2, ('c', 'TypeOrMethodDef'), 's']),
    0x2B: ('MethodSpec', [('c', 'MethodDefOrRef'), 'b']),
    0x2C: ('GenericParamConstraint', [('t', 0x2A), ('c', 'TypeDefOrRef')]),
}
CODED = {
    'TypeDefOrRef': [0x02, 0x01, 0x1B],
    'HasConstant': [0x04, 0x08, 0x17],
    'HasCustomAttribute': [0x06, 0x04, 0x01, 0x02, 0x08, 0x09, 0x0A, 0x00, 0x0E, 0x17, 0x14, 0x11, 0x1A, 0x1B,
                           0x20, 0x23, 0x26, 0x27, 0x28, 0x2A, 0x2C, 0x2B],
    'HasFieldMarshal': [0x04, 0x08],
    'HasDeclSecurity': [0x02, 0x06, 0x20],
    'MemberRefParent': [0x02, 0x01, 0x1A, 0x06, 0x1B],
    'HasSemantics': [0x14, 0x17],
    'MethodDefOrRef': [0x06, 0x0A],
    'MemberForwarded': [0x04, 0x06],
    'Implementation': [0x26, 0x23, 0x27],
    'CustomAttributeType': [None, None, 0x06, 0x0A, None],
    'ResolutionScope': [0x00, 0x1A, 0x23, 0x01],
    'TypeOrMethodDef': [0x02, 0x06],
}


class Metadata:
    def __init__(self, path):
        data = open(path, 'rb').read()
        pe = struct.unpack_from('<I', data, 0x3c)[0]
        nsec = struct.unpack_from('<H', data, pe + 6)[0]
        optsize = struct.unpack_from('<H', data, pe + 20)[0]
        opt = pe + 24
        magic = struct.unpack_from('<H', data, opt)[0]
        ddir = opt + (96 if magic == 0x10b else 112)
        cli_rva = struct.unpack_from('<I', data, ddir + 14 * 8)[0]
        secs = []
        for i in range(nsec):
            s = opt + optsize + i * 40
            vsize, va, rawsize, rawptr = struct.unpack_from('<IIII', data, s + 8)
            secs.append((va, max(vsize, rawsize), rawptr))

        def off(rva):
            for va, size, raw in secs:
                if va <= rva < va + size: return rva - va + raw
            raise ValueError(rva)
        cli = off(cli_rva)
        md_rva, _ = struct.unpack_from('<II', data, cli + 8)
        md = off(md_rva)
        vlen = struct.unpack_from('<I', data, md + 12)[0]
        p = md + 16 + vlen + 2
        nstreams = struct.unpack_from('<H', data, p)[0]
        p += 2
        streams = {}
        for _ in range(nstreams):
            o, size = struct.unpack_from('<II', data, p)
            p += 8
            e = data.index(b'\0', p)
            name = data[p:e].decode()
            p = (e + 4) & ~3
            streams[name] = data[md + o: md + o + size]
        self.strings, self.blobs, self.guids = streams['#Strings'], streams['#Blob'], streams['#GUID']
        t = streams['#~']
        heap = t[6]
        self.str_size = 4 if heap & 1 else 2
        self.guid_size = 4 if heap & 2 else 2
        self.blob_size = 4 if heap & 4 else 2
        valid = struct.unpack_from('<Q', t, 8)[0]
        p = 24
        self.rows = collections.defaultdict(int)
        for i in range(64):
            if valid >> i & 1:
                self.rows[i] = struct.unpack_from('<I', t, p)[0]
                p += 4
        self.tables = {}
        for tid in range(64):
            if not self.rows[tid]: continue
            name, cols = TABLES[tid]
            sizes = [self.colsize(c) for c in cols]
            rowsize = sum(sizes)
            rows = []
            for r in range(self.rows[tid]):
                vals, q = [], p + r * rowsize
                for c, sz in zip(cols, sizes):
                    v = struct.unpack_from('<I' if sz == 4 else '<H', t, q)[0]
                    q += sz
                    vals.append(self.decode_col(c, v))
                rows.append(vals)
            self.tables[tid] = rows
            p += rowsize * self.rows[tid]

    def colsize(self, c):
        if c in (2, 4): return c
        if c == 's': return self.str_size
        if c == 'g': return self.guid_size
        if c == 'b': return self.blob_size
        if c[0] == 't': return 2 if self.rows[c[1]] < 0x10000 else 4
        tabs = CODED[c[1]]
        bits = (len(tabs) - 1).bit_length()
        m = max(self.rows[x] for x in tabs if x is not None)
        return 2 if m < (1 << (16 - bits)) else 4

    def decode_col(self, c, v):
        if c in (2, 4): return v
        if c == 's':
            return self.strings[v:self.strings.index(b'\0', v)].decode()
        if c == 'g': return v
        if c == 'b':
            n, l = self.blob_len(v)
            return self.blobs[v + l: v + l + n]
        if c[0] == 't': return (c[1], v)
        tabs = CODED[c[1]]
        bits = (len(tabs) - 1).bit_length()
        tag = v & ((1 << bits) - 1)
        return (tabs[tag] if tag < len(tabs) else None, v >> bits)

    def blob_len(self, p):
        b = self.blobs[p]
        if b & 0x80 == 0: return b, 1
        if b & 0xc0 == 0x80: return ((b & 0x3f) << 8) | self.blobs[p + 1], 2
        return ((b & 0x1f) << 24) | (self.blobs[p + 1] << 16) | (self.blobs[p + 2] << 8) | self.blobs[p + 3], 4

    def row(self, ref):
        tid, idx = ref
        return self.tables[tid][idx - 1] if idx else None

    def range_of(self, tid, start_col, ridx, target):
        rows = self.tables[tid]
        start = rows[ridx][start_col][1]
        end = rows[ridx + 1][start_col][1] if ridx + 1 < len(rows) else self.rows[target] + 1
        return range(start, end)


def uncompress(b, p):
    x = b[p]
    if x & 0x80 == 0: return x, p + 1
    if x & 0xc0 == 0x80: return ((x & 0x3f) << 8) | b[p + 1], p + 2
    return ((x & 0x1f) << 24) | (b[p + 1] << 16) | (b[p + 2] << 8) | b[p + 3], p + 4


# ---------------------------------------------------------------- WinRT model

PRIM = {0x01: 'void', 0x02: 'boolean', 0x03: 'WCHAR', 0x04: 'INT8', 0x05: 'BYTE', 0x06: 'INT16', 0x07: 'UINT16',
        0x08: 'INT32', 0x09: 'UINT32', 0x0a: 'INT64', 0x0b: 'UINT64', 0x0c: 'FLOAT', 0x0d: 'DOUBLE', 0x0e: 'HSTRING',
        0x1c: 'IInspectable'}


class Type:
    """A type reference in a signature: kind is 'prim', 'named', 'generic', 'var', 'array', 'byref'."""
    def __init__(self, kind, name=None, args=None, inner=None, valuetype=False):
        self.kind, self.name, self.args, self.inner, self.valuetype = kind, name, args or [], inner, valuetype


class WinRT:
    def __init__(self, md):
        self.md = md
        self.types = {}      # full name -> typedef row index (0-based)
        for i, r in enumerate(md.tables[0x02]):
            self.types[(r[2] + '.' + r[1]) if r[2] else r[1]] = i
        # custom attributes by parent
        self.attrs = collections.defaultdict(list)
        for parent, ctor, value in md.tables.get(0x0C, []):
            self.attrs[parent].append((self.attr_name(ctor), value, ctor))
        self.ifaces = collections.defaultdict(list)
        for i, (cls, iface) in enumerate(md.tables.get(0x09, [])):
            self.ifaces[cls[1] - 1].append((iface, (0x09, i + 1)))
        self.generic_params = collections.defaultdict(list)
        for num, flags, owner, name in md.tables.get(0x2A, []):
            if owner[0] == 0x02: self.generic_params[owner[1] - 1].append((num, name))

    def typename_of(self, ref):
        tid, idx = ref
        r = self.md.row(ref)
        if tid == 0x02: return (r[2] + '.' + r[1]) if r[2] else r[1]
        if tid == 0x01: return (r[2] + '.' + r[1]) if r[2] else r[1]
        raise ValueError(ref)

    def attr_name(self, ctor):
        tid, idx = ctor
        if tid == 0x0A:
            parent = self.md.row(ctor)[0]
            return self.typename_of(parent) if parent[0] in (0x01, 0x02) else '?'
        if tid == 0x06:
            for i, r in enumerate(self.md.tables[0x02]):
                if idx in self.md.range_of(0x02, 5, i, 0x06): return r[2] + '.' + r[1]
        return '?'

    def attr(self, parent, name):
        return [v for n, v, c in self.attrs[parent] if n == 'Windows.Foundation.Metadata.' + name]

    def decode_type(self, b, p, gen=None):
        e = b[p]; p += 1
        if e in PRIM: return Type('prim', PRIM[e]), p
        if e in (0x11, 0x12):
            tok, p = uncompress(b, p)
            ref = (CODED['TypeDefOrRef'][tok & 3], tok >> 2)
            n = self.typename_of(ref)
            if n == 'System.Guid': return Type('prim', 'GUID'), p
            if n == 'Windows.Foundation.HResult': return Type('prim', 'HRESULT'), p
            if n == 'System.Type': return Type('named', 'Windows.UI.Xaml.Interop.TypeName', valuetype=True), p
            return Type('named', n, valuetype=(e == 0x11)), p
        if e == 0x15:
            t, p = self.decode_type(b, p, gen)
            n, p = uncompress(b, p)
            args = []
            for _ in range(n):
                a, p = self.decode_type(b, p, gen)
                args.append(a)
            return Type('generic', t.name, args), p
        if e == 0x13:
            n, p = uncompress(b, p)
            return Type('var', gen[n] if gen else 'T%d' % n), p
        if e == 0x1d:
            t, p = self.decode_type(b, p, gen)
            return Type('array', inner=t), p
        if e == 0x10:
            t, p = self.decode_type(b, p, gen)
            return Type('byref', inner=t), p
        if e in (0x1f, 0x20):
            _, p = uncompress(b, p)
            return self.decode_type(b, p, gen)
        raise ValueError('element type %#x' % e)

    def typespec(self, ref):
        if ref[0] != 0x1B: return Type('named', self.typename_of(ref))
        t, _ = self.decode_type(self.md.row(ref)[0], 0)
        return t

    def kind(self, idx):
        r = self.md.tables[0x02][idx]
        flags, ext = r[0], r[3]
        if flags & 0x20: return 'interface'
        base = self.typename_of(ext) if ext[1] else ''
        if base == 'System.Enum': return 'enum'
        if base == 'System.ValueType':
            return 'struct'
        if base == 'System.MulticastDelegate': return 'delegate'
        if base == 'System.Attribute': return 'attribute'
        return 'runtimeclass'

    def guid(self, idx):
        v = self.attr((0x02, idx + 1), 'GuidAttribute')
        if not v: return None
        a = struct.unpack_from('<IHH8B', v[0], 2)
        return '%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x' % a

    def methods(self, idx):
        md = self.md
        gen = [n for _, n in sorted(self.generic_params[idx])]
        out = []
        for m in md.range_of(0x02, 5, idx, 0x06):
            rva, iflags, flags, name, sig, plist = md.tables[0x06][m - 1]
            params = {}
            for pi in md.range_of(0x06, 5, m - 1, 0x08):
                pf, seq, pn = md.tables[0x08][pi - 1]
                params[seq] = (pf, pn)
            p = 1
            count, p = uncompress(sig, p)
            ret, p = self.decode_type(sig, p, gen)
            args = []
            for i in range(count):
                t, p = self.decode_type(sig, p, gen)
                pf, pn = params.get(i + 1, (0, 'p%d' % i))
                args.append((pn, t, pf))
            attrs = {n.split('.')[-1]: v for n, v, c in self.attrs[(0x06, m)]}
            out.append(dict(name=name, flags=flags, ret=ret, args=args, attrs=attrs,
                            retname=params.get(0, (0, None))[1]))
        return out

    def fields(self, idx):
        md = self.md
        out = []
        for f in md.range_of(0x02, 4, idx, 0x04):
            flags, name, sig = md.tables[0x04][f - 1]
            t, _ = self.decode_type(sig, 1)
            out.append((flags, name, t, f))
        return out

    def constant(self, field):
        for typ, parent, value in self.md.tables.get(0x0B, []):
            if parent == (0x04, field):
                return struct.unpack('<i' if (typ & 0xff) == 0x08 else '<I', value[:4])[0]


def attr_string(blob, p):
    if blob[p] == 0xff: return None, p + 1
    n, p = uncompress(blob, p)
    return blob[p:p + n].decode(), p + n


# ---------------------------------------------------------------- IDL emitter

class Emitter:
    def __init__(self, w, have):
        self.w, self.have = w, have
        self.full = []            # types to emit in full, in order
        self.forward = set()      # interfaces/delegates/classes referenced
        self.full_set = set()
        self.generics = collections.OrderedDict()
        self.imports = set()

    def ns_name(self, full):
        ns, _, n = full.rpartition('.')
        return ns, n

    def add_full(self, name):
        if name in self.full_set or name in self.have: return
        if name not in self.w.types: raise SystemExit('unknown type ' + name)
        self.full_set.add(name)
        self.full.append(name)
        idx = self.w.types[name]
        k = self.w.kind(idx)
        if k == 'runtimeclass':
            for iface, _ in self.w.ifaces[idx]:
                t = self.w.typespec(iface)
                if t.kind == 'named': self.add_full(t.name)
                else: self.ref(t)
            for a in ('StaticAttribute', 'ActivatableAttribute', 'ComposableAttribute'):
                for v in self.w.attr((0x02, idx + 1), a):
                    s, _ = attr_string(v, 2)
                    if s and s in self.w.types and self.w.kind(self.w.types[s]) == 'interface': self.add_full(s)
            ext = self.w.md.tables[0x02][idx][3]
            if ext[1]:
                base = self.w.typename_of(ext)
                if base != 'System.Object': self.ref(Type('named', base))
        elif k in ('interface', 'delegate'):
            for m in self.w.methods(idx):
                if k == 'delegate' and m['name'] != 'Invoke': continue
                self.ref(m['ret'])
                for _, t, _ in m['args']: self.ref(t)
            for iface, _ in self.w.ifaces[idx]: self.ref(self.w.typespec(iface))
        elif k == 'struct':
            for _, _, t, _ in self.w.fields(idx): self.ref(t)

    def ref(self, t):
        if t.kind in ('array', 'byref'): return self.ref(t.inner)
        if t.kind in ('named', 'generic') and t.name.split('`')[0] in self.have:
            self.imports.add(self.have[t.name.split('`')[0]])
        if t.kind == 'generic':
            for a in t.args:
                self.ref(a)
                # widl computes parameterized IIDs, so it needs the uuid of every argument's default interface
                if a.kind == 'named' and a.name in self.w.types and a.name not in self.have:
                    k = self.w.kind(self.w.types[a.name])
                    if k == 'runtimeclass':
                        d = self.default_iface(self.w.types[a.name])
                        if d and d.kind == 'named': self.add_full(d.name)
                    elif k == 'interface': self.add_full(a.name)
            C = 'Windows.Foundation.Collections.'
            base = t.name.split('`')[0]
            G = lambda n, *a: Type('generic', C + n, list(a))
            implied = {
                'IVector': lambda a: [G('IVectorView', *a), G('IIterable', *a)],
                'IObservableVector': lambda a: [G('IVector', *a), G('VectorChangedEventHandler', *a)],
                'IVectorView': lambda a: [G('IIterable', *a)],
                'IIterable': lambda a: [G('IIterator', *a)],
                'IMap': lambda a: [G('IMapView', *a), G('IIterable', G('IKeyValuePair', *a))],
                'IMapView': lambda a: [G('IIterable', G('IKeyValuePair', *a))],
                'IObservableMap': lambda a: [G('IMap', *a), G('MapChangedEventHandler', *a), G('IMapChangedEventArgs', a[0])],
            }.get(base[len(C):] if base.startswith(C) else None)
            # declare what the type requires before the type itself
            for d in (implied(t.args) if implied else []): self.ref(d)
            key = self.tname(t)
            if not key.startswith('T'): self.generics[key] = True
            return
        if t.kind != 'named' or t.name in self.have or t.name in self.full_set: return
        if t.name not in self.w.types: return
        k = self.w.kind(self.w.types[t.name])
        if k in ('enum', 'struct', 'delegate'): self.add_full(t.name)  # widl can't forward-declare delegates
        elif k == 'runtimeclass':
            self.forward.add(t.name)
            d = self.default_iface(self.w.types[t.name])
            if d and d.kind == 'generic': self.ref(d)
            elif d:
                self.add_full(d.name)   # widl wants the member interfaces defined
                for r in self.requires(d):
                    if r.kind == 'named': self.add_full(r.name)
                    else: self.ref(r)
        else: self.forward.add(t.name)

    def requires(self, t):
        """Interfaces an interface type requires, transitively."""
        if t.kind != 'named' or t.name not in self.w.types: return []
        out = []
        for i, _ in self.w.ifaces[self.w.types[t.name]]:
            r = self.w.typespec(i)
            out.append(r)
            out += self.requires(r)
        return out

    def default_iface(self, idx):
        for iface, ii in self.w.ifaces[idx]:
            if self.w.attr(ii, 'DefaultAttribute'):
                return self.w.typespec(iface)
        return None

    def tname(self, t):
        if t.kind == 'prim': return t.name
        if t.kind == 'var': return t.name
        if t.kind == 'named': return t.name
        if t.kind == 'generic':
            return t.name.split('`')[0] + '<' + ', '.join(self.argname(a) for a in t.args) + '>'
        raise ValueError(t.kind)

    def argname(self, a):
        n = self.tname(a)
        if a.kind == 'prim' and a.name == 'IInspectable': return 'IInspectable *'
        if a.kind == 'prim' and a.name == 'HSTRING': return 'HSTRING'
        if a.kind in ('named', 'generic') and self.is_ref_type(a): return n + ' *'
        return n

    def is_ref_type(self, t):
        if t.kind == 'generic': return True
        if t.kind == 'prim': return t.name == 'IInspectable'
        if t.kind == 'var': return False
        if t.name in self.w.types:
            return self.w.kind(self.w.types[t.name]) not in ('enum', 'struct')
        return not t.valuetype

    def ptype(self, t):
        """C-ish IDL type of a value of type t."""
        n = self.tname(t)
        if t.kind == 'prim' and t.name == 'IInspectable': return 'IInspectable *'
        if self.is_ref_type(t): return n + ' *'
        return n

    def contract(self, parent):
        v = self.w.attr(parent, 'ContractVersionAttribute')
        if not v: return None
        b = v[0]
        if len(b) >= 2 + 1 and b[2] != 0xff:
            s, p = attr_string(b, 2)
            if s and '.' in s and p + 4 <= len(b):
                ver = struct.unpack_from('<I', b, p)[0]
                return '%s, %d.%d' % (s, ver >> 16, ver & 0xffff)
        return None

    def emit(self):
        out = ['/* needs: %s */\n' % ', '.join('import "%s";' % i for i in sorted(self.imports))]
        # method names that collide with Win32 A/W macros
        clash = {'GetClassName', 'GetObject', 'GetMessage', 'SendMessage', 'PostMessage', 'DrawText', 'LoadImage',
                 'CreateFont', 'GetCurrentDirectory', 'FindText', 'ReplaceText', 'CreateEvent', 'GetUserName', 'StartService'}
        used = set()
        for n in self.full:
            k = self.w.kind(self.w.types[n])
            if k == 'interface': used |= {self.mname(m) for m in self.w.methods(self.w.types[n])}
        for c in sorted(clash & used): out.append('cpp_quote("#undef %s")' % c)
        if clash & used: out.append('')
        byns = collections.OrderedDict()
        # forward declarations
        fwd = collections.defaultdict(list)
        for n in sorted(self.forward | set(self.full)):
            if n in self.have: continue
            ns, short = self.ns_name(n)
            k = self.w.kind(self.w.types[n])
            if k == 'delegate': continue
            kw = {'interface': 'interface', 'delegate': 'delegate', 'runtimeclass': 'runtimeclass',
                  'enum': 'typedef enum %s %s' % (short, short), 'struct': 'typedef struct %s %s' % (short, short)}[k]
            fwd[ns].append(kw + ';' if k in ('enum', 'struct') else '%s %s;' % (kw, short))
        for ns in sorted(fwd):
            out.append('namespace %s {' % ns)
            out += ['    ' + l for l in fwd[ns]]
            out.append('}\n')
        if self.generics:
            out.append('namespace Windows.UI.Xaml {')
            out.append('    declare {')
            for g in self.generics: out.append('        interface %s;' % g)
            out.append('    }')
            out.append('}\n')
        # definitions: enums and structs first, then interfaces/delegates, then classes
        order = {'enum': 0, 'struct': 1, 'delegate': 2, 'interface': 3, 'runtimeclass': 4}
        items = sorted(self.full, key=lambda n: (order[self.w.kind(self.w.types[n])], self.ns_name(n)))
        defs = collections.OrderedDict()   # (phase, namespace) -> definitions
        for n in items:
            ns, _ = self.ns_name(n)
            defs.setdefault((order[self.w.kind(self.w.types[n])], ns), []).append(self.define(n))
        # forward-only runtime classes need a definition with their default interface
        for n in sorted(self.forward):
            idx = self.w.types[n]
            if self.w.kind(idx) == 'runtimeclass' and n not in self.full_set:
                ns, short = self.ns_name(n)
                d = self.default_iface(idx)
                attrs = [a for a in [self.contract_attr((0x02, idx + 1))] if a]
                body = '    [\n%s\n    ]\n    runtimeclass %s\n    {\n' % (',\n'.join('        ' + a for a in attrs), short)
                cc = self.contract((0x02, idx + 1))
                gc = lambda t: '[contract(%s)] ' % cc if t.kind == 'generic' and cc else ''
                if d: body += '        [default] %sinterface %s;\n' % (gc(d), self.tname(d))
                for r in (self.requires(d) if d else []): body += '        %sinterface %s;\n' % (gc(r), self.tname(r))
                body += '    }\n'
                defs.setdefault((5, ns), []).append(body)
        defs = collections.OrderedDict((k[1] if False else k, defs[k]) for k in sorted(defs))
        for (phase, ns), ds in defs.items():
            out.append('namespace %s {' % ns)
            out.append('\n'.join(ds))
            out.append('}\n')
        return '\n'.join(out)

    def contract_attr(self, parent):
        c = self.contract(parent)
        return 'contract(%s)' % c if c else None

    def define(self, n):
        w = self.w
        idx = w.types[n]
        k = w.kind(idx)
        ns, short = self.ns_name(n)
        parent = (0x02, idx + 1)
        attrs = []
        if k == 'enum':
            a = [x for x in [self.contract_attr(parent)] if x]
            if w.attr(parent, 'FlagsAttribute'): a.append('flags')
            s = ('    [\n%s\n    ]\n' % ',\n'.join('        ' + x for x in a)) if a else ''
            s += '    enum %s\n    {\n' % short
            vals = []
            for flags, fname, t, f in w.fields(idx):
                if flags & 0x10:
                    vals.append('        %s = %d' % (fname, w.constant(f)))
            return s + ',\n'.join(vals) + ',\n    };\n'.replace(',\n    };', '\n    };')
        if k == 'struct':
            a = [x for x in [self.contract_attr(parent)] if x]
            s = ('    [\n%s\n    ]\n' % ',\n'.join('        ' + x for x in a)) if a else ''
            s += '    struct %s\n    {\n' % short
            for flags, fname, t, f in w.fields(idx):
                s += '        %s %s;\n' % (self.ptype(t), fname)
            return s + '    };\n'
        if k in ('interface', 'delegate'):
            gen = [nm for _, nm in sorted(w.generic_params[idx])]
            if gen: raise SystemExit('generic definitions are not supported: ' + n)
            c = self.contract_attr(parent)
            if c: attrs.append(c)
            ex = w.attr(parent, 'ExclusiveToAttribute')
            if ex:
                s, _ = attr_string(ex[0], 2)
                attrs.append('exclusiveto(%s)' % s)
            attrs.append('uuid(%s)' % w.guid(idx))
            s = '    [\n%s\n    ]\n' % ',\n'.join('        ' + a for a in attrs)
            if k == 'delegate':
                m = [m for m in w.methods(idx) if m['name'] == 'Invoke'][0]
                s += '    delegate HRESULT %s(%s);\n' % (short, self.params(m))
                return s
            req = [w.typespec(i) for i, _ in w.ifaces[idx]]
            s += '    interface %s : IInspectable%s\n    {\n' % (short, '' if not req else '\n        requires ' + ', '.join(self.tname(t) for t in req))
            for m in w.methods(idx):
                s += '        %sHRESULT %s(%s);\n' % (self.mattrs(m), self.mname(m), self.params(m))
            return s + '    }\n'
        # runtime class
        c = self.contract_attr(parent)
        for v in w.attr(parent, 'ActivatableAttribute'):
            if v[2] != 0xff and len(v) > 2:
                s_, p = attr_string(v, 2)
                if s_ and s_ in w.types and w.kind(w.types[s_]) == 'interface':
                    cv = self.attr_contract_tail(v, p)
                    attrs.append('activatable(%s%s)' % (s_, cv))
                    continue
            # activatable(contract) form: UInt32 version, String contract
            cv = self.attr_contract_tail(v, 2)
            attrs.append('activatable(%s)' % cv.lstrip(', '))
        for v in w.attr(parent, 'ComposableAttribute'):
            s_, p = attr_string(v, 2)
            kindv = struct.unpack_from('<i', v, p)[0]
            cv = self.attr_contract_tail(v, p + 4)
            attrs.append('composable(%s, %s%s)' % (s_, 'public' if kindv == 2 else 'protected', cv))
        if c: attrs.append(c)
        mb = w.attr(parent, 'MarshalingBehaviorAttribute')
        if mb:
            attrs.append('marshaling_behavior(%s)' % {1: 'none', 2: 'agile', 3: 'standard'}.get(struct.unpack_from('<i', mb[0], 2)[0], 'agile'))
        for v in w.attr(parent, 'StaticAttribute'):
            s_, p = attr_string(v, 2)
            attrs.append('static(%s%s)' % (s_, self.attr_contract_tail(v, p)))
        th = w.attr(parent, 'ThreadingAttribute')
        if th:
            attrs.append('threading(%s)' % {1: 'sta', 2: 'mta', 3: 'both'}.get(struct.unpack_from('<i', th[0], 2)[0], 'both'))
        s = '    [\n%s\n    ]\n' % ',\n'.join('        ' + a for a in sorted(set(attrs), key=attrs.index))
        ext = w.md.tables[0x02][idx][3]
        s += '    runtimeclass %s' % short
        if ext[1] and w.typename_of(ext) != 'System.Object': s += ' : ' + w.typename_of(ext)
        s += '\n    {\n'
        for iface, ii in w.ifaces[idx]:
            t = w.typespec(iface)
            a = []
            if w.attr(ii, 'DefaultAttribute'): a.append('[default] ')
            if w.attr(ii, 'OverridableAttribute'): a.append('/* overridable */ ')
            if w.attr(ii, 'ProtectedAttribute'): a.append('/* protected */ ')
            cc = self.contract(ii) or (self.contract(parent) if t.kind == 'generic' else None)  # widl needs one
            if cc: a.append('[contract(%s)] ' % cc)
            s += '        %sinterface %s;\n' % (''.join(a), self.tname(t))
        return s + '    }\n'

    def attr_contract_tail(self, v, p):
        # trailing (UInt32 version, String contract) or (UInt32 version) or nothing
        if p + 4 > len(v): return ''
        ver = struct.unpack_from('<I', v, p)[0]
        p += 4
        if p < len(v) and v[p] != 0xff:
            s, _ = attr_string(v, p)
            if s and '.' in s: return ', %s, %d.%d' % (s, ver >> 16, ver & 0xffff)
        return ''

    def mattrs(self, m):
        a = []
        sem = None
        nm = m['name']
        if m['flags'] & 0x0800:  # SpecialName
            for pre, at in (('get_', 'propget'), ('put_', 'propput'), ('add_', 'eventadd'), ('remove_', 'eventremove')):
                if nm.startswith(pre): sem = at
        if 'DefaultOverloadAttribute' in m['attrs']: a.append('[default_overload] ')
        if 'OverloadAttribute' in m['attrs']:
            a.append('[overload("%s")] ' % m['name'])
        if sem: a.insert(0, '[%s] ' % sem)
        return ''.join(a)

    def mname(self, m):
        nm = m['name']
        if 'OverloadAttribute' in m['attrs']:
            return attr_string(m['attrs']['OverloadAttribute'], 2)[0]
        if m['flags'] & 0x0800:
            for pre in ('get_', 'put_', 'add_', 'remove_'):
                if nm.startswith(pre): return nm[len(pre):]
        return nm

    def params(self, m):
        ps = []
        for pn, t, pf in m['args']:
            pn = self.safe(pn)
            if t.kind == 'array':
                ps.append('[in] UINT32 __%sSize' % pn)
                d = '[out, size_is(__%sSize)]' % pn if pf & 2 else '[in, size_is(__%sSize)]' % pn
                ps.append('%s %s *%s' % (d, self.ptype(t.inner), pn))
            elif t.kind == 'byref' and t.inner.kind == 'array':
                ps.append('[out] UINT32 *__%sSize' % pn)
                ps.append('[out, size_is(, *__%sSize)] %s **%s' % (pn, self.ptype(t.inner.inner), pn))
            elif t.kind == 'byref':
                ps.append('[out] %s *%s' % (self.ptype(t.inner), pn))
            else:
                ps.append('[in] %s %s' % (self.ptype(t), pn))
        r = m['ret']
        rn = self.safe(m['retname'] or 'value')
        if rn in [self.safe(pn) for pn, _, _ in m['args']]: rn = 'result'
        if r.kind == 'array':
            ps.append('[out] UINT32 *__%sSize' % rn)
            ps.append('[out, retval, size_is(, *__%sSize)] %s **%s' % (rn, self.ptype(r.inner), rn))
        elif not (r.kind == 'prim' and r.name == 'void'):
            ps.append('[out, retval] %s *%s' % (self.ptype(r), rn))
        return ', '.join(ps)

    def safe(self, n):
        return n + '_' if n in ('value_type', 'interface', 'delegate', 'namespace', 'module', 'object', 'default', 'handle', 'context', 'string', 'size', 'event') else n


def idl_definitions(directory):
    """Full names of the WinRT types defined (not just declared) in the IDL files of a directory."""
    import glob, re
    names = {}
    tok = re.compile(r'namespace\s+([\w.]+)\s*\{|(\{)|(\})|\b(interface|runtimeclass|enum|struct)\s+(\w+)\s*(?=[:{\n])|'
                     r'\bdelegate\s+HRESULT\s+(\w+)\s*\(')
    for f in glob.glob(directory + '/*.idl'):
        text = re.sub(r'//[^\n]*|/\*.*?\*/', '', open(f, errors='replace').read(), flags=re.S)
        stack = []
        for m in tok.finditer(text):
            if m.group(1): stack.append(m.group(1))
            elif m.group(2): stack.append(None)
            elif m.group(3):
                if stack: stack.pop()
            else:
                ns = '.'.join(x for x in stack if x)
                if not ns: continue
                name = m.group(5) or m.group(6)
                names.setdefault(ns + '.' + name, f.rsplit('/', 1)[-1])
    return names


def main():
    args = sys.argv[1:]
    if not args: sys.exit(__doc__)
    md = Metadata(args.pop(0))
    have = {'Windows.Foundation.EventRegistrationToken': 'eventtoken.idl',  # defined outside a namespace
            'Windows.Foundation.HResult': 'windows.foundation.idl'}
    while args and args[0] == '--have':
        args.pop(0)
        have.update(idl_definitions(args.pop(0)))
    w = WinRT(md)
    e = Emitter(w, have)
    for n in args: e.add_full(n)
    print(e.emit())


if __name__ == '__main__':
    main()
