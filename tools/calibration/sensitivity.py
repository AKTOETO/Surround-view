"""Equal-RMS UV perturbations and directional finite output response."""
import numpy as np

from calibration.optical_models import distribution, fitted_model, project
from calibration.raster_board import K

AMPLITUDES = (.025, .05, .2)
DIRECTIONS = (('shift_x', 0), ('radial', 0), ('tangential', 0),
              ('random', 1021), ('random', 1022), ('random', 1023))


def direction_field(truth, name, seed=0):
    truth = np.asarray(truth, dtype=float)
    if truth.shape != (12, 54, 2) or not np.isfinite(truth).all():
        raise ValueError('12 finite views with 54 UV corners required')
    if name == 'shift_x':
        field = np.zeros_like(truth)
        field[..., 0] = 1.
    elif name in ('radial', 'tangential'):
        radial = truth - K[:2, 2]
        norm = np.linalg.norm(radial, axis=-1)
        if np.any(norm <= 1e-12):
            raise ValueError('radial direction undefined at optical axis')
        field = radial / norm[..., None]
        if name == 'tangential':
            field = np.stack([-field[..., 1], field[..., 0]], axis=-1)
    elif name == 'random':
        field = np.random.default_rng(seed).normal(size=truth.shape)
        field -= field.mean(axis=1, keepdims=True)
    else:
        raise ValueError('unknown perturbation direction')
    return field / np.sqrt(np.mean(np.sum(field**2, axis=-1)))


def output_controls(estimate, optics):
    matrix, model = fitted_model(estimate)
    theta, phi = np.meshgrid(np.linspace(.02, 1., 100), np.linspace(-np.pi, np.pi, 128, endpoint=False))
    rays = np.stack([np.sin(theta)*np.cos(phi), np.sin(theta)*np.sin(phi), np.cos(theta)], axis=-1)
    true_uv = project(rays, K, optics)
    visible = ((true_uv[..., 0] >= 0) & (true_uv[..., 0] <= 639) &
               (true_uv[..., 1] >= 0) & (true_uv[..., 1] <= 479) & (theta >= .7))
    projected = project(rays, matrix, model)[visible]
    x, z = np.meshgrid(np.linspace(-3, 3, 31), np.linspace(1, 8, 40))
    floor = np.stack([x, np.full_like(x, 1.2), z], axis=-1).reshape(-1, 3)
    uv = project(floor, K, optics)
    angle = np.arctan2(np.linalg.norm(floor[:, :2], axis=-1), floor[:, 2])
    visible = ((angle < .95) & (uv[:, 0] >= 0) & (uv[:, 0] <= 639) &
               (uv[:, 1] >= 0) & (uv[:, 1] <= 479))
    normalized = (uv[visible] - matrix[:2, 2]) / [matrix[0, 0], matrix[1, 1]]
    radius = np.linalg.norm(normalized, axis=-1)
    limit = model.radius(estimate['theta_max_rad'])
    valid = radius <= limit
    angle = model.inverse(np.minimum(radius, limit), theta_max=estimate['theta_max_rad'])
    scale = np.divide(np.sin(angle), radius, out=np.ones_like(radius), where=radius > 1e-14)
    rays = np.column_stack([normalized * scale[:, None], np.cos(angle)])
    valid &= rays[:, 1] > 1e-12
    recovered = np.full_like(rays, np.nan)
    recovered[valid] = rays[valid] * (1.2 / rays[valid, 1, None])
    return projected, recovered, valid


def pair_response(positive, negative, baseline, amplitude, optics):
    if not np.isfinite(amplitude) or amplitude <= 0:
        raise ValueError('positive finite amplitude required')
    a, ap, av = output_controls(positive, optics)
    b, bp, bv = output_controls(negative, optics)
    c, cp, cv = output_controls(baseline, optics)
    valid = av & bv & cv
    vector = lambda m: np.array([m['fx'], m['fy'], m['cx'], m['cy'], *m['k']])
    derivative = (vector(positive) - vector(negative)) / (2 * amplitude)
    return {'outer_gain_px_per_px': distribution(np.linalg.norm(a-b, axis=-1) / (2*amplitude)),
            'floor_gain_m_per_px': distribution(np.linalg.norm(ap[valid]-bp[valid], axis=-1) / (2*amplitude)),
            'outer_even_px_per_px2': distribution(np.linalg.norm((a+b)/2-c, axis=-1) / amplitude**2),
            'floor_even_m_per_px2': distribution(np.linalg.norm((ap[valid]+bp[valid])/2-cp[valid], axis=-1) / amplitude**2),
            'floor_points': len(valid), 'invalid_floor_points': int((~valid).sum()),
            'parameter_derivative_per_px': derivative.tolist(),
            'scaled_parameter_derivative_l2_per_px': float(np.linalg.norm(derivative / [300, 295, 640, 480, 1, 1, 1, 1]))}
