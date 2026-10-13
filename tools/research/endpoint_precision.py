"""Read-only attribution of two historical backend pairing failures; no runtime policy."""
import argparse
from decimal import Decimal, localcontext
import itertools
import json
from pathlib import Path
import numpy as np
from source_correspondence import sha, verified, load_product
from planar_precision import view_projection, reproject


def face_oracle(origin, endpoint, minimum, maximum, bias):
    """Independent 80-digit finite-face intersection on the exact supplied binary values."""
    if any(np.asarray(v).shape!=(3,) for v in [origin,endpoint,minimum,maximum]):
        raise ValueError('XYZ vectors required')
    def number(x): return Decimal.from_float(float(x))
    with localcontext() as context:
        context.prec = 80
        o,p,lo,hi = [[number(x) for x in v] for v in [origin,endpoint,minimum,maximum]]
        b = number(bias)
        if any(not x.is_finite() for v in [o,p,lo,hi] for x in v) or not b.is_finite() or b<0 or any(a>z for a,z in zip(lo,hi)):
            raise ValueError('finite geometry, ordered bounds, nonnegative bias required')
        d = [p[i]-o[i] for i in range(3)]
        length = sum(x*x for x in d).sqrt()
        hits = []
        if all(lo[i]<=o[i]<=hi[i] for i in range(3)):
            hits.append((Decimal(0),'origin_inside'))
        for axis in range(3):
            if d[axis] == 0: continue
            for label, plane in [('min',lo[axis]),('max',hi[axis])]:
                t = (plane-o[axis])/d[axis]
                if t<0 or t>1: continue
                if all(lo[j] <= o[j]+t*d[j] <= hi[j] for j in range(3) if j!=axis):
                    hits.append((t,'xyz'[axis]+'_'+label))
        if not hits:
            return dict(blocked=False, entry=None, face=None, gap_m=None, margin_m=None)
        entry,face = min(hits)
        gap = (1-entry)*length
        return dict(blocked=gap>b,entry=str(entry),face=face,gap_m=str(gap),margin_m=str(gap-b))


def slab_replay(origin, endpoint, minimum, maximum, inset, bias, origin32=False, parameters32=False, arithmetic32=False):
    """Host float32/64 unfused operation replay, NOT a shader-instruction emulator."""
    dtype=np.float32 if arithmetic32 else np.float64
    o=np.asarray(origin,np.float32 if origin32 else np.float64).astype(dtype)
    p=np.asarray(endpoint,dtype)
    param=np.float32 if parameters32 else np.float64
    lo=np.asarray(minimum,param).astype(dtype)+dtype(param(inset))
    hi=np.asarray(maximum,param).astype(dtype)-dtype(param(inset))
    bias=dtype(param(bias))
    d=p-o
    # Deliberate individual rounding; hardware FMA/rsqrt instruction choices not modeled.
    length=dtype(np.sqrt(dtype(dtype(d[0]*d[0]+d[1]*d[1])+d[2]*d[2])))
    entry,exit=dtype(0),dtype(1)
    if length<=bias or np.any(lo>hi):
        return dict(blocked=False,entry=float(entry),exit=float(exit),limit=None,gap_m=None)
    for axis in range(3):
        if abs(d[axis])<dtype(1e-8):
            if o[axis]<lo[axis] or o[axis]>hi[axis]:
                return dict(blocked=False,entry=float(entry),exit=float(exit),limit=None,gap_m=None)
        else:
            a=dtype((lo[axis]-o[axis])/d[axis]);b=dtype((hi[axis]-o[axis])/d[axis])
            entry=max(entry,min(a,b));exit=min(exit,max(a,b))
    limit=dtype(dtype(1)-bias/length)
    return dict(blocked=bool(entry<=exit and entry<limit),entry=float(entry),exit=float(exit),
                limit=float(limit),gap_m=float(dtype(dtype(1)-entry)*length))


