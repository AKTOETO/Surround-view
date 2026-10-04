#!/usr/bin/env python3
"""Reproducible desktop smoke experiments; no claim of Aurora or physical latency."""
import argparse,copy,hashlib,json,os,platform,subprocess,sys,time
from pathlib import Path
import numpy as np
from simulator import generate
from configurator import calibrate,diagnose,rotation
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tests'))
from test_calibration import observation_fixture,perturbed_config


def run(args):
    root=Path(__file__).resolve().parents[1];out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
    cfg=json.loads(args.config.read_text());manifest=generate(out/'fixture',cfg,8)
    environment=os.environ.copy()
    if args.platform=='surfaceless':environment.update(LIBGL_ALWAYS_SOFTWARE='1',EGL_PLATFORM='surfaceless')
    else:environment.pop('LIBGL_ALWAYS_SOFTWARE',None)
    records=[]
    # Alternate order between independent process launches to limit order bias.
    variants=[('plane',0.,32,640,360),('bowl',1.5,32,640,360),('bowl_dense',1.5,64,640,360),('bowl_720p',1.5,32,1280,720)]
    for repeat in range(args.repeats):
        for name,height,cells,width,pxheight in (variants if repeat%2==0 else variants[::-1]):
            case=out/f'{name}_r{repeat+1}'
            command=[str(args.build.resolve()/'sv-bench'),'--egl-platform',args.platform,'--config',str(args.config.resolve()),'--manifest',str(manifest),'--output',str(case),'--height',str(height),'--cells',str(cells),'--width',str(width),'--output-height',str(pxheight),'--warmup','10','--iterations',str(args.iterations)]
            result=subprocess.run(command,env=environment,text=True,capture_output=True,timeout=60)
            if result.returncode:raise RuntimeError(result.stderr or result.stdout)
            metrics=json.loads((case/'metrics.json').read_text());metrics.update(variant=name,repeat=repeat+1,config_sha256=hashlib.sha256((case/'effective_config.json').read_bytes()).hexdigest())
            records.append(metrics);print(f"{name} repeat {repeat+1}: {metrics['gl_renderer']}, p95 {metrics['p95_ms']:.3f} ms",flush=True)
    observations=observation_fixture();initial=perturbed_config();calibrated,calibration=calibrate(initial,observations)
    if not calibration['accepted']:raise RuntimeError('calibration rejected')
    (out/'observations.json').write_text(json.dumps(observations,indent=2)+'\n');(out/'calibrated.json').write_text(json.dumps(calibrated,indent=2)+'\n')
    drift=[]
    for degrees in [0,.1,.25,.5,1.,2.]:
        changed=copy.deepcopy(calibrated);T=np.array(changed['cameras'][2]['T_camera_from_vehicle']);T[:3,:3]=rotation([0,np.deg2rad(degrees),0])@T[:3,:3];changed['cameras'][2]['T_camera_from_vehicle']=T.tolist()
        diagnostic=diagnose(changed,observations);drift.append(dict(angle_deg=degrees,**diagnostic))
    # Re-estimate after a controlled perturbation using the original independent split.
    recovered,recovery=calibrate(changed,observations)
    if not recovery['accepted']:raise RuntimeError('recovery rejected')
    metadata=dict(date=time.strftime('%Y-%m-%dT%H:%M:%S%z'),platform=platform.platform(),python=sys.version.split()[0],numpy=np.__version__,
        cpu=next((line.split(':',1)[1].strip() for line in Path('/proc/cpuinfo').read_text().splitlines() if line.startswith('model name')),'unknown'),
        fixture_sha256=hashlib.sha256(manifest.read_bytes()).hexdigest(),requested_backend=args.platform,
        compiler=subprocess.run(['c++','--version'],text=True,capture_output=True).stdout.splitlines()[0],
        limitations=['Static cached inputs in render benchmark','Render includes CPU work and synchronous RGBA readback','No sensor capture or physical display latency','No real camera calibration','No Aurora device','Short runs; no thermal conclusion'])
    summary=dict(schema_version=1,metadata=metadata,render=records,calibration=calibration,drift=drift,recovery=recovery)
    (out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(out/'summary.json',flush=True)


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--build',type=Path,default=Path('build'));parser.add_argument('--config',type=Path,default=Path('configs/synthetic.json'));parser.add_argument('--output',type=Path,required=True);parser.add_argument('--platform',choices=['device','surfaceless'],default='surfaceless');parser.add_argument('--repeats',type=int,default=3);parser.add_argument('--iterations',type=int,default=60);args=parser.parse_args()
    if not 1<=args.repeats<=10 or not 10<=args.iterations<=1000:parser.error('repeats 1..10, iterations 10..1000')
    run(args)
