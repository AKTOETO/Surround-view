"""Bounded SV01 stream framing shared by CLI and integration tests."""
import json, socket, struct
PREFIX=struct.Struct('!4sHHIQI')
MAX_HEADER=65536;MAX_PAYLOAD=64*1024*1024


def pack(kind, header, payload=b''):
    metadata=json.dumps(header,allow_nan=False,separators=(',',':')).encode()
    if len(metadata)+24>MAX_HEADER or len(payload)>MAX_PAYLOAD:raise ValueError('message size limit')
    return PREFIX.pack(b'SV01',1,kind,len(metadata)+24,len(payload),0)+metadata+payload


def receive(sock):
    def exact(n):
        parts=[]
        while n:
            part=sock.recv(min(n,65536))
            if not part:raise EOFError('connection closed')
            parts.append(part);n-=len(part)
        return b''.join(parts)
    magic,version,kind,hs,ps,flags=PREFIX.unpack(exact(24))
    if magic!=b'SV01' or version!=1 or flags or not 24<=hs<=MAX_HEADER or ps>MAX_PAYLOAD:raise ValueError('invalid framing')
    return kind,json.loads(exact(hs-24)),exact(ps)


class Client:
    def __init__(self,directory,timeout=3):
        self.control=socket.socket(socket.AF_UNIX);self.control.settimeout(timeout);self.control.connect(str(directory)+'/control.sock')
        self.control.sendall(pack(1,dict(role='control')));kind,hello,_=receive(self.control)
        if kind!=2:raise ValueError('control handshake')
        self.data=socket.socket(socket.AF_UNIX);self.data.settimeout(timeout);self.data.connect(str(directory)+'/data.sock')
        self.data.sendall(pack(1,dict(role='data',session_id=hello['session_id'],data_token=hello['data_token'])));kind,_,_=receive(self.data)
        if kind!=2:raise ValueError('data handshake')
        self.command_id=0
    def command(self,kind,**parameters):
        self.command_id+=1;self.control.sendall(pack(20,dict(command_id=str(self.command_id),type=kind,**parameters)))
        kind,header,_=receive(self.control)
        if kind!=21:raise ValueError('command reply')
        return header
    def frame(self):
        kind,header,pixels=receive(self.data)
        if kind!=11 or header['pixel_format']!='RGBA8' or len(pixels)!=header['width']*header['height']*4:raise ValueError('frame format/size')
        self.data.sendall(pack(22,{k:header[k] for k in ['session_id','frame_id','buffer_token']}))
        return header,pixels
    def close(self):
        self.data.close();self.control.close()
