#!/usr/bin/env python3
"""XBF (compiled XAML, version 2) tool for wine-uwp's Windows.UI.Xaml work.

  xbf.py dump FILE.xbf            print the metadata tables and the node streams
  xbf.py compile FILE.xaml OUT    compile a small XAML subset to XBF 2.1 (for test apps; see compile_xaml)

The format follows the MIT-licensed reader in github.com/microsoft/microsoft-ui-xaml
(dxaml/xcp/tools/XbfParser/WidgetSpinner). Built-in types and properties are referred to by "stable XBF
indexes"; the table is read from the Wine source (dlls/windows.ui.xaml/xbf_stable.h) or XBF_STABLE_H.
"""
import os, re, struct, sys

HERE = os.path.dirname(os.path.abspath(__file__))
STABLE_H = os.environ.get('XBF_STABLE_H', os.path.join(HERE, '..', 'build', 'src', 'wine', 'dlls', 'windows.ui.xaml',
                                                       'xbf_stable.h'))


def load_stable():
    text = open(STABLE_H).read()
    def table(name):
        a = text.index(name)
        return re.findall(r'"([^"]*)"', text[a:text.index('};', a)])
    return table('xbf_stable_types'), table('xbf_stable_properties')


STABLE_TYPES, STABLE_PROPS = load_stable()

NODE_NAMES = {1: 'PushScope', 2: 'PopScope', 3: 'AddNamespace', 4: 'PushConstant', 7: 'SetValue', 8: 'AddToCollection',
              9: 'AddToDictionary', 10: 'AddToDictionaryWithKey', 11: 'CheckPeerType', 12: 'SetConnectionId',
              13: 'SetName', 14: 'GetResourcePropertyBag', 15: 'SetCustomRuntimeData', 17: 'SetDeferredProperty',
              18: 'PushScopeAddNamespace', 19: 'PushScopeGetValue', 20: 'PushScopeCreateTypeBeginInit',
              21: 'PushScopeCreateTypeWithConstantBeginInit', 22: 'PushScopeCreateTypeWithTypeConvertedConstantBeginInit',
              23: 'CreateTypeBeginInit', 24: 'CreateTypeWithConstantBeginInit',
              25: 'CreateTypeWithTypeConvertedConstantBeginInit', 26: 'SetValueConstant', 27: 'SetValueTypeConvertedConstant',
              28: 'SetValueTypeConvertedResolvedProperty', 29: 'SetValueTypeConvertedResolvedType',
              30: 'SetValueFromStaticResource', 31: 'SetValueFromTemplateBinding', 32: 'SetValueFromMarkupExtension',
              33: 'EndInitPopScope', 34: 'ProvideStaticResourceValue', 35: 'ProvideThemeResourceValue',
              36: 'SetValueFromThemeResource', 38: 'BeginConditionalScope', 39: 'EndConditionalScope',
              40: 'EndInitProvideValuePopScope'}
NODE = {v: k for k, v in NODE_NAMES.items()}

C_BOOL_FALSE, C_BOOL_TRUE, C_FLOAT, C_SIGNED, C_SHARED, C_THICKNESS, C_GRIDLENGTH, C_COLOR, C_UNIQUE, C_NULL, C_ENUM = range(1, 12)


class Reader:
    def __init__(self, data, pos=0, end=None):
        self.d, self.p, self.end = data, pos, len(data) if end is None else end

    def take(self, fmt):
        v = struct.unpack_from('<' + fmt, self.d, self.p)
        self.p += struct.calcsize('<' + fmt)
        return v if len(v) > 1 else v[0]

    def u7(self):
        result = shift = 0
        while True:
            b = self.d[self.p]
            self.p += 1
            result |= (b & 0x7f) << shift
            shift += 7
            if not b & 0x80: return result

    def string(self):
        n = self.take('I')
        s = self.d[self.p:self.p + 2 * n].decode('utf-16-le')
        self.p += 2 * n
        return s


