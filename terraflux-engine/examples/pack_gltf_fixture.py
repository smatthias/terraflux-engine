#!/usr/bin/env python3
"""Package a .gltf with one embedded base64 geometry buffer as GLB 2.0.

This is a reproducible integration fixture generator, not a general-purpose
GLB packer. Images may remain data URIs in the JSON chunk.
"""
import base64
import json
import struct
import sys
from pathlib import Path

def make_glb(source: Path, target: Path) -> None:
    document=json.loads(source.read_text())
    buffers=document.get("buffers",[])
    if len(buffers)!=1 or not buffers[0].get("uri","").startswith(
            "data:application/octet-stream;base64,"):
        raise ValueError("fixture requires one embedded geometry buffer")
    geometry=base64.b64decode(buffers[0].pop("uri").split(",",1)[1],validate=True)
    if len(geometry)!=buffers[0]["byteLength"]:
        raise ValueError("source geometry length differs from glTF byteLength")
    encoded=json.dumps(document,separators=(",",":")).encode()
    encoded+=b" " * (-len(encoded)%4)
    geometry+=b"\0" * (-len(geometry)%4)
    length=12+8+len(encoded)+8+len(geometry)
    target.write_bytes(
        struct.pack("<III",0x46546c67,2,length)
        +struct.pack("<II",len(encoded),0x4e4f534a)+encoded
        +struct.pack("<II",len(geometry),0x004e4942)+geometry)
if __name__=="__main__":
    if len(sys.argv)!=3:
        raise SystemExit("usage: python examples/pack_gltf_fixture.py input.gltf output.glb")
    make_glb(Path(sys.argv[1]),Path(sys.argv[2]))
