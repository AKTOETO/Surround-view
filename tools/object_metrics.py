"""Controlled coded-target metrics measured from final RGB, with explicit scope limits."""
import numpy as np
from scipy import ndimage as ndi


def spatial_rois(labels, objects, ego_ids, target_id, visibility, ground_prefixes):
    """Disjoint semantic interiors/bands from independent direct-view IDs only."""
    labels, visibility = np.asarray(labels), np.asarray(visibility)
    ids = list(objects.values())
    if (labels.ndim != 2 or not np.issubdtype(labels.dtype, np.integer)
            or visibility.dtype != bool or visibility.shape != labels.shape
            or len(set(ids)) != len(ids) or any(type(v) is not int or v <= 0 for v in ids)
            or target_id not in ids or target_id in ego_ids
            or not set(ego_ids) <= set(ids) or not ground_prefixes
            or not np.isin(labels, [0]+ids).all()):
        raise ValueError('known unique object IDs, non-ego target and boolean visibility required')
    def base_name(name):
        base, dot, suffix = name.rpartition('.')
        return base if dot and suffix.isdigit() else name
    ground_ids = [value for name,value in objects.items() if base_name(name) in ground_prefixes]
    if target_id in ground_ids or set(ground_ids) & set(ego_ids):
        raise ValueError('semantic ground must not contain ego or diagnostic target')
    visible = (labels != 0) & ~np.isin(labels,ego_ids) & visibility
    boundary = ndi.maximum_filter(labels,size=3) != ndi.minimum_filter(labels,size=3)
    boundary = ndi.binary_dilation(boundary,iterations=2)
    ground = np.isin(labels,ground_ids)
    target = labels == target_id
    classes = {'ground':ground, 'coded_target':target, 'other_scene':~(ground | target)}
    return {group+'_'+part:visible & mask & (boundary if part == 'boundary' else ~boundary)
            for group,mask in classes.items() for part in ('interior','boundary')}


def spatial_errors(linear_absolute_error, masks):
    """Summarize scene error; an empty stratum has undefined, not zero, error."""
    error = np.asarray(linear_absolute_error)
    if (error.ndim != 3 or error.shape[-1] != 3 or not error.size or not np.isfinite(error).all()
            or error.min() < 0 or error.max() > 1):
        raise ValueError('finite HxWx3 absolute linear error in [0,1] required')
    output = {}
    assigned = np.zeros(error.shape[:2],bool)
    for name,mask in masks.items():
        mask = np.asarray(mask)
        if mask.dtype != bool or mask.shape != error.shape[:2]:
            raise ValueError('matching boolean spatial mask required')
        if (assigned & mask).any():
            raise ValueError('spatial strata must be disjoint')
        assigned |= mask
        values = error[mask]
        output[name] = {'pixels':int(mask.sum()),
                        'linear_mae':float(values.mean()) if values.size else None,
                        'linear_channel_p95':float(np.percentile(values,95)) if values.size else None,
                        'linear_channel_max':float(values.max()) if values.size else None}
    return output


def target_mask(rgb, threshold=.15):
    rgb = np.asarray(rgb)
    if rgb.ndim != 3 or rgb.shape[-1] != 3 or not np.isfinite(rgb).all():
        raise ValueError('finite HxWx3 RGB required')
    value = rgb.astype(float)/255 if rgb.dtype == np.uint8 else rgb.astype(float)
    if value.min() < 0 or value.max() > 1 or not 0 < threshold < 1:
        raise ValueError('RGB in [0,1] and chroma threshold in (0,1) required')
    # Magenta emission target; natural-scene object recognition is outside this fixture.
    return np.minimum(value[..., 0], value[..., 2])-value[..., 1] > threshold


def remove_small(mask, minimum):
    labels, count = ndi.label(mask, structure=np.ones((3,3)))
    sizes = np.bincount(labels.ravel())
    keep = sizes >= minimum
    keep[0] = False
    return keep[labels], int(keep.sum())


def row_runs(mask):
    return np.sum(mask & ~np.pad(mask[:, :-1], ((0,0),(1,0))), axis=1)


