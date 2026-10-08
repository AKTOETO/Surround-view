"""Read Blender float-Z EXR cube faces and convert them to fisheye ray ranges."""
import numpy as np

from blender.rig import cube_coordinates, face_basis
from simulator import camera_rays


def read_openexr_depth(path):
    """Read one scalar float channel from a Blender Z-pass OpenEXR file."""
    try:
        import OpenEXR
    except ImportError as error:
        raise RuntimeError("depth truth conversion requires the Python OpenEXR module") from error
    image = OpenEXR.InputFile(str(path))
    header = image.header()
    channels = header["channels"]
    matches = [name for name in channels if name.rsplit(".", 1)[0].lower() in {"depth", "z"}]
    if len(matches) != 1:
        raise ValueError(f"expected one depth EXR channel, found: {list(channels)}")
    window = header["dataWindow"]
    width = window.max.x - window.min.x + 1
    height = window.max.y - window.min.y + 1
    values = np.frombuffer(image.channel(matches[0]), dtype=np.float32)
    if values.size != width * height:
        raise ValueError("EXR depth payload size does not match dataWindow")
    return values.reshape(height, width).copy()


def _sample_depth(image, uv, invalid_value):
    """Bilinear-sample continuous surfaces; use nearest valid texel at depth edges."""
    h, w = image.shape
    x = np.clip(uv[..., 0], 0, w - 1)
    y = np.clip(uv[..., 1], 0, h - 1)
    x0, y0 = np.floor(x).astype(int), np.floor(y).astype(int)
    x1, y1 = np.minimum(x0 + 1, w - 1), np.minimum(y0 + 1, h - 1)
    fx, fy = x - x0, y - y0
    samples = np.stack([image[y0, x0], image[y0, x1], image[y1, x0], image[y1, x1]], axis=-1)
    valid = np.isfinite(samples) & (samples > 0) & (samples < invalid_value)
    low = np.min(np.where(valid, samples, np.inf), axis=-1)
    high = np.max(np.where(valid, samples, -np.inf), axis=-1)
    stable = valid.all(axis=-1) & ((high - low) <= np.maximum(.01, .01 * low))
    weights = np.stack([(1 - fx) * (1 - fy), fx * (1 - fy),
                        (1 - fx) * fy, fx * fy], axis=-1)
    bilinear = np.sum(np.where(valid, samples, 0) * weights, axis=-1)
    # Mask invalid texels with -1, not zero: a valid edge texel can itself
    # have a zero bilinear coefficient, and ties at zero otherwise select an
    # invalid sentinel at index 0.
    nearest = np.argmax(np.where(valid, weights, -1.0), axis=-1)
    selected = np.take_along_axis(samples, nearest[..., None], axis=-1)[..., 0]
    any_valid = valid.any(axis=-1)
    result = np.where(stable, bilinear, np.where(any_valid, selected, np.nan))
    return result.astype(np.float32)


def convert_camera_depth(camera, faces, face_size, invalid_value=1e9):
    """Return float32 radial ranges (metres) for calibrated fisheye pixel rays.

    Blender Z is camera-axis depth, while the returned range is distance from the
    optical center along the unit fisheye ray. NaN means the ray hit no surface.
    """
    rays, theta = camera_rays(camera)
    result = np.full(theta.shape, np.nan, dtype=np.float32)
    valid_fov = theta <= camera["projection"]["theta_max_rad"]
    mappings = cube_coordinates(rays, face_size)
    for name, (mask, uv) in mappings.items():
        mask &= valid_fov
        if not np.any(mask):
            continue
        depth = faces.get(name)
        if depth is None or depth.shape != (face_size, face_size):
            raise ValueError(f"missing or incorrectly sized depth face {name}")
        basis = face_basis(name)
        optical_axis_cosine = (rays @ basis.T)[..., 2]
        z_depth = _sample_depth(depth, uv, invalid_value)
        radial_range = np.divide(z_depth, optical_axis_cosine,
                                 out=np.full_like(z_depth, np.nan),
                                 where=optical_axis_cosine > 1e-8)
        result[mask] = radial_range[mask]
    return result
