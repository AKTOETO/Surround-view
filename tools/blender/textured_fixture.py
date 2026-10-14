"""Independent, deterministic bitmap fixture; not a product camera source."""
import numpy as np


def texture(seed, size=128):
    if isinstance(seed, bool) or not isinstance(seed, int) or not 0 <= seed < 2**32:
        raise ValueError('uint32 seed required')
    if not isinstance(size, int) or isinstance(size, bool) or not 16 <= size <= 1024:
        raise ValueError('texture size 16..1024 required')
    y, x = np.mgrid[:size, :size] / size
    rng = np.random.default_rng(seed)
    phase = rng.uniform(0, 2*np.pi, 3)
    coarse = rng.uniform(-.12, .12, (8, 8, 3))
    grain = coarse[(y*8).astype(int), (x*8).astype(int)]
    colors = np.stack([.45+.23*np.sin(2*np.pi*(3*x+2*y)+phase[0]),
                       .45+.23*np.cos(2*np.pi*(x-4*y)+phase[1]),
                       .45+.23*np.sin(2*np.pi*(5*x-y)+phase[2])], axis=-1)
    return np.rint(np.clip(colors+grain, .06, .94)*255).astype(np.uint8)
