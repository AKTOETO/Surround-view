"""Audit native carrier-position pilot outputs and draw chapter4 figures; no rendering."""
import argparse
import hashlib
import json
from pathlib import Path
import xml.etree.ElementTree as ET

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np
from matplotlib.colors import ListedColormap, BoundaryNorm
from matplotlib.patches import Patch

ROOT = Path(__file__).resolve().parents[2]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def verified(path, digest):
    if sha(path) != digest:
        raise ValueError(f'checksum mismatch: {path}')
    return path


def collect(plan_path, root):
    plan = json.loads(plan_path.read_text())
    provenance = json.loads((root/'test-provenance.json').read_text())
    for name,digest in provenance['files_sha256'].items():
        verified(root/name,digest)
    for name,digest in provenance['code_sha256'].items():
        verified(ROOT/name,digest)
    verified(ROOT/plan['native_plan'],plan['native_plan_sha256'])
    tests = []
    for device in ('nvidia','mesa'):
        xml = ET.parse(root/'tests'/(device+'.xml')).getroot()
        if (int(xml.attrib['tests']) != 6 or int(xml.attrib['failures']) or int(xml.attrib['errors'])
                or int(xml.attrib['disabled'])):
            raise ValueError('complete successful six-test suite required per backend')
        rows = []
        for case in xml.iter('testcase'):
            fields = {p.attrib['name']:p.attrib['value'] for p in case.findall('properties/property')}
            if not fields:
                continue
            for key in ('checked_rays','raster_subpixel_bits'): fields[key] = int(fields[key])
            for key in ('max_axis_error_m','max_reprojection_error_px'): fields[key] = float(fields[key])
            rows.append(fields)
        if len(rows) != 5 or {r['carrier'] for r in rows} != {'plane','bowl','dome_floor_v1','cylinder_floor_v1','cube_floor_v1'}:
            raise ValueError('incomplete carrier oracle matrix')
        for row in rows:
            if (row['max_axis_error_m'] > plan['oracle_fixture']['max_axis_error_m'] or
                    row['max_reprojection_error_px'] > plan['oracle_fixture']['max_reprojection_error_px']):
                raise ValueError('pilot fixture error bound exceeded')
        tests.append({'backend':device,'xml_sha256':sha(root/'tests'/(device+'.xml')),'rows':rows})
    geometry,parity,fingerprints = [],[],set()
    for source in plan['configs']:
        carrier = source['carrier']
        cfg = json.loads(verified(ROOT/source['config'],source['config_sha256']).read_text())
        directory = root/'positions'/carrier
        report = json.loads((directory/'report.json').read_text())
        if (report['config_sha256'] != source['config_sha256'] or not report['normal_output_unchanged']
                or report['source_frames'] != 0 or report['scope'] != 'depth_tested_carrier_geometry'):
            raise ValueError('geometry config/parity/scope mismatch')
        descriptor = report['carrier_positions']
        if (descriptor['dtype'],descriptor['byte_order'],descriptor['channels'],descriptor['origin'],
                descriptor['coordinate_frame'],descriptor['scope'],descriptor['invalid']) != (
                'float32','little_endian',3,'top_left','vehicle_metres',
                'perspective_interpolated_actual_carrier_triangles','NaN_for_background_and_ego'):
            raise ValueError('unsupported position layout/scope')
        height,width = report['height'],report['width']
        if cfg['output'] != {'width':width,'height':height}:
            raise ValueError('actual output dimensions differ')
        regions = np.fromfile(verified(directory/'carrier-regions.u8',report['mask_sha256']),np.uint8).reshape(height,width)
        points = np.fromfile(verified(directory/descriptor['file'],descriptor['sha256']),dtype='<f4').reshape(height,width,3)
        support = (regions != 0)&(regions != 5)
        if (regions.max() > 5 or not np.isfinite(points[support]).all() or not np.isnan(points[~support]).all()
                or descriptor['payload_bytes'] != height*width*12):
            raise ValueError('invalid carrier support/NaN/payload contract')
        names = ('background','floor','shell','vehicle_footprint','raised_bowl','vehicle_model')
        if ({name:int((regions==index).sum()) for index,name in enumerate(names)} != report['pixels']
                or descriptor['extra_passes'] != 3):
            raise ValueError('carrier counters/pass count differ from actual data')
        row = dict(report,carrier=carrier,report_sha256=sha(directory/'report.json'),
                   supported_pixels=int(support.sum()),invalid_pixels=int((~support).sum()))
        geometry.append(row);fingerprints.add(report['source_fingerprint'])
        old_path = verified(ROOT/source['reference_report'],source['reference_report_sha256'])
        new_path = root/'native/seed101'/carrier/'report.json'
        old,new = json.loads(old_path.read_text()),json.loads(new_path.read_text())
        verified(new_path.parent/'config.json',source['config_sha256'])
        fingerprints.add(new['catalog']['source_fingerprint'])
        catalogs = [{k:v for k,v in r['catalog'].items() if k not in ('source_revision','source_fingerprint')} for r in (old,new)]
        if catalogs[0] != catalogs[1] or old['scenario'] != new['scenario']:
            raise ValueError('parity device/catalog/scenario changed')
        selected = lambda r:[{k:s[k] for k in ('frame_index','variant','block','warmup','rgba_sha256')} for s in r['samples']]
        if selected(old) != selected(new):
            raise ValueError('ordinary native output changed')
        invocation_path = new_path.parent/'invocation.json'
        invocation = json.loads(invocation_path.read_text())
        for name,digest in invocation['binary_sha256'].items():
            verified(root/'binaries'/name,digest)
        stored = 0
        for old_sample,new_sample in zip(old['samples'],new['samples']):
            if not old_sample['warmup']:
                for directory_,sample in ((old_path.parent,old_sample),(new_path.parent,new_sample)):
                    verified(directory_/sample['rgba_file'],sample['rgba_sha256'])
                stored += 1
        parity.append({'carrier':carrier,'reference_report_sha256':sha(old_path),'report_sha256':sha(new_path),
                       'invocation_sha256':sha(invocation_path),'binary_sha256':invocation['binary_sha256'],
                       'old_source_fingerprint':old['catalog']['source_fingerprint'],
                       'new_source_fingerprint':new['catalog']['source_fingerprint'],
                       'recorded_identical_samples':len(old['samples']),'stored_identical_samples':stored,
                       'conditions':len(old['scenario']['variants'])*old['scenario']['frames']})
    if len(fingerprints) != 1:
        raise ValueError('mixed native implementations')
    return {'schema_version':1,'experiment':plan['experiment'],'plan':plan,'plan_sha256':sha(plan_path),
            'analyzer_sha256':sha(Path(__file__)),'test_provenance_sha256':sha(root/'test-provenance.json'),
            'test_provenance':provenance,'source_fingerprint':fingerprints.pop(),
            'geometry':geometry,'tests':tests,'native_parity':parity,'limitations':plan['limitations']}


