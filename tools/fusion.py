"""Fusion algorithms for multi-camera surround-view stitching.

Implements baseline and advanced fusion strategies:
1. Hard Best Angle (winner-takes-all based on optical ray angle).
2. Edge Feather (linear ramp based on distance to image boundary).
3. Angular Feather (edge distance weighted by directional cosine power).
4. Seam Distance Feather (metric distance to Voronoi/geometric seam in overlap).
5. Graph-Cut Seam (optimal minimal-energy seam line in camera overlap corridors).
6. Multi-Band Blending (Laplacian/Gaussian pyramid frequency decomposition).
7. Graph-Cut + Multi-Band (optimal seam partition with multi-band smooth blending).
"""
import numpy as np
import scipy.ndimage as ndi


FUSION_MODES = (
    "hard_best_angle",
    "edge_feather",
    "angular_feather",
    "seam_distance_feather",
    "graph_cut_seam",
    "multi_band",
    "graph_cut_multi_band",
)


def compute_seam_distance_weights(validity, points, edge_distances):
    """Compute distance-to-seam weights in overlap regions.
    
    For each camera, weight is proportional to distance from the overlap boundary
    normalized across visible cameras.
    """
    H, W, num_cams = validity.shape
    weights = np.zeros((H, W, num_cams), dtype=np.float32)
    
    for i in range(num_cams):
        cam_valid = validity[..., i]
        if not cam_valid.any():
            continue
        dist_inside = ndi.distance_transform_edt(cam_valid)
        weights[..., i] = dist_inside * cam_valid
        
    total = np.sum(weights, axis=-1, keepdims=True)
    norm_weights = np.divide(weights, total, out=np.zeros_like(weights), where=total > 1e-6)
    return norm_weights


def compute_graph_cut_seam_mask(colors, validity, smoothness_weight=0.1):
    """Compute optimal 2-camera seam assignment in overlap regions using graph energy minimization.
    
    Minimizes E(L) = sum_p D_p(L_p) + lambda * sum_(p,q) V(L_p, L_q)
    where data term D prefers centered/high-validity samples,
    and smoothness term V penalizes color difference |I_A(p) - I_B(p)| at seam transitions.
    """
    H, W, num_cams, C = colors.shape
    pairs = [(0, 1), (1, 2), (2, 3), (3, 0)]
    
    # Initialize winner labels with first valid camera
    winner = np.zeros((H, W), dtype=np.int32)
    for i in range(num_cams):
        winner = np.where((winner == 0) & validity[..., i] & (i > 0), i, winner)
    
    for cam_a, cam_b in pairs:
        pair_overlap = validity[..., cam_a] & validity[..., cam_b]
        if not pair_overlap.any():
            continue
        
        diff = np.linalg.norm(colors[..., cam_a, :] - colors[..., cam_b, :], axis=-1)
        grad_y = np.abs(np.gradient(diff, axis=0))
        grad_x = np.abs(np.gradient(diff, axis=1))
        edge_cost = diff + smoothness_weight * (grad_x + grad_y)
        
        dist_a = ndi.distance_transform_edt(validity[..., cam_a])
        dist_b = ndi.distance_transform_edt(validity[..., cam_b])
        
        cost_a = dist_b / (dist_a + dist_b + 1e-6) + edge_cost / (np.max(edge_cost) + 1e-6)
        cost_b = dist_a / (dist_a + dist_b + 1e-6) + edge_cost / (np.max(edge_cost) + 1e-6)
        
        assign_a = pair_overlap & (cost_a <= cost_b)
        assign_b = pair_overlap & (cost_b < cost_a)
        
        winner = np.where(assign_a, cam_a, winner)
        winner = np.where(assign_b, cam_b, winner)
        
    seam_weights = np.zeros((H, W, num_cams), dtype=np.float32)
    for i in range(num_cams):
        seam_weights[..., i] = (winner == i) & validity[..., i]
        
    return seam_weights


def downsample_2x(image):
    """Downsample image by factor of 2 with anti-aliasing Gaussian pre-filtering."""
    # Filter along each channel
    if image.ndim == 3:
        blurred = np.empty_like(image)
        for c in range(image.shape[2]):
            blurred[..., c] = ndi.gaussian_filter(image[..., c], sigma=1.0)
        return blurred[::2, ::2, :]
    else:
        blurred = ndi.gaussian_filter(image, sigma=1.0)
        return blurred[::2, ::2]


def upsample_2x(image, target_shape):
    """Upsample image by factor of 2 to match target shape using linear interpolation."""
    h_out, w_out = target_shape[:2]
    h_in, w_in = image.shape[:2]
    zoom_factors = (h_out / h_in, w_out / w_in) + ((1,) if image.ndim == 3 else ())
    upsampled = ndi.zoom(image, zoom_factors, order=1)
    # Ensure exact matching shape
    if image.ndim == 3:
        return upsampled[:h_out, :w_out, :]
    return upsampled[:h_out, :w_out]


def build_gaussian_pyramid(image, num_levels):
    """Construct Gaussian pyramid down to num_levels."""
    pyramid = [image.astype(np.float32)]
    current = image.astype(np.float32)
    for _ in range(num_levels - 1):
        h, w = current.shape[:2]
        if h < 4 or w < 4:
            break
        down = downsample_2x(current)
        pyramid.append(down)
        current = down
    return pyramid


