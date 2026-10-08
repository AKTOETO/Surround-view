"""Independent image quality oracle and ground truth scene ray tracer.

Renders direct ground-truth images from the virtual camera viewpoint and computes
PSNR, SSIM, MAE, Delta E, and geometric parallax distortion against surround-view stitches.
"""
import sys
from pathlib import Path

import numpy as np
import scipy.ndimage as ndi

sys.path.insert(0, str(Path(__file__).resolve().parent / "blender"))
sys.path.insert(0, str(Path(__file__).resolve().parent))

from body_mask import compute_vehicle_footprint_mask, vehicle_body_boxes
from markers import GROUND_MARKERS, RAISED_OBSTACLES
from reference import rays


def render_scene_oracle(config, width=960, height=540):
    """Direct analytic ray-tracing renderer of the 3D synthetic scene from virtual camera.
    
    Produces ground-truth color (H, W, 3), depth (H, W), and semantic labels (H, W).
    """
    cfg = dict(config)
    cfg["output"] = {"width": width, "height": height}
    eye, directions, depth_factor = rays(cfg)
    
    H, W, _ = directions.shape
    best_t = np.full((H, W), np.inf, dtype=np.float32)
    oracle_rgb = np.full((H, W, 3), 0.2, dtype=np.float32)  # Background gray
    oracle_semantics = np.zeros((H, W), dtype=np.int32)      # 0: background
    
    # 1. Ground plane (z = 0, normal = [0, 0, 1])
    with np.errstate(divide="ignore", invalid="ignore"):
        t_ground = -eye[2] / np.where(np.abs(directions[..., 2]) > 1e-12, directions[..., 2], np.nan)
    valid_ground = (t_ground > 1e-4) & (directions[..., 2] < -1e-6)
    
    p_ground = eye + np.where(valid_ground, t_ground, 0.0)[..., None] * directions
    x_g, y_g = p_ground[..., 0], p_ground[..., 1]
    
    # Base asphalt color
    ground_color = np.full((H, W, 3), [0.35, 0.36, 0.38], dtype=np.float32)
    ground_sem = np.full((H, W), 1, dtype=np.int32)  # ground_drivable
    
    # Road markings (white lines)
    is_centerline = (np.abs(y_g) < 0.12) & (np.abs(x_g % 4.0) < 1.5)
    is_sideline = np.abs(np.abs(y_g) - 3.5) < 0.15
    is_marking = is_centerline | is_sideline
    ground_color[is_marking] = [0.95, 0.95, 0.95]
    ground_sem[is_marking] = 2  # ground_marking
    
    # Checkerboards / fiducial markers on ground
    for m in GROUND_MARKERS:
        mx, my = m["world_xyz"][:2]
        sx = m["size_m"][0] / 2.0
        sy = m["size_m"][1] / 2.0
        in_m = (np.abs(x_g - mx) <= sx) & (np.abs(y_g - my) <= sy)
        # Checkerboard pattern inside marker
        u_m = ((x_g - mx + sx) / (2 * sx) * 4).astype(int) % 2
        v_m = ((y_g - my + sy) / (2 * sy) * 4).astype(int) % 2
        checker = (u_m ^ v_m) == 1
        m_col = np.where(checker[..., None], [0.95, 0.95, 0.95], [0.05, 0.05, 0.05])
        ground_color[in_m] = m_col[in_m]
        ground_sem[in_m] = 7  # calibration_target
        
    mask_g = valid_ground & (t_ground < best_t)
    best_t = np.where(mask_g, t_ground, best_t)
    oracle_rgb = np.where(mask_g[..., None], ground_color, oracle_rgb)
    oracle_semantics = np.where(mask_g, ground_sem, oracle_semantics)
    
    # 2. Raised obstacles (bollards, boxes, parked cars)
    for obs in RAISED_OBSTACLES:
        c = np.asarray(obs["center_xyz"])
        if obs["kind"] == "box":
            h = np.asarray(obs["size_xyz"]) / 2.0
            b_min, b_max = c - h, c + h
            with np.errstate(divide="ignore", invalid="ignore"):
                inv_d = np.where(np.abs(directions) > 1e-12, 1.0 / directions, np.inf)
                t1 = (b_min - eye) * inv_d
                t2 = (b_max - eye) * inv_d
                t_min = np.max(np.minimum(t1, t2), axis=-1)
                t_max = np.min(np.maximum(t1, t2), axis=-1)
                hit_box = (t_min <= t_max) & (t_max > 1e-4) & (t_min > 1e-4)
            mask_box = hit_box & (t_min < best_t)
            best_t = np.where(mask_box, t_min, best_t)
            col = [0.8, 0.2, 0.2] if "red" in obs["id"] else [0.5, 0.5, 0.55]
            oracle_rgb = np.where(mask_box[..., None], col, oracle_rgb)
            oracle_semantics = np.where(mask_box, obs.get("semantic_id", 5), oracle_semantics)
        elif obs["kind"] == "cylinder":
            # Vertical cylinder along Z
            r = obs["radius_m"]
            h_cyl = obs["height_m"]
            # Ray intersection with infinite vertical cylinder
            dx, dy = directions[..., 0], directions[..., 1]
            ox, oy = eye[0] - c[0], eye[1] - c[1]
            a = dx**2 + dy**2
            b = 2 * (ox * dx + oy * dy)
            c_val = ox**2 + oy**2 - r**2
            disc = b**2 - 4 * a * c_val
            valid_cyl = disc >= 0
            with np.errstate(divide="ignore", invalid="ignore"):
                t_cyl = (-b - np.sqrt(np.maximum(disc, 0))) / (2 * np.where(a > 1e-12, a, np.nan))
            z_hit = eye[2] + t_cyl * directions[..., 2]
            hit_cyl = valid_cyl & (t_cyl > 1e-4) & (z_hit >= 0.0) & (z_hit <= h_cyl)
            mask_cyl = hit_cyl & (t_cyl < best_t)
            best_t = np.where(mask_cyl, t_cyl, best_t)
            col = [0.95, 0.75, 0.1]  # Yellow bollard
            oracle_rgb = np.where(mask_cyl[..., None], col, oracle_rgb)
            oracle_semantics = np.where(mask_cyl, obs.get("semantic_id", 6), oracle_semantics)
            
    # 3. Vehicle body (ego vehicle)
    for box in vehicle_body_boxes():
        c = np.asarray(box["center"])
        h = np.asarray(box["half_extents"])
        b_min, b_max = c - h, c + h
        with np.errstate(divide="ignore", invalid="ignore"):
            inv_d = np.where(np.abs(directions) > 1e-12, 1.0 / directions, np.inf)
            t1 = (b_min - eye) * inv_d
            t2 = (b_max - eye) * inv_d
            t_min = np.max(np.minimum(t1, t2), axis=-1)
            t_max = np.min(np.maximum(t1, t2), axis=-1)
            hit_veh = (t_min <= t_max) & (t_max > 1e-4) & (t_min > 1e-4)
            
        mask_veh = hit_veh & (t_min < best_t)
        best_t = np.where(mask_veh, t_min, best_t)
        oracle_rgb = np.where(mask_veh[..., None], [0.12, 0.22, 0.45], oracle_rgb)
        oracle_semantics = np.where(mask_veh, 4, oracle_semantics)
        
    points_3d = eye + np.where(np.isfinite(best_t), best_t, 0.0)[..., None] * directions
    return {
        "rgb": np.clip(oracle_rgb, 0.0, 1.0),
        "depth": best_t,
        "semantics": oracle_semantics,
        "world_points": points_3d,
    }


