"""Signed localization diagnostics with the unmarked board's 180-degree ambiguity."""
import numpy as np

from calibration.raster_board import K, detection_error


def aligned_truth(detected, truth):
    detected, truth = np.asarray(detected, dtype=float), np.asarray(truth, dtype=float)
    if not np.isfinite(detected).all() or not np.isfinite(truth).all():
        raise ValueError('finite corner coordinates required')
    error = detection_error(detected, truth)
    return truth[::-1].copy() if error['reversed_indexing'] else truth.copy()


def signed_localization(detected, truth):
    detected, truth = np.asarray(detected, dtype=float), np.asarray(truth, dtype=float)
    result = detection_error(detected, truth)
    truth = aligned_truth(detected, truth)
    delta = detected - truth
    radial = truth - K[:2, 2]
    radius = np.linalg.norm(radial, axis=1)
    valid = radius > 1e-12
    radial = radial[valid] / radius[valid, None]
    tangent = np.column_stack([-radial[:, 1], radial[:, 0]])
    result.update(mean_uv_px=delta.mean(axis=0).tolist(), std_uv_px=delta.std(axis=0).tolist(),
                  radial_tangential_count=int(valid.sum()))
    for name, axis in (('radial', radial), ('tangential', tangent)):
        values = np.sum(delta[valid] * axis, axis=1)
        result[name + '_px'] = {'mean': float(values.mean()), 'std': float(values.std()),
                               'absolute_p95': float(np.percentile(np.abs(values), 95))} if len(values) else None
    return result
