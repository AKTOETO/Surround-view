"""Seeded peripheral board poses and measured angular occupancy."""
import numpy as np
from scipy.spatial.transform import Rotation
from calibration.optical_models import project
from calibration.raster_board import K, SIZE


def peripheral_poses(seed, angles=(.75,.75,.75,.75,.9,.9,.9,.9)):
    rng = np.random.default_rng(seed)
    result = []
    for i,theta in enumerate(angles):
        phi = (.6, -.6, np.pi-.6, np.pi+.6)[i%4]+rng.uniform(-.025,.025)
        normal = np.array([np.sin(theta)*np.cos(phi),np.sin(theta)*np.sin(phi),np.cos(theta)])
        right = np.cross([0.,1.,0.],normal)
        right /= np.linalg.norm(right)
        up = np.cross(normal,right)
        rotation = np.column_stack([right,up,normal]) @ Rotation.from_euler('z',rng.uniform(-.15,.15)).as_matrix()
        translation = normal*rng.uniform(3.0,3.3)
        result.append((rotation,translation))
    return result


def board_points(rotation,translation,outer=False):
    x,y = np.meshgrid([-1,9] if outer else np.arange(9),[-1,6] if outer else np.arange(6))
    xyz = np.stack([x.ravel()*.08-.32,y.ravel()*.08-.2,np.zeros(x.size)],axis=-1)
    return xyz @ rotation.T+translation


def coverage(geometry, family):
    points = np.concatenate([board_points(r,t) for r,t in geometry])
    theta = np.arctan2(np.linalg.norm(points[:,:2],axis=-1),points[:,2])
    uv = project(points,K,family)
    visible = (points[:,2]>0)&(uv[:,0]>=0)&(uv[:,0]<=SIZE[0]-1)&(uv[:,1]>=0)&(uv[:,1]<=SIZE[1]-1)
    edges = [0,.2,.4,.6,.8,1.,1.2]
    return {'theta_min_rad':float(theta.min()),'theta_max_rad':float(theta.max()),
            'bins_rad':edges,'counts':np.histogram(theta,bins=edges)[0].tolist(),
            'visible_corners':int(visible.sum()),'total_corners':len(theta)}