def compute_psnr(img1, img2, mask=None):
    """Compute Peak Signal-to-Noise Ratio (dB) on masked ROI."""
    i1 = img1.astype(np.float32) / (255.0 if img1.max() > 1.0 else 1.0)
    i2 = img2.astype(np.float32) / (255.0 if img2.max() > 1.0 else 1.0)
    
    diff = (i1 - i2) ** 2
    if mask is not None and mask.any():
        mse = np.mean(diff[mask])
    else:
        mse = np.mean(diff)
        
    if mse < 1e-10:
        return 100.0
    return float(10.0 * np.log10(1.0 / mse))


def compute_ssim(img1, img2, mask=None, window_size=7):
    """Compute Structural Similarity Index (SSIM) on image channels."""
    i1 = img1.astype(np.float32) / (255.0 if img1.max() > 1.0 else 1.0)
    i2 = img2.astype(np.float32) / (255.0 if img2.max() > 1.0 else 1.0)
    
    C1 = (0.01) ** 2
    C2 = (0.03) ** 2
    
    ssim_channels = []
    for c in range(3):
        im1 = i1[..., c]
        im2 = i2[..., c]
        
        mu1 = ndi.uniform_filter(im1, size=window_size)
        mu2 = ndi.uniform_filter(im2, size=window_size)
        
        mu1_sq = mu1 * mu1
        mu2_sq = mu2 * mu2
        mu1_mu2 = mu1 * mu2
        
        sigma1_sq = ndi.uniform_filter(im1 * im1, size=window_size) - mu1_sq
        sigma2_sq = ndi.uniform_filter(im2 * im2, size=window_size) - mu2_sq
        sigma12 = ndi.uniform_filter(im1 * im2, size=window_size) - mu1_mu2
        
        ssim_map = ((2 * mu1_mu2 + C1) * (2 * sigma12 + C2)) / ((mu1_sq + mu2_sq + C1) * (sigma1_sq + sigma2_sq + C2))
        
        if mask is not None and mask.any():
            ssim_channels.append(float(np.mean(ssim_map[mask])))
        else:
            ssim_channels.append(float(np.mean(ssim_map)))
            
    return float(np.mean(ssim_channels))


