"""Comprehensive image quality, seam, ghosting, and depth consistency metrics for E-STITCH-01."""
import numpy as np
import scipy.ndimage as ndi

from depth_truth import _sample_depth


def rgb_to_gray(rgb):
    """Convert RGB float array to grayscale."""
    return 0.2989 * rgb[..., 0] + 0.5870 * rgb[..., 1] + 0.1140 * rgb[..., 2]


def rgb_to_lab(rgb):
    """Convert linear RGB to approximate CIELAB space."""
    r, g, b = rgb[..., 0], rgb[..., 1], rgb[..., 2]
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
    """Measure color discontinuity (delta E) and gradient jump across seam boundaries."""
    img = fused_rgb.astype(np.float32)
    if img.max() > 1.0:
        img /= 255.0
        
    H, W, num_cams = weights.shape
    overlap = np.sum(validity, axis=-1) > 1
    
    seam_mask = np.zeros((H, W), dtype=bool)
    for i in range(num_cams):
        w_grad_x = np.abs(ndi.sobel(weights[..., i], axis=1))
        w_grad_y = np.abs(ndi.sobel(weights[..., i], axis=0))
        w_grad = np.hypot(w_grad_x, w_grad_y)
        seam_mask |= (w_grad > 0.1) & overlap
        
    if not seam_mask.any():
        return {
            "seam_pixels": 0,
            "mean_delta_e": 0.0,
            "p95_delta_e": 0.0,
            "max_delta_e": 0.0,
            "gradient_discontinuity": 0.0,
        }
        
    lab = rgb_to_lab(img)
    lab_grad_x = ndi.sobel(lab, axis=1)
    lab_grad_y = ndi.sobel(lab, axis=0)
    lab_grad_mag = np.sqrt(np.sum(lab_grad_x**2 + lab_grad_y**2, axis=-1))
    
    seam_deltas = lab_grad_mag[seam_mask]
    
    gray = rgb_to_gray(img)
    laplacian = np.abs(ndi.laplace(gray))
    seam_laplacian = laplacian[seam_mask]
    
    return {
        "seam_pixels": int(seam_mask.sum()),
        "mean_delta_e": float(np.mean(seam_deltas)),
        "p95_delta_e": float(np.percentile(seam_deltas, 95)),
        "max_delta_e": float(np.max(seam_deltas)),
        "gradient_discontinuity": float(np.mean(seam_laplacian)),
    }


def compute_ghost_contours(fused_rgb, individual_rgb_samples, validity, overlap_corridor_px=20):
    """Detect secondary duplicate edge responses (ghosting) in camera overlap zones."""
    img = fused_rgb.astype(np.float32)
    if img.max() > 1.0:
        img /= 255.0
        
    gray_fused = rgb_to_gray(img)
    overlap = np.sum(validity, axis=-1) > 1
    overlap_pixels = int(overlap.sum())
    
    if overlap_pixels == 0:
        return {"overlap_pixels": 0, "ghost_pixel_count": 0, "ghost_fraction_of_overlap": 0.0, "mean_ghost_offset_px": 0.0}
        
    edges_fused = simple_edge_detection(gray_fused, threshold=0.08) & overlap
    dist_from_edges = ndi.distance_transform_edt(~edges_fused)
    
    ghost_pixels = 0
    ghost_offsets = []
    
    for i in range(4):
        for j in range(i + 1, 4):
            pair_mask = validity[..., i] & validity[..., j]
            if not pair_mask.any():
                continue
            cam_i_gray = rgb_to_gray(individual_rgb_samples[..., i, :])
            cam_j_gray = rgb_to_gray(individual_rgb_samples[..., j, :])
            
            edge_i = simple_edge_detection(cam_i_gray, threshold=0.08) & pair_mask
            edge_j = simple_edge_detection(cam_j_gray, threshold=0.08) & pair_mask
            
            dist_i = ndi.distance_transform_edt(~edge_i)
            secondary = edge_j & (dist_i >= 2.0) & (dist_i <= overlap_corridor_px) & pair_mask
            if secondary.any():
                ghost_pixels += int(secondary.sum())
                ghost_offsets.extend(dist_i[secondary].tolist())
                
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
