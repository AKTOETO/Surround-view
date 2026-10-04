#!/usr/bin/env python3
"""Headless control/capture client. Desktop interactive UI is build/sv-client."""
import argparse,json,time
from pathlib import Path
from ipc import Client
p=argparse.ArgumentParser();p.add_argument('--ipc-dir',type=Path,required=True);p.add_argument('--output',type=Path,default=Path('artifacts/capture'));p.add_argument('--preset',choices=['top','front','rear'],default='top');a=p.parse_args()
a.output.mkdir(parents=True,exist_ok=True);c=Client(a.ipc_dir)
try:
    ack=c.command('preset',name=a.preset)
    for _ in range(20):
        header,pixels=c.frame()
        if int(header['state_revision'])>=int(ack['state_revision']):break
    rgb=bytearray()
    for i in range(0,len(pixels),4):rgb.extend(pixels[i:i+3])
    (a.output/'frame.ppm').write_bytes(f"P6\n{header['width']} {header['height']}\n255\n".encode()+rgb)
    header['cli_receive_timestamp_ns']=str(time.monotonic_ns());(a.output/'frame.json').write_text(json.dumps(header,indent=2)+'\n');print(json.dumps(header))
finally:c.close()
