"""Independent inverse-ray checkerboard rasterizer: synthetic images, no corner jitter."""
import numpy as np
from scipy.ndimage import gaussian_filter
from scipy.spatial.transform import Rotation

SIZE = (640, 480)
K = np.array([[300., 0., 319.5], [0., 295., 239.5], [0., 0., 1.]])
BOARD = {'columns': 9, 'rows': 6, 'square_size_m': .08}


def poses(seed, count=16):
    rng = np.random.default_rng(seed)
    return [(Rotation.from_euler('xyz', rng.uniform([-.5, -.5, -.2], [.5, .5, .2])).as_matrix(),
             rng.uniform([-.28, -.22, .8], [.28, .22, 1.35])) for _ in range(count)]


def corners(rotation, translation):
    x, y = np.meshgrid(np.arange(9)*.08-.32, np.arange(6)*.08-.2)
    points = np.stack([x.ravel(), y.ravel(), np.zeros(54)], axis=-1)
    points = points @ rotation.T + translation
    radius = np.linalg.norm(points[:, :2], axis=1)
    theta = np.arctan2(radius, points[:, 2])
    scale = np.divide(theta, radius, out=np.ones_like(radius), where=radius > 1e-14)
    return points[:, :2]*scale[:, None]*[K[0,0], K[1,1]]+[K[0,2], K[1,2]]


def image(rotation, translation, blur=0., noise=0., seed=0, supersampling=2):
    """Equidistant optics, plane intersection, box pixel integration, then blur/noise."""
    if supersampling not in (1, 2, 4) or blur < 0 or noise < 0:
        raise ValueError('unsupported raster/perturbation parameters')
    w, h = SIZE
    yy, xx = np.mgrid[:h*supersampling, :w*supersampling]
    x = ((xx+.5)/supersampling-.5-K[0,2])/K[0,0]
    y = ((yy+.5)/supersampling-.5-K[1,2])/K[1,1]
    theta = np.hypot(x,y)
    scale = np.divide(np.sin(theta), theta, out=np.ones_like(theta), where=theta > 1e-14)
    rays = np.stack([x*scale, y*scale, np.cos(theta)], axis=-1)
    normal = rotation[:,2]
    denominator = rays @ normal
    distance = np.divide(translation @ normal, denominator, out=np.full_like(theta, -1.),
                         where=np.abs(denominator) > 1e-12)
    local = (rays*distance[...,None]-translation) @ rotation
    # Inner intersections are grid coordinates (0..8, 0..5); include an outer square border.
    grid = (local[...,:2]+[.32,.2])/.08
    valid = ((distance > 0) & (grid[...,0] >= -1) & (grid[...,0] < 9)
             & (grid[...,1] >= -1) & (grid[...,1] < 6))
    parity = np.mod(np.floor(grid[...,0])+np.floor(grid[...,1]), 2)
    value = np.where(valid, np.where(parity == 0, .08, .92), .75)
    value = value.reshape(h,supersampling,w,supersampling).mean(axis=(1,3))
    if blur:
        value = gaussian_filter(value, blur)
    value += np.random.default_rng(seed).normal(0,noise,value.shape)
    return np.rint(np.clip(value,0,1)*255).astype(np.uint8)


def detection_error(detected, expected):
    """Resolve the unmarked board's 180-degree indexing ambiguity for evaluation only."""
    detected, expected = np.asarray(detected), np.asarray(expected)
    if detected.shape != (54,2) or expected.shape != (54,2):
        raise ValueError('54 two-dimensional corners required')
    options = [np.linalg.norm(detected-truth, axis=1) for truth in (expected, expected[::-1])]
    reverse = int(np.mean(options[1]**2) < np.mean(options[0]**2))
    residual = options[reverse]
    return {'rmse_px':float(np.sqrt(np.mean(residual**2))),
            'p95_px':float(np.percentile(residual,95)), 'reversed_indexing':bool(reverse)}