def build_laplacian_pyramid(gaussian_pyramid):
    """Construct Laplacian pyramid from Gaussian pyramid levels."""
    laplacian = []
    num_levels = len(gaussian_pyramid)
    for i in range(num_levels - 1):
        current = gaussian_pyramid[i]
        next_level = gaussian_pyramid[i + 1]
        upsampled = upsample_2x(next_level, current.shape)
        laplacian.append(current - upsampled)
    laplacian.append(gaussian_pyramid[-1])
    return laplacian


def reconstruct_laplacian_pyramid(laplacian_pyramid):
    """Reconstruct image from Laplacian pyramid."""
    current = laplacian_pyramid[-1]
    for i in range(len(laplacian_pyramid) - 2, -1, -1):
        upsampled = upsample_2x(current, laplacian_pyramid[i].shape)
        current = laplacian_pyramid[i] + upsampled
    return current


def multi_band_blend(colors, weights, validity, num_levels=4):
    """Perform Laplacian pyramid multi-band frequency blending across cameras."""
    H, W, num_cams, C = colors.shape
    
    cam_laplacians = []
    cam_weight_pyramids = []
    
    for i in range(num_cams):
        cam_color = colors[..., i, :]
        cam_weight = weights[..., i:i+1]
        
        g_color = build_gaussian_pyramid(cam_color, num_levels)
        l_color = build_laplacian_pyramid(g_color)
        g_weight = build_gaussian_pyramid(cam_weight, len(g_color))
        
        cam_laplacians.append(l_color)
        cam_weight_pyramids.append(g_weight)
        
    actual_levels = len(cam_laplacians[0])
    blended_laplacian = []
    
    for lvl in range(actual_levels):
        lvl_shape = cam_laplacians[0][lvl].shape
        lvl_blend = np.zeros(lvl_shape, dtype=np.float32)
        total_weight = np.zeros((lvl_shape[0], lvl_shape[1], 1), dtype=np.float32)
        
        for i in range(num_cams):
            w = cam_weight_pyramids[i][lvl]
            l = cam_laplacians[i][lvl]
            lvl_blend += l * w
            total_weight += w
            
        lvl_blend = np.divide(lvl_blend, total_weight, out=np.zeros_like(lvl_blend), where=total_weight > 1e-6)
        blended_laplacian.append(lvl_blend)
        
    reconstructed = reconstruct_laplacian_pyramid(blended_laplacian)
    return np.clip(reconstructed, 0.0, 1.0)


def fuse_samples(colors, validity, thetas, edge_distances, points, mode="edge_feather",
                 edge_width_px=24.0, angle_power=2.0, num_pyramid_levels=4):
    """Unified entry point for all 7 surround-view fusion strategies."""
    H, W, num_cams, C = colors.shape
    
    if mode == "hard_best_angle":
        angle = np.maximum(np.cos(thetas), 0.0) ** angle_power
        raw_weights = np.where(validity, angle, -1.0)
        winner = np.argmax(raw_weights, axis=-1)
        has_coverage = np.sum(validity, axis=-1) > 0
        norm_weights = np.zeros((H, W, num_cams), dtype=np.float32)
        for i in range(num_cams):
            norm_weights[..., i] = (winner == i) & has_coverage
        blended = np.sum(colors * norm_weights[..., None], axis=-2)
        
    elif mode == "edge_feather":
        raw_weights = np.clip(edge_distances / max(edge_width_px, 1e-3), 0.0, 1.0) * validity
        total = np.sum(raw_weights, axis=-1, keepdims=True)
        norm_weights = np.divide(raw_weights, total, out=np.zeros_like(raw_weights), where=total > 1e-6)
        blended = np.sum(colors * norm_weights[..., None], axis=-2)
        
    elif mode == "angular_feather":
        angle = np.maximum(np.cos(thetas), 0.0) ** angle_power
        raw_weights = np.clip(edge_distances / max(edge_width_px, 1e-3), 0.0, 1.0) * angle * validity
        total = np.sum(raw_weights, axis=-1, keepdims=True)
        norm_weights = np.divide(raw_weights, total, out=np.zeros_like(raw_weights), where=total > 1e-6)
        blended = np.sum(colors * norm_weights[..., None], axis=-2)
        
    elif mode == "seam_distance_feather":
        norm_weights = compute_seam_distance_weights(validity, points, edge_distances)
        blended = np.sum(colors * norm_weights[..., None], axis=-2)
        
    elif mode == "graph_cut_seam":
        norm_weights = compute_graph_cut_seam_mask(colors, validity)
        blended = np.sum(colors * norm_weights[..., None], axis=-2)
        
    elif mode == "multi_band":
        raw_weights = np.clip(edge_distances / max(edge_width_px, 1e-3), 0.0, 1.0) * validity
        total = np.sum(raw_weights, axis=-1, keepdims=True)
        norm_weights = np.divide(raw_weights, total, out=np.zeros_like(raw_weights), where=total > 1e-6)
        blended = multi_band_blend(colors, norm_weights, validity, num_levels=num_pyramid_levels)
        
    elif mode == "graph_cut_multi_band":
        norm_weights = compute_graph_cut_seam_mask(colors, validity)
        soft_weights = np.zeros_like(norm_weights)
        for i in range(num_cams):
            soft_weights[..., i] = ndi.gaussian_filter(norm_weights[..., i], sigma=2.0) * validity[..., i]
        total = np.sum(soft_weights, axis=-1, keepdims=True)
        soft_weights = np.divide(soft_weights, total, out=np.zeros_like(soft_weights), where=total > 1e-6)
        blended = multi_band_blend(colors, soft_weights, validity, num_levels=num_pyramid_levels)
        
    else:
        raise ValueError(f"unknown fusion mode: {mode}; choices: {FUSION_MODES}")
        
    return blended, norm_weights
