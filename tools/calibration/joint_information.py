"""Joint camera/board-pose nonlinear least squares and local information analysis."""
from dataclasses import dataclass

import numpy as np
from scipy.optimize import least_squares
from scipy.sparse import lil_matrix
from scipy.spatial.transform import Rotation

from calibration.optical_models import OpticalModel


@dataclass
class JointFit:
    estimate: dict
    poses: list
    residuals: np.ndarray
    jacobian: np.ndarray
    success: bool
    status: int
    message: str
    cost: float
    optimality: float
    evaluations: int
    parameter_names: list
    parameter_scales: np.ndarray
    intrinsics_count: int


def _model_vector(estimate, order):
    names = ['fx', 'fy', 'cx', 'cy'] + [f'k{i + 1}' for i in range(order)]
    values = np.array([estimate['fx'], estimate['fy'], estimate['cx'], estimate['cy'],
                       *estimate['k'][:order]], dtype=float)
    scales = np.array([300., 295., 640., 480.] + [1.] * order)
    return names, values, scales


def _unpack(parameters, estimate, poses, order, camera_scales):
    intrinsic_count = len(camera_scales)
    camera = np.array([estimate['fx'], estimate['fy'], estimate['cx'], estimate['cy'],
                       *estimate['k'][:order]], dtype=float) + parameters[:intrinsic_count] * camera_scales
    fx, fy, cx, cy = camera[:4]
    coefficients = np.zeros(4)
    coefficients[:order] = camera[4:]
    fitted_poses = []
    offset = intrinsic_count
    for rotation, translation in poses:
        delta = parameters[offset:offset+6]
        fitted_poses.append((Rotation.from_rotvec(rotation + delta[:3]).as_matrix(),
                             translation + delta[3:]))
        offset += 6
    return fx, fy, cx, cy, coefficients, fitted_poses


def _predict(object_points, rotation, translation, fx, fy, cx, cy, coefficients):
    points = object_points @ rotation.T + translation
    radial = np.linalg.norm(points[:, :2], axis=1)
    theta = np.arctan2(radial, points[:, 2])
    radius = theta.copy()
    for index, coefficient in enumerate(coefficients):
        radius += coefficient * theta ** (2 * index + 3)
    scale = np.divide(radius, radial, out=np.ones_like(radial), where=radial > 1e-14)
    return points[:, :2] * scale[:, None] * [fx, fy] + [cx, cy]


def residual_vector(parameters, object_points, observed, initial_poses, estimate, order, camera_scales):
    fx, fy, cx, cy, coefficients, fitted_poses = _unpack(
        parameters, estimate, initial_poses, order, camera_scales)
    return np.concatenate([(_predict(object_points, r, t, fx, fy, cx, cy, coefficients) - uv).ravel()
                           for (r, t), uv in zip(fitted_poses, observed)])


def solve_joint(object_points, observed, initial_poses, estimate, order, max_nfev=750):
    names, _, scales = _model_vector(estimate, order)
    intrinsic_count = len(names)
    count = len(observed)
    parameters = np.zeros(intrinsic_count + 6 * count)
    parameters_per_view = 2 * len(object_points)
    sparsity = lil_matrix((parameters_per_view * count, len(parameters)), dtype=np.int8)
    for view in range(count):
        rows = slice(view * parameters_per_view, (view + 1) * parameters_per_view)
        pose_columns = slice(intrinsic_count + view * 6, intrinsic_count + (view + 1) * 6)
        sparsity[rows, :intrinsic_count] = 1
        sparsity[rows, pose_columns] = 1
    result = least_squares(
        residual_vector, parameters, args=(object_points, observed, initial_poses, estimate, order, scales),
        jac='3-point', jac_sparsity=sparsity.tocsr(), x_scale='jac', loss='linear',
        ftol=1e-9, xtol=1e-9, gtol=1e-8, max_nfev=max_nfev)
    fx, fy, cx, cy, coefficients, fitted_poses = _unpack(
        result.x, estimate, initial_poses, order, scales)
    joint_estimate = {'fx': float(fx), 'fy': float(fy), 'cx': float(cx), 'cy': float(cy),
                      'k': coefficients.tolist(),
                      'theta_max_rad': float(estimate.get('theta_max_rad', 1.0))}
    parameter_names = names.copy()
    for view in range(count):
        parameter_names.extend(f'view{view:02d}.{name}' for name in ('rx', 'ry', 'rz', 'tx', 'ty', 'tz'))
    jacobian = result.jac.toarray() if hasattr(result.jac, 'toarray') else np.asarray(result.jac)
    return JointFit(joint_estimate, fitted_poses, result.fun, jacobian, bool(result.success),
                    int(result.status), str(result.message), float(result.cost),
                    float(result.optimality), int(result.nfev), parameter_names,
                    np.r_[scales, np.ones(6 * count)], intrinsic_count)


