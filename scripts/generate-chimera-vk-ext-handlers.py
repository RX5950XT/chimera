#!/usr/bin/env python3
"""Generate missing gfxstream Vulkan pNext extension-struct handlers.

This tree is a mixed-generation snapshot: vulkan_core.h and the extension size
table (goldfish_vk_extension_structs.cpp) know ~647 struct types, but the
generated marshaling switches only know ~228. Any known-size struct without a
switch case hits `default: abort()` and kills the emulator (GravityMark et al.
chain such structs into vkGetPhysicalDeviceFeatures2/Properties2).

We synthesize handlers for every missing struct whose fields are all plain
scalars / fixed arrays (no pointers, unions, or handles) - which covers every
VkPhysicalDevice*Features/Properties query struct. Structs with pointers keep
the abort (they are not blind-query material).

Idempotent: skips a file if the autogen marker is already present.
Usage: python generate-chimera-vk-ext-handlers.py <gfxstream-source-root>
"""
import re
import sys
import os

MARKER = "chimera-vk-ext-autogen"

POD_STRUCTS = {  # padding-free nested POD structs -> byte size
    "VkExtent2D": 8, "VkExtent3D": 12, "VkOffset2D": 8, "VkOffset3D": 12,
    "VkConformanceVersion": 4,
}
SIZE4 = {"VkBool32", "uint32_t", "int32_t", "float", "VkFlags"}
SIZE8 = {"uint64_t", "int64_t", "double", "VkDeviceSize", "VkDeviceAddress", "VkFlags64"}
BYTE1 = {"char", "uint8_t", "int8_t"}


def read(p):
    with open(p, encoding="utf-8") as f:
        return f.read()


def write(p, s):
    old = read(p) if os.path.exists(p) else None
    if old == s:
        return False
    with open(p, "w", encoding="utf-8", newline="\n") as f:
        f.write(s)
    return True


def strip_beta(header_src):
    """Drop #ifdef VK_ENABLE_BETA_EXTENSIONS regions (enums/structs there are
    not declared in a normal build), tracking nested #if levels."""
    out = []
    depth = 0
    for line in header_src.splitlines(keepends=True):
        s = line.strip()
        if depth:
            if s.startswith(("#if", "#ifdef", "#ifndef")):
                depth += 1
            elif s.startswith("#endif"):
                depth -= 1
            continue
        if s == "#ifdef VK_ENABLE_BETA_EXTENSIONS":
            depth = 1
            continue
        out.append(line)
    return "".join(out)


def parse_header(header_src):
    enums = set(re.findall(r"typedef enum (Vk\w+) \{", header_src))
    flags32 = set(re.findall(r"typedef VkFlags (Vk\w+);", header_src))
    flags64 = set(re.findall(r"typedef VkFlags64 (Vk\w+);", header_src))
    defines = {m[0]: int(m[1]) for m in
               re.findall(r"#define (VK_\w+)\s+(\d+)U?\b", header_src)}
    structs = {}
    for m in re.finditer(r"typedef struct (Vk\w+) \{(.*?)\} \1;", header_src, re.S):
        name, body = m.group(1), m.group(2)
        fields = []
        ok = True
        for line in body.splitlines():
            line = line.strip().rstrip(";")
            if not line:
                continue
            fm = re.match(r"(?:const\s+)?([A-Za-z_]\w+)\s*(\**)\s*(\w+)(?:\[(\w+)\])?$", line)
            if not fm:
                ok = False
                break
            ftype, ptr, fname, arr = fm.groups()
            if ptr:
                if fname == "pNext":
                    continue
                ok = False
                break
            if fname == "sType" and ftype == "VkStructureType":
                continue
            if arr is not None:
                arr = defines.get(arr, None) if not arr.isdigit() else int(arr)
                if arr is None:
                    ok = False
                    break
            fields.append((ftype, fname, arr))
        if ok:
            structs[name] = fields
    return enums, flags32, flags64, structs


def field_size(ftype, enums, flags32, flags64):
    if ftype in SIZE4 or ftype in enums or ftype in flags32:
        return 4, False
    if ftype in SIZE8 or ftype in flags64:
        return 8, False
    if ftype in BYTE1:
        return 1, False
    if ftype in POD_STRUCTS:
        return POD_STRUCTS[ftype], False
    if ftype == "size_t":
        return 8, True  # BE64 on the wire
    return None, False