class Xbf:
    def __init__(self, data):
        if data[:4] != b'XBF\0': raise ValueError('not an XBF file')
        r = Reader(data, 4)
        self.metadata_size, self.node_size = r.take('II')
        start = r.p
        self.major, self.minor = r.take('II')
        offsets = r.take('6Q')
        r.p += 64
        nul = (self.major, self.minor) >= (2, 1)

        def vector(fn):
            return [fn() for _ in range(r.take('I'))]

        def table_string():
            s = r.string()
            if nul: r.p += 2
            return s
        assert r.p == start + offsets[0], 'string table offset'
        self.strings = vector(table_string)
        self.assemblies = vector(lambda: r.take('II'))
        self.type_namespaces = vector(lambda: r.take('II'))
        self.types = vector(lambda: r.take('III'))          # flags, namespace, name
        self.properties = vector(lambda: r.take('III'))     # flags, declaring type, name
        self.xmlns = vector(lambda: r.take('I'))
        r.p = start + self.metadata_size
        table = vector(lambda: r.take('II'))
        base = r.p
        self.streams = []
        for i, (node_off, line_off) in enumerate(table):
            end = table[i + 1][0] if i + 1 < len(table) else len(data) - base
            self.streams.append((base + node_off, base + line_off, base + end))

    def type_name(self, node):
        idx, trusted = node & 0x7fff, node & 0x8000
        if trusted: return STABLE_TYPES[idx]
        flags, ns, name = self.types[idx]
        if flags & 2: return self.strings[self.xmlns[ns]] + ':' + self.strings[name]
        if flags & 1: return 'x:' + self.strings[name]
        return self.strings[self.type_namespaces[ns][1]] + '.' + self.strings[name]

    def property_name(self, node):
        idx, trusted = node & 0x7fff, node & 0x8000
        if trusted: return STABLE_PROPS[idx]
        flags, decl, name = self.properties[idx]
        if flags & 2: return 'x:' + self.strings[name]
        if flags & 4: return self.strings[name]
        return self.type_name(decl).split('.')[-1] + '.' + self.strings[name]

    def constant(self, r):
        t = r.take('B')
        if t == C_BOOL_FALSE: return False
        if t == C_BOOL_TRUE: return True
        if t == C_FLOAT: return r.take('f')
        if t == C_SIGNED: return r.take('i')
        if t == C_SHARED: return self.strings[r.take('H') & 0x7fff]
        if t == C_THICKNESS: return ('Thickness',) + r.take('4f')
        if t == C_GRIDLENGTH: return ('GridLength',) + r.take('If')
        if t == C_COLOR: return '#%08x' % r.take('I')
        if t == C_UNIQUE: return r.string()
        if t == C_NULL: return None
        if t == C_ENUM:
            ty, v = r.take('HI')
            return ('Enum', STABLE_TYPES[ty], v)
        raise ValueError('constant type %d' % t)

    def skip_custom_data(self, r):
        """Read a custom runtime data block (it must be consumed to reach the next node); return a summary."""
        kind = r.u7()
        tok = r.u7
        sstr = lambda: r.take('H')
        vec = lambda fn: [fn() for _ in range(r.u7())]      # all vectors here have 7-bit encoded lengths
        predicates = lambda: vec(lambda: (r.take('H'), r.take('H')))
        conditional = lambda: vec(lambda: (tok(), predicates()))
        if kind in (371, 7, 10, 12):   # ResourceDictionary v1..v4
            keys = []
            if kind == 12:
                for _ in range(r.u7()):
                    k = self.strings[sstr()]; flags = r.take('Q'); t = tok()
                    keys.append((k, t, bool(flags & 1)))
                vec(sstr)
                for _ in range(r.u7()):
                    sstr(); r.take('Q'); vec(tok)
                conditional()
            else:
                keys += [(self.strings[k], t, False) for k, t in vec(lambda: (sstr(), tok()))]
                vec(sstr)
                keys += [(self.strings[k], t, True) for k, t in vec(lambda: (sstr(), tok()))]
                if kind == 371:
                    vec(sstr); vec(sstr)
                elif kind == 7:
                    vec(sstr)
                else:
                    vec(lambda: (sstr(), vec(tok)))
                    vec(lambda: (sstr(), vec(tok)))
                    conditional()
            return ('ResourceDictionary', keys)
        if kind in (2, 8, 11):          # Style v1..v3
            def setter():
                flags = r.u7()
                if not flags & 0x40:
                    if flags & 0x10: r.take('H')                 # resolved property
                    else: sstr(); r.take('H')                    # property name, declaring type
                if flags & 0x04: sstr()                          # string value
                elif flags & 0x20: self.constant(r)              # container value
                elif flags & (0x01 | 0x02 | 0x08 | 0x40): tok()  # theme/static resource, object, self
                return flags
            setters = vec(setter)
            if kind == 11: conditional()
            return ('Style', len(setters))
        if kind in (420, 1, 3, 4, 5):    # VisualStateGroupCollection v1..v5
            vec(r.u7)
            def state():
                sstr(); tok(); r.take('B')
                if kind != 420:
                    vec(tok); vec(lambda: vec(r.u7))
                    if kind != 1:
                        vec(tok); vec(tok)
                        if kind not in (3, 4): vec(tok)
            vec(state)
            vec(lambda: (sstr(), r.take('B'), tok()))
            vec(lambda: (sstr(), sstr(), tok()))
            r.take('B')
            vec(lambda: (r.u7(), r.u7(), r.u7()))
            vec(r.u7)
            tok()
            if kind in (4, 5): vec(sstr)
            return ('VisualStateGroupCollection',)
        if kind in (745, 6, 9):          # DeferredElement v1..v3
            name = self.strings[sstr()]
            if kind != 745:
                vec(lambda: (r.take('H'), self.constant(r)))
                if kind == 9: r.take('B')
            return ('DeferredElement', name)
        raise ValueError('custom runtime data type %d' % kind)

    def decode(self, data, index):
        start, end, _ = self.streams[index]
        r = Reader(data, start, end)
        out = []
        while r.p < end:
            off = r.p - start
            op = r.take('B')
            name = NODE_NAMES.get(op, 'op%d' % op)
            args = []
            if op in (3, 18):
                ns = r.take('H'); args = [r.string(), self.strings[self.xmlns[ns & 0x7fff]]]
            elif op in (4, 10, 12, 13, 14, 34, 35):
                args = [self.constant(r)]
            elif op in (7, 32, 19):
                args = [self.property_name(r.take('H'))]
            elif op == 11:
                args = [r.string()]
            elif op == 17:
                prop = self.property_name(r.take('H')); sub = r.u7(); ns, nt = r.u7(), r.u7()
                res = [self.strings[r.take('H') & 0x7fff] for _ in range(ns + nt)]
                args = [prop, 'stream %d' % sub, res]
            elif op == 15:
                sub = r.u7(); ns, nt = r.u7(), r.u7()
                for _ in range(ns + nt): r.take('H')
                args = ['stream %d' % sub, self.skip_custom_data(r)]
            elif op in (20, 23):
                args = [self.type_name(r.take('H'))]
            elif op in (21, 22, 24, 25):
                args = [self.type_name(r.take('H')), self.constant(r)]
            elif op in (26, 27, 30, 36):
                args = [self.property_name(r.take('H')), self.constant(r)]
            elif op == 29:
                args = [self.property_name(r.take('H')), self.type_name(r.take('H'))]
            elif op in (28, 31):
                args = [self.property_name(r.take('H')), self.property_name(r.take('H'))]
            elif op == 38:
                args = [self.type_name(r.take('H')), self.strings[r.take('H') & 0x7fff]]
            out.append((off, name, args))
        return out


