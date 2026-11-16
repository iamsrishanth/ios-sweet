#!/usr/bin/env python3
"""Parse and pack legacy Android boot images (header version 0).

Samsung's msm8953 ABL for the Galaxy J8 uses the legacy ANDROID! v0 header
(page size 2048, kernel_addr 0x80008000, ramdisk_addr 0x82000000,
tags_addr 0x81e00000). This tool only handles that format; it deliberately
refuses v1/v2/v3/v4 headers so that an image built here can never be mistaken
for a modern boot image.

Commands:
    parse IMAGE
    pack --kernel FILE [--ramdisk FILE] [--header-from REF] -o OUT
         [--cmdline STR] [--kernel-addr HEX] [--ramdisk-addr HEX]
         [--tags-addr HEX] [--page-size N] [--name STR]
    selftest REF

`pack --header-from REF` copies page size and load addresses from an existing
image that is known to boot on the target. `selftest` re-packs an image from its
own parsed fields and checks that kernel, ramdisk and metadata are byte-identical.

No third-party dependencies.
"""

import argparse
import hashlib
import struct
import sys

MAGIC = b"ANDROID!"
HDR_FMT = "<8s10I"
HDR_SIZE = 48
CMDLINE_OFF = 64
CMDLINE_LEN = 512
NAME_OFF = 48
NAME_LEN = 16
V0_HEADER_TOTAL = 1632  # magic..extra_cmdline, before padding to page size


def _align(n, page):
    return (n + page - 1) // page * page


def parse_image(path):
    with open(path, "rb") as f:
        data = f.read()
    if len(data) < V0_HEADER_TOTAL:
        raise SystemExit(f"{path}: too small for a v0 boot image")
    (magic, ksize, kaddr, rsize, raddr, ssize, saddr, tags, page,
     hdr_version, os_version) = struct.unpack_from(HDR_FMT, data, 0)
    if magic != MAGIC:
        raise SystemExit(f"{path}: bad magic {magic!r}")
    if hdr_version != 0:
        raise SystemExit(
            f"{path}: header version {hdr_version} is not supported; "
            "this tool only builds/parses legacy v0 images")
    if page < 2048 or page & (page - 1):
        raise SystemExit(f"{path}: invalid page size {page}")
    name = data[NAME_OFF:NAME_OFF + NAME_LEN].split(b"\0")[0].decode(
        "ascii", "replace")
    cmdline = data[CMDLINE_OFF:CMDLINE_OFF + CMDLINE_LEN].split(b"\0")[0].decode(
        "ascii", "replace")
    kernel_off = page
    ramdisk_off = kernel_off + _align(ksize, page)
    second_off = ramdisk_off + _align(rsize, page)
    return {
        "path": path,
        "page_size": page,
        "header_version": hdr_version,
        "os_version": os_version,
        "kernel_size": ksize,
        "kernel_addr": kaddr,
        "ramdisk_size": rsize,
        "ramdisk_addr": raddr,
        "second_size": ssize,
        "second_addr": saddr,
        "tags_addr": tags,
        "name": name,
        "cmdline": cmdline,
        "kernel": data[kernel_off:kernel_off + ksize],
        "ramdisk": data[ramdisk_off:ramdisk_off + rsize],
        "second": data[second_off:second_off + ssize],
        "header_raw": data[:V0_HEADER_TOTAL],
        "total_size": len(data),
        "has_avb_footer": b"AVBf" in data[-4096:],
        "has_seandroid_footer": b"SEANDROIDENFORCE" in data,
    }


def build_header(info):
    name = info["name"].encode("ascii")[:NAME_LEN - 1]
    cmdline = info["cmdline"].encode("ascii")[:CMDLINE_LEN - 1]
    # Start from the reference header when one was given, so fields this tool
    # does not model (id, extra_cmdline, vendor bytes) are preserved.
    base = info.get("header_base")
    hdr = bytearray(base if base else bytes(V0_HEADER_TOTAL))
    if len(hdr) != V0_HEADER_TOTAL:
        raise SystemExit("header_base has the wrong length")
    struct.pack_into(
        HDR_FMT, hdr, 0, MAGIC, len(info["kernel"]), info["kernel_addr"],
        len(info["ramdisk"]), info["ramdisk_addr"], len(info["second"]),
        info["second_addr"], info["tags_addr"], info["page_size"], 0,
        info["os_version"])
    hdr[NAME_OFF:NAME_OFF + NAME_LEN] = bytes(NAME_LEN)
    hdr[CMDLINE_OFF:CMDLINE_OFF + CMDLINE_LEN] = bytes(CMDLINE_LEN)
    hdr[NAME_OFF:NAME_OFF + len(name)] = name
    hdr[CMDLINE_OFF:CMDLINE_OFF + len(cmdline)] = cmdline
    return bytes(hdr)