def measure_target(predicted, truth, minimum_pixels=8):
    """Components, repeated horizontal runs, missing support and geometric error.

    Extra components/runs are controlled-target diagnostics, not a general ghost detector.
    Joined copies may remain one component/run; IoU still records their shape error.
    """
    predicted, truth = np.asarray(predicted, bool), np.asarray(truth, bool)
    if predicted.shape != truth.shape or truth.ndim != 2 or minimum_pixels < 1:
        raise ValueError('matching 2D masks and positive minimum area required')
    predicted, components = remove_small(predicted, minimum_pixels)
    truth, expected = remove_small(truth, minimum_pixels)
    n, t = int(predicted.sum()), int(truth.sum())
    intersection, union = int((predicted & truth).sum()), int((predicted | truth).sum())
    rows = truth.any(axis=1)
    extra = rows & (row_runs(predicted) > row_runs(truth))
    missing = rows & ~predicted.any(axis=1)
    return {'predicted_pixels': n, 'truth_pixels': t, 'components': components,
            'truth_components': expected, 'extra_components': max(0, components-expected),
            'iou': intersection/union if union else None,
            'recall': intersection/t if t else None,
            'precision': intersection/n if n else None,
            'truth_rows': int(rows.sum()), 'extra_run_rows': int(extra.sum()),
            'extra_run_row_fraction': float(extra.sum()/rows.sum()) if rows.any() else None,
            'missing_rows': int(missing.sum())}


def measure_target_support(support, truth, thresholds=(.25, .50, .75)):
    """Measure camera-source identity support inside/outside direct target truth.

    This is provenance, not a reconstruction of final RGB contribution (especially
    for multi-band blending). Thresholds are fixed by the experiment protocol.
    """
    support, truth = np.asarray(support, float), np.asarray(truth, bool)
    if (support.ndim != 2 or support.shape != truth.shape or not np.isfinite(support).all()
            or np.any(support < -1e-6) or np.any(support > 1+1e-6) or not truth.any()):
        raise ValueError('finite normalized 2D support and nonempty matching truth required')
    if any(not np.isfinite(t) or not 0 < t <= 1 for t in thresholds):
        raise ValueError('support thresholds must be finite and in (0,1]')
    support = np.clip(support, 0, 1)
    total = float(support.sum())
    inside = float(support[truth].sum())
    outside = float(support[~truth].sum())
    return {
        'mean_support_inside_truth': float(support[truth].mean()),
        'outside_support_fraction': outside/total if total > 0 else None,
        'support_mass_over_truth_area': total/float(truth.sum()),
        'thresholds': {str(t): measure_target(support >= t, truth) for t in thresholds},
    }


def projected_object_ids(camera, points, ids):
    """Nearest-label sampling at the carrier's fisheye UV; no interpolation of IDs."""
    ids = np.asarray(ids)
    if ids.ndim != 2 or not np.issubdtype(ids.dtype, np.integer):
        raise ValueError('2D integer source labels required')
    T, k = np.asarray(camera['T_camera_from_vehicle']), camera['projection']
    if k.get('alpha', 0) != 0:
        raise ValueError('reference renderer currently requires zero skew')
    points = np.asarray(points, float)
    finite = np.isfinite(points).all(axis=-1)
    p = np.nan_to_num(points) @ T[:3,:3].T + T[:3,3]
    rho = np.hypot(p[...,0], p[...,1])
    theta = np.arctan2(rho, p[...,2])
    distorted = theta+sum(c*theta**(2*j+3) for j,c in enumerate(k['k']))
    scale = np.divide(distorted, rho, out=np.zeros_like(rho), where=rho > 1e-14)
    u, v = k['fx']*p[...,0]*scale+k['cx'], k['fy']*p[...,1]*scale+k['cy']
    h,w = ids.shape
    valid = (finite & (p[...,2] > k['z_epsilon_m']) & (theta <= k['theta_max_rad'])
             & (u >= 0) & (v >= 0) & (u <= w-1) & (v <= h-1))
    x,y = np.rint(np.clip(u,0,w-1)).astype(int), np.rint(np.clip(v,0,h-1)).astype(int)
    return np.where(valid, ids[y,x], 0)
