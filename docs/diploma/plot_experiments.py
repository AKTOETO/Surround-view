#!/usr/bin/env python3
"""Publication figures from recorded Markdown data, plus optional actual render previews."""
import argparse,json,os,re
from pathlib import Path
os.environ.setdefault('MPLCONFIGDIR','/tmp/sv-matplotlib')
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np


def generate(data,output,previews=None):
    output.mkdir(parents=True,exist_ok=True)
    plt.rcParams.update({'font.family':'DejaVu Sans','font.size':10,'svg.fonttype':'none'})
    variants=['plane','bowl','bowl_dense','bowl_720p'];labels=['Плоскость\n640×360','Чаша\n640×360','Чаша, плотная сетка\n640×360','Чаша\n1280×720']
    fig,ax=plt.subplots(figsize=(9,4.4),layout='constrained')
    for offset,key,color,label in [(-.18,'rtx','#287b8e','RTX 5070 Ti'),(.18,'mesa','#ce9154','Mesa llvmpipe')]:
        values=[[r['p95_ms'] for r in data[key]['render'] if r['variant']==v] for v in variants]
        centers=np.median(values,axis=1);low=centers-np.min(values,axis=1);high=np.max(values,axis=1)-centers
        ax.bar(np.arange(4)+offset,centers,.32,color=color,label=label)
        ax.errorbar(np.arange(4)+offset,centers,yerr=[low,high],fmt='none',ecolor='#344052',capsize=4)
    ax.set_xticks(np.arange(4),labels);ax.set_ylabel('p95 времени render + readback, мс');ax.set_title('Три независимых запуска; столбец — медиана p95, ус — диапазон p95');ax.legend();ax.grid(axis='y',alpha=.2);ax.set_axisbelow(True)
    fig.savefig(output/'04_performance.svg');plt.close(fig)
    fig,axes=plt.subplots(1,2,figsize=(10,4),layout='constrained');s=data['rtx'];cams=s['calibration']['cameras']
    before=[c['before_validation']['rmse_px'] for c in cams];after=[c['validation']['rmse_px'] for c in cams]
    x=np.arange(4);axes[0].bar(x-.18,before,.34,label='До оценивания',color='#ce9154');axes[0].bar(x+.18,after,.34,label='После оценивания',color='#287b8e');axes[0].set_xticks(x,['Передняя','Правая','Задняя','Левая']);axes[0].set_ylabel('RMSE на независимых точках, пикс.');axes[0].set_title('Известные 3D-точки; шум σ=0.08 пикс.');axes[0].legend()
    drift=s['drift'];angles=[d['angle_deg'] for d in drift];errors=[d['cameras'][2]['p95_px'] for d in drift]
    axes[1].plot(angles,errors,'o-',color='#287b8e');axes[1].axhline(1.,ls='--',color='#bb5548',label='Порог повторной калибровки');axes[1].axhline(.5,ls=':',color='#8b764c',label='Порог подозрения');axes[1].set_xlabel('Заданный поворот задней камеры, град.');axes[1].set_ylabel('p95 ошибки, пикс.');axes[1].set_title('Один сценарий, критерий по известным точкам');axes[1].legend(fontsize=8)
    for ax in axes:ax.grid(axis='y',alpha=.2);ax.set_axisbelow(True)
    fig.savefig(output/'04_calibration_and_drift.svg');plt.close(fig)
    if previews:
        fig,axes=plt.subplots(1,2,figsize=(11,3.6),layout='constrained')
        for ax,name,label in zip(axes,['plane_r1','bowl_r1'],['Плоскость: H=0','Чаша: H=1.5 м']):
            ax.imshow(plt.imread(previews/name/'preview.ppm'));ax.set_title(label);ax.axis('off')
        fig.suptitle('Одинаковые входы и виртуальная камера; глубина сцены не восстанавливается')
        fig.savefig(output/'04_plane_vs_bowl.png',dpi=150);plt.close(fig)


if __name__=='__main__':
    parser=argparse.ArgumentParser();parent=Path(__file__).resolve().parent
    parser.add_argument('--data',type=Path,default=parent.parent/'prototype/MEASUREMENTS.md');parser.add_argument('--output',type=Path,default=parent/'figures/experiments');parser.add_argument('--previews',type=Path)
    args=parser.parse_args();text=args.data.read_text();block=re.search(r'<!-- measurements: linux_prototype_v1 -->\s*```json\s*(.*?)\s*```',text,re.S)
    if not block:raise SystemExit('measurement block not found')
    generate(json.loads(block.group(1)),args.output,args.previews)
