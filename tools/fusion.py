"""Fusion algorithms for multi-camera surround-view stitching.

Implements baseline and advanced fusion strategies:
1. Hard Best Angle (winner-takes-all based on optical ray angle).
2. Edge Feather (linear ramp based on distance to image boundary).
3. Angular Feather (edge distance weighted by directional cosine power).
4. Seam Distance Feather (pixel-space EDT of each camera validity mask).
5. Graph-Cut Seam (independent binary cuts in exactly-two-camera overlap).
6. Multi-Band Blending (Laplacian/Gaussian pyramid frequency decomposition).
7. Graph-Cut + Multi-Band (binary cuts / centrality fallback and pyramid blending).
"""
import numpy as np
import scipy.ndimage as ndi
from scipy.sparse import coo_matrix
from scipy.sparse.csgraph import breadth_first_order, maximum_flow


FUSION_MODES = (
    "hard_best_angle",
    "edge_feather",
    "angular_feather",
    "seam_distance_feather",
    "graph_cut_seam",
    "multi_band",
    "graph_cut_multi_band",
)
FUSION_IMPLEMENTATION = "validity_zero_extension_v2"


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
    """Solve binary s-t cuts independently in regions covered by exactly two cameras.

    The unary term prefers samples farther from each camera's validity boundary. The
    pairwise Potts cost is lower where the two source colors agree, so a seam can pass
    through those pixels. In 3+ camera overlaps, maximum-centrality labels are used;
    exact centrality ties share weight uniformly, avoiding a camera-index preference.
    This is not a global multi-label optimization.
    """
    H, W, num_cams, C = colors.shape
    if colors.ndim != 4 or validity.shape != (H, W, num_cams):
        raise ValueError("colors must be HxWxCx3 and validity must be HxWxC")
    if smoothness_weight < 0 or not np.isfinite(smoothness_weight):
        raise ValueError("smoothness_weight must be finite and non-negative")

    distances = np.stack(
        [ndi.distance_transform_edt(validity[..., i]) for i in range(num_cams)], axis=-1
    )
    winner = np.argmax(np.where(validity, distances, -1.0), axis=-1)
    coverage = np.sum(validity, axis=-1)
    winner[coverage == 0] = -1
    pair_only = coverage == 2
    capacity_scale = 1000.0

    for cam_a in range(num_cams):
        for cam_b in range(cam_a + 1, num_cams):
            mask = validity[..., cam_a] & validity[..., cam_b] & pair_only
            coords = np.argwhere(mask)
            count = len(coords)
            if count == 0:
                continue

            node_ids = np.full((H, W), -1, dtype=np.int32)
            node_ids[mask] = np.arange(count, dtype=np.int32)
            y = coords[:, 0]
            x = coords[:, 1]
            d_a = distances[y, x, cam_a]
            d_b = distances[y, x, cam_b]
            d_sum = d_a + d_b + 1e-6
            cost_a = 1.0 - d_a / d_sum
            cost_b = 1.0 - d_b / d_sum

            rows = [np.full(count, count, dtype=np.int32), np.arange(count, dtype=np.int32)]
            cols = [np.arange(count, dtype=np.int32), np.full(count, count + 1, dtype=np.int32)]
            data = [
                np.rint(cost_b * capacity_scale).astype(np.int32),
                np.rint(cost_a * capacity_scale).astype(np.int32),
            ]

            disagreement = np.linalg.norm(
                colors[..., cam_a, :] - colors[..., cam_b, :], axis=-1
            )
            scale = float(np.percentile(disagreement[mask], 95)) if count else 0.0
            normalized = np.clip(disagreement / max(scale, 1e-6), 0.0, 1.0)
            for dy, dx in ((0, 1), (1, 0)):
                if dy:
                    valid_edge = mask[:-1, :] & mask[1:, :]
                    y0, x0 = np.where(valid_edge)
                    y1, x1 = y0 + 1, x0
                else:
                    valid_edge = mask[:, :-1] & mask[:, 1:]
                    y0, x0 = np.where(valid_edge)
                    y1, x1 = y0, x0 + 1
                if not len(y0):
                    continue
                first = node_ids[y0, x0]
                second = node_ids[y1, x1]
                local_diff = 0.5 * (normalized[y0, x0] + normalized[y1, x1])
                edge_capacity = np.maximum(
                    1,
                    np.rint(smoothness_weight * (0.05 + local_diff) * capacity_scale),
                ).astype(np.int32)
                rows.extend((first, second))
                cols.extend((second, first))
                data.extend((edge_capacity, edge_capacity))

            source, sink = count, count + 1
            graph = coo_matrix(
                (np.concatenate(data), (np.concatenate(rows), np.concatenate(cols))),
                shape=(count + 2, count + 2),
                dtype=np.int32,
            ).tocsr()
            flow = maximum_flow(graph, source, sink, method="dinic").flow.tocsr()
            residual = (graph - flow).tocsr()
            reachable = breadth_first_order(
                residual, source, directed=True, return_predecessors=False
            )
            source_side = np.zeros(count, dtype=bool)
            source_side[reachable[reachable < count]] = True
            winner[y, x] = np.where(source_side, cam_a, cam_b)

    seam_weights = np.zeros((H, W, num_cams), dtype=np.float32)
    for i in range(num_cams):
        seam_weights[..., i] = (winner == i) & validity[..., i]

    # The fallback for 3+ overlaps has no multi-label energy. Equal centrality
    # should not privilege whichever camera happens to have the smallest index.
    multi_overlap = coverage >= 3
    if np.any(multi_overlap):
        maximum_distance = np.max(np.where(validity, distances, -np.inf), axis=-1, keepdims=True)
        centrality_ties = validity & np.isclose(
            distances, maximum_distance, rtol=1e-6, atol=1e-6
        )
        tie_count = np.sum(centrality_ties, axis=-1, keepdims=True)
        centrality_weights = np.divide(
            centrality_ties,
            tie_count,
            out=np.zeros_like(distances, dtype=np.float32),
            where=tie_count > 0,
        )
        seam_weights[multi_overlap] = centrality_weights[multi_overlap]
        
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


