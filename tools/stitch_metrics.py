"""Comprehensive image quality, seam, ghosting, and depth consistency metrics for E-STITCH-01."""
import numpy as np
import scipy.ndimage as ndi

from depth_truth import _sample_depth


def rgb_to_gray(rgb):
    """Convert RGB float array to grayscale."""
    return 0.2989 * rgb[..., 0] + 0.5870 * rgb[..., 1] + 0.1140 * rgb[..., 2]


def rgb_to_lab(rgb):
    """Convert sRGB samples to CIE L*a*b* (D65), using the CIE76 Euclidean delta."""
    srgb = np.asarray(rgb, dtype=np.float32)
    if srgb.max(initial=0.0) > 1.0:
        srgb = srgb / 255.0
    linear = np.where(
        srgb <= 0.04045,
        srgb / 12.92,
        ((srgb + 0.055) / 1.055) ** 2.4,
    )
    r, g, b = linear[..., 0], linear[..., 1], linear[..., 2]
    x = 0.412453 * r + 0.357580 * g + 0.180423 * b
    y = 0.212671 * r + 0.715160 * g + 0.072169 * b
    z = 0.019334 * r + 0.119193 * g + 0.950227 * b
    xn, yn, zn = 0.950456, 1.0, 1.088754
    xr, yr, zr = x / xn, y / yn, z / zn
    
    def f(t):
        return np.where(t > 0.008856, np.cbrt(np.maximum(t, 1e-10)), 7.787 * t + 16.0 / 116.0)
        
    fx, fy, fz = f(xr), f(yr), f(zr)
    L = 116.0 * fy - 16.0
    a = 500.0 * (fx - fy)
    b_val = 200.0 * (fy - fz)
    return np.stack([L, a, b_val], axis=-1)


def simple_edge_detection(gray, threshold=0.1):
    """Compute binary edge map from grayscale gradient magnitude."""
    gx = ndi.sobel(gray, axis=1)
    gy = ndi.sobel(gray, axis=0)
    mag = np.hypot(gx, gy)
    return mag > threshold


def compute_seam_metrics(fused_rgb, weights, validity):
    """Measure CIE76 color step and luminance-gradient jump across label boundaries."""
    img = np.asarray(fused_rgb)
    H, W, num_cams = weights.shape
    if img.shape != (H, W, 3) or validity.shape != (H, W, num_cams):
        raise ValueError("fused image, weights, and validity dimensions do not match")

    overlap = np.sum(validity, axis=-1) > 1
    has_weight = np.sum(weights, axis=-1) > 1e-6
    labels = np.argmax(weights, axis=-1)
    lab = rgb_to_lab(img)
    boundaries = []
    gradient_jumps = []

    horizontal = (
        overlap[:, :-1]
        & overlap[:, 1:]
        & has_weight[:, :-1]
        & has_weight[:, 1:]
        & (labels[:, :-1] != labels[:, 1:])
    )
    if horizontal.any():
        delta = np.linalg.norm(lab[:, :-1] - lab[:, 1:], axis=-1)
        boundaries.append(delta[horizontal])
        if W >= 4:
            inner = horizontal[:, 1:-1]
            if inner.any():
                left_gradient = lab[:, 1:-2, 0] - lab[:, :-3, 0]
                right_gradient = lab[:, 3:, 0] - lab[:, 2:-1, 0]
                gradient_jumps.append(np.abs(right_gradient - left_gradient)[inner])

    vertical = (
        overlap[:-1, :]
        & overlap[1:, :]
        & has_weight[:-1, :]
        & has_weight[1:, :]
        & (labels[:-1, :] != labels[1:, :])
    )
    if vertical.any():
        delta = np.linalg.norm(lab[:-1, :] - lab[1:, :], axis=-1)
        boundaries.append(delta[vertical])
        if H >= 4:
            inner = vertical[1:-1, :]
            if inner.any():
                upper_gradient = lab[1:-2, :, 0] - lab[:-3, :, 0]
                lower_gradient = lab[3:, :, 0] - lab[2:-1, :, 0]
                gradient_jumps.append(np.abs(lower_gradient - upper_gradient)[inner])

    if not boundaries:
        return {
            "seam_pixels": 0,
            "mean_delta_e": 0.0,
            "p95_delta_e": 0.0,
            "max_delta_e": 0.0,
            "gradient_discontinuity": 0.0,
        }

    seam_deltas = np.concatenate(boundaries)
    jumps = np.concatenate(gradient_jumps) if gradient_jumps else np.zeros(0, dtype=float)
    return {
        "seam_pixels": int(seam_deltas.size),
        "mean_delta_e": float(np.mean(seam_deltas)),
        "p95_delta_e": float(np.percentile(seam_deltas, 95)),
        "max_delta_e": float(np.max(seam_deltas)),
        "gradient_discontinuity": float(np.mean(jumps)) if jumps.size else 0.0,
    }