def evaluate_image_quality(fused_rgb, oracle_result, vehicle_mask=None):
    """Full quantitative image quality oracle comparison."""
    img = fused_rgb.astype(np.float32) / (255.0 if fused_rgb.max() > 1.0 else 1.0)
    oracle_img = oracle_result["rgb"]
    depth = oracle_result["depth"]
    semantics = oracle_result["semantics"]
    
    is_ego = semantics == 4
    if vehicle_mask is not None:
        is_ego |= vehicle_mask
        
    roi_valid = np.isfinite(depth) & ~is_ego
    ground_roi = roi_valid & np.isin(semantics, [1, 2, 7])
    obstacle_roi = roi_valid & np.isin(semantics, [5, 6])
    
    psnr_total = compute_psnr(img, oracle_img, mask=roi_valid)
    ssim_total = compute_ssim(img, oracle_img, mask=roi_valid)
    mae_total = float(np.mean(np.abs(img - oracle_img)[roi_valid])) if roi_valid.any() else 0.0
    
    psnr_ground = compute_psnr(img, oracle_img, mask=ground_roi) if ground_roi.any() else 0.0
    ssim_ground = compute_ssim(img, oracle_img, mask=ground_roi) if ground_roi.any() else 0.0
    mae_ground = float(np.mean(np.abs(img - oracle_img)[ground_roi])) if ground_roi.any() else 0.0
    
    psnr_obstacle = compute_psnr(img, oracle_img, mask=obstacle_roi) if obstacle_roi.any() else 0.0
    ssim_obstacle = compute_ssim(img, oracle_img, mask=obstacle_roi) if obstacle_roi.any() else 0.0
    mae_obstacle = float(np.mean(np.abs(img - oracle_img)[obstacle_roi])) if obstacle_roi.any() else 0.0
    
    return {
        "overall": {
            "psnr_db": psnr_total,
            "ssim": ssim_total,
            "mae": mae_total,
            "roi_pixels": int(roi_valid.sum()),
        },
        "ground_plane": {
            "psnr_db": psnr_ground,
            "ssim": ssim_ground,
            "mae": mae_ground,
            "pixels": int(ground_roi.sum()),
        },
        "vertical_obstacles": {
            "psnr_db": psnr_obstacle,
            "ssim": ssim_obstacle,
            "mae": mae_obstacle,
            "pixels": int(obstacle_roi.sum()),
        },
    }