def gen_for_struct(sname, fields, enums, flags32, flags64):
    """Return (reserved_fn, marshal_fn, unmarshal_fn, deepcopy_fn) or None if unsupported."""
    res_lines, mar_lines, unm_lines = [], [], []
    for ftype, fname, arr in fields:
        sz, is_size_t = field_size(ftype, enums, flags32, flags64)
        if sz is None:
            return None
        if is_size_t:
            if arr is not None:
                return None
            res_lines.append(
                f"    memcpy((size_t*)&forUnmarshaling->{fname}, (*ptr), 8);\n"
                f"    gfxstream::Stream::fromBe64((uint8_t*)&forUnmarshaling->{fname});\n"
                f"    *ptr += 8;")
            mar_lines.append(f"    vkStream->putBe64(forMarshaling->{fname});")
            unm_lines.append(f"    forUnmarshaling->{fname} = (size_t)vkStream->getBe64();")
            continue
        if arr is not None:
            total = f"{arr} * {sz}"
            res_lines.append(
                f"    memcpy((void*)forUnmarshaling->{fname}, *ptr, {total});\n"
                f"    *ptr += {total};")
            mar_lines.append(f"    vkStream->write((const void*)forMarshaling->{fname}, {total});")
            unm_lines.append(f"    vkStream->read((void*)forUnmarshaling->{fname}, {total});")
        else:
            res_lines.append(
                f"    memcpy((void*)&forUnmarshaling->{fname}, *ptr, {sz});\n"
                f"    *ptr += {sz};")
            mar_lines.append(f"    vkStream->write((const void*)&forMarshaling->{fname}, {sz});")
            unm_lines.append(f"    vkStream->read((void*)&forUnmarshaling->{fname}, {sz});")

    pnext_reserved = """    uint32_t pNext_size;
    memcpy((uint32_t*)&pNext_size, *ptr, sizeof(uint32_t));
    gfxstream::Stream::fromBe32((uint8_t*)&pNext_size);
    *ptr += sizeof(uint32_t);
    forUnmarshaling->pNext = nullptr;
    if (pNext_size) {
        vkStream->alloc((void**)&forUnmarshaling->pNext, sizeof(VkStructureType));
        memcpy((void*)forUnmarshaling->pNext, *ptr, sizeof(VkStructureType));
        *ptr += sizeof(VkStructureType);
        VkStructureType extType = *(VkStructureType*)(forUnmarshaling->pNext);
        vkStream->alloc((void**)&forUnmarshaling->pNext,
                        goldfish_vk_extension_struct_size_with_stream_features(
                            vkStream->getFeatureBits(), rootType, forUnmarshaling->pNext));
        *(VkStructureType*)forUnmarshaling->pNext = extType;
        reservedunmarshal_extension_struct(vkStream, rootType, (void*)(forUnmarshaling->pNext),
                                           ptr);
    }"""
    reserved_fn = f"""void reservedunmarshal_{sname}(
    VulkanStream* vkStream, VkStructureType rootType,
    {sname}* forUnmarshaling, uint8_t** ptr) {{
    memcpy((VkStructureType*)&forUnmarshaling->sType, *ptr, sizeof(VkStructureType));
    *ptr += sizeof(VkStructureType);
    if (rootType == VK_STRUCTURE_TYPE_MAX_ENUM) {{
        rootType = forUnmarshaling->sType;
    }}
{pnext_reserved}
{chr(10).join(res_lines)}
}}
"""
    marshal_fn = f"""void marshal_{sname}(
    VulkanStream* vkStream, VkStructureType rootType,
    const {sname}* forMarshaling) {{
    (void)rootType;
    vkStream->write((VkStructureType*)&forMarshaling->sType, sizeof(VkStructureType));
    if (rootType == VK_STRUCTURE_TYPE_MAX_ENUM) {{
        rootType = forMarshaling->sType;
    }}
    marshal_extension_struct(vkStream, rootType, forMarshaling->pNext);
{chr(10).join(mar_lines)}
}}
"""
    unmarshal_fn = f"""void unmarshal_{sname}(
    VulkanStream* vkStream, VkStructureType rootType,
    {sname}* forUnmarshaling) {{
    (void)rootType;
    vkStream->read((VkStructureType*)&forUnmarshaling->sType, sizeof(VkStructureType));
    if (rootType == VK_STRUCTURE_TYPE_MAX_ENUM) {{
        rootType = forUnmarshaling->sType;
    }}
    size_t pNext_size;
    pNext_size = vkStream->getBe32();
    forUnmarshaling->pNext = nullptr;
    if (pNext_size) {{
        vkStream->alloc((void**)&forUnmarshaling->pNext, sizeof(VkStructureType));
        vkStream->read((void*)forUnmarshaling->pNext, sizeof(VkStructureType));
        VkStructureType extType = *(VkStructureType*)(forUnmarshaling->pNext);
        vkStream->alloc((void**)&forUnmarshaling->pNext,
                        goldfish_vk_extension_struct_size_with_stream_features(
                            vkStream->getFeatureBits(), rootType, forUnmarshaling->pNext));
        *(VkStructureType*)forUnmarshaling->pNext = extType;
        unmarshal_extension_struct(vkStream, rootType, (void*)(forUnmarshaling->pNext));
    }}
{chr(10).join(unm_lines)}
}}
"""
    deepcopy_fn = f"""void deepcopy_{sname}(
    Allocator* alloc, VkStructureType rootType,
    const {sname}* from, {sname}* to) {{
    (void)alloc;
    (void)rootType;
    *to = *from;
    if (rootType == VK_STRUCTURE_TYPE_MAX_ENUM) {{
        rootType = from->sType;
    }}
    const void* from_pNext = from;
    size_t pNext_size = 0u;
    while (!pNext_size && from_pNext) {{
        from_pNext = static_cast<const VkBaseOutStructure*>(from_pNext)->pNext;
        pNext_size = goldfish_vk_extension_struct_size(rootType, from_pNext);
    }}
    to->pNext = nullptr;
    if (pNext_size) {{
        to->pNext = alloc->alloc(pNext_size);
        deepcopy_extension_struct(alloc, rootType, from_pNext, (void*)(to->pNext));
    }}
}}
"""
    return reserved_fn, marshal_fn, unmarshal_fn, deepcopy_fn