def compute_ghost_contours(fused_rgb, individual_rgb_samples, validity, overlap_corridor_px=20):
    """Detect separated source contours that both survive in the fused output.

    This is a source-disagreement proxy, not an object-ID ground-truth metric. A pair
    contributes only when its two separated source edges are both present in the output.
    """
    img = np.asarray(fused_rgb, dtype=np.float32)
    if img.max(initial=0.0) > 1.0:
        img /= 255.0
    samples = np.asarray(individual_rgb_samples, dtype=np.float32)
    if samples.shape[:2] != img.shape[:2] or samples.shape[-1] != 3:
        raise ValueError("camera samples must have shape HxWxCx3 matching fused image")
    if validity.shape != samples.shape[:3]:
        raise ValueError("validity must have shape HxWxC matching camera samples")

    gray_fused = rgb_to_gray(img)
    overlap = np.sum(validity, axis=-1) > 1
    overlap_pixels = int(overlap.sum())
    
    if overlap_pixels == 0:
        return {"overlap_pixels": 0, "ghost_pixel_count": 0, "ghost_fraction_of_overlap": 0.0, "mean_ghost_offset_px": 0.0}
        
    edges_fused = simple_edge_detection(gray_fused, threshold=0.08) & overlap
    dist_from_edges = ndi.distance_transform_edt(~edges_fused)
    camera_edges = [
        simple_edge_detection(rgb_to_gray(samples[..., i, :]), threshold=0.08)
        for i in range(samples.shape[2])
    ]
    ghost_mask = np.zeros(overlap.shape, dtype=bool)
    ghost_offsets = []
    
    for i in range(samples.shape[2]):
        for j in range(i + 1, samples.shape[2]):
            pair_mask = validity[..., i] & validity[..., j]
            if not pair_mask.any():
                continue

            edge_i = camera_edges[i] & pair_mask
            edge_j = camera_edges[j] & pair_mask
            dist_i = ndi.distance_transform_edt(~edge_i)
            dist_j = ndi.distance_transform_edt(~edge_j)
            candidate_i = edge_i & (dist_j >= 2.0) & (dist_j <= overlap_corridor_px)
            candidate_j = edge_j & (dist_i >= 2.0) & (dist_i <= overlap_corridor_px)
            retained_i = candidate_i & (dist_from_edges <= 1.5)
            retained_j = candidate_j & (dist_from_edges <= 1.5)
            if retained_i.any() and retained_j.any():
                ghost_mask |= retained_i | retained_j
                ghost_offsets.extend(dist_j[retained_i].tolist())
                ghost_offsets.extend(dist_i[retained_j].tolist())

    ghost_pixels = int(ghost_mask.sum())
    ghost_frac = float(ghost_pixels / max(overlap_pixels, 1))
    mean_offset = float(np.mean(ghost_offsets)) if ghost_offsets else 0.0
    
    return {
        "overlap_pixels": overlap_pixels,
        "ghost_pixel_count": ghost_pixels,
        "ghost_fraction_of_overlap": ghost_frac,
        "mean_ghost_offset_px": mean_offset,
    }


def compute_depth_consistency(points, weights, depth_maps, camera_configs, tolerances=(0.02, 0.05, 0.10)):
    """Evaluate physical radial depth matching using exact fisheye projection and depth sampling."""
    H, W, _ = points.shape
    num_cams = len(camera_configs)
    
    exact_matches = {tol: np.zeros((H, W, num_cams), dtype=bool) for tol in tolerances}
    foreground_occlusions = np.zeros((H, W, num_cams), dtype=bool)
    background_occlusions = np.zeros((H, W, num_cams), dtype=bool)
    no_returns = np.zeros((H, W, num_cams), dtype=bool)
    
    for i, cam in enumerate(sorted(camera_configs, key=lambda c: c['id'])):
        T = np.asarray(cam['T_camera_from_vehicle'], dtype=float)
        k = cam['projection']
        p = points @ T[:3, :3].T + T[:3, 3]
        r = np.linalg.norm(p, axis=-1)
        rho = np.hypot(p[..., 0], p[..., 1])
        theta = np.arctan2(rho, p[..., 2])
        td = theta.copy()
        for j, coeff in enumerate(k['k']):
            td += coeff * theta**(2*j+3)
        scale = np.divide(td, rho, out=np.zeros_like(td), where=rho > 1e-14)
        u = k['fx'] * p[..., 0] * scale + k['cx']
        v = k['fy'] * p[..., 1] * scale + k['cy']
        
        h_cam, w_cam = depth_maps[i].shape[:2]
        valid_proj = (
            (p[..., 2] > k['z_epsilon_m'])
            & (theta <= k['theta_max_rad'])
            & (u >= 0)
            & (v >= 0)
            & (u <= w_cam - 1)
            & (v <= h_cam - 1)
        )
        
        uv = np.stack([u, v], axis=-1)
        sampled = _sample_depth(depth_maps[i], uv, invalid_value=1e9)
        has_depth = np.isfinite(sampled) & (sampled > 0)
        
        no_returns[..., i] = valid_proj & ~has_depth
        delta_depth = sampled - r
        
        for tol in tolerances:
            tol_bound = np.maximum(tol, 0.01 * r)
            exact_matches[tol][..., i] = valid_proj & has_depth & (np.abs(delta_depth) <= tol_bound)
            
        foreground_occlusions[..., i] = valid_proj & has_depth & (delta_depth < -np.maximum(0.05, 0.01 * r))
        background_occlusions[..., i] = valid_proj & has_depth & (delta_depth > np.maximum(0.05, 0.01 * r))
        
    total_weights = np.sum(weights, axis=-1)
    has_weights = total_weights > 1e-6
    
    results = {}
    for tol in tolerances:
        tol_matches = exact_matches[tol]
        any_exact = np.any(tol_matches, axis=-1)
        exact_cov = float(np.mean(any_exact[has_weights])) if has_weights.any() else 0.0
        
        exact_weight = np.sum(weights * tol_matches, axis=-1)
        consistent_weight_frac = float(np.sum(exact_weight) / np.sum(total_weights)) if np.sum(total_weights) > 0 else 0.0
        
        results[str(tol)] = {
            "exact_visible_coverage_fraction": exact_cov,
            "depth_consistent_weight_fraction": consistent_weight_frac,
        }
        
    fg_weight = np.sum(weights * foreground_occlusions, axis=-1)
    results["foreground_occluded_weight_fraction"] = float(np.sum(fg_weight) / np.sum(total_weights)) if np.sum(total_weights) > 0 else 0.0
    return results
