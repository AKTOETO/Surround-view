#!/usr/bin/env python3
"""Independent analytic four-camera fixture; image rows start at top left."""
import argparse, hashlib, json
from pathlib import Path
import numpy as np


def cameras(width=320, height=180):
    result = []
    positions = [(2.3, 0, 1.2), (0, -.9, 1.2), (-2.3, 0, 1.2), (0, .9, 1.2)]
    for i, name in enumerate(['front', 'right', 'rear', 'left']):
        az = [0, -np.pi/2, np.pi, np.pi/2][i]
        forward = np.array([np.cos(az)*np.cos(np.pi/6), np.sin(az)*np.cos(np.pi/6), -.5])
        right = np.cross(forward, [0, 0, 1]); right /= np.linalg.norm(right)
        down = np.cross(forward, right)
        R = np.array([right, down, forward]); C = np.array(positions[i])
        T = np.eye(4); T[:3, :3] = R; T[:3, 3] = -R @ C
        result.append(dict(id=i, name=name, calibration_id=f'analytic-{name}-v1',
            resolution=dict(width=width, height=height),
            projection=dict(model='opencv_fisheye', fx=100., fy=100., cx=(width-1)/2,
                cy=(height-1)/2, alpha=0., k=[0., 0., 0., 0.], theta_max_rad=1.45, z_epsilon_m=1e-6),
            T_camera_from_vehicle=T.tolist()))
    return result


def config():
    return dict(schema_version=1, profile_id='linux-prototype-v1', units=dict(length='m', angle='rad', time='ns'),
        vehicle=dict(length_m=4.6, width_m=1.8, mask_margin_m=.1), cameras=cameras(),
        surface=dict(type='rectangular_bowl_v1', flat_half_length_m=2.6, flat_half_width_m=1.2,
            outer_half_length_m=6., outer_half_width_m=4.5, corner_height_m=1.5, uniform_cells=[32,32]),
        virtual_camera=dict(azimuth_rad=.45, elevation_rad=.8, distance_m=10., fov_y_rad=1., clip_m=[.1,50.]),
        output=dict(width=640, height=360), runtime=dict(input_queue_per_camera=3, skew_window_ms=10., max_input_age_ms=100.))


def project(cam, points):
    """Ray angle via atan2, independent of C++ X/Z -> atan(hypot) formulation."""
    T = np.asarray(cam['T_camera_from_vehicle']); p = np.asarray(points) @ T[:3,:3].T + T[:3,3]
    rho = np.hypot(p[:,0],p[:,1]); theta = np.arctan2(rho,p[:,2]); k = cam['projection']
    td = theta.copy()
    for j, coeff in enumerate(k['k']): td += coeff * theta**(2*j+3)
    direction = np.divide(p[:,:2],rho[:,None],out=np.zeros((len(p),2)),where=rho[:,None]>1e-15)
    uv = direction*td[:,None]*[k['fx'],k['fy']]+[k['cx'],k['cy']]
    return uv


def camera_rays(cam):
    """Invert the fisheye polynomial into unit rays in camera coordinates."""
    r=cam['resolution']; k=cam['projection']
    yy,xx=np.mgrid[:r['height'],:r['width']]; a=(xx-k['cx'])/k['fx']; b=(yy-k['cy'])/k['fy']
    radius=np.hypot(a,b);theta=radius.copy()
    for _ in range(12):
        td=theta.copy();derivative=np.ones_like(theta)
        for j,coef in enumerate(k['k']):
            td+=coef*theta**(2*j+3);derivative+=(2*j+3)*coef*theta**(2*j+2)
        if np.any(derivative<=0):raise ValueError('synthetic inverse model is not monotonic')
        theta-= (td-radius)/derivative
    q=np.divide(np.sin(theta),radius,out=np.ones_like(theta),where=radius>1e-12)
    return np.stack([a*q,b*q,np.cos(theta)],axis=-1),theta


def render_camera(cam, phase):
    k=cam['projection']; T=np.asarray(cam['T_camera_from_vehicle'])
    camera_directions,theta=camera_rays(cam);rays=camera_directions @ T[:3,:3]
    origin=-T[:3,:3].T @ T[:3,3]
    t=np.divide(-origin[2],rays[:,:,2],out=np.full_like(theta,-1),where=np.abs(rays[:,:,2])>1e-9)
    points=origin+rays*t[:,:,None]; x,y=points[:,:,0],points[:,:,1]
    valid=(t>0)&(theta<k['theta_max_rad'])
    tile=(np.floor(x*2)+np.floor(y*2)).astype(int)%2
    color=np.where(tile[:,:,None]!=0,np.array([90,114,132]),np.array([176,190,200])).astype(np.uint8)
    lines=(np.abs(x-np.round(x))<.025)|(np.abs(y-np.round(y))<.025)
    color[lines]=[225,233,242]
    marker=(x-(3.6+.2*phase))**2+(y-1.4)**2<.12
    color[marker]=[235,140,45]
    # Raised sphere demonstrates unavoidable distortion of off-surface objects.
    center=np.array([-3.7,-1.6,.8]); oc=origin-center
    bb=np.einsum('ijk,k->ij',rays,oc); disc=bb*bb-(oc@oc-.45**2)
    root=-bb-np.sqrt(np.maximum(disc,0)); sphere=(disc>0)&(root>0)&((root<t)|(t<0))
    color[sphere]=[54,184,121]
    color[~valid & ~sphere]=[27,44,64]
    return color


def generate(output, cfg, frames=12):
    output=Path(output); output.mkdir(parents=True,exist_ok=True)
    rows=[]; hashes={}
    for seq in range(frames):
        paths=[]
        for cam in cfg['cameras']:
            name=f"camera{cam['id']}_{seq:04d}.ppm"
            image=render_camera(cam,seq/frames)
            data=f"P6\n{image.shape[1]} {image.shape[0]}\n255\n".encode()+image.tobytes()
            (output/name).write_bytes(data); hashes[name]=hashlib.sha256(data).hexdigest(); paths.append(name)
        rows.append(dict(scenario_timestamp_ns=str(seq*33333333),paths=paths,offset_ns=[0]*4))
    manifest=dict(schema_version=1,calibration_ids=[c['calibration_id'] for c in cfg['cameras']],
        origin='analytic_ground_and_sphere_v1',frames=rows,sha256=hashes)
    (output/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    return output/'manifest.json'


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--config',type=Path);parser.add_argument('--frames',type=int,default=12)
    args=parser.parse_args()
    if not 1<=args.frames<=300:parser.error('frames must be 1..300')
    cfg=json.loads(args.config.read_text()) if args.config else config()
    print(generate(args.output,cfg,args.frames))