def _covariance_summary(jacobian, residuals, view_count, parameter_scales, parameter_names,
                        intrinsic_names, detector_sigma_px):
    rows, columns = jacobian.shape
    intrinsic_count = len(intrinsic_names)
    dof = rows - columns
    u, singular, vt = np.linalg.svd(jacobian, full_matrices=False)
    numerical_tolerance = np.finfo(float).eps * max(rows, columns) * singular[0]
    practical_tolerance = 1e-10 * singular[0]
    numerical_rank = int(np.count_nonzero(singular > numerical_tolerance))
    practical_rank = int(np.count_nonzero(singular > practical_tolerance))
    full_rank = numerical_rank == columns
    output = {'rows': rows, 'columns': columns, 'degrees_of_freedom': dof,
              'singular_values': singular.tolist(),
              'condition_number': float(singular[0] / singular[-1]) if singular[-1] > 0 else None,
              'numerical_rank': numerical_rank, 'practical_rank_1e-10': practical_rank,
              'rank_tolerances': {'machine': float(numerical_tolerance),
                                  'practical_relative_1e-10': float(practical_tolerance)},
              'weakest_modes': []}
    for index in range(1, min(4, len(singular)) + 1):
        vector = vt[-index]
        order = np.argsort(np.abs(vector))[::-1][:10]
        output['weakest_modes'].append({'singular_value': float(singular[-index]),
                                        'relative_to_max': float(singular[-index] / singular[0]),
                                        'largest_loadings': [{'parameter': parameter_names[i], 'magnitude': float(abs(vector[i])),
                                                              'signed': float(vector[i])} for i in order]})
    if not full_rank or dof <= 0:
        output['uncertainty_status'] = 'undefined_rank_deficient_or_no_dof'
        return output
    covariance_q = (vt.T / singular**2) @ vt
    rss = float(residuals @ residuals)
    residual_sigma = float(np.sqrt(rss / dof))
    camera_scale = parameter_scales[:intrinsic_count]
    transformation = np.diag(camera_scale)
    camera_covariance_q = covariance_q[:intrinsic_count, :intrinsic_count]
    output['uncertainty_status'] = 'local_linear_iid_assumption'
    output['residual_rss_px2'] = rss
    output['residual_sigma_px_iid'] = residual_sigma
    output['detector_truth_sigma_px_iid_scale'] = float(detector_sigma_px)
    output['intrinsics_covariance_by_sigma_source'] = {}
    for name, variance, covariance in (
        ('fit_residual_iid', residual_sigma**2, camera_covariance_q),
        ('detector_truth_rms_iid', detector_sigma_px**2, camera_covariance_q)):
        physical_covariance = transformation @ (variance * covariance) @ transformation
        std = np.sqrt(np.maximum(np.diag(physical_covariance), 0.))
        denom = np.outer(std, std)
        corr = np.divide(physical_covariance, denom, out=np.full_like(physical_covariance, np.nan), where=denom > 0)
        correlation = [[float(corr[i, j]) if np.isfinite(corr[i, j]) else None
                        for j in range(intrinsic_count)] for i in range(intrinsic_count)]
        output['intrinsics_covariance_by_sigma_source'][name] = {
            'standard_errors': std.tolist(), 'correlation': correlation,
            'covariance': physical_covariance.tolist()}
    meat = np.zeros((columns, columns))
    block_rows = 2 * 54
    for view in range(view_count):
        rows_for_view = slice(view * block_rows, (view + 1) * block_rows)
        score = jacobian[rows_for_view].T @ residuals[rows_for_view]
        meat += np.outer(score, score)
    correction = (view_count / (view_count - 1)) * ((rows - 1) / dof)
    sandwich = covariance_q @ meat @ covariance_q * correction
    physical_sandwich = transformation @ sandwich[:intrinsic_count, :intrinsic_count] @ transformation
    output['cluster_sandwich_intrinsics'] = {
        'clusters': view_count, 'finite_cluster_correction': float(correction),
        'standard_errors': np.sqrt(np.maximum(np.diag(physical_sandwich), 0.)).tolist(),
        'covariance': physical_sandwich.tolist(),
        'assumption': 'View-cluster sandwich; only 12 synthetic board views'}
    output['parameter_names_intrinsics'] = intrinsic_names
    return output


def analyze_joint_fit(fit, detector_sigma_px, source):
    result = _covariance_summary(fit.jacobian, fit.residuals, len(fit.poses), fit.parameter_scales,
                                 fit.parameter_names, fit.parameter_names[:fit.intrinsics_count],
                                 detector_sigma_px)
    result['parameter_names'] = fit.parameter_names
    result['fit'] = {'success': fit.success, 'status': fit.status, 'message': fit.message,
                     'cost': fit.cost, 'optimality': fit.optimality, 'function_evaluations': fit.evaluations,
                     'joint_estimate': fit.estimate}
    result['observation_source'] = source
    result['detector_rms_px_from_truth'] = detector_sigma_px
    return result