def build_normalized_pyramid(image, validity, num_levels):
    """Filter observed color mass and support independently at each scale."""
    current = image.astype(np.float32)
    support = validity.astype(np.float32)
    result = [current]
    for _ in range(num_levels - 1):
        if min(current.shape[:2]) < 4:
            break
        mass = downsample_2x(current * support[..., None])
        support = downsample_2x(support)
        current = np.divide(mass, support[..., None], out=np.zeros_like(mass),
                            where=support[..., None] > 1e-12)
        result.append(current)
    return result


def multi_band_blend(colors, weights, validity, num_levels=4, boundary="zero"):
    """Perform Laplacian pyramid multi-band frequency blending across cameras."""
    H, W, num_cams, C = colors.shape
    
    cam_laplacians = []
    cam_weight_pyramids = []
    
    for i in range(num_cams):
        # Unobserved RGB is not evidence. Define zero extension before filtering,
        # matching the projected layers used by the native server backend.
        cam_color = np.where(validity[..., i, None], colors[..., i, :], 0)
        cam_weight = weights[..., i:i+1]
        
        if boundary == "normalized":
            g_color = build_normalized_pyramid(cam_color, validity[..., i], num_levels)
        elif boundary == "zero":
            g_color = build_gaussian_pyramid(cam_color, num_levels)
        else:
            raise ValueError("unknown pyramid boundary")
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
                 edge_width_px=24.0, angle_power=2.0, num_pyramid_levels=4,
                 smoothness_weight=0.1, pyramid_boundary="zero"):
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
        norm_weights = compute_graph_cut_seam_mask(colors, validity, smoothness_weight)
        blended = np.sum(colors * norm_weights[..., None], axis=-2)
        
    elif mode == "multi_band":
        raw_weights = np.clip(edge_distances / max(edge_width_px, 1e-3), 0.0, 1.0) * validity
        total = np.sum(raw_weights, axis=-1, keepdims=True)
        norm_weights = np.divide(raw_weights, total, out=np.zeros_like(raw_weights), where=total > 1e-6)
        blended = multi_band_blend(colors, norm_weights, validity, num_levels=num_pyramid_levels, boundary=pyramid_boundary)
        
    elif mode == "graph_cut_multi_band":
        norm_weights = compute_graph_cut_seam_mask(colors, validity, smoothness_weight)
        soft_weights = np.zeros_like(norm_weights)
        for i in range(num_cams):
            soft_weights[..., i] = ndi.gaussian_filter(norm_weights[..., i], sigma=2.0) * validity[..., i]
        total = np.sum(soft_weights, axis=-1, keepdims=True)
        soft_weights = np.divide(soft_weights, total, out=np.zeros_like(soft_weights), where=total > 1e-6)
        blended = multi_band_blend(colors, soft_weights, validity, num_levels=num_pyramid_levels, boundary=pyramid_boundary)
        
    else:
        raise ValueError(f"unknown fusion mode: {mode}; choices: {FUSION_MODES}")
        
    return blended, norm_weights