def collect(root, path):
    plan=json.loads(path.read_text())
    for name,digest in plan['frozen_files'].items(): verified(root/name,digest)
    pairing=json.loads((root/plan['pairing_report']).read_text())
    primary=json.loads((root/plan['historical_report']).read_text())
    rows=[]
    for case in pairing['rows']:
        original=next(r for r in primary['rows'] if (r['frame'],r['variant'])==(case['frame'],case['variant']))
        directories={'nvidia':root/original['directory']}
        for control in case['matched_mesa_controls']:
            directories[control['label']]=(root/control['report']).parent
        products={};metas={}
        for label,directory in directories.items():
            m=json.loads((directory/'report.json').read_text());metas[label]=m
            products[label]={p['file']:load_product(directory,p) for p in m['products']}
        mesa=products['current_binary_mesa'];old=products['archived_binary_mesa']
        if any(not np.array_equal(mesa[k],old[k],equal_nan=True) for k in mesa):
            raise ValueError('old/current Mesa payload changed')
        cfg=metas['nvidia']['config']
        if any(m['config']!=cfg or m['inputs']!=metas['nvidia']['inputs'] for m in metas.values()):
            raise ValueError('capture input/config differs')
        y,x=case['y'],case['x'];camera=case['current_eligible'][0]
        transform=np.asarray(cfg['cameras'][camera]['T_camera_from_vehicle'],float)
        origin=-transform[:3,:3].T@transform[:3,3]
        box=cfg['surface']['source_occlusion_box'];inset=box.get('uncertainty',{}).get('radius_m',0)
        lo=np.asarray(box['min_m'])+inset;hi=np.asarray(box['max_m'])-inset;bias=box['tolerance_m']
        points={k:products[k]['carrier-positions.f32'][y,x] for k in ['nvidia','current_binary_mesa']}
        nvidia,point_mesa=points.values()
        records=[]
        for label,p in points.items():
            oracle=face_oracle(origin,p,lo,hi,bias)
            expected=int(products[label][f'camera{camera}-status.u8'][y,x,0])==7
            controls=[]
            for origin32,parameters32,arithmetic32 in itertools.product([False,True],repeat=3):
                result=slab_replay(origin,p,box['min_m'],box['max_m'],inset,bias,origin32,parameters32,arithmetic32)
                controls.append(dict(origin32=origin32,parameters32=parameters32,arithmetic32=arithmetic32,**result))
            mvp=view_projection(cfg);rounded=mvp.astype(np.float32).astype(float)
            records.append(dict(backend=label,point=p.tolist(),bits=p.view(np.uint32).tolist(),oracle=oracle,
                historical_central_rejected=expected,controls=controls,
                all_replays_agree=all(c['blocked']==oracle['blocked']==expected for c in controls),
                float_mvp_reprojection_error_px=(reproject(rounded,p[None,:],cfg['output']['width'],cfg['output']['height'])[0]-[x,y]).tolist()))
        mixes=[]
        for bits in itertools.product([False,True],repeat=3):
            p=np.where(bits,point_mesa,nvidia)
            mixes.append(dict(mesa_axes=[a for a,b in zip('xyz',bits) if b],point=p.tolist(),oracle=face_oracle(origin,p,lo,hi,bias)))
        delta=point_mesa.astype(float)-nvidia
        gaps=[float(r['oracle']['gap_m']) for r in records]
        rows.append(dict(frame=case['frame'],variant=case['variant'],y=y,x=x,camera=camera,
            origin=origin.tolist(),active_box_min=lo.tolist(),active_box_max=hi.tolist(),inset_m=inset,bias_m=bias,
            delta_xyz_m=delta.tolist(),endpoint_delta_m=float(np.linalg.norm(delta)),gap_delta_m=gaps[1]-gaps[0],
            gap_amplification=abs(gaps[1]-gaps[0])/float(np.linalg.norm(delta)),
            records=records,axis_substitutions=mixes))
    return dict(schema_version=1,scope=plan['scope'],plan_sha256=sha(path),rows=rows,
        all_replays_agree=all(r['all_replays_agree'] for row in rows for r in row['records']),
        limitations=plan['limitations'])


if __name__=='__main__':
    root=Path(__file__).resolve().parents[2]
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--plan',type=Path,default=Path('configs/research/endpoint-precision-plan.json'))
    parser.add_argument('--output-report',type=Path,required=True)
    args=parser.parse_args()
    args.output_report.write_text(json.dumps(collect(root,args.plan.resolve()),indent=2,allow_nan=False)+'\n')