def dump(path):
    data = open(path, 'rb').read()
    x = Xbf(data)
    print('XBF %d.%d: %d strings, %d types, %d properties, %d xmlns, %d streams' % (
        x.major, x.minor, len(x.strings), len(x.types), len(x.properties), len(x.xmlns), len(x.streams)))
    for i in range(len(x.streams)):
        print('[Stream %d]' % i)
        depth = 0
        for off, name, args in x.decode(data, i):
            if name in ('PopScope', 'EndInitPopScope', 'EndInitProvideValuePopScope'): depth -= 1
            print('%5d %s%s %s' % (off, '  ' * max(depth, 0), name, ', '.join(repr(a) for a in args) if args else ''))
            if name.startswith('PushScope'): depth += 1


# ---------------------------------------------------------------- a small XAML -> XBF compiler for test apps

PRESENTATION = 'http://schemas.microsoft.com/winfx/2006/xaml/presentation'
XAML = 'http://schemas.microsoft.com/winfx/2006/xaml'
# content properties of the built-in types the tests use
CONTENT = {'Grid': 'Panel_Children', 'StackPanel': 'Panel_Children', 'Border': 'Border_Child',
           'Button': 'ContentControl_Content', 'Page': 'UserControl_Content', 'UserControl': 'UserControl_Content',
           'Frame': 'ContentControl_Content'}