def pack_image(info):
    page = info["page_size"]
    out = bytearray()
    out += build_header(info).ljust(page, b"\0")
    out += info["kernel"].ljust(_align(len(info["kernel"]), page), b"\0")
    out += info["ramdisk"].ljust(_align(len(info["ramdisk"]), page), b"\0")
    out += info["second"].ljust(_align(len(info["second"]), page), b"\0")
    return bytes(out)


def cmd_parse(args):
    i = parse_image(args.image)
    print(f"image            {i['path']}")
    print(f"header version   {i['header_version']}")
    print(f"page size        {i['page_size']}")
    print(f"kernel           {i['kernel_size']} bytes @ 0x{i['kernel_addr']:08x}")
    print(f"ramdisk          {i['ramdisk_size']} bytes @ 0x{i['ramdisk_addr']:08x}")
    print(f"second           {i['second_size']} bytes @ 0x{i['second_addr']:08x}")
    print(f"tags             @ 0x{i['tags_addr']:08x}")
    print(f"name             {i['name']!r}")
    print(f"cmdline          {i['cmdline']}")
    print(f"total file size  {i['total_size']}")
    print(f"AVB footer       {i['has_avb_footer']}")
    print(f"SEANDROIDENFORCE {i['has_seandroid_footer']}")
    sha = hashlib.sha256(open(args.image, "rb").read()).hexdigest()
    print(f"sha256           {sha}")
    return 0


def cmd_pack(args):
    ref = parse_image(args.header_from) if args.header_from else None
    if ref is None and (args.kernel_addr is None or args.page_size is None):
        raise SystemExit(
            "pack: pass --header-from REF, or both --kernel-addr and --page-size")
    info = {
        "page_size": args.page_size or ref["page_size"],
        "os_version": 0,
        "kernel": open(args.kernel, "rb").read(),
        "kernel_addr": int(args.kernel_addr, 0) if args.kernel_addr else ref["kernel_addr"],
        "ramdisk": open(args.ramdisk, "rb").read() if args.ramdisk else b"",
        "ramdisk_addr": int(args.ramdisk_addr, 0) if args.ramdisk_addr else (
            ref["ramdisk_addr"] if ref else 0x82000000),
        "second": b"",
        "second_addr": ref["second_addr"] if ref else 0,
        "tags_addr": int(args.tags_addr, 0) if args.tags_addr else (
            ref["tags_addr"] if ref else 0x81e00000),
        "name": args.name or "j8-probe",
        "cmdline": args.cmdline if args.cmdline is not None else (
            ref["cmdline"] if ref else ""),
        "header_base": ref["header_raw"] if ref else None,
    }
    img = pack_image(info)
    with open(args.output, "wb") as f:
        f.write(img)
    sha = hashlib.sha256(img).hexdigest()
    print(f"wrote {args.output}: {len(img)} bytes, sha256 {sha}")
    return 0


def cmd_selftest(args):
    i = parse_image(args.reference)
    i["header_base"] = i["header_raw"]
    rebuilt = pack_image(i)
    if rebuilt == open(args.reference, "rb").read():
        print(f"selftest OK: {args.reference} round-trips byte-identically")
        return 0
    # Trailing bytes (e.g. a vendor footer) are preserved only if the original
    # layout had them; report the first difference instead of guessing.
    orig = open(args.reference, "rb").read()
    for n, (a, b) in enumerate(zip(orig, rebuilt)):
        if a != b:
            raise SystemExit(f"selftest FAIL: first difference at byte {n}")
    raise SystemExit(
        f"selftest FAIL: prefix matches but lengths differ "
        f"({len(orig)} vs {len(rebuilt)})")


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("parse")
    p.add_argument("image")
    p.set_defaults(func=cmd_parse)

    p = sub.add_parser("pack")
    p.add_argument("--kernel", required=True)
    p.add_argument("--ramdisk")
    p.add_argument("--header-from")
    p.add_argument("-o", "--output", required=True)
    p.add_argument("--cmdline")
    p.add_argument("--kernel-addr")
    p.add_argument("--ramdisk-addr")
    p.add_argument("--tags-addr")
    p.add_argument("--page-size", type=int)
    p.add_argument("--name")
    p.set_defaults(func=cmd_pack)

    p = sub.add_parser("selftest")
    p.add_argument("reference")
    p.set_defaults(func=cmd_selftest)

    args = ap.parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