def plot(report, root, output):
    output.mkdir(parents=True,exist_ok=True)
    fig,axes = plt.subplots(5,3,figsize=(16,14))
    colors = ['#28323c','#6bd4c5','#edb24a','#be8de8','#f27e7e','#405ab5']
    for row,record in enumerate(report['geometry']):
        directory = root/'positions'/record['carrier']
        descriptor = record['carrier_positions']
        h,w = record['height'],record['width']
        regions = np.fromfile(verified(directory/'carrier-regions.u8',record['mask_sha256']),np.uint8).reshape(h,w)
        points = np.fromfile(verified(directory/descriptor['file'],descriptor['sha256']),dtype='<f4').reshape(h,w,3)
        axes[row,0].imshow(regions,cmap=ListedColormap(colors),norm=BoundaryNorm(np.arange(-.5,6),6))
        axes[row,0].set_title(record['carrier']+' / depth-tested regions')
        height = axes[row,1].imshow(np.ma.masked_invalid(points[...,2]),vmin=0,vmax=14,cmap='viridis')
        axes[row,1].set_title('Actual triangle hit height / vehicle metres')
        distance = axes[row,2].imshow(np.ma.masked_invalid(np.linalg.norm(points[...,:2],axis=-1)),vmin=0,vmax=20,cmap='magma')
        axes[row,2].set_title('Distance from vehicle origin in XY / metres')
        fig.colorbar(height,ax=axes[row,1],shrink=.65);fig.colorbar(distance,ax=axes[row,2],shrink=.65)
    for ax in axes.flat:ax.set_axis_off()
    fig.suptitle('Native GLES positions, 640x360; coordinate panels: white = NaN/background/ego')
    names = ['background','floor','shell','footprint','raised bowl','ego model']
    fig.legend(handles=[Patch(color=color,label=name) for color,name in zip(colors,names)],
               loc='lower center',ncol=6)
    fig.tight_layout(rect=(0,.04,1,.96));fig.savefig(output/'carrier_positions_fields.png',dpi=150);plt.close(fig)
    fig,axes=plt.subplots(1,2,figsize=(13,5))
    for index,(key,scale,label) in enumerate((('max_axis_error_m',1000,'Maximum selected-ray axis error / mm'),
                                             ('max_reprojection_error_px',1,'Maximum all-support reprojection error / pixels'))):
        for offset,test in enumerate(report['tests']):
            values=[r[key]*scale for r in test['rows']]
            axes[index].bar(np.arange(5)+(offset-.5)*.36,values,width=.36,label=test['backend'])
        axes[index].set_xticks(np.arange(5),[r['carrier'].replace('_floor_v1','') for r in test['rows']])
        axes[index].set_yscale('log');axes[index].set_title(label);axes[index].legend()
    fig.suptitle('Pilot: 1343 selected coarse-mesh rays/backend; 160x100, fixture-specific bounds')
    fig.tight_layout(rect=(0,0,1,.92));fig.savefig(output/'carrier_positions_errors.png',dpi=150);plt.close(fig)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--plan',type=Path,default=ROOT/'configs/research/carrier-positions-plan.json')
    parser.add_argument('--root',type=Path,default=ROOT/'artifacts/carrier-inspection-v1')
    parser.add_argument('--report',type=Path,help='Plot a recorded baseline instead of collecting results')
    parser.add_argument('--output-report',type=Path)
    parser.add_argument('--output',type=Path,default=Path(__file__).parent/'figures/experiments')
    args=parser.parse_args()
    report=json.loads(args.report.read_text()) if args.report else collect(args.plan.resolve(),args.root.resolve())
    if args.output_report:args.output_report.write_text(json.dumps(report,indent=2)+'\n')
    plot(report,args.root.resolve(),args.output)


if __name__=='__main__':main()