COLLECTION_CONTENT = {'Panel_Children'}
ENUMS = {'HorizontalAlignment': ['Left', 'Center', 'Right', 'Stretch'], 'VerticalAlignment': ['Top', 'Center', 'Bottom', 'Stretch'],
         'Orientation': ['Vertical', 'Horizontal'], 'Visibility': ['Visible', 'Collapsed'],
         'TextWrapping': [None, 'NoWrap', 'Wrap', 'WrapWholeWords'], 'TextAlignment': ['Center', 'Left', 'Right', 'Justify'],
         'ApplicationTheme': ['Light', 'Dark'], 'ElementTheme': ['Default', 'Light', 'Dark']}
NAMED_COLORS = {'Red': 0xffff0000, 'Green': 0xff008000, 'Blue': 0xff0000ff, 'White': 0xffffffff, 'Black': 0xff000000,
                'Yellow': 0xffffff00, 'Transparent': 0x00ffffff, 'Gray': 0xff808080, 'Lime': 0xff00ff00}


def compile_xaml(src, out):
    """Compile a small XAML subset to XBF 2.1 the way Microsoft's compiler lays it out: built-in types and properties
    by stable index, x:Class types through the type table, Click handlers and x:Name through connection ids,
    {StaticResource} as SetValueFromStaticResource, Application/FrameworkElement.Resources as a ResourceDictionary
    custom-runtime-data block with its entries in a second node stream. Good enough for wine-uwp's test apps."""
    import xml.etree.ElementTree as ET
    root = ET.parse(src).getroot()
    strings, types, props, xmlns_list, type_ns, assemblies = [], [], [], [], [], []

    def s(v):
        if v not in strings: strings.append(v)
        return strings.index(v)
    stable_type = {n: i for i, n in enumerate(STABLE_TYPES) if n}
    stable_prop = {n: i for i, n in enumerate(STABLE_PROPS) if n}

    def type_ref(ns, name):
        if ns == PRESENTATION:
            return 0x8000 | stable_type[name]
        # a custom type, "using:Namespace"
        assert ns.startswith('using:'), ns
        nsname = ns[len('using:'):]
        if not assemblies: assemblies.append((2, s('')))   # tpkManaged-like: the app's own metadata provider
        key = (0, s(nsname))
        if key not in type_ns: type_ns.append(key)
        entry = (0, type_ns.index(key), s(name))
        if entry not in types: types.append(entry)
        return types.index(entry)

    def prop_ref(type_name, name):
        cand = ['%s_%s' % (type_name, name)] + ['%s_%s' % (b, name) for b in
                ('TextBlock', 'Panel', 'Border', 'Control', 'FrameworkElement', 'UIElement', 'ContentControl',
                 'UserControl', 'Page', 'Application', 'RowDefinition', 'ColumnDefinition', 'SolidColorBrush', 'Grid', 'StackPanel',
                 'ButtonBase', 'DependencyObject')]
        for c in cand:
            if c in stable_prop: return 0x8000 | stable_prop[c], c
        raise KeyError('no stable property for %s.%s' % (type_name, name))

    streams = [bytearray(), bytearray()]   # main, resources
    conn = [0]
    connections = []

    def w(stream, fmt, *v):
        stream.extend(struct.pack('<' + fmt, *v))

    def u7(stream, v):
        while True:
            b = v & 0x7f
            v >>= 7
            stream.append(b | (0x80 if v else 0))
            if not v: break

    def ustring(stream, v):
        w(stream, 'I', len(v)); stream.extend(v.encode('utf-16-le'))

    def constant(stream, value, prop):
        short = prop.split('_')[-1]
        enum_type = {'HorizontalAlignment': 'HorizontalAlignment', 'VerticalAlignment': 'VerticalAlignment',
                     'Orientation': 'Orientation', 'Visibility': 'Visibility', 'TextWrapping': 'TextWrapping',
                     'TextAlignment': 'TextAlignment', 'RequestedTheme': 'ApplicationTheme' if prop.startswith('Application') else 'ElementTheme',
                     'HorizontalContentAlignment': 'HorizontalAlignment', 'VerticalContentAlignment': 'VerticalAlignment'}.get(short)
        if enum_type:
            w(stream, 'BHI', C_ENUM, stable_type[enum_type], ENUMS[enum_type].index(value)); return
        if short in ('Margin', 'Padding', 'BorderThickness'):
            v = [float(x) for x in value.split(',')]
            v = v * 4 if len(v) == 1 else (v * 2 if len(v) == 2 else v)
            w(stream, 'B4f', C_THICKNESS, *v); return
        if short in ('Height', 'Width') and prop.split('_')[0] in ('RowDefinition', 'ColumnDefinition'):
            if value == 'Auto': w(stream, 'BIf', C_GRIDLENGTH, 0, 1.0)
            elif value.endswith('*'): w(stream, 'BIf', C_GRIDLENGTH, 2, float(value[:-1] or 1))
            else: w(stream, 'BIf', C_GRIDLENGTH, 1, float(value))
            return
        if short in ('FontSize', 'Width', 'Height', 'Opacity'):
            w(stream, 'Bf', C_FLOAT, float(value)); return
        if short in ('Row', 'Column', 'RowSpan', 'ColumnSpan'):
            w(stream, 'Bi', C_SIGNED, int(value)); return
        if short in ('IsEnabled',):
            w(stream, 'B', C_BOOL_TRUE if value.lower() == 'true' else C_BOOL_FALSE); return
        if short in ('Color', 'Foreground', 'Background', 'BorderBrush'):
            c = NAMED_COLORS.get(value)
            if c is None:
                h = value.lstrip('#'); c = int(h, 16) | (0xff000000 if len(h) == 6 else 0)
            w(stream, 'BI', C_COLOR, c); return
        w(stream, 'BH', C_SHARED, s(value))

    def element(stream, el, is_root, resources_stream):
        ns, name = el.tag[1:].split('}')
        attrs = dict(el.attrib)
        xclass = attrs.pop('{%s}Class' % XAML, None)
        if is_root:
            w(stream, 'BH', NODE['PushScopeAddNamespace'], xmlns(PRESENTATION)); ustring(stream, '')
            w(stream, 'BH', NODE['AddNamespace'], xmlns(XAML)); ustring(stream, 'x')
            if xclass:
                w(stream, 'B', NODE['CheckPeerType']); ustring(stream, xclass)
            w(stream, 'BH', NODE['CreateTypeBeginInit'], type_ref(ns, name))
        else:
            w(stream, 'BH', NODE['PushScopeCreateTypeBeginInit'], type_ref(ns, name))
        xname = attrs.pop('{%s}Name' % XAML, None) or attrs.pop('Name', None)
        if xname:
            w(stream, 'BBH', NODE['SetName'], C_SHARED, s(xname))
        events = {k: attrs.pop(k) for k in list(attrs) if k == 'Click'}
        if xname or events:
            conn[0] += 1
            w(stream, 'BBi', NODE['SetConnectionId'], C_SIGNED, conn[0])
            connections.append((conn[0], xname, events))
        for key, value in attrs.items():
            if key.startswith('{'): continue
            owner, _, pname = key.rpartition('.')
            pref, pfull = prop_ref(owner or name, pname)
            m = re.match(r'\{StaticResource (\w+)\}', value)
            m2 = re.match(r'\{ThemeResource (\w+)\}', value)
            if m: w(stream, 'BHBH', NODE['SetValueFromStaticResource'], pref, C_SHARED, s(m.group(1)))
            elif m2: w(stream, 'BHBH', NODE['SetValueFromThemeResource'], pref, C_SHARED, s(m2.group(1)))
            else:
                w(stream, 'BH', NODE['SetValueTypeConvertedConstant'], pref); constant(stream, value, pfull)
        content = []
        for child in el:
            cns, cname = child.tag[1:].split('}')
            if '.' in cname:     # property element
                owner, pname = cname.split('.')
                if pname == 'Resources':
                    resource_dictionary(stream, owner, child, resources_stream)
                    continue
                pref, pfull = prop_ref(owner, pname)
                if pfull in ('Grid_RowDefinitions', 'Grid_ColumnDefinitions'):
                    w(stream, 'BH', NODE['PushScopeGetValue'], pref)
                    for item in child:
                        element(stream, item, False, resources_stream); w(stream, 'B', NODE['AddToCollection'])
                    w(stream, 'B', NODE['PopScope'])
                    continue
                for item in child:
                    element(stream, item, False, resources_stream)
                    w(stream, 'BH', NODE['SetValue'], pref)
                continue
            content.append(child)
        if content or (el.text and el.text.strip()):
            prop = CONTENT[name]
            pref = 0x8000 | stable_prop[prop]
            if prop in COLLECTION_CONTENT:
                w(stream, 'BH', NODE['PushScopeGetValue'], pref)
                for c in content:
                    element(stream, c, False, resources_stream); w(stream, 'B', NODE['AddToCollection'])
                w(stream, 'B', NODE['PopScope'])
            elif content:
                element(stream, content[0], False, resources_stream); w(stream, 'BH', NODE['SetValue'], pref)
            else:
                w(stream, 'BH', NODE['SetValueConstant'], pref); w(stream, 'BH', C_SHARED, s(el.text.strip()))
        w(stream, 'B', NODE['EndInitPopScope'])

    def resource_dictionary(stream, owner, prop_el, res):
        pref, _ = prop_ref(owner, 'Resources')
        w(stream, 'BH', NODE['PushScopeCreateTypeBeginInit'], 0x8000 | stable_type['ResourceDictionary'])
        entries = []
        for item in prop_el:
            key = item.attrib.pop('{%s}Key' % XAML)
            entries.append((key, len(res)))
            element(res, item, False, res)
        w(stream, 'B', NODE['SetCustomRuntimeData']); u7(stream, 1); u7(stream, 0); u7(stream, 0)
        u7(stream, 7)                                    # ResourceDictionary_v2
        u7(stream, len(entries))
        for key, off in entries:
            w(stream, 'H', s(key)); u7(stream, off)
        u7(stream, 0)                                    # resources with x:Name
        u7(stream, 0)                                    # implicit-key resources
        u7(stream, 0)                                    # (v2 trailer)
        w(stream, 'B', NODE['EndInitPopScope'])
        w(stream, 'BH', NODE['SetValue'], pref)

    def xmlns(uri):
        if s(uri) not in xmlns_list: xmlns_list.append(s(uri))
        return xmlns_list.index(s(uri))

    element(streams[0], root, True, streams[1])
    if not streams[1]: streams.pop()

    meta = bytearray()
    def vec(items, fn):
        meta.extend(struct.pack('<I', len(items)))
        for it in items: fn(it)
    header_size = 8 + 6 * 8 + 64
    offs = []
    body = bytearray()
    def mark(): offs.append(header_size + len(body))
    # string table (2.1: null terminated)
    tmp = meta; meta = body
    mark(); vec(strings, lambda v: (meta.extend(struct.pack('<I', len(v)) + v.encode('utf-16-le') + b'\0\0')))
    mark(); vec(assemblies, lambda v: meta.extend(struct.pack('<II', *v)))
    mark(); vec(type_ns, lambda v: meta.extend(struct.pack('<II', *v)))
    mark(); vec(types, lambda v: meta.extend(struct.pack('<III', *v)))
    mark(); vec(props, lambda v: meta.extend(struct.pack('<III', *v)))
    mark(); vec(xmlns_list, lambda v: meta.extend(struct.pack('<I', v)))
    metadata = struct.pack('<II6Q', 2, 1, *offs) + bytes(64) + bytes(body)
    table = bytearray(struct.pack('<I', len(streams)))
    blob = bytearray()
    for st in streams:
        node_off = len(blob); blob.extend(st); line_off = len(blob)
        table.extend(struct.pack('<II', node_off, line_off))
    nodes = bytes(table) + bytes(blob)
    with open(out, 'wb') as f:
        f.write(b'XBF\0' + struct.pack('<II', len(metadata), len(nodes)) + metadata + nodes)
    return connections


def main():
    if len(sys.argv) >= 3 and sys.argv[1] == 'dump':
        dump(sys.argv[2])
    elif len(sys.argv) == 4 and sys.argv[1] == 'compile':
        for cid, name, events in compile_xaml(sys.argv[2], sys.argv[3]):
            print('connection %d: name %s, events %s' % (cid, name, events))
    else:
        sys.exit(__doc__)


if __name__ == '__main__':
    main()
