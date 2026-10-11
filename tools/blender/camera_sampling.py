"""Synthetic fisheye raster scaling with unchanged angular support and extrinsics."""
import copy
import math


def resample_config(config):
    result = copy.deepcopy(config)
    if [c['id'] for c in result['cameras']] != list(range(4)):
        raise ValueError('ordered four-camera rig required')
    for camera in result['cameras']:
        if camera['resolution'] != {'width':400,'height':400}:
            raise ValueError('frozen source grid400x400 required')
        p = camera['projection']
        if (p['model'] != 'opencv_fisheye' or any(p['k']) or p.get('alpha',0) != 0
                or p['fx'] != 128 or p['fy'] != 128 or p['cx'] != 199.5 or p['cy'] != 199.5
                or p['theta_max_rad'] != 1.48):
            raise ValueError('frozen centered equidistant optics required')
        for scale,size in ((1,400),(2,800)):
            for focal,center in ((p['fx'],p['cx']),(p['fy'],p['cy'])):
                radius = scale*focal*p['theta_max_rad']
                principal = scale*(center+.5)-.5
                if not math.isfinite(radius) or principal-radius <= 0 or principal+radius >= size-1:
                    raise ValueError('angular field must lie strictly within both sensor grids')
        camera['resolution'] = {'width':800,'height':800}
        for field in ('fx','fy'):
            p[field] *= 2
        for field in ('cx','cy'):
            p[field] = 2*(p[field]+.5)-.5
    return result
