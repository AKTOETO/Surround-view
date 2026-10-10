"""Validation and keyframe helpers for controlled coded-target motion."""
import math


def frame_positions(target, capture):
    """Validate the frozen per-capture-frame target trajectory."""
    positions = target.get('motion_positions_m')
    if positions is None:
        return []
    frames, step = capture.get('frames'), capture.get('frame_step')
    if (not isinstance(frames, int) or not isinstance(step, int) or frames < 1 or step < 1
            or len(positions) != frames):
        raise ValueError('motion_positions_m must contain one position per capture frame')
    result = []
    for index, position in enumerate(positions):
        if (not isinstance(position, (list, tuple)) or len(position) != 3
                or not all(isinstance(value, (int, float)) and math.isfinite(value) for value in position)):
            raise ValueError('each motion position must be three finite coordinates in metres')
        result.append((index*step+1, tuple(float(value) for value in position)))
    if 'center_m' in target and tuple(float(v) for v in target['center_m']) != result[0][1]:
        raise ValueError('target center_m must equal the first motion position')
    return result


def position_for_capture(target, capture, index):
    """Return the explicitly scheduled target position for one captured row."""
    positions = frame_positions(target, capture)
    if not positions:
        return None
    if not isinstance(index, int) or not 0 <= index < len(positions):
        raise ValueError('capture row index outside target motion plan')
    return positions[index][1]


def validate_captured_positions(target, frames, tolerance_m=1e-5):
    """Reject captures whose recorded evaluated target path differs from the plan."""
    planned = target.get('motion_positions_m')
    if planned is None:
        return
    if len(frames) != len(planned) or tolerance_m <= 0:
        raise ValueError('captured frame count does not match target motion plan')
    for index, (expected, frame) in enumerate(zip(planned, frames)):
        actual = frame.get('diagnostic_target_position_m')
        if actual is None or len(actual) != 3 or any(
                not math.isfinite(float(a)) or abs(float(a)-float(e)) > tolerance_m
                for a, e in zip(actual, expected)):
            raise ValueError(f'captured target position differs from motion plan at frame {index}')