def switch_cases(enum, sname, kind):
    if kind == "reserved":
        call = (f"            reservedunmarshal_{sname}(\n"
                f"                vkStream, rootType,\n"
                f"                reinterpret_cast<{sname}*>(structExtension_out), ptr);")
    elif kind == "marshal":
        call = (f"            marshal_{sname}(\n"
                f"                vkStream, rootType,\n"
                f"                reinterpret_cast<const {sname}*>(structExtension));")
    elif kind == "unmarshal":
        call = (f"            unmarshal_{sname}(\n"
                f"                vkStream, rootType,\n"
                f"                reinterpret_cast<{sname}*>(structExtension_out));")
    else:
        call = (f"            deepcopy_{sname}(\n"
                f"                alloc, rootType,\n"
                f"                reinterpret_cast<const {sname}*>(structExtension),\n"
                f"                reinterpret_cast<{sname}*>(structExtension_out));")
    return f"        case {enum}: {{\n{call}\n            break;\n        }}\n"


def insert_before(text, anchor_idx, block):
    return text[:anchor_idx] + block + text[anchor_idx:]


def outer_default_idx(text, fn_def_idx):
    """Index of the abort `default:` of the top-level switch in the function.

    The generated extension-struct switches contain NESTED `switch (rootType)`
    blocks with their own `default:` labels, so "first default after the
    function" lands inside a nested switch (dead code). The outer switch's
    default is the LAST one before the function's closing brace at column 0.
    """
    fn_end = text.index("\n}\n", fn_def_idx)
    idx = text.rfind("        default: {", fn_def_idx, fn_end)
    if idx < 0:
        raise RuntimeError("outer default not found")
    return idx


def strip_blocks(text):
    return re.sub(rf"// {MARKER} [\w-]+ BEGIN\n.*?// {MARKER} [\w-]+ END\n", "", text, flags=re.S)


def normalize(name):
    return name.replace("_", "").upper()


