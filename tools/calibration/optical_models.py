"""Synthetic optical families and independent fixed-pose/floor validation."""
from dataclasses import dataclass
import numpy as np


@dataclass(frozen=True)
class OpticalModel:
    name: str
    coefficients: tuple = (0., 0., 0., 0.)

    def radius(self, theta):
        theta = np.asarray(theta, float)
        if self.name == 'equidistant':
            return theta
        if self.name == 'equisolid':
            return 2*np.sin(theta/2)
        if self.name == 'stereographic':
            return 2*np.tan(theta/2)
        if self.name == 'kb_nonzero':
            return theta*(1+sum(k*theta**(2*i+2) for i,k in enumerate(self.coefficients)))
        raise ValueError('unknown optical family')

    def inverse(self, radius, theta_max=np.pi/2):
        radius = np.asarray(radius, float)
        if not np.isfinite(radius).all() or (radius < 0).any():
            raise ValueError('finite nonnegative optical radii required')
        if self.name == 'equidistant':
            return radius
        if self.name == 'equisolid':
            if (radius > 2).any():
                raise ValueError('equisolid radius exceeds domain')
            return 2*np.arcsin(radius/2)
        if self.name == 'stereographic':
            return 2*np.arctan(radius/2)
        if self.name != 'kb_nonzero':
            raise ValueError('unknown optical family')
        theta = np.linspace(0,theta_max,2049)
        derivative = 1+sum((2*i+3)*k*theta**(2*i+2) for i,k in enumerate(self.coefficients))
        if (derivative <= 0).any() or (radius > self.radius(theta_max)).any():
            raise ValueError('nonmonotonic or unsupported optical domain')
        low, high = np.zeros_like(radius), np.full_like(radius,theta_max)
        for _ in range(40):
            middle = (low+high)/2
            below = self.radius(middle) < radius
            low,high = np.where(below,middle,low),np.where(below,high,middle)
        return (low+high)/2


FAMILIES = (OpticalModel('equidistant'), OpticalModel('kb_nonzero',(.12,.025,-.01,.003)),
            OpticalModel('equisolid'), OpticalModel('stereographic'))


def project(points, K, optics):
    points = np.asarray(points,float)
    radius = np.linalg.norm(points[...,:2],axis=-1)
    theta = np.arctan2(radius,points[...,2])
    distorted = optics.radius(theta)
    scale = np.divide(distorted,radius,out=np.ones_like(radius),where=radius > 1e-14)
    return points[...,:2]*scale[...,None]*[K[0,0],K[1,1]]+[K[0,2],K[1,2]]


def fitted_model(estimate):
    K = np.array([[estimate['fx'],0,estimate['cx']],[0,estimate['fy'],estimate['cy']],[0,0,1.]])
    return K,OpticalModel('kb_nonzero',tuple(estimate['k']))


def distribution(values):
    values = np.asarray(values,float)
    if not len(values):
        return {'count':0,'rmse':None,'p95':None,'max':None}
    return {'count':len(values),'rmse':float(np.sqrt(np.mean(values**2))),
            'p95':float(np.percentile(values,95)),'max':float(values.max())}


def metric_validation(estimate, true_K, optics, geometry, size=(640,480)):
    """No pose fitting: held-out corners, image-domain rays, known horizontal plane."""
    K,fit = fitted_model(estimate)
    corner_errors = []
    theta_coverage = []
    for rotation,translation in geometry:
        # Same board geometry, immutable known camera/board poses.
        x,y = np.meshgrid(np.arange(9)*.08-.32,np.arange(6)*.08-.2)
        xyz = np.stack([x.ravel(),y.ravel(),np.zeros(54)],axis=-1) @ rotation.T+translation
        truth = project(xyz,true_K,optics)
        corner_errors.extend(np.linalg.norm(project(xyz,K,fit)-truth,axis=-1))
        theta_coverage.extend(np.arctan2(np.linalg.norm(xyz[:,:2],axis=-1),xyz[:,2]))
    theta,phi = np.meshgrid(np.linspace(.02,1.,100),np.linspace(-np.pi,np.pi,128,endpoint=False))
    rays = np.stack([np.sin(theta)*np.cos(phi),np.sin(theta)*np.sin(phi),np.cos(theta)],axis=-1)
    uv = project(rays,true_K,optics)
    inside = (uv[...,0]>=0)&(uv[...,0]<=size[0]-1)&(uv[...,1]>=0)&(uv[...,1]<=size[1]-1)
    residual = np.linalg.norm(project(rays,K,fit)-uv,axis=-1)
    zones = {name:distribution(residual[inside&(theta>=low)&(theta<high)])
             for name,low,high in [('central',0,.4),('middle',.4,.7),('outer',.7,1.01)]}
    # Camera coordinates: plane Y=1.2 m, forward Z. Ground extrinsics are known exactly.
    x,z = np.meshgrid(np.linspace(-3,3,31),np.linspace(1,8,40))
    floor = np.stack([x,np.full_like(x,1.2),z],axis=-1).reshape(-1,3)
    uv = project(floor,true_K,optics)
    theta = np.arctan2(np.linalg.norm(floor[:,:2],axis=-1),floor[:,2])
    inside = (theta<.95)&(uv[:,0]>=0)&(uv[:,0]<=size[0]-1)&(uv[:,1]>=0)&(uv[:,1]<=size[1]-1)
    uv,floor = uv[inside],floor[inside]
    normalized = (uv-[K[0,2],K[1,2]])/[K[0,0],K[1,1]]
    radius = np.linalg.norm(normalized,axis=-1)
    max_radius = fit.radius(estimate['theta_max_rad'])
    valid = radius <= max_radius
    recovered_theta = fit.inverse(np.minimum(radius,max_radius),theta_max=estimate['theta_max_rad'])
    scale = np.divide(np.sin(recovered_theta),radius,out=np.ones_like(radius),where=radius>1e-14)
    rays = np.column_stack([normalized*scale[:,None],np.cos(recovered_theta)])
    valid &= rays[:,1]>1e-12
    recovered = rays[valid]*(1.2/rays[valid,1,None])
    return {'fixed_pose_corner_px':distribution(corner_errors), 'ray_error_px':zones,
            'held_out_theta_max_rad':float(max(theta_coverage)),
            'floor_error_m':distribution(np.linalg.norm(recovered-floor[valid],axis=-1)),
            'floor_points':len(floor),'invalid_floor_points':int((~valid).sum())}