def main():
    root = sys.argv[1]
    common = os.path.join(root, "host", "vulkan", "cereal", "common")
    ext_path = os.path.join(common, "goldfish_vk_extension_structs.cpp")
    res_path = os.path.join(common, "goldfish_vk_reserved_marshaling.cpp")
    mar_path = os.path.join(common, "goldfish_vk_marshaling.cpp")
    dep_path = os.path.join(common, "goldfish_vk_deepcopy.cpp")
    header = os.path.join(root, "third_party", "vulkan", "include", "vulkan", "vulkan_core.h")

    header_src = strip_beta(read(header))
    ext = strip_blocks(read(ext_path))
    res = strip_blocks(read(res_path))
    mar = strip_blocks(read(mar_path))
    dep = strip_blocks(read(dep_path))

    enums_e, flags32, flags64, structs = parse_header(header_src)

    # Authoritative enum<->struct mapping straight from the header: normalize
    # VK_STRUCTURE_TYPE_X and VkX by dropping underscores/case. The size table
    # itself is incomplete (e.g. VK_KHR_ray_tracing_pipeline missing), so it
    # cannot be the mapping source.
    stype_enums = re.findall(r"(VK_STRUCTURE_TYPE_\w+?) =", header_src)
    norm_to_enum = {}
    for enum in stype_enums:
        key = normalize(enum[len("VK_STRUCTURE_TYPE_"):])
        # Keep the first spelling from vulkan_core.h. Deprecated aliases appear
        # later and must not make generated output depend on PYTHONHASHSEED.
        norm_to_enum.setdefault(key, enum)
    enum_to_struct = {}
    for sname in structs:
        e = norm_to_enum.get(normalize(sname[2:]))
        if e:
            enum_to_struct[e] = sname

    # Universe: every mapped header struct whose fields pass the POD filter.
    gen = {}
    skipped = []
    for enum, sname in sorted(enum_to_struct.items()):
        out = gen_for_struct(sname, structs[sname], enums_e, flags32, flags64)
        if out is None:
            skipped.append(sname)
        else:
            gen[enum] = (sname, out)

    def block(items, tag):
        return (f"// {MARKER} {tag} BEGIN\n" + "".join(items) + f"// {MARKER} {tag} END\n")

    # --- size table: add cases for known-POD structs it does not size yet ---
    def add_size_cases(text, fn_sig, tag):
        fd = text.index(fn_sig)
        fn_end = text.index("\n}\n", fd)
        have = set(re.findall(r"case (VK_STRUCTURE_TYPE_\w+):", text[fd:fn_end]))
        items = [f"        case {e}: {{\n            return sizeof({gen[e][0]});\n        }}\n"
                 for e in gen if e not in have]
        if not items:
            return text, 0
        return insert_before(text, outer_default_idx(text, fd), block(items, tag)), len(items)

    ext, n1 = add_size_cases(ext, "size_t goldfish_vk_extension_struct_size(", "size-cases")
    ext, n2 = add_size_cases(
        ext, "size_t goldfish_vk_extension_struct_size_with_stream_features(",
        "size-cases-stream")
    write(ext_path, ext)

    # --- reserved marshaling file ---
    # Cases: whatever the extension switch lacks. Functions: only if the file
    # does not already define one (root/nested structs already have handlers,
    # they were just never wired into the extension switch).
    res_def = res.rindex("void reservedunmarshal_extension_struct(")
    res_have = set(re.findall(r"case (VK_STRUCTURE_TYPE_\w+):", res[res_def:]))
    res_missing = [e for e in gen if e not in res_have]
    res_need_fn = [e for e in res_missing
                   if f"void reservedunmarshal_{gen[e][0]}(" not in res]
    fns = block([gen[e][1][0] for e in res_need_fn], "fns")
    res = insert_before(res, res.rindex("void reservedunmarshal_extension_struct("), fns)
    res_def = res.rindex("void reservedunmarshal_extension_struct(")
    cases = block([switch_cases(e, gen[e][0], "reserved") for e in res_missing], "cases")
    res = insert_before(res, outer_default_idx(res, res_def), cases)
    write(res_path, res)

    # --- regular marshaling file ---
    mdef = mar.rindex("void marshal_extension_struct(")
    mar_have = set(re.findall(r"case (VK_STRUCTURE_TYPE_\w+):",
                              mar[mdef:mar.index("\n}\n", mdef)]))
    mar_missing = [e for e in gen if e not in mar_have]
    mar_need_fn = [e for e in mar_missing if f"void marshal_{gen[e][0]}(" not in mar]
    fns = block([gen[e][1][1] + "\n" + gen[e][1][2] for e in mar_need_fn], "fns")
    mar = insert_before(mar, mdef, fns)
    mdef = mar.rindex("void marshal_extension_struct(")
    mcases = block([switch_cases(e, gen[e][0], "marshal") for e in mar_missing], "marshal-cases")
    mar = insert_before(mar, outer_default_idx(mar, mdef), mcases)
    udef = mar.rindex("void unmarshal_extension_struct(")
    ucases = block([switch_cases(e, gen[e][0], "unmarshal") for e in mar_missing], "unmarshal-cases")
    mar = insert_before(mar, outer_default_idx(mar, udef), ucases)
    write(mar_path, mar)

    # --- deepcopy file ---
    ddef = dep.rindex("void deepcopy_extension_struct(")
    dep_have = set(re.findall(r"case (VK_STRUCTURE_TYPE_\w+):", dep[ddef:]))
    dep_missing = [e for e in gen if e not in dep_have]
    dep_need_fn = [e for e in dep_missing if f"void deepcopy_{gen[e][0]}(" not in dep]
    fns = block([gen[e][1][3] for e in dep_need_fn], "deepcopy-fns")
    dep = insert_before(dep, ddef, fns)
    ddef = dep.rindex("void deepcopy_extension_struct(")
    dcases = block([switch_cases(e, gen[e][0], "deepcopy") for e in dep_missing], "deepcopy-cases")
    dep = insert_before(dep, outer_default_idx(dep, ddef), dcases)
    write(dep_path, dep)

    print(f"chimera-vk-ext-autogen: universe {len(gen)} POD structs; "
          f"size+{n1}/+{n2}, reserved+{len(res_missing)}, "
          f"marshal+{len(mar_missing)}, deepcopy+{len(dep_missing)}; "
          f"skipped {len(skipped)} (pointer/unknown fields)")


if __name__ == "__main__":
    main()
