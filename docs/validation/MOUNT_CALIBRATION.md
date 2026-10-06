# E-CAL-MOUNT-01: первичная проверка ошибок крепления

Дата: 07.10.2026, Linux, OpenCV 5.0.0. Протокол и интерпретация — [[research/MOUNT_CALIBRATION]], глава — [[diploma/04_EXPERIMENTAL_STUDY#4.17 Восстановление положения камер при ошибках монтажа]]. Это эксперимент с известными intrinsics и синтетическими XYZ/UV, а не image-based калибровка.

## Среда, исходные данные и повторение

Blender MCP снова доступен: addon 1.8, protocol 13, Blender 5.2.2 LTS. Сцена `SV Research Street` создана отдельно от исходной `Scene`, содержит 327 объектов. Экспортирован один момент времени, четыре камеры с разнесёнными центрами и 20 cube faces по 128×128. В Git сохранены рецепт и `.blend`: [[engineering/ASSETS]]. Промежуточные captures/конвертация находятся в ignored `artifacts/blender-mount-capture-v1` и `artifacts/blender-mount-v1`; вся численная серия и actual GLES изображения — `artifacts/mount-study-v3`. Подготовка и команды — [[engineering/BLENDER]], [[research/MOUNT_CALIBRATION]].

Пять mounting seeds × шесть условий × четыре метода = 120 запусков четырёхкамерного CLI. Каждый получает 120 train XYZ/UV на камеру; 120 независимых clean validation points на камеру остаются у evaluator. На seed 0 получены yaw/pitch/slide:

| Камера | Yaw, ° | Pitch, ° | Сдвиг вдоль кузова, м |
|---|---:|---:|---:|
| Front, 0 | −3.010775 | 3.794608 | 0.000908 |
| Right, 1 | 4.089552 | −1.360719 | −0.021302 |
| Rear, 2 | −2.303277 | 3.099644 | −0.077705 |
| Left, 3 | −4.037525 | −2.635035 | −0.129104 |

## Проверки реализации

Полный Release CTest: 15/15 групп после финальных изменений. CPU-профиль с GTest: 11/11 групп после финальных изменений. Новый `extrinsics` содержит три теста: восстановление четырьмя методами с ненулевой дисторсией, robust fit с 15/120 выбросами, отказ при некорректных данных. Blender fixture содержит восемь тестов, включая воспроизводимость recipe, оси, rigid transforms и validation.

## Интерпретация и ограничения

Номинальная калибровка даёт медиану held-out RMSE 8.199787 px. При σ=0.5 px без выбросов ITERATIVE: 0.162220 px, RANSAC+EPnP+LM: 0.226706 px. При 10% выбросов: 1.761342 и 0.215735 px соответственно. EPnP/SQPnP в последнем условии экспортировали только 2/5 четырёхкамерных trials; их медианы смещены отбором успешных запусков. Отказ означает выход хотя бы одной training projection из допустимой области. В условии σ=1 px, 10% выбросов EPnP/SQPnP экспортировали 5/5, но каждый имеет один invalid held-out point: успешный экспорт не равен приёмке качества.

Fit duration сохранена для аудита, но не является hardware benchmark: единичные короткие измерения без warmup и доверительных интервалов. RANSAC threshold и LM objective заданы в normalized pinhole coordinates. Простое деление pixel threshold на max(fx,fy) не сохраняет точный порог fisheye по всему полю зрения.

Первые иллюстрации показывают actual GLES на реальных Blender RGB одного seed, σ=0.5 px, 10% выбросов. UV solver синтезированы из известной модели; detector, occlusion, метрические шаблоны и совместная оценка intrinsics ещё не участвуют. Фото подтверждают визуальное изменение, но не заменяют held-out метрики. Полный raw JSON ниже сохраняет ошибки, counts и provenance. Отсутствующие результаты не заменены nominal или truth.

## Численные результаты

Известные intrinsics, synthetic noncoplanar XYZ/UV. Медианы по успешным trials × 4 камеры.

| Noise σ px | Outliers | Method | Success trials | Held-out RMSE px | Rotation deg | Center m |
|---:|---:|---|---:|---:|---:|---:|
| 0.0 | 0.0 | iterative | 5/5 | 0.000002 | 0.000000 | 0.000000 |
| 0.0 | 0.0 | epnp | 5/5 | 0.000000 | 0.000000 | 0.000000 |
| 0.0 | 0.0 | sqpnp | 5/5 | 0.000000 | 0.000000 | 0.000000 |
| 0.0 | 0.0 | ransac_epnp_lm | 5/5 | 0.000001 | 0.000000 | 0.000000 |
| 0.0 | 0.1 | iterative | 5/5 | 1.819653 | 1.045699 | 0.092767 |
| 0.0 | 0.1 | epnp | 4/5 | 16.645081 | 4.724749 | 0.803179 |
| 0.0 | 0.1 | sqpnp | 3/5 | 3.995247 | 1.363423 | 0.285629 |
| 0.0 | 0.1 | ransac_epnp_lm | 5/5 | 0.000001 | 0.000000 | 0.000000 |
| 0.5 | 0.0 | iterative | 5/5 | 0.162220 | 0.080803 | 0.008713 |
| 0.5 | 0.0 | epnp | 5/5 | 0.793651 | 0.200374 | 0.044031 |
| 0.5 | 0.0 | sqpnp | 5/5 | 0.208579 | 0.081047 | 0.016101 |
| 0.5 | 0.0 | ransac_epnp_lm | 5/5 | 0.226706 | 0.103005 | 0.014806 |
| 0.5 | 0.1 | iterative | 5/5 | 1.761342 | 1.010944 | 0.075100 |
| 0.5 | 0.1 | epnp | 2/5 | 9.758369 | 2.486626 | 0.797280 |
| 0.5 | 0.1 | sqpnp | 2/5 | 2.556452 | 1.015146 | 0.197291 |
| 0.5 | 0.1 | ransac_epnp_lm | 5/5 | 0.215735 | 0.115937 | 0.016719 |
| 1.0 | 0.0 | iterative | 5/5 | 0.347900 | 0.139029 | 0.019881 |
| 1.0 | 0.0 | epnp | 5/5 | 1.841318 | 0.526554 | 0.096016 |
| 1.0 | 0.0 | sqpnp | 5/5 | 0.396568 | 0.175792 | 0.032272 |
| 1.0 | 0.0 | ransac_epnp_lm | 5/5 | 0.599739 | 0.294124 | 0.042191 |
| 1.0 | 0.1 | iterative | 5/5 | 1.864336 | 0.982585 | 0.079109 |
| 1.0 | 0.1 | epnp | 5/5 | 11.840819 | 3.154462 | 0.665824 |
| 1.0 | 0.1 | sqpnp | 5/5 | 4.529814 | 1.538826 | 0.316687 |
| 1.0 | 0.1 | ransac_epnp_lm | 5/5 | 0.612743 | 0.299536 | 0.034783 |

## Первичный машинный отчёт (полная серия)

```json
{
  "schema_version": 1,
  "suite_id": "surround-view-mount-calibration-v1",
  "trials": 5,
  "observations_per_camera_train": 120,
  "observations_per_camera_validation": 120,
  "nominal_sha256": "cdf90d578d165f63734678a0709e9bc3a71199e13ba7095771bd351e4d1a9bca",
  "input_manifest_sha256": "a5aa568d82015a688f6d4bdd183e139f53fd33475ae883d8fc57bf5d07d03e33",
  "implementation_sha256": {
    "tools/calibration/study.py": "b07874704489e11b52fdb3e2d57123589d92bd2dc4e9d303bb24922f9b961b81",
    "tools/blender/scenario.py": "c6aa086131b82a1ca412c886accac510b9aeba84cd4218108c65615ad4057c13",
    "tools/simulator.py": "4b4214f40efc03df971bbc5c80f8aa69668bcb93fe80cd1a050c9769dd2cea0a"
  },
  "calibration_binary_sha256": "36326986c376a406d3dc303b836201b5f83334ab4e2822170df1123de181ae6d",
  "scenario_recipe": {
    "schema_version": 1,
    "seed": 20261007,
    "world": {
      "building_height_m": [
        4.0,
        11.0
      ],
      "building_spacing_m": 9.0
    },
    "mounts": {
      "yaw_deg": 5.0,
      "pitch_deg": 4.0,
      "along_body_m": 0.15,
      "overrides": {}
    }
  },
  "summary": [
    {
      "sigma_px": 0.0,
      "outlier_fraction": 0.0,
      "method": "iterative",
      "successful_trials": 5,
      "trials": 5,
      "validation_rmse_px_median": 2.098761381582664e-06,
      "rotation_error_deg_median": 0.0,
      "center_error_m_median": 6.280156448594983e-08,
      "fit_ms_median": 0.1363275,
      "nominal_validation_rmse_px_median": 8.19978673848989,
      "invalid_validation_points": 0
    },
    {
      "sigma_px": 0.0,
      "outlier_fraction": 0.0,
      "method": "epnp",
      "successful_trials": 5,
      "trials": 5,
      "validation_rmse_px_median": 9.031086026250526e-13,
      "rotation_error_deg_median": 0.0,
      "center_error_m_median": 3.773866139745071e-14,
      "fit_ms_median": 0.054849,
      "nominal_validation_rmse_px_median": 8.19978673848989,
      "invalid_validation_points": 0
    },
    {
      "sigma_px": 0.0,
      "outlier_fraction": 0.0,
      "method": "sqpnp",
      "successful_trials": 5,
      "trials": 5,
      "validation_rmse_px_median": 1.6175852686627042e-12,
      "rotation_error_deg_median": 0.0,
      "center_error_m_median": 6.476964387469678e-14,
      "fit_ms_median": 0.030092,
      "nominal_validation_rmse_px_median": 8.19978673848989,
      "invalid_validation_points": 0
    },
    {
      "sigma_px": 0.0,
      "outlier_fraction": 0.0,
      "method": "ransac_epnp_lm",
      "successful_trials": 5,
      "trials": 5,
      "validation_rmse_px_median": 8.203332501994963e-07,
      "rotation_error_deg_median": 0.0,
      "center_error_m_median": 3.471123589953059e-08,
      "fit_ms_median": 0.1721255,
      "nominal_validation_rmse_px_median": 8.19978673848989,
      "invalid_validation_points": 0
    },
    {
      "sigma_px": 0.0,
      "outlier_fraction": 0.1,
      "method": "iterative",
      "successful_trials": 5,
      "trials": 5,
      "validation_rmse_px_median": 1.8196533790334808,
      "rotation_error_deg_median": 1.0456993877221015,
      "center_error_m_median": 0.09276720629855702,
      "fit_ms_median": 0.17208600000000002,
      "nominal_validation_rmse_px_median": 8.19978673848989,
      "invalid_validation_points": 1
    },
    {
      "sigma_px": 0.0,
      "outlier_fraction": 0.1,
      "method": "epnp",
      "successful_trials": 4,
      "trials": 5,
      "validation_rmse_px_median": 16.64508065281013,
      "rotation_error_deg_median": 4.724748887687047,
      "center_error_m_median": 0.8031789411367112,
      "fit_ms_median": 0.052605,
      "nominal_validation_rmse_px_median": 8.19978673848989,
      "invalid_validation_points": 1
    },
    {
      "sigma_px": 0.0,
      "outlier_fraction": 0.1,
      "method": "sqpnp",
      "successful_trials": 3,
      "trials": 5,
      "validation_rmse_px_median": 3.995246774600141,
      "rotation_error_deg_median": 1.3634231931524692,
      "center_error_m_median": 0.28562878158253524,
      "fit_ms_median": 0.0349215,
      "nominal_validation_rmse_px_median": 8.19978673848989,
      "invalid_validation_points": 1
    },
    {
      "sigma_px": 0.0,
      "outlier_fraction": 0.1,
      "method": "ransac_epnp_lm",
      "successful_trials": 5,
      "trials": 5,
      "validation_rmse_px_median": 1.2285473766261188e-06,
      "rotation_error_deg_median": 0.0,
      "center_error_m_median": 2.51715265254628e-08,
      "fit_ms_median": 0.324374,
      "nominal_validation_rmse_px_median": 8.19978673848989,
      "invalid_validation_points": 0
    },
    {
      "sigma_px": 0.5,
      "outlier_fraction": 0.0,
      "method": "iterative",
      "successful_trials": 5,
      "trials": 5,
      "validation_rmse_px_median": 0.16221955650463485,
      "rotation_error_deg_median": 0.0808034743327104,
      "center_error_m_median": 0.008712574621677177,
      "fit_ms_median": 0.13451000000000002,
      "nominal_validation_rmse_px_median": 8.19978673848989,
      "invalid_validation_points": 0
    },
    {
      "sigma_px": 0.5,
      "outlier_fraction": 0.0,
      "method": "epnp",
      "successful_trials": 5,
      "trials": 5,
      "validation_rmse_px_median": 0.7936508395540802,
      "rotation_error_deg_median": 0.20037404026981376,
      "center_error_m_median": 0.04403133617944129,
      "fit_ms_median": 0.0551895,
      "nominal_validation_rmse_px_median": 8.19978673848989,
      "invalid_validation_points": 0
    },
    {
      "sigma_px": 0.5,
      "outlier_fraction": 0.0,
      "method": "sqpnp",
      "successful_trials": 5,
      "trials": 5,
      "validation_rmse_px_median": 0.20857863012602856,
      "rotation_error_deg_median": 0.08104655361906184,
      "center_error_m_median": 0.016100677481504427,
      "fit_ms_median": 0.031835,
      "nominal_validation_rmse_px_median": 8.19978673848989,
      "invalid_validation_points": 0
    },
    {
      "sigma_px": 0.5,
      "outlier_fraction": 0.0,
      "method": "ransac_epnp_lm",
      "successful_trials": 5,
      "trials": 5,
      "validation_rmse_px_median": 0.22670588106058104,
      "rotation_error_deg_median": 0.10300493232674776,
      "center_error_m_median": 0.014806336129806922,
      "fit_ms_median": 0.4215525,
      "nominal_validation_rmse_px_median": 8.19978673848989,
      "invalid_validation_points": 0
    },
    {
      "sigma_px": 0.5,
      "outlier_fraction": 0.1,
      "method": "iterative",
      "successful_trials": 5,
      "trials": 5,
      "validation_rmse_px_median": 1.7613416963668107,
      "rotation_error_deg_median": 1.0109435347635234,
      "center_error_m_median": 0.07509963564661057,
      "fit_ms_median": 0.1731375,
      "nominal_validation_rmse_px_median": 8.19978673848989,
      "invalid_validation_points": 0
    },
    {
      "sigma_px": 0.5,
      "outlier_fraction": 0.1,
      "method": "epnp",
      "successful_trials": 2,
      "trials": 5,
      "validation_rmse_px_median": 9.758368592826512,
      "rotation_error_deg_median": 2.486626484235188,
      "center_error_m_median": 0.7972804914422315,
      "fit_ms_median": 0.053321,
      "nominal_validation_rmse_px_median": 8.19978673848989,
      "invalid_validation_points": 0
    },
    {
      "sigma_px": 0.5,
      "outlier_fraction": 0.1,
      "method": "sqpnp",
      "successful_trials": 2,
      "trials": 5,
      "validation_rmse_px_median": 2.5564524839953293,
      "rotation_error_deg_median": 1.015146198916013,
      "center_error_m_median": 0.19729095222840615,
      "fit_ms_median": 0.033854499999999996,
      "nominal_validation_rmse_px_median": 8.19978673848989,
      "invalid_validation_points": 0
    },
    {
      "sigma_px": 0.5,
      "outlier_fraction": 0.1,
      "method": "ransac_epnp_lm",
      "successful_trials": 5,
      "trials": 5,
      "validation_rmse_px_median": 0.21573519843509845,
      "rotation_error_deg_median": 0.11593696445726684,
      "center_error_m_median": 0.01671922308224753,
      "fit_ms_median": 0.6119835,
      "nominal_validation_rmse_px_median": 8.19978673848989,
      "invalid_validation_points": 0
    },
    {
      "sigma_px": 1.0,
      "outlier_fraction": 0.0,
      "method": "iterative",
      "successful_trials": 5,
      "trials": 5,
      "validation_rmse_px_median": 0.34790043833848283,
      "rotation_error_deg_median": 0.13902940221192828,
      "center_error_m_median": 0.019881068722187367,
      "fit_ms_median": 0.13975949999999998,
      "nominal_validation_rmse_px_median": 8.19978673848989,
      "invalid_validation_points": 0
    },
    {
      "sigma_px": 1.0,
      "outlier_fraction": 0.0,
      "method": "epnp",
      "successful_trials": 5,
      "trials": 5,
      "validation_rmse_px_median": 1.8413181711550841,
      "rotation_error_deg_median": 0.5265541205164499,
      "center_error_m_median": 0.09601581493099735,
      "fit_ms_median": 0.056091,
      "nominal_validation_rmse_px_median": 8.19978673848989,
      "invalid_validation_points": 0
    },
    {
      "sigma_px": 1.0,
      "outlier_fraction": 0.0,
      "method": "sqpnp",
      "successful_trials": 5,
      "trials": 5,
      "validation_rmse_px_median": 0.3965677419406469,
      "rotation_error_deg_median": 0.17579155697120624,
      "center_error_m_median": 0.03227153715090982,
      "fit_ms_median": 0.033288,
      "nominal_validation_rmse_px_median": 8.19978673848989,
      "invalid_validation_points": 0
    },
    {
      "sigma_px": 1.0,
      "outlier_fraction": 0.0,
      "method": "ransac_epnp_lm",
      "successful_trials": 5,
      "trials": 5,
      "validation_rmse_px_median": 0.599738884284164,
      "rotation_error_deg_median": 0.29412358870558086,
      "center_error_m_median": 0.042191236767839105,
      "fit_ms_median": 1.3211955,
      "nominal_validation_rmse_px_median": 8.19978673848989,
      "invalid_validation_points": 0
    },
    {
      "sigma_px": 1.0,
      "outlier_fraction": 0.1,
      "method": "iterative",
      "successful_trials": 5,
      "trials": 5,
      "validation_rmse_px_median": 1.8643363861230395,
      "rotation_error_deg_median": 0.9825854672311998,
      "center_error_m_median": 0.07910920191367905,
      "fit_ms_median": 0.1785875,
      "nominal_validation_rmse_px_median": 8.19978673848989,
      "invalid_validation_points": 0
    },
    {
      "sigma_px": 1.0,
      "outlier_fraction": 0.1,
      "method": "epnp",
      "successful_trials": 5,
      "trials": 5,
      "validation_rmse_px_median": 11.84081937492609,
      "rotation_error_deg_median": 3.154461914872101,
      "center_error_m_median": 0.6658238497881916,
      "fit_ms_median": 0.055189,
      "nominal_validation_rmse_px_median": 8.19978673848989,
      "invalid_validation_points": 1
    },
    {
      "sigma_px": 1.0,
      "outlier_fraction": 0.1,
      "method": "sqpnp",
      "successful_trials": 5,
      "trials": 5,
      "validation_rmse_px_median": 4.529814426271093,
      "rotation_error_deg_median": 1.5388258429680572,
      "center_error_m_median": 0.3166870272066832,
      "fit_ms_median": 0.0322705,
      "nominal_validation_rmse_px_median": 8.19978673848989,
      "invalid_validation_points": 1
    },
    {
      "sigma_px": 1.0,
      "outlier_fraction": 0.1,
      "method": "ransac_epnp_lm",
      "successful_trials": 5,
      "trials": 5,
      "validation_rmse_px_median": 0.61274327691446,
      "rotation_error_deg_median": 0.2995362309099974,
      "center_error_m_median": 0.03478303023718083,
      "fit_ms_median": 2.1279529999999998,
      "nominal_validation_rmse_px_median": 8.19978673848989,
      "invalid_validation_points": 0
    }
  ],
  "rows": [
    {
      "trial": 0,
      "mount_seed": 20261007,
      "sigma_px": 0.0,
      "outlier_fraction": 0.0,
      "method": "iterative",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": -3.010775491735372,
          "pitch_deg": 3.79460786232279,
          "along_body_m": 0.0009082963057430726,
          "center_vehicle_m": [
            2.36,
            0.0009082963057430726,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": 4.089552251488005,
          "pitch_deg": -1.3607194680997745,
          "along_body_m": -0.021301678009231162,
          "center_vehicle_m": [
            0.3286983219907688,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": -2.303277094446421,
          "pitch_deg": 3.099643788382867,
          "along_body_m": -0.07770516415254036,
          "center_vehicle_m": [
            -2.36,
            -0.07770516415254036,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": -4.03752505809479,
          "pitch_deg": -2.6350348186898787,
          "along_body_m": -0.1291036030828914,
          "center_vehicle_m": [
            0.22089639691710858,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.666384193484578,
          "validation_p95_px": 10.83408684160468,
          "rotation_error_deg": 4.843604460598397,
          "center_error_m": 0.0009082963057430588
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.31395428901275,
          "validation_p95_px": 9.592908257006345,
          "rotation_error_deg": 4.309896627831011,
          "center_error_m": 0.021301678009231162
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.216052519953206,
          "validation_p95_px": 8.055514664616624,
          "rotation_error_deg": 3.861551893252276,
          "center_error_m": 0.07770516415254033
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.120901155502272,
          "validation_p95_px": 9.849109761739927,
          "rotation_error_deg": 4.821010628472409,
          "center_error_m": 0.1291036030828914
        }
      ],
      "success": true,
      "observations_sha256": "0b563c25b1a5b21cfacc0f6f5a5b9ed8feaafef36359ea220dd19929cdffb969",
      "cameras": [
        {
          "camera_id": 0,
          "invalid_validation_count": 0,
          "validation_rmse_px": 4.097781906070227e-06,
          "validation_p95_px": 6.6667770817038366e-06,
          "rotation_error_deg": 0.0,
          "center_error_m": 1.4589418041464167e-07,
          "inliers": 120,
          "fit_ms": 0.387453
        },
        {
          "camera_id": 1,
          "invalid_validation_count": 0,
          "validation_rmse_px": 3.554909738844944e-07,
          "validation_p95_px": 4.443423270316706e-07,
          "rotation_error_deg": 0.0,
          "center_error_m": 2.099794210105925e-08,
          "inliers": 120,
          "fit_ms": 0.139144
        },
        {
          "camera_id": 2,
          "invalid_validation_count": 0,
          "validation_rmse_px": 1.4605007871295275e-06,
          "validation_p95_px": 2.1679929786635525e-06,
          "rotation_error_deg": 0.0,
          "center_error_m": 6.759109988062281e-08,
          "inliers": 120,
          "fit_ms": 0.128934
        },
        {
          "camera_id": 3,
          "invalid_validation_count": 0,
          "validation_rmse_px": 5.5186398186385995e-06,
          "validation_p95_px": 1.4499313129920349e-05,
          "rotation_error_deg": 0.0,
          "center_error_m": 3.755380708148954e-07,
          "inliers": 120,
          "fit_ms": 0.126109
        }
      ]
    },
    {
      "trial": 0,
      "mount_seed": 20261007,
      "sigma_px": 0.0,
      "outlier_fraction": 0.0,
      "method": "epnp",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": -3.010775491735372,
          "pitch_deg": 3.79460786232279,
          "along_body_m": 0.0009082963057430726,
          "center_vehicle_m": [
            2.36,
            0.0009082963057430726,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": 4.089552251488005,
          "pitch_deg": -1.3607194680997745,
          "along_body_m": -0.021301678009231162,
          "center_vehicle_m": [
            0.3286983219907688,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": -2.303277094446421,
          "pitch_deg": 3.099643788382867,
          "along_body_m": -0.07770516415254036,
          "center_vehicle_m": [
            -2.36,
            -0.07770516415254036,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": -4.03752505809479,
          "pitch_deg": -2.6350348186898787,
          "along_body_m": -0.1291036030828914,
          "center_vehicle_m": [
            0.22089639691710858,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.666384193484578,
          "validation_p95_px": 10.83408684160468,
          "rotation_error_deg": 4.843604460598397,
          "center_error_m": 0.0009082963057430588
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.31395428901275,
          "validation_p95_px": 9.592908257006345,
          "rotation_error_deg": 4.309896627831011,
          "center_error_m": 0.021301678009231162
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.216052519953206,
          "validation_p95_px": 8.055514664616624,
          "rotation_error_deg": 3.861551893252276,
          "center_error_m": 0.07770516415254033
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.120901155502272,
          "validation_p95_px": 9.849109761739927,
          "rotation_error_deg": 4.821010628472409,
          "center_error_m": 0.1291036030828914
        }
      ],
      "success": true,
      "observations_sha256": "0b563c25b1a5b21cfacc0f6f5a5b9ed8feaafef36359ea220dd19929cdffb969",
      "cameras": [
        {
          "camera_id": 0,
          "invalid_validation_count": 0,
          "validation_rmse_px": 1.019587149693216e-12,
          "validation_p95_px": 2.6542675942630715e-12,
          "rotation_error_deg": 0.0,
          "center_error_m": 5.1665973321788204e-14,
          "inliers": 120,
          "fit_ms": 0.309495
        },
        {
          "camera_id": 1,
          "invalid_validation_count": 0,
          "validation_rmse_px": 1.3663720181291498e-12,
          "validation_p95_px": 2.941165058520099e-12,
          "rotation_error_deg": 0.0,
          "center_error_m": 6.689628083229709e-14,
          "inliers": 120,
          "fit_ms": 0.057458
        },
        {
          "camera_id": 2,
          "invalid_validation_count": 0,
          "validation_rmse_px": 1.9339101541474314e-12,
          "validation_p95_px": 2.6216176659153954e-12,
          "rotation_error_deg": 0.0,
          "center_error_m": 6.167127918602248e-14,
          "inliers": 120,
          "fit_ms": 0.049534
        },
        {
          "camera_id": 3,
          "invalid_validation_count": 0,
          "validation_rmse_px": 1.1318985180846146e-13,
          "validation_p95_px": 2.3535257549081863e-13,
          "rotation_error_deg": 0.0,
          "center_error_m": 8.887506940822315e-15,
          "inliers": 120,
          "fit_ms": 0.04787
        }
      ]
    },
    {
      "trial": 0,
      "mount_seed": 20261007,
      "sigma_px": 0.0,
      "outlier_fraction": 0.0,
      "method": "sqpnp",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": -3.010775491735372,
          "pitch_deg": 3.79460786232279,
          "along_body_m": 0.0009082963057430726,
          "center_vehicle_m": [
            2.36,
            0.0009082963057430726,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": 4.089552251488005,
          "pitch_deg": -1.3607194680997745,
          "along_body_m": -0.021301678009231162,
          "center_vehicle_m": [
            0.3286983219907688,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": -2.303277094446421,
          "pitch_deg": 3.099643788382867,
          "along_body_m": -0.07770516415254036,
          "center_vehicle_m": [
            -2.36,
            -0.07770516415254036,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": -4.03752505809479,
          "pitch_deg": -2.6350348186898787,
          "along_body_m": -0.1291036030828914,
          "center_vehicle_m": [
            0.22089639691710858,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.666384193484578,
          "validation_p95_px": 10.83408684160468,
          "rotation_error_deg": 4.843604460598397,
          "center_error_m": 0.0009082963057430588
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.31395428901275,
          "validation_p95_px": 9.592908257006345,
          "rotation_error_deg": 4.309896627831011,
          "center_error_m": 0.021301678009231162
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.216052519953206,
          "validation_p95_px": 8.055514664616624,
          "rotation_error_deg": 3.861551893252276,
          "center_error_m": 0.07770516415254033
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.120901155502272,
          "validation_p95_px": 9.849109761739927,
          "rotation_error_deg": 4.821010628472409,
          "center_error_m": 0.1291036030828914
        }
      ],
      "success": true,
      "observations_sha256": "0b563c25b1a5b21cfacc0f6f5a5b9ed8feaafef36359ea220dd19929cdffb969",
      "cameras": [
        {
          "camera_id": 0,
          "invalid_validation_count": 0,
          "validation_rmse_px": 6.144531166819434e-12,
          "validation_p95_px": 1.1201770035493509e-11,
          "rotation_error_deg": 0.0,
          "center_error_m": 1.8876642323387003e-13,
          "inliers": 120,
          "fit_ms": 0.207933
        },
        {
          "camera_id": 1,
          "invalid_validation_count": 0,
          "validation_rmse_px": 1.5351237025751325e-12,
          "validation_p95_px": 2.479891211244514e-12,
          "rotation_error_deg": 0.0,
          "center_error_m": 3.001716420100399e-14,
          "inliers": 120,
          "fit_ms": 0.031159
        },
        {
          "camera_id": 2,
          "invalid_validation_count": 0,
          "validation_rmse_px": 5.884986153536341e-12,
          "validation_p95_px": 9.731764459302645e-12,
          "rotation_error_deg": 0.0,
          "center_error_m": 1.9746558119922557e-13,
          "inliers": 120,
          "fit_ms": 0.025949
        },
        {
          "camera_id": 3,
          "invalid_validation_count": 0,
          "validation_rmse_px": 6.735030661393584e-13,
          "validation_p95_px": 1.1252060887151404e-12,
          "rotation_error_deg": 1.2074182697257333e-06,
          "center_error_m": 2.5608395718426847e-14,
          "inliers": 120,
          "fit_ms": 0.023294
        }
      ]
    },
    {
      "trial": 0,
      "mount_seed": 20261007,
      "sigma_px": 0.0,
      "outlier_fraction": 0.0,
      "method": "ransac_epnp_lm",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": -3.010775491735372,
          "pitch_deg": 3.79460786232279,
          "along_body_m": 0.0009082963057430726,
          "center_vehicle_m": [
            2.36,
            0.0009082963057430726,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": 4.089552251488005,
          "pitch_deg": -1.3607194680997745,
          "along_body_m": -0.021301678009231162,
          "center_vehicle_m": [
            0.3286983219907688,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": -2.303277094446421,
          "pitch_deg": 3.099643788382867,
          "along_body_m": -0.07770516415254036,
          "center_vehicle_m": [
            -2.36,
            -0.07770516415254036,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": -4.03752505809479,
          "pitch_deg": -2.6350348186898787,
          "along_body_m": -0.1291036030828914,
          "center_vehicle_m": [
            0.22089639691710858,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.666384193484578,
          "validation_p95_px": 10.83408684160468,
          "rotation_error_deg": 4.843604460598397,
          "center_error_m": 0.0009082963057430588
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.31395428901275,
          "validation_p95_px": 9.592908257006345,
          "rotation_error_deg": 4.309896627831011,
          "center_error_m": 0.021301678009231162
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.216052519953206,
          "validation_p95_px": 8.055514664616624,
          "rotation_error_deg": 3.861551893252276,
          "center_error_m": 0.07770516415254033
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.120901155502272,
          "validation_p95_px": 9.849109761739927,
          "rotation_error_deg": 4.821010628472409,
          "center_error_m": 0.1291036030828914
        }
      ],
      "success": true,
      "observations_sha256": "0b563c25b1a5b21cfacc0f6f5a5b9ed8feaafef36359ea220dd19929cdffb969",
      "cameras": [
        {
          "camera_id": 0,
          "invalid_validation_count": 0,
          "validation_rmse_px": 1.9464527593030304e-06,
          "validation_p95_px": 3.5340567152822373e-06,
          "rotation_error_deg": 0.0,
          "center_error_m": 5.43935553583397e-08,
          "inliers": 120,
          "fit_ms": 0.420396
        },
        {
          "camera_id": 1,
          "invalid_validation_count": 0,
          "validation_rmse_px": 5.511569720651329e-07,
          "validation_p95_px": 1.138428461650857e-06,
          "rotation_error_deg": 0.0,
          "center_error_m": 2.777770894452296e-08,
          "inliers": 120,
          "fit_ms": 0.166515
        },
        {
          "camera_id": 2,
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.78077387112338e-10,
          "validation_p95_px": 1.3160753233995027e-09,
          "rotation_error_deg": 0.0,
          "center_error_m": 5.768656343019215e-11,
          "inliers": 120,
          "fit_ms": 0.128042
        },
        {
          "camera_id": 3,
          "invalid_validation_count": 0,
          "validation_rmse_px": 1.055992962555819e-06,
          "validation_p95_px": 1.3944381208139867e-06,
          "rotation_error_deg": 1.2074182697257333e-06,
          "center_error_m": 4.046814233093915e-08,
          "inliers": 120,
          "fit_ms": 0.118374
        }
      ]
    },
    {
      "trial": 0,
      "mount_seed": 20261007,
      "sigma_px": 0.0,
      "outlier_fraction": 0.1,
      "method": "iterative",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": -3.010775491735372,
          "pitch_deg": 3.79460786232279,
          "along_body_m": 0.0009082963057430726,
          "center_vehicle_m": [
            2.36,
            0.0009082963057430726,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": 4.089552251488005,
          "pitch_deg": -1.3607194680997745,
          "along_body_m": -0.021301678009231162,
          "center_vehicle_m": [
            0.3286983219907688,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": -2.303277094446421,
          "pitch_deg": 3.099643788382867,
          "along_body_m": -0.07770516415254036,
          "center_vehicle_m": [
            -2.36,
            -0.07770516415254036,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": -4.03752505809479,
          "pitch_deg": -2.6350348186898787,
          "along_body_m": -0.1291036030828914,
          "center_vehicle_m": [
            0.22089639691710858,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.666384193484578,
          "validation_p95_px": 10.83408684160468,
          "rotation_error_deg": 4.843604460598397,
          "center_error_m": 0.0009082963057430588
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.31395428901275,
          "validation_p95_px": 9.592908257006345,
          "rotation_error_deg": 4.309896627831011,
          "center_error_m": 0.021301678009231162
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.216052519953206,
          "validation_p95_px": 8.055514664616624,
          "rotation_error_deg": 3.861551893252276,
          "center_error_m": 0.07770516415254033
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.120901155502272,
          "validation_p95_px": 9.849109761739927,
          "rotation_error_deg": 4.821010628472409,
          "center_error_m": 0.1291036030828914
        }
      ],
      "success": true,
      "observations_sha256": "1427c93285d4caa3c96c9b7cc4f8e9e8519aa3ac5288c5e36e1f5ca954b37581",
      "cameras": [
        {
          "camera_id": 0,
          "invalid_validation_count": 0,
          "validation_rmse_px": 1.4056718408625841,
          "validation_p95_px": 2.5617317676136238,
          "rotation_error_deg": 0.6350381628151854,
          "center_error_m": 0.08311353322918659,
          "inliers": 120,
          "fit_ms": 0.456293
        },
        {
          "camera_id": 1,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.8168813705687965,
          "validation_p95_px": 1.186736476282735,
          "rotation_error_deg": 0.42734230262045914,
          "center_error_m": 0.02356881327486479,
          "inliers": 120,
          "fit_ms": 0.173769
        },
        {
          "camera_id": 2,
          "invalid_validation_count": 0,
          "validation_rmse_px": 1.750993411216102,
          "validation_p95_px": 2.9915607547108425,
          "rotation_error_deg": 0.8488116516083372,
          "center_error_m": 0.06374220233431287,
          "inliers": 120,
          "fit_ms": 0.160484
        },
        {
          "camera_id": 3,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.6202060908630508,
          "validation_p95_px": 0.8536254835641591,
          "rotation_error_deg": 0.22955744383540527,
          "center_error_m": 0.018371014225864957,
          "inliers": 120,
          "fit_ms": 0.180131
        }
      ]
    },
    {
      "trial": 0,
      "mount_seed": 20261007,
      "sigma_px": 0.0,
      "outlier_fraction": 0.1,
      "method": "epnp",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": -3.010775491735372,
          "pitch_deg": 3.79460786232279,
          "along_body_m": 0.0009082963057430726,
          "center_vehicle_m": [
            2.36,
            0.0009082963057430726,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": 4.089552251488005,
          "pitch_deg": -1.3607194680997745,
          "along_body_m": -0.021301678009231162,
          "center_vehicle_m": [
            0.3286983219907688,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": -2.303277094446421,
          "pitch_deg": 3.099643788382867,
          "along_body_m": -0.07770516415254036,
          "center_vehicle_m": [
            -2.36,
            -0.07770516415254036,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": -4.03752505809479,
          "pitch_deg": -2.6350348186898787,
          "along_body_m": -0.1291036030828914,
          "center_vehicle_m": [
            0.22089639691710858,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.666384193484578,
          "validation_p95_px": 10.83408684160468,
          "rotation_error_deg": 4.843604460598397,
          "center_error_m": 0.0009082963057430588
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.31395428901275,
          "validation_p95_px": 9.592908257006345,
          "rotation_error_deg": 4.309896627831011,
          "center_error_m": 0.021301678009231162
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.216052519953206,
          "validation_p95_px": 8.055514664616624,
          "rotation_error_deg": 3.861551893252276,
          "center_error_m": 0.07770516415254033
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.120901155502272,
          "validation_p95_px": 9.849109761739927,
          "rotation_error_deg": 4.821010628472409,
          "center_error_m": 0.1291036030828914
        }
      ],
      "success": true,
      "observations_sha256": "1427c93285d4caa3c96c9b7cc4f8e9e8519aa3ac5288c5e36e1f5ca954b37581",
      "cameras": [
        {
          "camera_id": 0,
          "invalid_validation_count": 0,
          "validation_rmse_px": 30.392042328478187,
          "validation_p95_px": 56.05344650843659,
          "rotation_error_deg": 5.9277311517937905,
          "center_error_m": 1.370432997586776,
          "inliers": 120,
          "fit_ms": 0.255925
        },
        {
          "camera_id": 1,
          "invalid_validation_count": 0,
          "validation_rmse_px": 13.149815823273533,
          "validation_p95_px": 25.874259804132382,
          "rotation_error_deg": 2.061199621766697,
          "center_error_m": 0.7890731970583589,
          "inliers": 120,
          "fit_ms": 0.064632
        },
        {
          "camera_id": 2,
          "invalid_validation_count": 0,
          "validation_rmse_px": 8.86700170132662,
          "validation_p95_px": 11.516942541635496,
          "rotation_error_deg": 4.694920026088156,
          "center_error_m": 0.30231984500483583,
          "inliers": 120,
          "fit_ms": 0.050446
        },
        {
          "camera_id": 3,
          "invalid_validation_count": 0,
          "validation_rmse_px": 4.765078358230663,
          "validation_p95_px": 11.09177568421897,
          "rotation_error_deg": 1.1365244140167376,
          "center_error_m": 0.31769878667029133,
          "inliers": 120,
          "fit_ms": 0.048622
        }
      ]
    },
    {
      "trial": 0,
      "mount_seed": 20261007,
      "sigma_px": 0.0,
      "outlier_fraction": 0.1,
      "method": "sqpnp",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": -3.010775491735372,
          "pitch_deg": 3.79460786232279,
          "along_body_m": 0.0009082963057430726,
          "center_vehicle_m": [
            2.36,
            0.0009082963057430726,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": 4.089552251488005,
          "pitch_deg": -1.3607194680997745,
          "along_body_m": -0.021301678009231162,
          "center_vehicle_m": [
            0.3286983219907688,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": -2.303277094446421,
          "pitch_deg": 3.099643788382867,
          "along_body_m": -0.07770516415254036,
          "center_vehicle_m": [
            -2.36,
            -0.07770516415254036,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": -4.03752505809479,
          "pitch_deg": -2.6350348186898787,
          "along_body_m": -0.1291036030828914,
          "center_vehicle_m": [
            0.22089639691710858,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.666384193484578,
          "validation_p95_px": 10.83408684160468,
          "rotation_error_deg": 4.843604460598397,
          "center_error_m": 0.0009082963057430588
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.31395428901275,
          "validation_p95_px": 9.592908257006345,
          "rotation_error_deg": 4.309896627831011,
          "center_error_m": 0.021301678009231162
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.216052519953206,
          "validation_p95_px": 8.055514664616624,
          "rotation_error_deg": 3.861551893252276,
          "center_error_m": 0.07770516415254033
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.120901155502272,
          "validation_p95_px": 9.849109761739927,
          "rotation_error_deg": 4.821010628472409,
          "center_error_m": 0.1291036030828914
        }
      ],
      "success": true,
      "observations_sha256": "1427c93285d4caa3c96c9b7cc4f8e9e8519aa3ac5288c5e36e1f5ca954b37581",
      "cameras": [
        {
          "camera_id": 0,
          "invalid_validation_count": 0,
          "validation_rmse_px": 4.5649727725044835,
          "validation_p95_px": 10.854794462993437,
          "rotation_error_deg": 1.8159887946981648,
          "center_error_m": 0.291245139253573,
          "inliers": 120,
          "fit_ms": 0.196522
        },
        {
          "camera_id": 1,
          "invalid_validation_count": 0,
          "validation_rmse_px": 2.33510511156702,
          "validation_p95_px": 5.624610874774019,
          "rotation_error_deg": 0.27344371365684084,
          "center_error_m": 0.1472665179621206,
          "inliers": 120,
          "fit_ms": 0.034636
        },
        {
          "camera_id": 2,
          "invalid_validation_count": 0,
          "validation_rmse_px": 3.340472557024904,
          "validation_p95_px": 6.562623989699485,
          "rotation_error_deg": 1.4490619838780991,
          "center_error_m": 0.28001242391149744,
          "inliers": 120,
          "fit_ms": 0.030358
        },
        {
          "camera_id": 3,
          "invalid_validation_count": 0,
          "validation_rmse_px": 1.588361192621273,
          "validation_p95_px": 3.2989928909866895,
          "rotation_error_deg": 0.8024874696964615,
          "center_error_m": 0.08950618639133237,
          "inliers": 120,
          "fit_ms": 0.02658
        }
      ]
    },
    {
      "trial": 0,
      "mount_seed": 20261007,
      "sigma_px": 0.0,
      "outlier_fraction": 0.1,
      "method": "ransac_epnp_lm",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": -3.010775491735372,
          "pitch_deg": 3.79460786232279,
          "along_body_m": 0.0009082963057430726,
          "center_vehicle_m": [
            2.36,
            0.0009082963057430726,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": 4.089552251488005,
          "pitch_deg": -1.3607194680997745,
          "along_body_m": -0.021301678009231162,
          "center_vehicle_m": [
            0.3286983219907688,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": -2.303277094446421,
          "pitch_deg": 3.099643788382867,
          "along_body_m": -0.07770516415254036,
          "center_vehicle_m": [
            -2.36,
            -0.07770516415254036,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": -4.03752505809479,
          "pitch_deg": -2.6350348186898787,
          "along_body_m": -0.1291036030828914,
          "center_vehicle_m": [
            0.22089639691710858,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.666384193484578,
          "validation_p95_px": 10.83408684160468,
          "rotation_error_deg": 4.843604460598397,
          "center_error_m": 0.0009082963057430588
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.31395428901275,
          "validation_p95_px": 9.592908257006345,
          "rotation_error_deg": 4.309896627831011,
          "center_error_m": 0.021301678009231162
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.216052519953206,
          "validation_p95_px": 8.055514664616624,
          "rotation_error_deg": 3.861551893252276,
          "center_error_m": 0.07770516415254033
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.120901155502272,
          "validation_p95_px": 9.849109761739927,
          "rotation_error_deg": 4.821010628472409,
          "center_error_m": 0.1291036030828914
        }
      ],
      "success": true,
      "observations_sha256": "1427c93285d4caa3c96c9b7cc4f8e9e8519aa3ac5288c5e36e1f5ca954b37581",
      "cameras": [
        {
          "camera_id": 0,
          "invalid_validation_count": 0,
          "validation_rmse_px": 2.059278997438169e-06,
          "validation_p95_px": 3.3789562894289298e-06,
          "rotation_error_deg": 0.0,
          "center_error_m": 8.080591386508457e-08,
          "inliers": 108,
          "fit_ms": 0.646142
        },
        {
          "camera_id": 1,
          "invalid_validation_count": 0,
          "validation_rmse_px": 1.2823348872945077e-06,
          "validation_p95_px": 1.6115819372870372e-06,
          "rotation_error_deg": 0.0,
          "center_error_m": 2.1005629298518207e-08,
          "inliers": 108,
          "fit_ms": 0.330165
        },
        {
          "camera_id": 2,
          "invalid_validation_count": 0,
          "validation_rmse_px": 1.0919104434716419e-06,
          "validation_p95_px": 1.5284996251747766e-06,
          "rotation_error_deg": 0.0,
          "center_error_m": 5.407936776727856e-08,
          "inliers": 108,
          "fit_ms": 0.420516
        },
        {
          "camera_id": 3,
          "invalid_validation_count": 0,
          "validation_rmse_px": 8.543857147836763e-07,
          "validation_p95_px": 1.061858354482495e-06,
          "rotation_error_deg": 0.0,
          "center_error_m": 2.1666230453977258e-08,
          "inliers": 108,
          "fit_ms": 0.298605
        }
      ]
    },
    {
      "trial": 0,
      "mount_seed": 20261007,
      "sigma_px": 0.5,
      "outlier_fraction": 0.0,
      "method": "iterative",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": -3.010775491735372,
          "pitch_deg": 3.79460786232279,
          "along_body_m": 0.0009082963057430726,
          "center_vehicle_m": [
            2.36,
            0.0009082963057430726,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": 4.089552251488005,
          "pitch_deg": -1.3607194680997745,
          "along_body_m": -0.021301678009231162,
          "center_vehicle_m": [
            0.3286983219907688,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": -2.303277094446421,
          "pitch_deg": 3.099643788382867,
          "along_body_m": -0.07770516415254036,
          "center_vehicle_m": [
            -2.36,
            -0.07770516415254036,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": -4.03752505809479,
          "pitch_deg": -2.6350348186898787,
          "along_body_m": -0.1291036030828914,
          "center_vehicle_m": [
            0.22089639691710858,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.666384193484578,
          "validation_p95_px": 10.83408684160468,
          "rotation_error_deg": 4.843604460598397,
          "center_error_m": 0.0009082963057430588
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.31395428901275,
          "validation_p95_px": 9.592908257006345,
          "rotation_error_deg": 4.309896627831011,
          "center_error_m": 0.021301678009231162
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.216052519953206,
          "validation_p95_px": 8.055514664616624,
          "rotation_error_deg": 3.861551893252276,
          "center_error_m": 0.07770516415254033
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.120901155502272,
          "validation_p95_px": 9.849109761739927,
          "rotation_error_deg": 4.821010628472409,
          "center_error_m": 0.1291036030828914
        }
      ],
      "success": true,
      "observations_sha256": "1b9447518b28cf8fab36eba62ef889c9ee41a13f8f9c3f331eef56e51b7c4c3e",
      "cameras": [
        {
          "camera_id": 0,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.09579208696586318,
          "validation_p95_px": 0.1822304466246353,
          "rotation_error_deg": 0.030336575092634377,
          "center_error_m": 0.005002940086290918,
          "inliers": 120,
          "fit_ms": 0.347287
        },
        {
          "camera_id": 1,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.1669772758093441,
          "validation_p95_px": 0.2048065841301357,
          "rotation_error_deg": 0.11950101394043901,
          "center_error_m": 0.008709373567832981,
          "inliers": 120,
          "fit_ms": 0.13231
        },
        {
          "camera_id": 2,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.1440636778233898,
          "validation_p95_px": 0.3134367484897323,
          "rotation_error_deg": 0.05840261595061102,
          "center_error_m": 0.011507146618465339,
          "inliers": 120,
          "fit_ms": 0.125788
        },
        {
          "camera_id": 3,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.07757695801178989,
          "validation_p95_px": 0.09828123609434242,
          "rotation_error_deg": 0.05618062209943743,
          "center_error_m": 0.0044417722890776985,
          "inliers": 120,
          "fit_ms": 0.129735
        }
      ]
    },
    {
      "trial": 0,
      "mount_seed": 20261007,
      "sigma_px": 0.5,
      "outlier_fraction": 0.0,
      "method": "epnp",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": -3.010775491735372,
          "pitch_deg": 3.79460786232279,
          "along_body_m": 0.0009082963057430726,
          "center_vehicle_m": [
            2.36,
            0.0009082963057430726,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": 4.089552251488005,
          "pitch_deg": -1.3607194680997745,
          "along_body_m": -0.021301678009231162,
          "center_vehicle_m": [
            0.3286983219907688,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": -2.303277094446421,
          "pitch_deg": 3.099643788382867,
          "along_body_m": -0.07770516415254036,
          "center_vehicle_m": [
            -2.36,
            -0.07770516415254036,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": -4.03752505809479,
          "pitch_deg": -2.6350348186898787,
          "along_body_m": -0.1291036030828914,
          "center_vehicle_m": [
            0.22089639691710858,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.666384193484578,
          "validation_p95_px": 10.83408684160468,
          "rotation_error_deg": 4.843604460598397,
          "center_error_m": 0.0009082963057430588
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.31395428901275,
          "validation_p95_px": 9.592908257006345,
          "rotation_error_deg": 4.309896627831011,
          "center_error_m": 0.021301678009231162
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.216052519953206,
          "validation_p95_px": 8.055514664616624,
          "rotation_error_deg": 3.861551893252276,
          "center_error_m": 0.07770516415254033
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.120901155502272,
          "validation_p95_px": 9.849109761739927,
          "rotation_error_deg": 4.821010628472409,
          "center_error_m": 0.1291036030828914
        }
      ],
      "success": true,
      "observations_sha256": "1b9447518b28cf8fab36eba62ef889c9ee41a13f8f9c3f331eef56e51b7c4c3e",
      "cameras": [
        {
          "camera_id": 0,
          "invalid_validation_count": 0,
          "validation_rmse_px": 1.784539551833892,
          "validation_p95_px": 3.6982151931479734,
          "rotation_error_deg": 0.3955993262882251,
          "center_error_m": 0.1177919081379009,
          "inliers": 120,
          "fit_ms": 0.226509
        },
        {
          "camera_id": 1,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.5124596553900937,
          "validation_p95_px": 0.9880096779515815,
          "rotation_error_deg": 0.09236141572421018,
          "center_error_m": 0.020238854291191273,
          "inliers": 120,
          "fit_ms": 0.0578
        },
        {
          "camera_id": 2,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.7506622570208364,
          "validation_p95_px": 1.5364331288060484,
          "rotation_error_deg": 0.12460019843915696,
          "center_error_m": 0.04714672153535946,
          "inliers": 120,
          "fit_ms": 0.049834
        },
        {
          "camera_id": 3,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.471281274578746,
          "validation_p95_px": 0.8739057011568819,
          "rotation_error_deg": 0.10346314555465325,
          "center_error_m": 0.031007103188728725,
          "inliers": 120,
          "fit_ms": 0.051597
        }
      ]
    },
    {
      "trial": 0,
      "mount_seed": 20261007,
      "sigma_px": 0.5,
      "outlier_fraction": 0.0,
      "method": "sqpnp",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": -3.010775491735372,
          "pitch_deg": 3.79460786232279,
          "along_body_m": 0.0009082963057430726,
          "center_vehicle_m": [
            2.36,
            0.0009082963057430726,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": 4.089552251488005,
          "pitch_deg": -1.3607194680997745,
          "along_body_m": -0.021301678009231162,
          "center_vehicle_m": [
            0.3286983219907688,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": -2.303277094446421,
          "pitch_deg": 3.099643788382867,
          "along_body_m": -0.07770516415254036,
          "center_vehicle_m": [
            -2.36,
            -0.07770516415254036,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": -4.03752505809479,
          "pitch_deg": -2.6350348186898787,
          "along_body_m": -0.1291036030828914,
          "center_vehicle_m": [
            0.22089639691710858,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.666384193484578,
          "validation_p95_px": 10.83408684160468,
          "rotation_error_deg": 4.843604460598397,
          "center_error_m": 0.0009082963057430588
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.31395428901275,
          "validation_p95_px": 9.592908257006345,
          "rotation_error_deg": 4.309896627831011,
          "center_error_m": 0.021301678009231162
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.216052519953206,
          "validation_p95_px": 8.055514664616624,
          "rotation_error_deg": 3.861551893252276,
          "center_error_m": 0.07770516415254033
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.120901155502272,
          "validation_p95_px": 9.849109761739927,
          "rotation_error_deg": 4.821010628472409,
          "center_error_m": 0.1291036030828914
        }
      ],
      "success": true,
      "observations_sha256": "1b9447518b28cf8fab36eba62ef889c9ee41a13f8f9c3f331eef56e51b7c4c3e",
      "cameras": [
        {
          "camera_id": 0,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.256345505831232,
          "validation_p95_px": 0.5212173220223513,
          "rotation_error_deg": 0.05210383211205027,
          "center_error_m": 0.01615451888284244,
          "inliers": 120,
          "fit_ms": 0.194689
        },
        {
          "camera_id": 1,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.26149201060046234,
          "validation_p95_px": 0.6146260244938403,
          "rotation_error_deg": 0.1038118578459223,
          "center_error_m": 0.019691361752605958,
          "inliers": 120,
          "fit_ms": 0.033663
        },
        {
          "camera_id": 2,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.20564633920394554,
          "validation_p95_px": 0.4555957831488916,
          "rotation_error_deg": 0.08187776215405969,
          "center_error_m": 0.014930604283122581,
          "inliers": 120,
          "fit_ms": 0.027361
        },
        {
          "camera_id": 3,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.1532079014992809,
          "validation_p95_px": 0.3281858052351924,
          "rotation_error_deg": 0.04270448217383182,
          "center_error_m": 0.009850926922264674,
          "inliers": 120,
          "fit_ms": 0.030978
        }
      ]
    },
    {
      "trial": 0,
      "mount_seed": 20261007,
      "sigma_px": 0.5,
      "outlier_fraction": 0.0,
      "method": "ransac_epnp_lm",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": -3.010775491735372,
          "pitch_deg": 3.79460786232279,
          "along_body_m": 0.0009082963057430726,
          "center_vehicle_m": [
            2.36,
            0.0009082963057430726,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": 4.089552251488005,
          "pitch_deg": -1.3607194680997745,
          "along_body_m": -0.021301678009231162,
          "center_vehicle_m": [
            0.3286983219907688,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": -2.303277094446421,
          "pitch_deg": 3.099643788382867,
          "along_body_m": -0.07770516415254036,
          "center_vehicle_m": [
            -2.36,
            -0.07770516415254036,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": -4.03752505809479,
          "pitch_deg": -2.6350348186898787,
          "along_body_m": -0.1291036030828914,
          "center_vehicle_m": [
            0.22089639691710858,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.666384193484578,
          "validation_p95_px": 10.83408684160468,
          "rotation_error_deg": 4.843604460598397,
          "center_error_m": 0.0009082963057430588
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.31395428901275,
          "validation_p95_px": 9.592908257006345,
          "rotation_error_deg": 4.309896627831011,
          "center_error_m": 0.021301678009231162
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.216052519953206,
          "validation_p95_px": 8.055514664616624,
          "rotation_error_deg": 3.861551893252276,
          "center_error_m": 0.07770516415254033
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.120901155502272,
          "validation_p95_px": 9.849109761739927,
          "rotation_error_deg": 4.821010628472409,
          "center_error_m": 0.1291036030828914
        }
      ],
      "success": true,
      "observations_sha256": "1b9447518b28cf8fab36eba62ef889c9ee41a13f8f9c3f331eef56e51b7c4c3e",
      "cameras": [
        {
          "camera_id": 0,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.23931198374360063,
          "validation_p95_px": 0.5583410734529403,
          "rotation_error_deg": 0.0583770962871093,
          "center_error_m": 0.015842938456721037,
          "inliers": 110,
          "fit_ms": 0.602581
        },
        {
          "camera_id": 1,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.32200757497036864,
          "validation_p95_px": 0.5597154529720366,
          "rotation_error_deg": 0.1718089518482592,
          "center_error_m": 0.02928799152631903,
          "inliers": 101,
          "fit_ms": 0.499726
        },
        {
          "camera_id": 2,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.13977533889680657,
          "validation_p95_px": 0.21013009917784528,
          "rotation_error_deg": 0.09079711902059485,
          "center_error_m": 0.008120364114000873,
          "inliers": 107,
          "fit_ms": 0.349121
        },
        {
          "camera_id": 3,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.16527035133970147,
          "validation_p95_px": 0.39535269939409534,
          "rotation_error_deg": 0.059158117491717434,
          "center_error_m": 0.012161315848340171,
          "inliers": 111,
          "fit_ms": 0.276433
        }
      ]
    },
    {
      "trial": 0,
      "mount_seed": 20261007,
      "sigma_px": 0.5,
      "outlier_fraction": 0.1,
      "method": "iterative",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": -3.010775491735372,
          "pitch_deg": 3.79460786232279,
          "along_body_m": 0.0009082963057430726,
          "center_vehicle_m": [
            2.36,
            0.0009082963057430726,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": 4.089552251488005,
          "pitch_deg": -1.3607194680997745,
          "along_body_m": -0.021301678009231162,
          "center_vehicle_m": [
            0.3286983219907688,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": -2.303277094446421,
          "pitch_deg": 3.099643788382867,
          "along_body_m": -0.07770516415254036,
          "center_vehicle_m": [
            -2.36,
            -0.07770516415254036,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": -4.03752505809479,
          "pitch_deg": -2.6350348186898787,
          "along_body_m": -0.1291036030828914,
          "center_vehicle_m": [
            0.22089639691710858,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.666384193484578,
          "validation_p95_px": 10.83408684160468,
          "rotation_error_deg": 4.843604460598397,
          "center_error_m": 0.0009082963057430588
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.31395428901275,
          "validation_p95_px": 9.592908257006345,
          "rotation_error_deg": 4.309896627831011,
          "center_error_m": 0.021301678009231162
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.216052519953206,
          "validation_p95_px": 8.055514664616624,
          "rotation_error_deg": 3.861551893252276,
          "center_error_m": 0.07770516415254033
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.120901155502272,
          "validation_p95_px": 9.849109761739927,
          "rotation_error_deg": 4.821010628472409,
          "center_error_m": 0.1291036030828914
        }
      ],
      "success": true,
      "observations_sha256": "bf40440f84366f3bd86c628e7947542ba24f6872efbf50bccd89011acbca0bcb",
      "cameras": [
        {
          "camera_id": 0,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.8879965102986935,
          "validation_p95_px": 1.308877266354638,
          "rotation_error_deg": 0.5826994712989315,
          "center_error_m": 0.044411138432631905,
          "inliers": 120,
          "fit_ms": 0.389216
        },
        {
          "camera_id": 1,
          "invalid_validation_count": 0,
          "validation_rmse_px": 3.5362244560594456,
          "validation_p95_px": 4.648946938515136,
          "rotation_error_deg": 2.5438916486593532,
          "center_error_m": 0.16284545418891225,
          "inliers": 120,
          "fit_ms": 0.183989
        },
        {
          "camera_id": 2,
          "invalid_validation_count": 0,
          "validation_rmse_px": 1.0022576828456624,
          "validation_p95_px": 2.019196619434531,
          "rotation_error_deg": 0.627122015408953,
          "center_error_m": 0.06724244305187511,
          "inliers": 120,
          "fit_ms": 0.162087
        },
        {
          "camera_id": 3,
          "invalid_validation_count": 0,
          "validation_rmse_px": 4.35396325687686,
          "validation_p95_px": 6.364664969541853,
          "rotation_error_deg": 1.2321673527832804,
          "center_error_m": 0.12304621694960964,
          "inliers": 120,
          "fit_ms": 0.266535
        }
      ]
    },
    {
      "trial": 0,
      "mount_seed": 20261007,
      "sigma_px": 0.5,
      "outlier_fraction": 0.1,
      "method": "epnp",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": -3.010775491735372,
          "pitch_deg": 3.79460786232279,
          "along_body_m": 0.0009082963057430726,
          "center_vehicle_m": [
            2.36,
            0.0009082963057430726,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": 4.089552251488005,
          "pitch_deg": -1.3607194680997745,
          "along_body_m": -0.021301678009231162,
          "center_vehicle_m": [
            0.3286983219907688,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": -2.303277094446421,
          "pitch_deg": 3.099643788382867,
          "along_body_m": -0.07770516415254036,
          "center_vehicle_m": [
            -2.36,
            -0.07770516415254036,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": -4.03752505809479,
          "pitch_deg": -2.6350348186898787,
          "along_body_m": -0.1291036030828914,
          "center_vehicle_m": [
            0.22089639691710858,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.666384193484578,
          "validation_p95_px": 10.83408684160468,
          "rotation_error_deg": 4.843604460598397,
          "center_error_m": 0.0009082963057430588
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.31395428901275,
          "validation_p95_px": 9.592908257006345,
          "rotation_error_deg": 4.309896627831011,
          "center_error_m": 0.021301678009231162
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.216052519953206,
          "validation_p95_px": 8.055514664616624,
          "rotation_error_deg": 3.861551893252276,
          "center_error_m": 0.07770516415254033
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.120901155502272,
          "validation_p95_px": 9.849109761739927,
          "rotation_error_deg": 4.821010628472409,
          "center_error_m": 0.1291036030828914
        }
      ],
      "success": false,
      "observations_sha256": "bf40440f84366f3bd86c628e7947542ba24f6872efbf50bccd89011acbca0bcb",
      "error": "estimated pose projects training point outside validity"
    },
    {
      "trial": 0,
      "mount_seed": 20261007,
      "sigma_px": 0.5,
      "outlier_fraction": 0.1,
      "method": "sqpnp",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": -3.010775491735372,
          "pitch_deg": 3.79460786232279,
          "along_body_m": 0.0009082963057430726,
          "center_vehicle_m": [
            2.36,
            0.0009082963057430726,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": 4.089552251488005,
          "pitch_deg": -1.3607194680997745,
          "along_body_m": -0.021301678009231162,
          "center_vehicle_m": [
            0.3286983219907688,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": -2.303277094446421,
          "pitch_deg": 3.099643788382867,
          "along_body_m": -0.07770516415254036,
          "center_vehicle_m": [
            -2.36,
            -0.07770516415254036,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": -4.03752505809479,
          "pitch_deg": -2.6350348186898787,
          "along_body_m": -0.1291036030828914,
          "center_vehicle_m": [
            0.22089639691710858,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.666384193484578,
          "validation_p95_px": 10.83408684160468,
          "rotation_error_deg": 4.843604460598397,
          "center_error_m": 0.0009082963057430588
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.31395428901275,
          "validation_p95_px": 9.592908257006345,
          "rotation_error_deg": 4.309896627831011,
          "center_error_m": 0.021301678009231162
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.216052519953206,
          "validation_p95_px": 8.055514664616624,
          "rotation_error_deg": 3.861551893252276,
          "center_error_m": 0.07770516415254033
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.120901155502272,
          "validation_p95_px": 9.849109761739927,
          "rotation_error_deg": 4.821010628472409,
          "center_error_m": 0.1291036030828914
        }
      ],
      "success": false,
      "observations_sha256": "bf40440f84366f3bd86c628e7947542ba24f6872efbf50bccd89011acbca0bcb",
      "error": "estimated pose projects training point outside validity"
    },
    {
      "trial": 0,
      "mount_seed": 20261007,
      "sigma_px": 0.5,
      "outlier_fraction": 0.1,
      "method": "ransac_epnp_lm",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": -3.010775491735372,
          "pitch_deg": 3.79460786232279,
          "along_body_m": 0.0009082963057430726,
          "center_vehicle_m": [
            2.36,
            0.0009082963057430726,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": 4.089552251488005,
          "pitch_deg": -1.3607194680997745,
          "along_body_m": -0.021301678009231162,
          "center_vehicle_m": [
            0.3286983219907688,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": -2.303277094446421,
          "pitch_deg": 3.099643788382867,
          "along_body_m": -0.07770516415254036,
          "center_vehicle_m": [
            -2.36,
            -0.07770516415254036,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": -4.03752505809479,
          "pitch_deg": -2.6350348186898787,
          "along_body_m": -0.1291036030828914,
          "center_vehicle_m": [
            0.22089639691710858,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.666384193484578,
          "validation_p95_px": 10.83408684160468,
          "rotation_error_deg": 4.843604460598397,
          "center_error_m": 0.0009082963057430588
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.31395428901275,
          "validation_p95_px": 9.592908257006345,
          "rotation_error_deg": 4.309896627831011,
          "center_error_m": 0.021301678009231162
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.216052519953206,
          "validation_p95_px": 8.055514664616624,
          "rotation_error_deg": 3.861551893252276,
          "center_error_m": 0.07770516415254033
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.120901155502272,
          "validation_p95_px": 9.849109761739927,
          "rotation_error_deg": 4.821010628472409,
          "center_error_m": 0.1291036030828914
        }
      ],
      "success": true,
      "observations_sha256": "bf40440f84366f3bd86c628e7947542ba24f6872efbf50bccd89011acbca0bcb",
      "cameras": [
        {
          "camera_id": 0,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.2144382361893258,
          "validation_p95_px": 0.4685597546939556,
          "rotation_error_deg": 0.12808580733414293,
          "center_error_m": 0.012984842789643037,
          "inliers": 95,
          "fit_ms": 0.915272
        },
        {
          "camera_id": 1,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.44436194953877894,
          "validation_p95_px": 0.967093603370646,
          "rotation_error_deg": 0.20664900335234387,
          "center_error_m": 0.03305643950455599,
          "inliers": 88,
          "fit_ms": 0.87141
        },
        {
          "camera_id": 2,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.2882657468479837,
          "validation_p95_px": 0.7001676982584547,
          "rotation_error_deg": 0.1343519798836324,
          "center_error_m": 0.021025495893802666,
          "inliers": 96,
          "fit_ms": 0.563977
        },
        {
          "camera_id": 3,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.11791241084263246,
          "validation_p95_px": 0.20807272086276027,
          "rotation_error_deg": 0.07398370192663634,
          "center_error_m": 0.0069023094307112155,
          "inliers": 99,
          "fit_ms": 0.47575
        }
      ]
    },
    {
      "trial": 0,
      "mount_seed": 20261007,
      "sigma_px": 1.0,
      "outlier_fraction": 0.0,
      "method": "iterative",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": -3.010775491735372,
          "pitch_deg": 3.79460786232279,
          "along_body_m": 0.0009082963057430726,
          "center_vehicle_m": [
            2.36,
            0.0009082963057430726,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": 4.089552251488005,
          "pitch_deg": -1.3607194680997745,
          "along_body_m": -0.021301678009231162,
          "center_vehicle_m": [
            0.3286983219907688,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": -2.303277094446421,
          "pitch_deg": 3.099643788382867,
          "along_body_m": -0.07770516415254036,
          "center_vehicle_m": [
            -2.36,
            -0.07770516415254036,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": -4.03752505809479,
          "pitch_deg": -2.6350348186898787,
          "along_body_m": -0.1291036030828914,
          "center_vehicle_m": [
            0.22089639691710858,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.666384193484578,
          "validation_p95_px": 10.83408684160468,
          "rotation_error_deg": 4.843604460598397,
          "center_error_m": 0.0009082963057430588
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.31395428901275,
          "validation_p95_px": 9.592908257006345,
          "rotation_error_deg": 4.309896627831011,
          "center_error_m": 0.021301678009231162
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.216052519953206,
          "validation_p95_px": 8.055514664616624,
          "rotation_error_deg": 3.861551893252276,
          "center_error_m": 0.07770516415254033
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.120901155502272,
          "validation_p95_px": 9.849109761739927,
          "rotation_error_deg": 4.821010628472409,
          "center_error_m": 0.1291036030828914
        }
      ],
      "success": true,
      "observations_sha256": "74f06c6b5593c5a9630c12ec04253a08d5c3c0189477e525d0dd1ef063ab8617",
      "cameras": [
        {
          "camera_id": 0,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.2648543727841747,
          "validation_p95_px": 0.503083273785614,
          "rotation_error_deg": 0.07978293161631632,
          "center_error_m": 0.008103726178514053,
          "inliers": 120,
          "fit_ms": 0.360372
        },
        {
          "camera_id": 1,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.6207387859103648,
          "validation_p95_px": 0.9418033635665322,
          "rotation_error_deg": 0.3595649527040981,
          "center_error_m": 0.03080424591896918,
          "inliers": 120,
          "fit_ms": 0.1689
        },
        {
          "camera_id": 2,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.5736883638092211,
          "validation_p95_px": 1.1447252239680956,
          "rotation_error_deg": 0.12049445144833801,
          "center_error_m": 0.027291248893575135,
          "inliers": 120,
          "fit_ms": 0.139895
        },
        {
          "camera_id": 3,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.593293014303867,
          "validation_p95_px": 1.072217895479159,
          "rotation_error_deg": 0.15152785364449745,
          "center_error_m": 0.023455812744273687,
          "inliers": 120,
          "fit_ms": 0.155845
        }
      ]
    },
    {
      "trial": 0,
      "mount_seed": 20261007,
      "sigma_px": 1.0,
      "outlier_fraction": 0.0,
      "method": "epnp",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": -3.010775491735372,
          "pitch_deg": 3.79460786232279,
          "along_body_m": 0.0009082963057430726,
          "center_vehicle_m": [
            2.36,
            0.0009082963057430726,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": 4.089552251488005,
          "pitch_deg": -1.3607194680997745,
          "along_body_m": -0.021301678009231162,
          "center_vehicle_m": [
            0.3286983219907688,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": -2.303277094446421,
          "pitch_deg": 3.099643788382867,
          "along_body_m": -0.07770516415254036,
          "center_vehicle_m": [
            -2.36,
            -0.07770516415254036,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": -4.03752505809479,
          "pitch_deg": -2.6350348186898787,
          "along_body_m": -0.1291036030828914,
          "center_vehicle_m": [
            0.22089639691710858,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.666384193484578,
          "validation_p95_px": 10.83408684160468,
          "rotation_error_deg": 4.843604460598397,
          "center_error_m": 0.0009082963057430588
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.31395428901275,
          "validation_p95_px": 9.592908257006345,
          "rotation_error_deg": 4.309896627831011,
          "center_error_m": 0.021301678009231162
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.216052519953206,
          "validation_p95_px": 8.055514664616624,
          "rotation_error_deg": 3.861551893252276,
          "center_error_m": 0.07770516415254033
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.120901155502272,
          "validation_p95_px": 9.849109761739927,
          "rotation_error_deg": 4.821010628472409,
          "center_error_m": 0.1291036030828914
        }
      ],
      "success": true,
      "observations_sha256": "74f06c6b5593c5a9630c12ec04253a08d5c3c0189477e525d0dd1ef063ab8617",
      "cameras": [
        {
          "camera_id": 0,
          "invalid_validation_count": 0,
          "validation_rmse_px": 2.02993078110218,
          "validation_p95_px": 4.7647509221669715,
          "rotation_error_deg": 0.4162013748070307,
          "center_error_m": 0.10913293614443378,
          "inliers": 120,
          "fit_ms": 0.213394
        },
        {
          "camera_id": 1,
          "invalid_validation_count": 0,
          "validation_rmse_px": 4.719921045593357,
          "validation_p95_px": 10.486364009092796,
          "rotation_error_deg": 0.7096629039010294,
          "center_error_m": 0.30333616551779397,
          "inliers": 120,
          "fit_ms": 0.062779
        },
        {
          "camera_id": 2,
          "invalid_validation_count": 0,
          "validation_rmse_px": 3.4392302837557693,
          "validation_p95_px": 5.18344084689349,
          "rotation_error_deg": 1.9108326045391808,
          "center_error_m": 0.14318820547321331,
          "inliers": 120,
          "fit_ms": 0.050445
        },
        {
          "camera_id": 3,
          "invalid_validation_count": 0,
          "validation_rmse_px": 1.3239410134111074,
          "validation_p95_px": 2.3102491721508325,
          "rotation_error_deg": 0.3105655599187766,
          "center_error_m": 0.06546515826511229,
          "inliers": 120,
          "fit_ms": 0.048031
        }
      ]
    },
    {
      "trial": 0,
      "mount_seed": 20261007,
      "sigma_px": 1.0,
      "outlier_fraction": 0.0,
      "method": "sqpnp",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": -3.010775491735372,
          "pitch_deg": 3.79460786232279,
          "along_body_m": 0.0009082963057430726,
          "center_vehicle_m": [
            2.36,
            0.0009082963057430726,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": 4.089552251488005,
          "pitch_deg": -1.3607194680997745,
          "along_body_m": -0.021301678009231162,
          "center_vehicle_m": [
            0.3286983219907688,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": -2.303277094446421,
          "pitch_deg": 3.099643788382867,
          "along_body_m": -0.07770516415254036,
          "center_vehicle_m": [
            -2.36,
            -0.07770516415254036,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": -4.03752505809479,
          "pitch_deg": -2.6350348186898787,
          "along_body_m": -0.1291036030828914,
          "center_vehicle_m": [
            0.22089639691710858,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.666384193484578,
          "validation_p95_px": 10.83408684160468,
          "rotation_error_deg": 4.843604460598397,
          "center_error_m": 0.0009082963057430588
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.31395428901275,
          "validation_p95_px": 9.592908257006345,
          "rotation_error_deg": 4.309896627831011,
          "center_error_m": 0.021301678009231162
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.216052519953206,
          "validation_p95_px": 8.055514664616624,
          "rotation_error_deg": 3.861551893252276,
          "center_error_m": 0.07770516415254033
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.120901155502272,
          "validation_p95_px": 9.849109761739927,
          "rotation_error_deg": 4.821010628472409,
          "center_error_m": 0.1291036030828914
        }
      ],
      "success": true,
      "observations_sha256": "74f06c6b5593c5a9630c12ec04253a08d5c3c0189477e525d0dd1ef063ab8617",
      "cameras": [
        {
          "camera_id": 0,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.33946276821248383,
          "validation_p95_px": 0.6617794692819484,
          "rotation_error_deg": 0.09782553115371985,
          "center_error_m": 0.021769891511618164,
          "inliers": 120,
          "fit_ms": 0.178808
        },
        {
          "camera_id": 1,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.5236511475450442,
          "validation_p95_px": 0.9577589371008829,
          "rotation_error_deg": 0.35273531603392194,
          "center_error_m": 0.04794409086072013,
          "inliers": 120,
          "fit_ms": 0.032812
        },
        {
          "camera_id": 2,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.5940625806980182,
          "validation_p95_px": 1.1574067996307744,
          "rotation_error_deg": 0.16204525866806715,
          "center_error_m": 0.03872551014799307,
          "inliers": 120,
          "fit_ms": 0.027902
        },
        {
          "camera_id": 3,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.47500340017052345,
          "validation_p95_px": 1.0992823926593644,
          "rotation_error_deg": 0.04060860971364899,
          "center_error_m": 0.029686026436911747,
          "inliers": 120,
          "fit_ms": 0.025458
        }
      ]
    },
    {
      "trial": 0,
      "mount_seed": 20261007,
      "sigma_px": 1.0,
      "outlier_fraction": 0.0,
      "method": "ransac_epnp_lm",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": -3.010775491735372,
          "pitch_deg": 3.79460786232279,
          "along_body_m": 0.0009082963057430726,
          "center_vehicle_m": [
            2.36,
            0.0009082963057430726,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": 4.089552251488005,
          "pitch_deg": -1.3607194680997745,
          "along_body_m": -0.021301678009231162,
          "center_vehicle_m": [
            0.3286983219907688,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": -2.303277094446421,
          "pitch_deg": 3.099643788382867,
          "along_body_m": -0.07770516415254036,
          "center_vehicle_m": [
            -2.36,
            -0.07770516415254036,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": -4.03752505809479,
          "pitch_deg": -2.6350348186898787,
          "along_body_m": -0.1291036030828914,
          "center_vehicle_m": [
            0.22089639691710858,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.666384193484578,
          "validation_p95_px": 10.83408684160468,
          "rotation_error_deg": 4.843604460598397,
          "center_error_m": 0.0009082963057430588
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.31395428901275,
          "validation_p95_px": 9.592908257006345,
          "rotation_error_deg": 4.309896627831011,
          "center_error_m": 0.021301678009231162
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.216052519953206,
          "validation_p95_px": 8.055514664616624,
          "rotation_error_deg": 3.861551893252276,
          "center_error_m": 0.07770516415254033
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.120901155502272,
          "validation_p95_px": 9.849109761739927,
          "rotation_error_deg": 4.821010628472409,
          "center_error_m": 0.1291036030828914
        }
      ],
      "success": true,
      "observations_sha256": "74f06c6b5593c5a9630c12ec04253a08d5c3c0189477e525d0dd1ef063ab8617",
      "cameras": [
        {
          "camera_id": 0,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.8469789653280374,
          "validation_p95_px": 1.7169117668628824,
          "rotation_error_deg": 0.25085848170799563,
          "center_error_m": 0.057356780686685885,
          "inliers": 87,
          "fit_ms": 1.192157
        },
        {
          "camera_id": 1,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.49426405971550613,
          "validation_p95_px": 0.7124594197204954,
          "rotation_error_deg": 0.3251701512721874,
          "center_error_m": 0.024340030281323373,
          "inliers": 78,
          "fit_ms": 1.539032
        },
        {
          "camera_id": 2,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.745174377878534,
          "validation_p95_px": 1.53868397792974,
          "rotation_error_deg": 0.3420623510323877,
          "center_error_m": 0.04286969574360403,
          "inliers": 85,
          "fit_ms": 1.039387
        },
        {
          "camera_id": 3,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.6003776890729112,
          "validation_p95_px": 1.2590386378093676,
          "rotation_error_deg": 0.2730928345354906,
          "center_error_m": 0.04055720828000183,
          "inliers": 80,
          "fit_ms": 1.340106
        }
      ]
    },
    {
      "trial": 0,
      "mount_seed": 20261007,
      "sigma_px": 1.0,
      "outlier_fraction": 0.1,
      "method": "iterative",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": -3.010775491735372,
          "pitch_deg": 3.79460786232279,
          "along_body_m": 0.0009082963057430726,
          "center_vehicle_m": [
            2.36,
            0.0009082963057430726,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": 4.089552251488005,
          "pitch_deg": -1.3607194680997745,
          "along_body_m": -0.021301678009231162,
          "center_vehicle_m": [
            0.3286983219907688,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": -2.303277094446421,
          "pitch_deg": 3.099643788382867,
          "along_body_m": -0.07770516415254036,
          "center_vehicle_m": [
            -2.36,
            -0.07770516415254036,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": -4.03752505809479,
          "pitch_deg": -2.6350348186898787,
          "along_body_m": -0.1291036030828914,
          "center_vehicle_m": [
            0.22089639691710858,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.666384193484578,
          "validation_p95_px": 10.83408684160468,
          "rotation_error_deg": 4.843604460598397,
          "center_error_m": 0.0009082963057430588
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.31395428901275,
          "validation_p95_px": 9.592908257006345,
          "rotation_error_deg": 4.309896627831011,
          "center_error_m": 0.021301678009231162
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.216052519953206,
          "validation_p95_px": 8.055514664616624,
          "rotation_error_deg": 3.861551893252276,
          "center_error_m": 0.07770516415254033
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.120901155502272,
          "validation_p95_px": 9.849109761739927,
          "rotation_error_deg": 4.821010628472409,
          "center_error_m": 0.1291036030828914
        }
      ],
      "success": true,
      "observations_sha256": "72bd773c2100f5ab0a3fa3e11f617e917c87fed13c06a3ae64e7441919d7c6d2",
      "cameras": [
        {
          "camera_id": 0,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.9702328327089517,
          "validation_p95_px": 1.5750916273893216,
          "rotation_error_deg": 0.48087431763744415,
          "center_error_m": 0.04827555626845261,
          "inliers": 120,
          "fit_ms": 0.575109
        },
        {
          "camera_id": 1,
          "invalid_validation_count": 0,
          "validation_rmse_px": 8.977926280109852,
          "validation_p95_px": 12.001013866233208,
          "rotation_error_deg": 6.241621851141431,
          "center_error_m": 0.42433872293157704,
          "inliers": 120,
          "fit_ms": 0.248901
        },
        {
          "camera_id": 2,
          "invalid_validation_count": 0,
          "validation_rmse_px": 1.9453199079930468,
          "validation_p95_px": 2.9129850814573546,
          "rotation_error_deg": 0.9737739734596843,
          "center_error_m": 0.05449092326243717,
          "inliers": 120,
          "fit_ms": 0.127071
        },
        {
          "camera_id": 3,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.8911454830152541,
          "validation_p95_px": 1.2835905109151016,
          "rotation_error_deg": 0.563413894380181,
          "center_error_m": 0.025564657626166908,
          "inliers": 120,
          "fit_ms": 0.152959
        }
      ]
    },
    {
      "trial": 0,
      "mount_seed": 20261007,
      "sigma_px": 1.0,
      "outlier_fraction": 0.1,
      "method": "epnp",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": -3.010775491735372,
          "pitch_deg": 3.79460786232279,
          "along_body_m": 0.0009082963057430726,
          "center_vehicle_m": [
            2.36,
            0.0009082963057430726,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": 4.089552251488005,
          "pitch_deg": -1.3607194680997745,
          "along_body_m": -0.021301678009231162,
          "center_vehicle_m": [
            0.3286983219907688,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": -2.303277094446421,
          "pitch_deg": 3.099643788382867,
          "along_body_m": -0.07770516415254036,
          "center_vehicle_m": [
            -2.36,
            -0.07770516415254036,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": -4.03752505809479,
          "pitch_deg": -2.6350348186898787,
          "along_body_m": -0.1291036030828914,
          "center_vehicle_m": [
            0.22089639691710858,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.666384193484578,
          "validation_p95_px": 10.83408684160468,
          "rotation_error_deg": 4.843604460598397,
          "center_error_m": 0.0009082963057430588
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.31395428901275,
          "validation_p95_px": 9.592908257006345,
          "rotation_error_deg": 4.309896627831011,
          "center_error_m": 0.021301678009231162
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.216052519953206,
          "validation_p95_px": 8.055514664616624,
          "rotation_error_deg": 3.861551893252276,
          "center_error_m": 0.07770516415254033
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.120901155502272,
          "validation_p95_px": 9.849109761739927,
          "rotation_error_deg": 4.821010628472409,
          "center_error_m": 0.1291036030828914
        }
      ],
      "success": true,
      "observations_sha256": "72bd773c2100f5ab0a3fa3e11f617e917c87fed13c06a3ae64e7441919d7c6d2",
      "cameras": [
        {
          "camera_id": 0,
          "invalid_validation_count": 0,
          "validation_rmse_px": 13.44060973849069,
          "validation_p95_px": 33.987259378685714,
          "rotation_error_deg": 5.0810280142047874,
          "center_error_m": 0.7877343134071327,
          "inliers": 120,
          "fit_ms": 0.220938
        },
        {
          "camera_id": 1,
          "invalid_validation_count": 0,
          "validation_rmse_px": 33.864628578119955,
          "validation_p95_px": 75.60897099017832,
          "rotation_error_deg": 6.164402525613119,
          "center_error_m": 2.4580200979858065,
          "inliers": 120,
          "fit_ms": 0.068659
        },
        {
          "camera_id": 2,
          "invalid_validation_count": 0,
          "validation_rmse_px": 13.766508022139522,
          "validation_p95_px": 32.537954438028486,
          "rotation_error_deg": 3.035170422797263,
          "center_error_m": 0.8403729582845384,
          "inliers": 120,
          "fit_ms": 0.052659
        },
        {
          "camera_id": 3,
          "invalid_validation_count": 0,
          "validation_rmse_px": 4.179859791990581,
          "validation_p95_px": 5.966769459328571,
          "rotation_error_deg": 3.0488534955437796,
          "center_error_m": 0.12246561289228555,
          "inliers": 120,
          "fit_ms": 0.047219
        }
      ]
    },
    {
      "trial": 0,
      "mount_seed": 20261007,
      "sigma_px": 1.0,
      "outlier_fraction": 0.1,
      "method": "sqpnp",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": -3.010775491735372,
          "pitch_deg": 3.79460786232279,
          "along_body_m": 0.0009082963057430726,
          "center_vehicle_m": [
            2.36,
            0.0009082963057430726,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": 4.089552251488005,
          "pitch_deg": -1.3607194680997745,
          "along_body_m": -0.021301678009231162,
          "center_vehicle_m": [
            0.3286983219907688,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": -2.303277094446421,
          "pitch_deg": 3.099643788382867,
          "along_body_m": -0.07770516415254036,
          "center_vehicle_m": [
            -2.36,
            -0.07770516415254036,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": -4.03752505809479,
          "pitch_deg": -2.6350348186898787,
          "along_body_m": -0.1291036030828914,
          "center_vehicle_m": [
            0.22089639691710858,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.666384193484578,
          "validation_p95_px": 10.83408684160468,
          "rotation_error_deg": 4.843604460598397,
          "center_error_m": 0.0009082963057430588
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.31395428901275,
          "validation_p95_px": 9.592908257006345,
          "rotation_error_deg": 4.309896627831011,
          "center_error_m": 0.021301678009231162
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.216052519953206,
          "validation_p95_px": 8.055514664616624,
          "rotation_error_deg": 3.861551893252276,
          "center_error_m": 0.07770516415254033
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.120901155502272,
          "validation_p95_px": 9.849109761739927,
          "rotation_error_deg": 4.821010628472409,
          "center_error_m": 0.1291036030828914
        }
      ],
      "success": true,
      "observations_sha256": "72bd773c2100f5ab0a3fa3e11f617e917c87fed13c06a3ae64e7441919d7c6d2",
      "cameras": [
        {
          "camera_id": 0,
          "invalid_validation_count": 0,
          "validation_rmse_px": 3.9539373004233895,
          "validation_p95_px": 10.310844776673687,
          "rotation_error_deg": 2.4287173102558377,
          "center_error_m": 0.25379095694003145,
          "inliers": 120,
          "fit_ms": 0.213323
        },
        {
          "camera_id": 1,
          "invalid_validation_count": 0,
          "validation_rmse_px": 8.78952350728315,
          "validation_p95_px": 20.985568898005003,
          "rotation_error_deg": 1.5885444714650443,
          "center_error_m": 0.5989823311747345,
          "inliers": 120,
          "fit_ms": 0.04201
        },
        {
          "camera_id": 2,
          "invalid_validation_count": 0,
          "validation_rmse_px": 2.972332599847218,
          "validation_p95_px": 5.842643081813662,
          "rotation_error_deg": 0.9276892021989044,
          "center_error_m": 0.23445667226293918,
          "inliers": 120,
          "fit_ms": 0.028274
        },
        {
          "camera_id": 3,
          "invalid_validation_count": 0,
          "validation_rmse_px": 2.02096526369278,
          "validation_p95_px": 3.7562433168336136,
          "rotation_error_deg": 1.1051311581710248,
          "center_error_m": 0.14294578024632512,
          "inliers": 120,
          "fit_ms": 0.02645
        }
      ]
    },
    {
      "trial": 0,
      "mount_seed": 20261007,
      "sigma_px": 1.0,
      "outlier_fraction": 0.1,
      "method": "ransac_epnp_lm",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": -3.010775491735372,
          "pitch_deg": 3.79460786232279,
          "along_body_m": 0.0009082963057430726,
          "center_vehicle_m": [
            2.36,
            0.0009082963057430726,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": 4.089552251488005,
          "pitch_deg": -1.3607194680997745,
          "along_body_m": -0.021301678009231162,
          "center_vehicle_m": [
            0.3286983219907688,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": -2.303277094446421,
          "pitch_deg": 3.099643788382867,
          "along_body_m": -0.07770516415254036,
          "center_vehicle_m": [
            -2.36,
            -0.07770516415254036,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": -4.03752505809479,
          "pitch_deg": -2.6350348186898787,
          "along_body_m": -0.1291036030828914,
          "center_vehicle_m": [
            0.22089639691710858,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.666384193484578,
          "validation_p95_px": 10.83408684160468,
          "rotation_error_deg": 4.843604460598397,
          "center_error_m": 0.0009082963057430588
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.31395428901275,
          "validation_p95_px": 9.592908257006345,
          "rotation_error_deg": 4.309896627831011,
          "center_error_m": 0.021301678009231162
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.216052519953206,
          "validation_p95_px": 8.055514664616624,
          "rotation_error_deg": 3.861551893252276,
          "center_error_m": 0.07770516415254033
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.120901155502272,
          "validation_p95_px": 9.849109761739927,
          "rotation_error_deg": 4.821010628472409,
          "center_error_m": 0.1291036030828914
        }
      ],
      "success": true,
      "observations_sha256": "72bd773c2100f5ab0a3fa3e11f617e917c87fed13c06a3ae64e7441919d7c6d2",
      "cameras": [
        {
          "camera_id": 0,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.6240476813621753,
          "validation_p95_px": 1.2772974256309155,
          "rotation_error_deg": 0.23740436479522198,
          "center_error_m": 0.030922604560673136,
          "inliers": 71,
          "fit_ms": 2.607735
        },
        {
          "camera_id": 1,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.46834444704503914,
          "validation_p95_px": 0.5575704328097961,
          "rotation_error_deg": 0.279701641422501,
          "center_error_m": 0.026587151396962373,
          "inliers": 72,
          "fit_ms": 2.260919
        },
        {
          "camera_id": 2,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.9189985138857303,
          "validation_p95_px": 2.1643978867708027,
          "rotation_error_deg": 0.3295640957797297,
          "center_error_m": 0.06156143282213094,
          "inliers": 78,
          "fit_ms": 1.515248
        },
        {
          "camera_id": 3,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.8555799483851984,
          "validation_p95_px": 1.865230671191123,
          "rotation_error_deg": 0.3541727150955396,
          "center_error_m": 0.056613180270361774,
          "inliers": 78,
          "fit_ms": 1.475102
        }
      ]
    },
    {
      "trial": 1,
      "mount_seed": 20261008,
      "sigma_px": 0.0,
      "outlier_fraction": 0.0,
      "method": "iterative",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": -3.7436353995614047,
          "pitch_deg": 3.764938643595558,
          "along_body_m": 0.14741958466872532,
          "center_vehicle_m": [
            2.36,
            0.14741958466872532,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": -1.0338720775203525,
          "pitch_deg": 2.0961203702794737,
          "along_body_m": -0.11724611718297473,
          "center_vehicle_m": [
            0.23275388281702525,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": -1.6328273219140668,
          "pitch_deg": -2.561924859921124,
          "along_body_m": 0.13115914301930795,
          "center_vehicle_m": [
            -2.36,
            0.13115914301930795,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": 2.8875833512616724,
          "pitch_deg": 3.6013340330058012,
          "along_body_m": 0.0068570772710759564,
          "center_vehicle_m": [
            0.35685707727107596,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.70014261977302,
          "validation_p95_px": 10.740561821590058,
          "rotation_error_deg": 5.308910002935901,
          "center_error_m": 0.14741958466872532
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 6.128252357274026,
          "validation_p95_px": 7.655726834153855,
          "rotation_error_deg": 2.3371976507558307,
          "center_error_m": 0.1172461171829747
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.226666130785293,
          "validation_p95_px": 9.185553367601289,
          "rotation_error_deg": 3.0379499373488215,
          "center_error_m": 0.13115914301930798
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.418421334400252,
          "validation_p95_px": 10.283011717150426,
          "rotation_error_deg": 4.6157338484881985,
          "center_error_m": 0.00685707727107604
        }
      ],
      "success": true,
      "observations_sha256": "991498d0d9c176985c8de93f6699808eef797f46487be3d786572d37f3cfd139",
      "cameras": [
        {
          "camera_id": 0,
          "invalid_validation_count": 0,
          "validation_rmse_px": 1.2543366243857484e-06,
          "validation_p95_px": 1.6839365719711223e-06,
          "rotation_error_deg": 0.0,
          "center_error_m": 6.946361726890148e-08,
          "inliers": 120,
          "fit_ms": 0.360282
        },
        {
          "camera_id": 1,
          "invalid_validation_count": 0,
          "validation_rmse_px": 1.9800329811128966e-06,
          "validation_p95_px": 2.5587434679574572e-06,
          "rotation_error_deg": 0.0,
          "center_error_m": 5.7971641705984524e-08,
          "inliers": 120,
          "fit_ms": 0.145867
        },
        {
          "camera_id": 2,
          "invalid_validation_count": 0,
          "validation_rmse_px": 2.2174897820524313e-06,
          "validation_p95_px": 2.9410645946548003e-06,
          "rotation_error_deg": 0.0,
          "center_error_m": 4.6348594095399034e-08,
          "inliers": 120,
          "fit_ms": 0.125608
        },
        {
          "camera_id": 3,
          "invalid_validation_count": 0,
          "validation_rmse_px": 3.9024537238997644e-07,
          "validation_p95_px": 6.324250980739638e-07,
          "rotation_error_deg": 0.0,
          "center_error_m": 2.448646910931423e-08,
          "inliers": 120,
          "fit_ms": 0.144153
        }
      ]
    },
    {
      "trial": 1,
      "mount_seed": 20261008,
      "sigma_px": 0.0,
      "outlier_fraction": 0.0,
      "method": "epnp",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": -3.7436353995614047,
          "pitch_deg": 3.764938643595558,
          "along_body_m": 0.14741958466872532,
          "center_vehicle_m": [
            2.36,
            0.14741958466872532,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": -1.0338720775203525,
          "pitch_deg": 2.0961203702794737,
          "along_body_m": -0.11724611718297473,
          "center_vehicle_m": [
            0.23275388281702525,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": -1.6328273219140668,
          "pitch_deg": -2.561924859921124,
          "along_body_m": 0.13115914301930795,
          "center_vehicle_m": [
            -2.36,
            0.13115914301930795,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": 2.8875833512616724,
          "pitch_deg": 3.6013340330058012,
          "along_body_m": 0.0068570772710759564,
          "center_vehicle_m": [
            0.35685707727107596,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.70014261977302,
          "validation_p95_px": 10.740561821590058,
          "rotation_error_deg": 5.308910002935901,
          "center_error_m": 0.14741958466872532
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 6.128252357274026,
          "validation_p95_px": 7.655726834153855,
          "rotation_error_deg": 2.3371976507558307,
          "center_error_m": 0.1172461171829747
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.226666130785293,
          "validation_p95_px": 9.185553367601289,
          "rotation_error_deg": 3.0379499373488215,
          "center_error_m": 0.13115914301930798
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.418421334400252,
          "validation_p95_px": 10.283011717150426,
          "rotation_error_deg": 4.6157338484881985,
          "center_error_m": 0.00685707727107604
        }
      ],
      "success": true,
      "observations_sha256": "991498d0d9c176985c8de93f6699808eef797f46487be3d786572d37f3cfd139",
      "cameras": [
        {
          "camera_id": 0,
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.482484981306947e-13,
          "validation_p95_px": 2.142565577235162e-12,
          "rotation_error_deg": 0.0,
          "center_error_m": 6.663743196804369e-14,
          "inliers": 120,
          "fit_ms": 0.249191
        },
        {
          "camera_id": 1,
          "invalid_validation_count": 0,
          "validation_rmse_px": 2.637209141054336e-12,
          "validation_p95_px": 2.7412931334022992e-12,
          "rotation_error_deg": 0.0,
          "center_error_m": 1.9346273337374712e-14,
          "inliers": 120,
          "fit_ms": 0.055225
        },
        {
          "camera_id": 2,
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.522509842276505e-13,
          "validation_p95_px": 1.7107778747013445e-12,
          "rotation_error_deg": 0.0,
          "center_error_m": 7.165438769405199e-14,
          "inliers": 120,
          "fit_ms": 0.049083
        },
        {
          "camera_id": 3,
          "invalid_validation_count": 0,
          "validation_rmse_px": 6.374720788175205e-13,
          "validation_p95_px": 8.461701205576598e-13,
          "rotation_error_deg": 0.0,
          "center_error_m": 3.049907126880956e-14,
          "inliers": 120,
          "fit_ms": 0.047811
        }
      ]
    },
    {
      "trial": 1,
      "mount_seed": 20261008,
      "sigma_px": 0.0,
      "outlier_fraction": 0.0,
      "method": "sqpnp",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": -3.7436353995614047,
          "pitch_deg": 3.764938643595558,
          "along_body_m": 0.14741958466872532,
          "center_vehicle_m": [
            2.36,
            0.14741958466872532,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": -1.0338720775203525,
          "pitch_deg": 2.0961203702794737,
          "along_body_m": -0.11724611718297473,
          "center_vehicle_m": [
            0.23275388281702525,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": -1.6328273219140668,
          "pitch_deg": -2.561924859921124,
          "along_body_m": 0.13115914301930795,
          "center_vehicle_m": [
            -2.36,
            0.13115914301930795,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": 2.8875833512616724,
          "pitch_deg": 3.6013340330058012,
          "along_body_m": 0.0068570772710759564,
          "center_vehicle_m": [
            0.35685707727107596,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.70014261977302,
          "validation_p95_px": 10.740561821590058,
          "rotation_error_deg": 5.308910002935901,
          "center_error_m": 0.14741958466872532
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 6.128252357274026,
          "validation_p95_px": 7.655726834153855,
          "rotation_error_deg": 2.3371976507558307,
          "center_error_m": 0.1172461171829747
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.226666130785293,
          "validation_p95_px": 9.185553367601289,
          "rotation_error_deg": 3.0379499373488215,
          "center_error_m": 0.13115914301930798
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.418421334400252,
          "validation_p95_px": 10.283011717150426,
          "rotation_error_deg": 4.6157338484881985,
          "center_error_m": 0.00685707727107604
        }
      ],
      "success": true,
      "observations_sha256": "991498d0d9c176985c8de93f6699808eef797f46487be3d786572d37f3cfd139",
      "cameras": [
        {
          "camera_id": 0,
          "invalid_validation_count": 0,
          "validation_rmse_px": 4.2700800588797896e-13,
          "validation_p95_px": 7.69167263752943e-13,
          "rotation_error_deg": 0.0,
          "center_error_m": 2.878649289947837e-14,
          "inliers": 120,
          "fit_ms": 0.178178
        },
        {
          "camera_id": 1,
          "invalid_validation_count": 0,
          "validation_rmse_px": 1.9873347764909217e-11,
          "validation_p95_px": 2.3097963421158382e-11,
          "rotation_error_deg": 0.0,
          "center_error_m": 2.1364960655625572e-13,
          "inliers": 120,
          "fit_ms": 0.033453
        },
        {
          "camera_id": 2,
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.32527864100243e-13,
          "validation_p95_px": 1.2794684087445133e-12,
          "rotation_error_deg": 1.2074182697257333e-06,
          "center_error_m": 5.2401299210554475e-14,
          "inliers": 120,
          "fit_ms": 0.024577
        },
        {
          "camera_id": 3,
          "invalid_validation_count": 0,
          "validation_rmse_px": 3.1990048599763034e-12,
          "validation_p95_px": 4.953107148253364e-12,
          "rotation_error_deg": 0.0,
          "center_error_m": 1.0804895874085626e-13,
          "inliers": 120,
          "fit_ms": 0.022463
        }
      ]
    },
    {
      "trial": 1,
      "mount_seed": 20261008,
      "sigma_px": 0.0,
      "outlier_fraction": 0.0,
      "method": "ransac_epnp_lm",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": -3.7436353995614047,
          "pitch_deg": 3.764938643595558,
          "along_body_m": 0.14741958466872532,
          "center_vehicle_m": [
            2.36,
            0.14741958466872532,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": -1.0338720775203525,
          "pitch_deg": 2.0961203702794737,
          "along_body_m": -0.11724611718297473,
          "center_vehicle_m": [
            0.23275388281702525,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": -1.6328273219140668,
          "pitch_deg": -2.561924859921124,
          "along_body_m": 0.13115914301930795,
          "center_vehicle_m": [
            -2.36,
            0.13115914301930795,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": 2.8875833512616724,
          "pitch_deg": 3.6013340330058012,
          "along_body_m": 0.0068570772710759564,
          "center_vehicle_m": [
            0.35685707727107596,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.70014261977302,
          "validation_p95_px": 10.740561821590058,
          "rotation_error_deg": 5.308910002935901,
          "center_error_m": 0.14741958466872532
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 6.128252357274026,
          "validation_p95_px": 7.655726834153855,
          "rotation_error_deg": 2.3371976507558307,
          "center_error_m": 0.1172461171829747
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.226666130785293,
          "validation_p95_px": 9.185553367601289,
          "rotation_error_deg": 3.0379499373488215,
          "center_error_m": 0.13115914301930798
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.418421334400252,
          "validation_p95_px": 10.283011717150426,
          "rotation_error_deg": 4.6157338484881985,
          "center_error_m": 0.00685707727107604
        }
      ],
      "success": true,
      "observations_sha256": "991498d0d9c176985c8de93f6699808eef797f46487be3d786572d37f3cfd139",
      "cameras": [
        {
          "camera_id": 0,
          "invalid_validation_count": 0,
          "validation_rmse_px": 4.776283865971087e-07,
          "validation_p95_px": 6.881756554339289e-07,
          "rotation_error_deg": 0.0,
          "center_error_m": 1.6640138517540734e-08,
          "inliers": 120,
          "fit_ms": 0.431086
        },
        {
          "camera_id": 1,
          "invalid_validation_count": 0,
          "validation_rmse_px": 2.192937747658071e-06,
          "validation_p95_px": 3.1029429439341266e-06,
          "rotation_error_deg": 0.0,
          "center_error_m": 6.714439408148667e-08,
          "inliers": 120,
          "fit_ms": 0.177736
        },
        {
          "camera_id": 2,
          "invalid_validation_count": 0,
          "validation_rmse_px": 1.5441255565260883e-06,
          "validation_p95_px": 2.3059773385061888e-06,
          "rotation_error_deg": 0.0,
          "center_error_m": 5.477986948985598e-08,
          "inliers": 120,
          "fit_ms": 0.157037
        },
        {
          "camera_id": 3,
          "invalid_validation_count": 0,
          "validation_rmse_px": 1.4827216761310716e-06,
          "validation_p95_px": 1.9928790379570486e-06,
          "rotation_error_deg": 0.0,
          "center_error_m": 3.3504094531246846e-08,
          "inliers": 120,
          "fit_ms": 0.118845
        }
      ]
    },
    {
      "trial": 1,
      "mount_seed": 20261008,
      "sigma_px": 0.0,
      "outlier_fraction": 0.1,
      "method": "iterative",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": -3.7436353995614047,
          "pitch_deg": 3.764938643595558,
          "along_body_m": 0.14741958466872532,
          "center_vehicle_m": [
            2.36,
            0.14741958466872532,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": -1.0338720775203525,
          "pitch_deg": 2.0961203702794737,
          "along_body_m": -0.11724611718297473,
          "center_vehicle_m": [
            0.23275388281702525,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": -1.6328273219140668,
          "pitch_deg": -2.561924859921124,
          "along_body_m": 0.13115914301930795,
          "center_vehicle_m": [
            -2.36,
            0.13115914301930795,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": 2.8875833512616724,
          "pitch_deg": 3.6013340330058012,
          "along_body_m": 0.0068570772710759564,
          "center_vehicle_m": [
            0.35685707727107596,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.70014261977302,
          "validation_p95_px": 10.740561821590058,
          "rotation_error_deg": 5.308910002935901,
          "center_error_m": 0.14741958466872532
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 6.128252357274026,
          "validation_p95_px": 7.655726834153855,
          "rotation_error_deg": 2.3371976507558307,
          "center_error_m": 0.1172461171829747
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.226666130785293,
          "validation_p95_px": 9.185553367601289,
          "rotation_error_deg": 3.0379499373488215,
          "center_error_m": 0.13115914301930798
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.418421334400252,
          "validation_p95_px": 10.283011717150426,
          "rotation_error_deg": 4.6157338484881985,
          "center_error_m": 0.00685707727107604
        }
      ],
      "success": true,
      "observations_sha256": "53528f2e659507cb44e6df47ffeebb49b2a95add640fdcd1b312aa5472a1a302",
      "cameras": [
        {
          "camera_id": 0,
          "invalid_validation_count": 0,
          "validation_rmse_px": 1.7233982444126124,
          "validation_p95_px": 3.675186117832601,
          "rotation_error_deg": 1.0339379067103358,
          "center_error_m": 0.12751855601281153,
          "inliers": 120,
          "fit_ms": 0.446054
        },
        {
          "camera_id": 1,
          "invalid_validation_count": 1,
          "validation_rmse_px": 8.107007375913328,
          "validation_p95_px": 9.904830276912456,
          "rotation_error_deg": 2.4035120154672507,
          "center_error_m": 0.3783470096039665,
          "inliers": 120,
          "fit_ms": 0.138622
        },
        {
          "camera_id": 2,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.5573986388874012,
          "validation_p95_px": 0.7801100661152074,
          "rotation_error_deg": 0.3404814459986079,
          "center_error_m": 0.007548023402569037,
          "inliers": 120,
          "fit_ms": 0.126469
        },
        {
          "camera_id": 3,
          "invalid_validation_count": 0,
          "validation_rmse_px": 5.319814181569894,
          "validation_p95_px": 9.261387887464407,
          "rotation_error_deg": 2.621521907560124,
          "center_error_m": 0.26001999388495983,
          "inliers": 120,
          "fit_ms": 0.153871
        }
      ]
    },
    {
      "trial": 1,
      "mount_seed": 20261008,
      "sigma_px": 0.0,
      "outlier_fraction": 0.1,
      "method": "epnp",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": -3.7436353995614047,
          "pitch_deg": 3.764938643595558,
          "along_body_m": 0.14741958466872532,
          "center_vehicle_m": [
            2.36,
            0.14741958466872532,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": -1.0338720775203525,
          "pitch_deg": 2.0961203702794737,
          "along_body_m": -0.11724611718297473,
          "center_vehicle_m": [
            0.23275388281702525,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": -1.6328273219140668,
          "pitch_deg": -2.561924859921124,
          "along_body_m": 0.13115914301930795,
          "center_vehicle_m": [
            -2.36,
            0.13115914301930795,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": 2.8875833512616724,
          "pitch_deg": 3.6013340330058012,
          "along_body_m": 0.0068570772710759564,
          "center_vehicle_m": [
            0.35685707727107596,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.70014261977302,
          "validation_p95_px": 10.740561821590058,
          "rotation_error_deg": 5.308910002935901,
          "center_error_m": 0.14741958466872532
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 6.128252357274026,
          "validation_p95_px": 7.655726834153855,
          "rotation_error_deg": 2.3371976507558307,
          "center_error_m": 0.1172461171829747
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.226666130785293,
          "validation_p95_px": 9.185553367601289,
          "rotation_error_deg": 3.0379499373488215,
          "center_error_m": 0.13115914301930798
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.418421334400252,
          "validation_p95_px": 10.283011717150426,
          "rotation_error_deg": 4.6157338484881985,
          "center_error_m": 0.00685707727107604
        }
      ],
      "success": true,
      "observations_sha256": "53528f2e659507cb44e6df47ffeebb49b2a95add640fdcd1b312aa5472a1a302",
      "cameras": [
        {
          "camera_id": 0,
          "invalid_validation_count": 0,
          "validation_rmse_px": 49.95099719756881,
          "validation_p95_px": 99.97315891779847,
          "rotation_error_deg": 7.044470668639426,
          "center_error_m": 2.939140394829855,
          "inliers": 120,
          "fit_ms": 0.209477
        },
        {
          "camera_id": 1,
          "invalid_validation_count": 1,
          "validation_rmse_px": 53.82624115941049,
          "validation_p95_px": 99.87353652645116,
          "rotation_error_deg": 15.168023074762795,
          "center_error_m": 4.317550758554929,
          "inliers": 120,
          "fit_ms": 0.055224
        },
        {
          "camera_id": 2,
          "invalid_validation_count": 0,
          "validation_rmse_px": 4.42778505972062,
          "validation_p95_px": 9.172772434076338,
          "rotation_error_deg": 1.3197109440514623,
          "center_error_m": 0.43076687636788186,
          "inliers": 120,
          "fit_ms": 0.048341
        },
        {
          "camera_id": 3,
          "invalid_validation_count": 0,
          "validation_rmse_px": 19.321817880029965,
          "validation_p95_px": 34.23008117269123,
          "rotation_error_deg": 3.94232055599417,
          "center_error_m": 0.8172846852150634,
          "inliers": 120,
          "fit_ms": 0.04765
        }
      ]
    },
    {
      "trial": 1,
      "mount_seed": 20261008,
      "sigma_px": 0.0,
      "outlier_fraction": 0.1,
      "method": "sqpnp",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": -3.7436353995614047,
          "pitch_deg": 3.764938643595558,
          "along_body_m": 0.14741958466872532,
          "center_vehicle_m": [
            2.36,
            0.14741958466872532,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": -1.0338720775203525,
          "pitch_deg": 2.0961203702794737,
          "along_body_m": -0.11724611718297473,
          "center_vehicle_m": [
            0.23275388281702525,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": -1.6328273219140668,
          "pitch_deg": -2.561924859921124,
          "along_body_m": 0.13115914301930795,
          "center_vehicle_m": [
            -2.36,
            0.13115914301930795,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": 2.8875833512616724,
          "pitch_deg": 3.6013340330058012,
          "along_body_m": 0.0068570772710759564,
          "center_vehicle_m": [
            0.35685707727107596,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.70014261977302,
          "validation_p95_px": 10.740561821590058,
          "rotation_error_deg": 5.308910002935901,
          "center_error_m": 0.14741958466872532
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 6.128252357274026,
          "validation_p95_px": 7.655726834153855,
          "rotation_error_deg": 2.3371976507558307,
          "center_error_m": 0.1172461171829747
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.226666130785293,
          "validation_p95_px": 9.185553367601289,
          "rotation_error_deg": 3.0379499373488215,
          "center_error_m": 0.13115914301930798
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.418421334400252,
          "validation_p95_px": 10.283011717150426,
          "rotation_error_deg": 4.6157338484881985,
          "center_error_m": 0.00685707727107604
        }
      ],
      "success": true,
      "observations_sha256": "53528f2e659507cb44e6df47ffeebb49b2a95add640fdcd1b312aa5472a1a302",
      "cameras": [
        {
          "camera_id": 0,
          "invalid_validation_count": 0,
          "validation_rmse_px": 6.361960026446708,
          "validation_p95_px": 13.010735772989033,
          "rotation_error_deg": 2.630773996960061,
          "center_error_m": 0.5212214950504416,
          "inliers": 120,
          "fit_ms": 0.180802
        },
        {
          "camera_id": 1,
          "invalid_validation_count": 1,
          "validation_rmse_px": 22.191716902544776,
          "validation_p95_px": 43.04452340542216,
          "rotation_error_deg": 4.866497734342763,
          "center_error_m": 1.256801272984616,
          "inliers": 120,
          "fit_ms": 0.059082
        },
        {
          "camera_id": 2,
          "invalid_validation_count": 0,
          "validation_rmse_px": 1.7712294070959287,
          "validation_p95_px": 3.7993231310062203,
          "rotation_error_deg": 1.0379467816832186,
          "center_error_m": 0.1844818910423088,
          "inliers": 120,
          "fit_ms": 0.035247
        },
        {
          "camera_id": 3,
          "invalid_validation_count": 0,
          "validation_rmse_px": 8.110581681591617,
          "validation_p95_px": 15.01221444759309,
          "rotation_error_deg": 3.412444822572626,
          "center_error_m": 0.6123403195295258,
          "inliers": 120,
          "fit_ms": 0.027973
        }
      ]
    },
    {
      "trial": 1,
      "mount_seed": 20261008,
      "sigma_px": 0.0,
      "outlier_fraction": 0.1,
      "method": "ransac_epnp_lm",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": -3.7436353995614047,
          "pitch_deg": 3.764938643595558,
          "along_body_m": 0.14741958466872532,
          "center_vehicle_m": [
            2.36,
            0.14741958466872532,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": -1.0338720775203525,
          "pitch_deg": 2.0961203702794737,
          "along_body_m": -0.11724611718297473,
          "center_vehicle_m": [
            0.23275388281702525,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": -1.6328273219140668,
          "pitch_deg": -2.561924859921124,
          "along_body_m": 0.13115914301930795,
          "center_vehicle_m": [
            -2.36,
            0.13115914301930795,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": 2.8875833512616724,
          "pitch_deg": 3.6013340330058012,
          "along_body_m": 0.0068570772710759564,
          "center_vehicle_m": [
            0.35685707727107596,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.70014261977302,
          "validation_p95_px": 10.740561821590058,
          "rotation_error_deg": 5.308910002935901,
          "center_error_m": 0.14741958466872532
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 6.128252357274026,
          "validation_p95_px": 7.655726834153855,
          "rotation_error_deg": 2.3371976507558307,
          "center_error_m": 0.1172461171829747
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.226666130785293,
          "validation_p95_px": 9.185553367601289,
          "rotation_error_deg": 3.0379499373488215,
          "center_error_m": 0.13115914301930798
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.418421334400252,
          "validation_p95_px": 10.283011717150426,
          "rotation_error_deg": 4.6157338484881985,
          "center_error_m": 0.00685707727107604
        }
      ],
      "success": true,
      "observations_sha256": "53528f2e659507cb44e6df47ffeebb49b2a95add640fdcd1b312aa5472a1a302",
      "cameras": [
        {
          "camera_id": 0,
          "invalid_validation_count": 0,
          "validation_rmse_px": 1.2505080099101997e-06,
          "validation_p95_px": 1.3962752258614102e-06,
          "rotation_error_deg": 1.2074182697257333e-06,
          "center_error_m": 1.738878500915992e-08,
          "inliers": 108,
          "fit_ms": 0.56505
        },
        {
          "camera_id": 1,
          "invalid_validation_count": 0,
          "validation_rmse_px": 1.882396541099082e-06,
          "validation_p95_px": 2.397940605864982e-06,
          "rotation_error_deg": 0.0,
          "center_error_m": 8.050261153340801e-08,
          "inliers": 108,
          "fit_ms": 0.310066
        },
        {
          "camera_id": 2,
          "invalid_validation_count": 0,
          "validation_rmse_px": 1.799298895289783e-06,
          "validation_p95_px": 2.3707916251847436e-06,
          "rotation_error_deg": 0.0,
          "center_error_m": 7.612534084974868e-08,
          "inliers": 108,
          "fit_ms": 0.293746
        },
        {
          "camera_id": 3,
          "invalid_validation_count": 0,
          "validation_rmse_px": 3.92468221167049e-07,
          "validation_p95_px": 6.32789179046291e-07,
          "rotation_error_deg": 0.0,
          "center_error_m": 2.406195574724579e-08,
          "inliers": 108,
          "fit_ms": 0.287193
        }
      ]
    },
    {
      "trial": 1,
      "mount_seed": 20261008,
      "sigma_px": 0.5,
      "outlier_fraction": 0.0,
      "method": "iterative",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": -3.7436353995614047,
          "pitch_deg": 3.764938643595558,
          "along_body_m": 0.14741958466872532,
          "center_vehicle_m": [
            2.36,
            0.14741958466872532,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": -1.0338720775203525,
          "pitch_deg": 2.0961203702794737,
          "along_body_m": -0.11724611718297473,
          "center_vehicle_m": [
            0.23275388281702525,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": -1.6328273219140668,
          "pitch_deg": -2.561924859921124,
          "along_body_m": 0.13115914301930795,
          "center_vehicle_m": [
            -2.36,
            0.13115914301930795,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": 2.8875833512616724,
          "pitch_deg": 3.6013340330058012,
          "along_body_m": 0.0068570772710759564,
          "center_vehicle_m": [
            0.35685707727107596,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.70014261977302,
          "validation_p95_px": 10.740561821590058,
          "rotation_error_deg": 5.308910002935901,
          "center_error_m": 0.14741958466872532
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 6.128252357274026,
          "validation_p95_px": 7.655726834153855,
          "rotation_error_deg": 2.3371976507558307,
          "center_error_m": 0.1172461171829747
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.226666130785293,
          "validation_p95_px": 9.185553367601289,
          "rotation_error_deg": 3.0379499373488215,
          "center_error_m": 0.13115914301930798
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.418421334400252,
          "validation_p95_px": 10.283011717150426,
          "rotation_error_deg": 4.6157338484881985,
          "center_error_m": 0.00685707727107604
        }
      ],
      "success": true,
      "observations_sha256": "64c1b228b8e4c052226fbbc87fd0cd32047f991292ea356f598ebea0f2d5c055",
      "cameras": [
        {
          "camera_id": 0,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.1702074748288789,
          "validation_p95_px": 0.3882580377803871,
          "rotation_error_deg": 0.04203156921340507,
          "center_error_m": 0.012195379692870554,
          "inliers": 120,
          "fit_ms": 0.366543
        },
        {
          "camera_id": 1,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.42126613729173784,
          "validation_p95_px": 0.5920768960008456,
          "rotation_error_deg": 0.09797160025155373,
          "center_error_m": 0.019607846851777095,
          "inliers": 120,
          "fit_ms": 0.134445
        },
        {
          "camera_id": 2,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.08360051964655253,
          "validation_p95_px": 0.11443849801741167,
          "rotation_error_deg": 0.07125604326282266,
          "center_error_m": 0.005444037242722329,
          "inliers": 120,
          "fit_ms": 0.126168
        },
        {
          "camera_id": 3,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.31899329911712243,
          "validation_p95_px": 0.558944059294466,
          "rotation_error_deg": 0.13201176603531733,
          "center_error_m": 0.019982321444982333,
          "inliers": 120,
          "fit_ms": 0.127
        }
      ]
    },
    {
      "trial": 1,
      "mount_seed": 20261008,
      "sigma_px": 0.5,
      "outlier_fraction": 0.0,
      "method": "epnp",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": -3.7436353995614047,
          "pitch_deg": 3.764938643595558,
          "along_body_m": 0.14741958466872532,
          "center_vehicle_m": [
            2.36,
            0.14741958466872532,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": -1.0338720775203525,
          "pitch_deg": 2.0961203702794737,
          "along_body_m": -0.11724611718297473,
          "center_vehicle_m": [
            0.23275388281702525,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": -1.6328273219140668,
          "pitch_deg": -2.561924859921124,
          "along_body_m": 0.13115914301930795,
          "center_vehicle_m": [
            -2.36,
            0.13115914301930795,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": 2.8875833512616724,
          "pitch_deg": 3.6013340330058012,
          "along_body_m": 0.0068570772710759564,
          "center_vehicle_m": [
            0.35685707727107596,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.70014261977302,
          "validation_p95_px": 10.740561821590058,
          "rotation_error_deg": 5.308910002935901,
          "center_error_m": 0.14741958466872532
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 6.128252357274026,
          "validation_p95_px": 7.655726834153855,
          "rotation_error_deg": 2.3371976507558307,
          "center_error_m": 0.1172461171829747
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.226666130785293,
          "validation_p95_px": 9.185553367601289,
          "rotation_error_deg": 3.0379499373488215,
          "center_error_m": 0.13115914301930798
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.418421334400252,
          "validation_p95_px": 10.283011717150426,
          "rotation_error_deg": 4.6157338484881985,
          "center_error_m": 0.00685707727107604
        }
      ],
      "success": true,
      "observations_sha256": "64c1b228b8e4c052226fbbc87fd0cd32047f991292ea356f598ebea0f2d5c055",
      "cameras": [
        {
          "camera_id": 0,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.9667251466607015,
          "validation_p95_px": 2.0405386870050726,
          "rotation_error_deg": 0.1839426246691224,
          "center_error_m": 0.06805376172554488,
          "inliers": 120,
          "fit_ms": 0.313944
        },
        {
          "camera_id": 1,
          "invalid_validation_count": 0,
          "validation_rmse_px": 1.709885658427049,
          "validation_p95_px": 2.313739911479928,
          "rotation_error_deg": 0.2081701157140586,
          "center_error_m": 0.07422842829136363,
          "inliers": 120,
          "fit_ms": 0.055074
        },
        {
          "camera_id": 2,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.41654068857152116,
          "validation_p95_px": 0.7016348842684347,
          "rotation_error_deg": 0.31932546023006925,
          "center_error_m": 0.037832658882686325,
          "inliers": 120,
          "fit_ms": 0.048222
        },
        {
          "camera_id": 3,
          "invalid_validation_count": 0,
          "validation_rmse_px": 1.2961836681590972,
          "validation_p95_px": 2.5599703993569305,
          "rotation_error_deg": 0.20686665289198122,
          "center_error_m": 0.07494922259419544,
          "inliers": 120,
          "fit_ms": 0.048312
        }
      ]
    },
    {
      "trial": 1,
      "mount_seed": 20261008,
      "sigma_px": 0.5,
      "outlier_fraction": 0.0,
      "method": "sqpnp",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": -3.7436353995614047,
          "pitch_deg": 3.764938643595558,
          "along_body_m": 0.14741958466872532,
          "center_vehicle_m": [
            2.36,
            0.14741958466872532,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": -1.0338720775203525,
          "pitch_deg": 2.0961203702794737,
          "along_body_m": -0.11724611718297473,
          "center_vehicle_m": [
            0.23275388281702525,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": -1.6328273219140668,
          "pitch_deg": -2.561924859921124,
          "along_body_m": 0.13115914301930795,
          "center_vehicle_m": [
            -2.36,
            0.13115914301930795,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": 2.8875833512616724,
          "pitch_deg": 3.6013340330058012,
          "along_body_m": 0.0068570772710759564,
          "center_vehicle_m": [
            0.35685707727107596,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.70014261977302,
          "validation_p95_px": 10.740561821590058,
          "rotation_error_deg": 5.308910002935901,
          "center_error_m": 0.14741958466872532
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 6.128252357274026,
          "validation_p95_px": 7.655726834153855,
          "rotation_error_deg": 2.3371976507558307,
          "center_error_m": 0.1172461171829747
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.226666130785293,
          "validation_p95_px": 9.185553367601289,
          "rotation_error_deg": 3.0379499373488215,
          "center_error_m": 0.13115914301930798
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.418421334400252,
          "validation_p95_px": 10.283011717150426,
          "rotation_error_deg": 4.6157338484881985,
          "center_error_m": 0.00685707727107604
        }
      ],
      "success": true,
      "observations_sha256": "64c1b228b8e4c052226fbbc87fd0cd32047f991292ea356f598ebea0f2d5c055",
      "cameras": [
        {
          "camera_id": 0,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.08861330393250208,
          "validation_p95_px": 0.16381798224307006,
          "rotation_error_deg": 0.0637022302425433,
          "center_error_m": 0.008041037943299913,
          "inliers": 120,
          "fit_ms": 0.183858
        },
        {
          "camera_id": 1,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.45078975967224,
          "validation_p95_px": 0.6124212868033915,
          "rotation_error_deg": 0.12511609953398636,
          "center_error_m": 0.024195399973017305,
          "inliers": 120,
          "fit_ms": 0.032692
        },
        {
          "camera_id": 2,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.22561375048464427,
          "validation_p95_px": 0.516526509696219,
          "rotation_error_deg": 0.1868896948965846,
          "center_error_m": 0.023661642257724377,
          "inliers": 120,
          "fit_ms": 0.02613
        },
        {
          "camera_id": 3,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.3411247298013895,
          "validation_p95_px": 0.6246908554845466,
          "rotation_error_deg": 0.08248719046109032,
          "center_error_m": 0.024141503289225284,
          "inliers": 120,
          "fit_ms": 0.025418
        }
      ]
    },
    {
      "trial": 1,
      "mount_seed": 20261008,
      "sigma_px": 0.5,
      "outlier_fraction": 0.0,
      "method": "ransac_epnp_lm",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": -3.7436353995614047,
          "pitch_deg": 3.764938643595558,
          "along_body_m": 0.14741958466872532,
          "center_vehicle_m": [
            2.36,
            0.14741958466872532,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": -1.0338720775203525,
          "pitch_deg": 2.0961203702794737,
          "along_body_m": -0.11724611718297473,
          "center_vehicle_m": [
            0.23275388281702525,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": -1.6328273219140668,
          "pitch_deg": -2.561924859921124,
          "along_body_m": 0.13115914301930795,
          "center_vehicle_m": [
            -2.36,
            0.13115914301930795,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": 2.8875833512616724,
          "pitch_deg": 3.6013340330058012,
          "along_body_m": 0.0068570772710759564,
          "center_vehicle_m": [
            0.35685707727107596,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.70014261977302,
          "validation_p95_px": 10.740561821590058,
          "rotation_error_deg": 5.308910002935901,
          "center_error_m": 0.14741958466872532
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 6.128252357274026,
          "validation_p95_px": 7.655726834153855,
          "rotation_error_deg": 2.3371976507558307,
          "center_error_m": 0.1172461171829747
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.226666130785293,
          "validation_p95_px": 9.185553367601289,
          "rotation_error_deg": 3.0379499373488215,
          "center_error_m": 0.13115914301930798
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.418421334400252,
          "validation_p95_px": 10.283011717150426,
          "rotation_error_deg": 4.6157338484881985,
          "center_error_m": 0.00685707727107604
        }
      ],
      "success": true,
      "observations_sha256": "64c1b228b8e4c052226fbbc87fd0cd32047f991292ea356f598ebea0f2d5c055",
      "cameras": [
        {
          "camera_id": 0,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.07805245798984928,
          "validation_p95_px": 0.13067682022403618,
          "rotation_error_deg": 0.06940412260969388,
          "center_error_m": 0.006795026077119056,
          "inliers": 114,
          "fit_ms": 0.549701
        },
        {
          "camera_id": 1,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.38823058968154245,
          "validation_p95_px": 0.5403177324989814,
          "rotation_error_deg": 0.1054023510674629,
          "center_error_m": 0.019967105224511204,
          "inliers": 103,
          "fit_ms": 0.537858
        },
        {
          "camera_id": 2,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.2668486283307755,
          "validation_p95_px": 0.349711337784249,
          "rotation_error_deg": 0.20779642731037434,
          "center_error_m": 0.018263567213315957,
          "inliers": 105,
          "fit_ms": 0.374589
        },
        {
          "camera_id": 3,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.6460654238601556,
          "validation_p95_px": 1.2114179335405764,
          "rotation_error_deg": 0.06295788821392795,
          "center_error_m": 0.04419986885093472,
          "inliers": 105,
          "fit_ms": 0.394176
        }
      ]
    },
    {
      "trial": 1,
      "mount_seed": 20261008,
      "sigma_px": 0.5,
      "outlier_fraction": 0.1,
      "method": "iterative",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": -3.7436353995614047,
          "pitch_deg": 3.764938643595558,
          "along_body_m": 0.14741958466872532,
          "center_vehicle_m": [
            2.36,
            0.14741958466872532,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": -1.0338720775203525,
          "pitch_deg": 2.0961203702794737,
          "along_body_m": -0.11724611718297473,
          "center_vehicle_m": [
            0.23275388281702525,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": -1.6328273219140668,
          "pitch_deg": -2.561924859921124,
          "along_body_m": 0.13115914301930795,
          "center_vehicle_m": [
            -2.36,
            0.13115914301930795,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": 2.8875833512616724,
          "pitch_deg": 3.6013340330058012,
          "along_body_m": 0.0068570772710759564,
          "center_vehicle_m": [
            0.35685707727107596,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.70014261977302,
          "validation_p95_px": 10.740561821590058,
          "rotation_error_deg": 5.308910002935901,
          "center_error_m": 0.14741958466872532
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 6.128252357274026,
          "validation_p95_px": 7.655726834153855,
          "rotation_error_deg": 2.3371976507558307,
          "center_error_m": 0.1172461171829747
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.226666130785293,
          "validation_p95_px": 9.185553367601289,
          "rotation_error_deg": 3.0379499373488215,
          "center_error_m": 0.13115914301930798
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.418421334400252,
          "validation_p95_px": 10.283011717150426,
          "rotation_error_deg": 4.6157338484881985,
          "center_error_m": 0.00685707727107604
        }
      ],
      "success": true,
      "observations_sha256": "569527bcf5720794ec1548a6f3abd7a2420c0b4c42b40be3d8b9fcf213193f17",
      "cameras": [
        {
          "camera_id": 0,
          "invalid_validation_count": 0,
          "validation_rmse_px": 2.676571196252757,
          "validation_p95_px": 3.6341981671000227,
          "rotation_error_deg": 1.8871899291053442,
          "center_error_m": 0.15647454047105366,
          "inliers": 120,
          "fit_ms": 0.48614
        },
        {
          "camera_id": 1,
          "invalid_validation_count": 0,
          "validation_rmse_px": 3.2794105467548755,
          "validation_p95_px": 4.486365350536824,
          "rotation_error_deg": 0.9184683583597429,
          "center_error_m": 0.16186096698212143,
          "inliers": 120,
          "fit_ms": 0.141648
        },
        {
          "camera_id": 2,
          "invalid_validation_count": 0,
          "validation_rmse_px": 1.0925815266401455,
          "validation_p95_px": 1.8123454307328732,
          "rotation_error_deg": 0.584916859510146,
          "center_error_m": 0.0791445308994065,
          "inliers": 120,
          "fit_ms": 0.13778
        },
        {
          "camera_id": 3,
          "invalid_validation_count": 0,
          "validation_rmse_px": 2.135477290647507,
          "validation_p95_px": 2.765658352616422,
          "rotation_error_deg": 1.553956409939782,
          "center_error_m": 0.07105474039381462,
          "inliers": 120,
          "fit_ms": 0.183166
        }
      ]
    },
    {
      "trial": 1,
      "mount_seed": 20261008,
      "sigma_px": 0.5,
      "outlier_fraction": 0.1,
      "method": "epnp",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": -3.7436353995614047,
          "pitch_deg": 3.764938643595558,
          "along_body_m": 0.14741958466872532,
          "center_vehicle_m": [
            2.36,
            0.14741958466872532,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": -1.0338720775203525,
          "pitch_deg": 2.0961203702794737,
          "along_body_m": -0.11724611718297473,
          "center_vehicle_m": [
            0.23275388281702525,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": -1.6328273219140668,
          "pitch_deg": -2.561924859921124,
          "along_body_m": 0.13115914301930795,
          "center_vehicle_m": [
            -2.36,
            0.13115914301930795,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": 2.8875833512616724,
          "pitch_deg": 3.6013340330058012,
          "along_body_m": 0.0068570772710759564,
          "center_vehicle_m": [
            0.35685707727107596,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.70014261977302,
          "validation_p95_px": 10.740561821590058,
          "rotation_error_deg": 5.308910002935901,
          "center_error_m": 0.14741958466872532
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 6.128252357274026,
          "validation_p95_px": 7.655726834153855,
          "rotation_error_deg": 2.3371976507558307,
          "center_error_m": 0.1172461171829747
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.226666130785293,
          "validation_p95_px": 9.185553367601289,
          "rotation_error_deg": 3.0379499373488215,
          "center_error_m": 0.13115914301930798
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.418421334400252,
          "validation_p95_px": 10.283011717150426,
          "rotation_error_deg": 4.6157338484881985,
          "center_error_m": 0.00685707727107604
        }
      ],
      "success": false,
      "observations_sha256": "569527bcf5720794ec1548a6f3abd7a2420c0b4c42b40be3d8b9fcf213193f17",
      "error": "estimated pose projects training point outside validity"
    },
    {
      "trial": 1,
      "mount_seed": 20261008,
      "sigma_px": 0.5,
      "outlier_fraction": 0.1,
      "method": "sqpnp",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": -3.7436353995614047,
          "pitch_deg": 3.764938643595558,
          "along_body_m": 0.14741958466872532,
          "center_vehicle_m": [
            2.36,
            0.14741958466872532,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": -1.0338720775203525,
          "pitch_deg": 2.0961203702794737,
          "along_body_m": -0.11724611718297473,
          "center_vehicle_m": [
            0.23275388281702525,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": -1.6328273219140668,
          "pitch_deg": -2.561924859921124,
          "along_body_m": 0.13115914301930795,
          "center_vehicle_m": [
            -2.36,
            0.13115914301930795,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": 2.8875833512616724,
          "pitch_deg": 3.6013340330058012,
          "along_body_m": 0.0068570772710759564,
          "center_vehicle_m": [
            0.35685707727107596,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.70014261977302,
          "validation_p95_px": 10.740561821590058,
          "rotation_error_deg": 5.308910002935901,
          "center_error_m": 0.14741958466872532
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 6.128252357274026,
          "validation_p95_px": 7.655726834153855,
          "rotation_error_deg": 2.3371976507558307,
          "center_error_m": 0.1172461171829747
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.226666130785293,
          "validation_p95_px": 9.185553367601289,
          "rotation_error_deg": 3.0379499373488215,
          "center_error_m": 0.13115914301930798
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.418421334400252,
          "validation_p95_px": 10.283011717150426,
          "rotation_error_deg": 4.6157338484881985,
          "center_error_m": 0.00685707727107604
        }
      ],
      "success": false,
      "observations_sha256": "569527bcf5720794ec1548a6f3abd7a2420c0b4c42b40be3d8b9fcf213193f17",
      "error": "estimated pose projects training point outside validity"
    },
    {
      "trial": 1,
      "mount_seed": 20261008,
      "sigma_px": 0.5,
      "outlier_fraction": 0.1,
      "method": "ransac_epnp_lm",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": -3.7436353995614047,
          "pitch_deg": 3.764938643595558,
          "along_body_m": 0.14741958466872532,
          "center_vehicle_m": [
            2.36,
            0.14741958466872532,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": -1.0338720775203525,
          "pitch_deg": 2.0961203702794737,
          "along_body_m": -0.11724611718297473,
          "center_vehicle_m": [
            0.23275388281702525,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": -1.6328273219140668,
          "pitch_deg": -2.561924859921124,
          "along_body_m": 0.13115914301930795,
          "center_vehicle_m": [
            -2.36,
            0.13115914301930795,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": 2.8875833512616724,
          "pitch_deg": 3.6013340330058012,
          "along_body_m": 0.0068570772710759564,
          "center_vehicle_m": [
            0.35685707727107596,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.70014261977302,
          "validation_p95_px": 10.740561821590058,
          "rotation_error_deg": 5.308910002935901,
          "center_error_m": 0.14741958466872532
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 6.128252357274026,
          "validation_p95_px": 7.655726834153855,
          "rotation_error_deg": 2.3371976507558307,
          "center_error_m": 0.1172461171829747
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.226666130785293,
          "validation_p95_px": 9.185553367601289,
          "rotation_error_deg": 3.0379499373488215,
          "center_error_m": 0.13115914301930798
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.418421334400252,
          "validation_p95_px": 10.283011717150426,
          "rotation_error_deg": 4.6157338484881985,
          "center_error_m": 0.00685707727107604
        }
      ],
      "success": true,
      "observations_sha256": "569527bcf5720794ec1548a6f3abd7a2420c0b4c42b40be3d8b9fcf213193f17",
      "cameras": [
        {
          "camera_id": 0,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.27400155253633324,
          "validation_p95_px": 0.47535162761739724,
          "rotation_error_deg": 0.053724789802600016,
          "center_error_m": 0.018998878348063828,
          "inliers": 92,
          "fit_ms": 0.965277
        },
        {
          "camera_id": 1,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.22022724932243024,
          "validation_p95_px": 0.30900358187464616,
          "rotation_error_deg": 0.08716127261477566,
          "center_error_m": 0.009073640106386284,
          "inliers": 97,
          "fit_ms": 0.556322
        },
        {
          "camera_id": 2,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.33955157600305635,
          "validation_p95_px": 0.6223720141825663,
          "rotation_error_deg": 0.1985910443245883,
          "center_error_m": 0.032582469828237155,
          "inliers": 99,
          "fit_ms": 0.513462
        },
        {
          "camera_id": 3,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.332206172692382,
          "validation_p95_px": 0.5642333660806181,
          "rotation_error_deg": 0.12318712045720415,
          "center_error_m": 0.025939029165643473,
          "inliers": 101,
          "fit_ms": 0.442177
        }
      ]
    },
    {
      "trial": 1,
      "mount_seed": 20261008,
      "sigma_px": 1.0,
      "outlier_fraction": 0.0,
      "method": "iterative",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": -3.7436353995614047,
          "pitch_deg": 3.764938643595558,
          "along_body_m": 0.14741958466872532,
          "center_vehicle_m": [
            2.36,
            0.14741958466872532,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": -1.0338720775203525,
          "pitch_deg": 2.0961203702794737,
          "along_body_m": -0.11724611718297473,
          "center_vehicle_m": [
            0.23275388281702525,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": -1.6328273219140668,
          "pitch_deg": -2.561924859921124,
          "along_body_m": 0.13115914301930795,
          "center_vehicle_m": [
            -2.36,
            0.13115914301930795,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": 2.8875833512616724,
          "pitch_deg": 3.6013340330058012,
          "along_body_m": 0.0068570772710759564,
          "center_vehicle_m": [
            0.35685707727107596,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.70014261977302,
          "validation_p95_px": 10.740561821590058,
          "rotation_error_deg": 5.308910002935901,
          "center_error_m": 0.14741958466872532
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 6.128252357274026,
          "validation_p95_px": 7.655726834153855,
          "rotation_error_deg": 2.3371976507558307,
          "center_error_m": 0.1172461171829747
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.226666130785293,
          "validation_p95_px": 9.185553367601289,
          "rotation_error_deg": 3.0379499373488215,
          "center_error_m": 0.13115914301930798
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.418421334400252,
          "validation_p95_px": 10.283011717150426,
          "rotation_error_deg": 4.6157338484881985,
          "center_error_m": 0.00685707727107604
        }
      ],
      "success": true,
      "observations_sha256": "40215d02855218f7da65e77dbbb9bca9644110272c2421a848553fdcb1a535ef",
      "cameras": [
        {
          "camera_id": 0,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.5529658161513977,
          "validation_p95_px": 1.2426649477660658,
          "rotation_error_deg": 0.17288505432014173,
          "center_error_m": 0.03898856116530852,
          "inliers": 120,
          "fit_ms": 0.400408
        },
        {
          "camera_id": 1,
          "invalid_validation_count": 0,
          "validation_rmse_px": 1.1353900056820447,
          "validation_p95_px": 1.0212567546208384,
          "rotation_error_deg": 0.3708561849389714,
          "center_error_m": 0.04301165340841801,
          "inliers": 120,
          "fit_ms": 0.152939
        },
        {
          "camera_id": 2,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.2403689003527713,
          "validation_p95_px": 0.36758016852452924,
          "rotation_error_deg": 0.13767667388830365,
          "center_error_m": 0.017379880949071485,
          "inliers": 120,
          "fit_ms": 0.128313
        },
        {
          "camera_id": 3,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.3670328497814414,
          "validation_p95_px": 0.623724252482137,
          "rotation_error_deg": 0.2201720463067311,
          "center_error_m": 0.02481146258569645,
          "inliers": 120,
          "fit_ms": 0.125127
        }
      ]
    },
    {
      "trial": 1,
      "mount_seed": 20261008,
      "sigma_px": 1.0,
      "outlier_fraction": 0.0,
      "method": "epnp",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": -3.7436353995614047,
          "pitch_deg": 3.764938643595558,
          "along_body_m": 0.14741958466872532,
          "center_vehicle_m": [
            2.36,
            0.14741958466872532,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": -1.0338720775203525,
          "pitch_deg": 2.0961203702794737,
          "along_body_m": -0.11724611718297473,
          "center_vehicle_m": [
            0.23275388281702525,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": -1.6328273219140668,
          "pitch_deg": -2.561924859921124,
          "along_body_m": 0.13115914301930795,
          "center_vehicle_m": [
            -2.36,
            0.13115914301930795,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": 2.8875833512616724,
          "pitch_deg": 3.6013340330058012,
          "along_body_m": 0.0068570772710759564,
          "center_vehicle_m": [
            0.35685707727107596,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.70014261977302,
          "validation_p95_px": 10.740561821590058,
          "rotation_error_deg": 5.308910002935901,
          "center_error_m": 0.14741958466872532
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 6.128252357274026,
          "validation_p95_px": 7.655726834153855,
          "rotation_error_deg": 2.3371976507558307,
          "center_error_m": 0.1172461171829747
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.226666130785293,
          "validation_p95_px": 9.185553367601289,
          "rotation_error_deg": 3.0379499373488215,
          "center_error_m": 0.13115914301930798
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.418421334400252,
          "validation_p95_px": 10.283011717150426,
          "rotation_error_deg": 4.6157338484881985,
          "center_error_m": 0.00685707727107604
        }
      ],
      "success": true,
      "observations_sha256": "40215d02855218f7da65e77dbbb9bca9644110272c2421a848553fdcb1a535ef",
      "cameras": [
        {
          "camera_id": 0,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.7190998874971561,
          "validation_p95_px": 1.394860390979295,
          "rotation_error_deg": 0.2137530247893035,
          "center_error_m": 0.05667730615751825,
          "inliers": 120,
          "fit_ms": 0.220848
        },
        {
          "camera_id": 1,
          "invalid_validation_count": 0,
          "validation_rmse_px": 5.46340240088488,
          "validation_p95_px": 7.539414604846947,
          "rotation_error_deg": 0.6322463611797139,
          "center_error_m": 0.2218248865364809,
          "inliers": 120,
          "fit_ms": 0.055244
        },
        {
          "camera_id": 2,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.9817233525386089,
          "validation_p95_px": 1.889674964295513,
          "rotation_error_deg": 0.08789073548012773,
          "center_error_m": 0.06906034488901204,
          "inliers": 120,
          "fit_ms": 0.048371
        },
        {
          "camera_id": 3,
          "invalid_validation_count": 0,
          "validation_rmse_px": 2.624153191269411,
          "validation_p95_px": 3.0913058561995466,
          "rotation_error_deg": 1.383085914973374,
          "center_error_m": 0.04778326638312582,
          "inliers": 120,
          "fit_ms": 0.047089
        }
      ]
    },
    {
      "trial": 1,
      "mount_seed": 20261008,
      "sigma_px": 1.0,
      "outlier_fraction": 0.0,
      "method": "sqpnp",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": -3.7436353995614047,
          "pitch_deg": 3.764938643595558,
          "along_body_m": 0.14741958466872532,
          "center_vehicle_m": [
            2.36,
            0.14741958466872532,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": -1.0338720775203525,
          "pitch_deg": 2.0961203702794737,
          "along_body_m": -0.11724611718297473,
          "center_vehicle_m": [
            0.23275388281702525,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": -1.6328273219140668,
          "pitch_deg": -2.561924859921124,
          "along_body_m": 0.13115914301930795,
          "center_vehicle_m": [
            -2.36,
            0.13115914301930795,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": 2.8875833512616724,
          "pitch_deg": 3.6013340330058012,
          "along_body_m": 0.0068570772710759564,
          "center_vehicle_m": [
            0.35685707727107596,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.70014261977302,
          "validation_p95_px": 10.740561821590058,
          "rotation_error_deg": 5.308910002935901,
          "center_error_m": 0.14741958466872532
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 6.128252357274026,
          "validation_p95_px": 7.655726834153855,
          "rotation_error_deg": 2.3371976507558307,
          "center_error_m": 0.1172461171829747
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.226666130785293,
          "validation_p95_px": 9.185553367601289,
          "rotation_error_deg": 3.0379499373488215,
          "center_error_m": 0.13115914301930798
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.418421334400252,
          "validation_p95_px": 10.283011717150426,
          "rotation_error_deg": 4.6157338484881985,
          "center_error_m": 0.00685707727107604
        }
      ],
      "success": true,
      "observations_sha256": "40215d02855218f7da65e77dbbb9bca9644110272c2421a848553fdcb1a535ef",
      "cameras": [
        {
          "camera_id": 0,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.5256605893053699,
          "validation_p95_px": 1.0318336152817533,
          "rotation_error_deg": 0.1761625547189898,
          "center_error_m": 0.040883319218300004,
          "inliers": 120,
          "fit_ms": 0.187735
        },
        {
          "camera_id": 1,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.8823562615877071,
          "validation_p95_px": 0.7987993029405716,
          "rotation_error_deg": 0.2002632008388184,
          "center_error_m": 0.03496470208645232,
          "inliers": 120,
          "fit_ms": 0.035377
        },
        {
          "camera_id": 2,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.40846552230242256,
          "validation_p95_px": 0.7065994380726521,
          "rotation_error_deg": 0.13603842107230404,
          "center_error_m": 0.03402239655148364,
          "inliers": 120,
          "fit_ms": 0.028254
        },
        {
          "camera_id": 3,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.5761007010439606,
          "validation_p95_px": 1.1620675166780967,
          "rotation_error_deg": 0.17542055922342267,
          "center_error_m": 0.04202821403049595,
          "inliers": 120,
          "fit_ms": 0.026169
        }
      ]
    },
    {
      "trial": 1,
      "mount_seed": 20261008,
      "sigma_px": 1.0,
      "outlier_fraction": 0.0,
      "method": "ransac_epnp_lm",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": -3.7436353995614047,
          "pitch_deg": 3.764938643595558,
          "along_body_m": 0.14741958466872532,
          "center_vehicle_m": [
            2.36,
            0.14741958466872532,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": -1.0338720775203525,
          "pitch_deg": 2.0961203702794737,
          "along_body_m": -0.11724611718297473,
          "center_vehicle_m": [
            0.23275388281702525,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": -1.6328273219140668,
          "pitch_deg": -2.561924859921124,
          "along_body_m": 0.13115914301930795,
          "center_vehicle_m": [
            -2.36,
            0.13115914301930795,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": 2.8875833512616724,
          "pitch_deg": 3.6013340330058012,
          "along_body_m": 0.0068570772710759564,
          "center_vehicle_m": [
            0.35685707727107596,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.70014261977302,
          "validation_p95_px": 10.740561821590058,
          "rotation_error_deg": 5.308910002935901,
          "center_error_m": 0.14741958466872532
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 6.128252357274026,
          "validation_p95_px": 7.655726834153855,
          "rotation_error_deg": 2.3371976507558307,
          "center_error_m": 0.1172461171829747
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.226666130785293,
          "validation_p95_px": 9.185553367601289,
          "rotation_error_deg": 3.0379499373488215,
          "center_error_m": 0.13115914301930798
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.418421334400252,
          "validation_p95_px": 10.283011717150426,
          "rotation_error_deg": 4.6157338484881985,
          "center_error_m": 0.00685707727107604
        }
      ],
      "success": true,
      "observations_sha256": "40215d02855218f7da65e77dbbb9bca9644110272c2421a848553fdcb1a535ef",
      "cameras": [
        {
          "camera_id": 0,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.720421567851287,
          "validation_p95_px": 1.5026507672881297,
          "rotation_error_deg": 0.22885614037531724,
          "center_error_m": 0.052125744650924566,
          "inliers": 86,
          "fit_ms": 1.295983
        },
        {
          "camera_id": 1,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.534689179971537,
          "validation_p95_px": 0.5495228896808525,
          "rotation_error_deg": 0.1591151199836698,
          "center_error_m": 0.025892540459466342,
          "inliers": 85,
          "fit_ms": 1.022435
        },
        {
          "camera_id": 2,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.5048383264026921,
          "validation_p95_px": 0.9189051254083677,
          "rotation_error_deg": 0.36088690978737453,
          "center_error_m": 0.04993484434900876,
          "inliers": 81,
          "fit_ms": 1.238574
        },
        {
          "camera_id": 3,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.7097883894941516,
          "validation_p95_px": 1.3336180176981725,
          "rotation_error_deg": 0.2691107315488266,
          "center_error_m": 0.04308033117357543,
          "inliers": 84,
          "fit_ms": 1.068302
        }
      ]
    },
    {
      "trial": 1,
      "mount_seed": 20261008,
      "sigma_px": 1.0,
      "outlier_fraction": 0.1,
      "method": "iterative",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": -3.7436353995614047,
          "pitch_deg": 3.764938643595558,
          "along_body_m": 0.14741958466872532,
          "center_vehicle_m": [
            2.36,
            0.14741958466872532,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": -1.0338720775203525,
          "pitch_deg": 2.0961203702794737,
          "along_body_m": -0.11724611718297473,
          "center_vehicle_m": [
            0.23275388281702525,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": -1.6328273219140668,
          "pitch_deg": -2.561924859921124,
          "along_body_m": 0.13115914301930795,
          "center_vehicle_m": [
            -2.36,
            0.13115914301930795,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": 2.8875833512616724,
          "pitch_deg": 3.6013340330058012,
          "along_body_m": 0.0068570772710759564,
          "center_vehicle_m": [
            0.35685707727107596,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.70014261977302,
          "validation_p95_px": 10.740561821590058,
          "rotation_error_deg": 5.308910002935901,
          "center_error_m": 0.14741958466872532
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 6.128252357274026,
          "validation_p95_px": 7.655726834153855,
          "rotation_error_deg": 2.3371976507558307,
          "center_error_m": 0.1172461171829747
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.226666130785293,
          "validation_p95_px": 9.185553367601289,
          "rotation_error_deg": 3.0379499373488215,
          "center_error_m": 0.13115914301930798
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.418421334400252,
          "validation_p95_px": 10.283011717150426,
          "rotation_error_deg": 4.6157338484881985,
          "center_error_m": 0.00685707727107604
        }
      ],
      "success": true,
      "observations_sha256": "8eaae3596ba84851b6fb86c2acb12a4deb239b826dfd034789d6dd283bd97580",
      "cameras": [
        {
          "camera_id": 0,
          "invalid_validation_count": 0,
          "validation_rmse_px": 2.424641675454588,
          "validation_p95_px": 4.078383117876104,
          "rotation_error_deg": 1.4017028981035824,
          "center_error_m": 0.1444380028425996,
          "inliers": 120,
          "fit_ms": 0.394947
        },
        {
          "camera_id": 1,
          "invalid_validation_count": 0,
          "validation_rmse_px": 1.9595667436584803,
          "validation_p95_px": 3.4296483378697595,
          "rotation_error_deg": 1.0324077915794674,
          "center_error_m": 0.1395027316497139,
          "inliers": 120,
          "fit_ms": 0.179539
        },
        {
          "camera_id": 2,
          "invalid_validation_count": 0,
          "validation_rmse_px": 1.0288689690140371,
          "validation_p95_px": 1.6946896773019535,
          "rotation_error_deg": 0.5149411355624627,
          "center_error_m": 0.06187601317037203,
          "inliers": 120,
          "fit_ms": 0.196542
        },
        {
          "camera_id": 3,
          "invalid_validation_count": 0,
          "validation_rmse_px": 1.783352864253032,
          "validation_p95_px": 2.09679131233774,
          "rotation_error_deg": 0.9913969610027151,
          "center_error_m": 0.03569593110740637,
          "inliers": 120,
          "fit_ms": 0.223933
        }
      ]
    },
    {
      "trial": 1,
      "mount_seed": 20261008,
      "sigma_px": 1.0,
      "outlier_fraction": 0.1,
      "method": "epnp",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": -3.7436353995614047,
          "pitch_deg": 3.764938643595558,
          "along_body_m": 0.14741958466872532,
          "center_vehicle_m": [
            2.36,
            0.14741958466872532,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": -1.0338720775203525,
          "pitch_deg": 2.0961203702794737,
          "along_body_m": -0.11724611718297473,
          "center_vehicle_m": [
            0.23275388281702525,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": -1.6328273219140668,
          "pitch_deg": -2.561924859921124,
          "along_body_m": 0.13115914301930795,
          "center_vehicle_m": [
            -2.36,
            0.13115914301930795,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": 2.8875833512616724,
          "pitch_deg": 3.6013340330058012,
          "along_body_m": 0.0068570772710759564,
          "center_vehicle_m": [
            0.35685707727107596,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.70014261977302,
          "validation_p95_px": 10.740561821590058,
          "rotation_error_deg": 5.308910002935901,
          "center_error_m": 0.14741958466872532
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 6.128252357274026,
          "validation_p95_px": 7.655726834153855,
          "rotation_error_deg": 2.3371976507558307,
          "center_error_m": 0.1172461171829747
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.226666130785293,
          "validation_p95_px": 9.185553367601289,
          "rotation_error_deg": 3.0379499373488215,
          "center_error_m": 0.13115914301930798
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.418421334400252,
          "validation_p95_px": 10.283011717150426,
          "rotation_error_deg": 4.6157338484881985,
          "center_error_m": 0.00685707727107604
        }
      ],
      "success": true,
      "observations_sha256": "8eaae3596ba84851b6fb86c2acb12a4deb239b826dfd034789d6dd283bd97580",
      "cameras": [
        {
          "camera_id": 0,
          "invalid_validation_count": 0,
          "validation_rmse_px": 22.520962451555878,
          "validation_p95_px": 45.74778059855098,
          "rotation_error_deg": 7.891895458518919,
          "center_error_m": 1.7554664262953175,
          "inliers": 120,
          "fit_ms": 0.209437
        },
        {
          "camera_id": 1,
          "invalid_validation_count": 1,
          "validation_rmse_px": 7.298922089287312,
          "validation_p95_px": 11.48029427641489,
          "rotation_error_deg": 1.6232173855818177,
          "center_error_m": 0.40377819737757076,
          "inliers": 120,
          "fit_ms": 0.057388
        },
        {
          "camera_id": 2,
          "invalid_validation_count": 0,
          "validation_rmse_px": 8.143427284782112,
          "validation_p95_px": 12.857821735191102,
          "rotation_error_deg": 1.9075818876143036,
          "center_error_m": 0.3225205702854366,
          "inliers": 120,
          "fit_ms": 0.049834
        },
        {
          "camera_id": 3,
          "invalid_validation_count": 0,
          "validation_rmse_px": 109.1637704627665,
          "validation_p95_px": 161.69833609457456,
          "rotation_error_deg": 161.03845843493892,
          "center_error_m": 14.062372491124366,
          "inliers": 120,
          "fit_ms": 0.048341
        }
      ]
    },
    {
      "trial": 1,
      "mount_seed": 20261008,
      "sigma_px": 1.0,
      "outlier_fraction": 0.1,
      "method": "sqpnp",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": -3.7436353995614047,
          "pitch_deg": 3.764938643595558,
          "along_body_m": 0.14741958466872532,
          "center_vehicle_m": [
            2.36,
            0.14741958466872532,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": -1.0338720775203525,
          "pitch_deg": 2.0961203702794737,
          "along_body_m": -0.11724611718297473,
          "center_vehicle_m": [
            0.23275388281702525,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": -1.6328273219140668,
          "pitch_deg": -2.561924859921124,
          "along_body_m": 0.13115914301930795,
          "center_vehicle_m": [
            -2.36,
            0.13115914301930795,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": 2.8875833512616724,
          "pitch_deg": 3.6013340330058012,
          "along_body_m": 0.0068570772710759564,
          "center_vehicle_m": [
            0.35685707727107596,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.70014261977302,
          "validation_p95_px": 10.740561821590058,
          "rotation_error_deg": 5.308910002935901,
          "center_error_m": 0.14741958466872532
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 6.128252357274026,
          "validation_p95_px": 7.655726834153855,
          "rotation_error_deg": 2.3371976507558307,
          "center_error_m": 0.1172461171829747
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.226666130785293,
          "validation_p95_px": 9.185553367601289,
          "rotation_error_deg": 3.0379499373488215,
          "center_error_m": 0.13115914301930798
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.418421334400252,
          "validation_p95_px": 10.283011717150426,
          "rotation_error_deg": 4.6157338484881985,
          "center_error_m": 0.00685707727107604
        }
      ],
      "success": true,
      "observations_sha256": "8eaae3596ba84851b6fb86c2acb12a4deb239b826dfd034789d6dd283bd97580",
      "cameras": [
        {
          "camera_id": 0,
          "invalid_validation_count": 0,
          "validation_rmse_px": 5.515997084450156,
          "validation_p95_px": 12.526008389578724,
          "rotation_error_deg": 2.3929857520900666,
          "center_error_m": 0.38646360597998847,
          "inliers": 120,
          "fit_ms": 0.189358
        },
        {
          "camera_id": 1,
          "invalid_validation_count": 1,
          "validation_rmse_px": 4.872302992852137,
          "validation_p95_px": 6.143754162439534,
          "rotation_error_deg": 1.0286071521471714,
          "center_error_m": 0.25055777199968915,
          "inliers": 120,
          "fit_ms": 0.035156
        },
        {
          "camera_id": 2,
          "invalid_validation_count": 0,
          "validation_rmse_px": 4.42198693846193,
          "validation_p95_px": 9.739528067548624,
          "rotation_error_deg": 1.7318576121173983,
          "center_error_m": 0.4440996574125436,
          "inliers": 120,
          "fit_ms": 0.027583
        },
        {
          "camera_id": 3,
          "invalid_validation_count": 0,
          "validation_rmse_px": 5.030895435431806,
          "validation_p95_px": 8.615219295129757,
          "rotation_error_deg": 1.4891072144710702,
          "center_error_m": 0.368931889583886,
          "inliers": 120,
          "fit_ms": 0.027963
        }
      ]
    },
    {
      "trial": 1,
      "mount_seed": 20261008,
      "sigma_px": 1.0,
      "outlier_fraction": 0.1,
      "method": "ransac_epnp_lm",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": -3.7436353995614047,
          "pitch_deg": 3.764938643595558,
          "along_body_m": 0.14741958466872532,
          "center_vehicle_m": [
            2.36,
            0.14741958466872532,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": -1.0338720775203525,
          "pitch_deg": 2.0961203702794737,
          "along_body_m": -0.11724611718297473,
          "center_vehicle_m": [
            0.23275388281702525,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": -1.6328273219140668,
          "pitch_deg": -2.561924859921124,
          "along_body_m": 0.13115914301930795,
          "center_vehicle_m": [
            -2.36,
            0.13115914301930795,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": 2.8875833512616724,
          "pitch_deg": 3.6013340330058012,
          "along_body_m": 0.0068570772710759564,
          "center_vehicle_m": [
            0.35685707727107596,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.70014261977302,
          "validation_p95_px": 10.740561821590058,
          "rotation_error_deg": 5.308910002935901,
          "center_error_m": 0.14741958466872532
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 6.128252357274026,
          "validation_p95_px": 7.655726834153855,
          "rotation_error_deg": 2.3371976507558307,
          "center_error_m": 0.1172461171829747
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.226666130785293,
          "validation_p95_px": 9.185553367601289,
          "rotation_error_deg": 3.0379499373488215,
          "center_error_m": 0.13115914301930798
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.418421334400252,
          "validation_p95_px": 10.283011717150426,
          "rotation_error_deg": 4.6157338484881985,
          "center_error_m": 0.00685707727107604
        }
      ],
      "success": true,
      "observations_sha256": "8eaae3596ba84851b6fb86c2acb12a4deb239b826dfd034789d6dd283bd97580",
      "cameras": [
        {
          "camera_id": 0,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.4118345819938828,
          "validation_p95_px": 0.5669100470294803,
          "rotation_error_deg": 0.22880730370396307,
          "center_error_m": 0.021594393521094147,
          "inliers": 74,
          "fit_ms": 2.464034
        },
        {
          "camera_id": 1,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.6035751927598403,
          "validation_p95_px": 0.8629880829149941,
          "rotation_error_deg": 0.4114340600739004,
          "center_error_m": 0.03864345591368853,
          "inliers": 77,
          "fit_ms": 1.892431
        },
        {
          "camera_id": 2,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.5824676143067994,
          "validation_p95_px": 0.9965903730185391,
          "rotation_error_deg": 0.4169682541264396,
          "center_error_m": 0.04807480995833243,
          "inliers": 69,
          "fit_ms": 2.760204
        },
        {
          "camera_id": 3,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.665963271990602,
          "validation_p95_px": 1.2376145419285782,
          "rotation_error_deg": 0.2989640095711502,
          "center_error_m": 0.05935343973465672,
          "inliers": 75,
          "fit_ms": 2.089325
        }
      ]
    },
    {
      "trial": 2,
      "mount_seed": 20261009,
      "sigma_px": 0.0,
      "outlier_fraction": 0.0,
      "method": "iterative",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": -0.27097312815937435,
          "pitch_deg": -0.6861179651764964,
          "along_body_m": 0.03520825355631871,
          "center_vehicle_m": [
            2.36,
            0.03520825355631871,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": -2.90774132234998,
          "pitch_deg": 3.617461165784224,
          "along_body_m": -0.028069382676466248,
          "center_vehicle_m": [
            0.32193061732353373,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": 4.331163799612122,
          "pitch_deg": -1.1695778186096861,
          "along_body_m": -0.10208952178466521,
          "center_vehicle_m": [
            -2.36,
            -0.10208952178466521,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": 4.577384257622516,
          "pitch_deg": 1.57990476272649,
          "along_body_m": 0.12932066523328864,
          "center_vehicle_m": [
            0.47932066523328865,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 1.3022082123297247,
          "validation_p95_px": 1.5631876572319179,
          "rotation_error_deg": 0.7376878885185812,
          "center_error_m": 0.03520825355631871
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.614596998808949,
          "validation_p95_px": 10.653201386959722,
          "rotation_error_deg": 4.640924041374824,
          "center_error_m": 0.028069382676466303
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 11.178768361292331,
          "validation_p95_px": 12.4365676779789,
          "rotation_error_deg": 4.486227889495468,
          "center_error_m": 0.10208952178466522
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.38499372687485,
          "validation_p95_px": 10.112102804123595,
          "rotation_error_deg": 4.842232742710265,
          "center_error_m": 0.12932066523328856
        }
      ],
      "success": true,
      "observations_sha256": "a072a11039496555bad4295c299d61245c3384757d24fa808aef5e6b4b40131a",
      "cameras": [
        {
          "camera_id": 0,
          "invalid_validation_count": 0,
          "validation_rmse_px": 2.932131240268841e-06,
          "validation_p95_px": 5.0712011463735665e-06,
          "rotation_error_deg": 0.0,
          "center_error_m": 7.871104113493875e-08,
          "inliers": 120,
          "fit_ms": 0.321318
        },
        {
          "camera_id": 1,
          "invalid_validation_count": 0,
          "validation_rmse_px": 3.136329017330094e-07,
          "validation_p95_px": 4.974446668972673e-07,
          "rotation_error_deg": 1.2074182697257333e-06,
          "center_error_m": 1.2816633600399497e-08,
          "inliers": 120,
          "fit_ms": 0.136217
        },
        {
          "camera_id": 2,
          "invalid_validation_count": 0,
          "validation_rmse_px": 4.1495536017183515e-06,
          "validation_p95_px": 6.558051529138067e-06,
          "rotation_error_deg": 1.7075472925031877e-06,
          "center_error_m": 1.6956120337882952e-07,
          "inliers": 120,
          "fit_ms": 0.124956
        },
        {
          "camera_id": 3,
          "invalid_validation_count": 0,
          "validation_rmse_px": 6.997398857277676e-07,
          "validation_p95_px": 1.2300599667970003e-06,
          "rotation_error_deg": 0.0,
          "center_error_m": 3.924491370274765e-08,
          "inliers": 120,
          "fit_ms": 0.12133
        }
      ]
    },
    {
      "trial": 2,
      "mount_seed": 20261009,
      "sigma_px": 0.0,
      "outlier_fraction": 0.0,
      "method": "epnp",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": -0.27097312815937435,
          "pitch_deg": -0.6861179651764964,
          "along_body_m": 0.03520825355631871,
          "center_vehicle_m": [
            2.36,
            0.03520825355631871,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": -2.90774132234998,
          "pitch_deg": 3.617461165784224,
          "along_body_m": -0.028069382676466248,
          "center_vehicle_m": [
            0.32193061732353373,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": 4.331163799612122,
          "pitch_deg": -1.1695778186096861,
          "along_body_m": -0.10208952178466521,
          "center_vehicle_m": [
            -2.36,
            -0.10208952178466521,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": 4.577384257622516,
          "pitch_deg": 1.57990476272649,
          "along_body_m": 0.12932066523328864,
          "center_vehicle_m": [
            0.47932066523328865,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 1.3022082123297247,
          "validation_p95_px": 1.5631876572319179,
          "rotation_error_deg": 0.7376878885185812,
          "center_error_m": 0.03520825355631871
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.614596998808949,
          "validation_p95_px": 10.653201386959722,
          "rotation_error_deg": 4.640924041374824,
          "center_error_m": 0.028069382676466303
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 11.178768361292331,
          "validation_p95_px": 12.4365676779789,
          "rotation_error_deg": 4.486227889495468,
          "center_error_m": 0.10208952178466522
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.38499372687485,
          "validation_p95_px": 10.112102804123595,
          "rotation_error_deg": 4.842232742710265,
          "center_error_m": 0.12932066523328856
        }
      ],
      "success": true,
      "observations_sha256": "a072a11039496555bad4295c299d61245c3384757d24fa808aef5e6b4b40131a",
      "cameras": [
        {
          "camera_id": 0,
          "invalid_validation_count": 0,
          "validation_rmse_px": 2.3508837318175436e-13,
          "validation_p95_px": 4.557672451601656e-13,
          "rotation_error_deg": 0.0,
          "center_error_m": 1.9743566378366674e-14,
          "inliers": 120,
          "fit_ms": 0.24334
        },
        {
          "camera_id": 1,
          "invalid_validation_count": 0,
          "validation_rmse_px": 1.1246105657256343e-12,
          "validation_p95_px": 2.0922877452387675e-12,
          "rotation_error_deg": 0.0,
          "center_error_m": 1.0826837785464735e-13,
          "inliers": 120,
          "fit_ms": 0.05842
        },
        {
          "camera_id": 2,
          "invalid_validation_count": 0,
          "validation_rmse_px": 3.891435267956262e-13,
          "validation_p95_px": 6.251908901388753e-13,
          "rotation_error_deg": 0.0,
          "center_error_m": 1.3965833690293398e-14,
          "inliers": 120,
          "fit_ms": 0.048963
        },
        {
          "camera_id": 3,
          "invalid_validation_count": 0,
          "validation_rmse_px": 5.666595023212388e-13,
          "validation_p95_px": 8.269284385216181e-13,
          "rotation_error_deg": 0.0,
          "center_error_m": 4.063234262277312e-14,
          "inliers": 120,
          "fit_ms": 0.048912
        }
      ]
    },
    {
      "trial": 2,
      "mount_seed": 20261009,
      "sigma_px": 0.0,
      "outlier_fraction": 0.0,
      "method": "sqpnp",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": -0.27097312815937435,
          "pitch_deg": -0.6861179651764964,
          "along_body_m": 0.03520825355631871,
          "center_vehicle_m": [
            2.36,
            0.03520825355631871,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": -2.90774132234998,
          "pitch_deg": 3.617461165784224,
          "along_body_m": -0.028069382676466248,
          "center_vehicle_m": [
            0.32193061732353373,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": 4.331163799612122,
          "pitch_deg": -1.1695778186096861,
          "along_body_m": -0.10208952178466521,
          "center_vehicle_m": [
            -2.36,
            -0.10208952178466521,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": 4.577384257622516,
          "pitch_deg": 1.57990476272649,
          "along_body_m": 0.12932066523328864,
          "center_vehicle_m": [
            0.47932066523328865,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 1.3022082123297247,
          "validation_p95_px": 1.5631876572319179,
          "rotation_error_deg": 0.7376878885185812,
          "center_error_m": 0.03520825355631871
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.614596998808949,
          "validation_p95_px": 10.653201386959722,
          "rotation_error_deg": 4.640924041374824,
          "center_error_m": 0.028069382676466303
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 11.178768361292331,
          "validation_p95_px": 12.4365676779789,
          "rotation_error_deg": 4.486227889495468,
          "center_error_m": 0.10208952178466522
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.38499372687485,
          "validation_p95_px": 10.112102804123595,
          "rotation_error_deg": 4.842232742710265,
          "center_error_m": 0.12932066523328856
        }
      ],
      "success": true,
      "observations_sha256": "a072a11039496555bad4295c299d61245c3384757d24fa808aef5e6b4b40131a",
      "cameras": [
        {
          "camera_id": 0,
          "invalid_validation_count": 0,
          "validation_rmse_px": 6.351738072356886e-13,
          "validation_p95_px": 1.0585676757144745e-12,
          "rotation_error_deg": 0.0,
          "center_error_m": 5.0692418881971354e-14,
          "inliers": 120,
          "fit_ms": 0.298846
        },
        {
          "camera_id": 1,
          "invalid_validation_count": 0,
          "validation_rmse_px": 3.736945262903905e-12,
          "validation_p95_px": 4.719775184128278e-12,
          "rotation_error_deg": 0.0,
          "center_error_m": 8.076689372500816e-14,
          "inliers": 120,
          "fit_ms": 0.032852
        },
        {
          "camera_id": 2,
          "invalid_validation_count": 0,
          "validation_rmse_px": 1.5681014411448308e-12,
          "validation_p95_px": 2.0553080790023935e-12,
          "rotation_error_deg": 0.0,
          "center_error_m": 1.7477645413852665e-14,
          "inliers": 120,
          "fit_ms": 0.024707
        },
        {
          "camera_id": 3,
          "invalid_validation_count": 0,
          "validation_rmse_px": 1.6657077826842784e-12,
          "validation_p95_px": 2.2543354938120643e-12,
          "rotation_error_deg": 0.0,
          "center_error_m": 4.5664729510753e-14,
          "inliers": 120,
          "fit_ms": 0.029025
        }
      ]
    },
    {
      "trial": 2,
      "mount_seed": 20261009,
      "sigma_px": 0.0,
      "outlier_fraction": 0.0,
      "method": "ransac_epnp_lm",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": -0.27097312815937435,
          "pitch_deg": -0.6861179651764964,
          "along_body_m": 0.03520825355631871,
          "center_vehicle_m": [
            2.36,
            0.03520825355631871,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": -2.90774132234998,
          "pitch_deg": 3.617461165784224,
          "along_body_m": -0.028069382676466248,
          "center_vehicle_m": [
            0.32193061732353373,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": 4.331163799612122,
          "pitch_deg": -1.1695778186096861,
          "along_body_m": -0.10208952178466521,
          "center_vehicle_m": [
            -2.36,
            -0.10208952178466521,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": 4.577384257622516,
          "pitch_deg": 1.57990476272649,
          "along_body_m": 0.12932066523328864,
          "center_vehicle_m": [
            0.47932066523328865,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 1.3022082123297247,
          "validation_p95_px": 1.5631876572319179,
          "rotation_error_deg": 0.7376878885185812,
          "center_error_m": 0.03520825355631871
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.614596998808949,
          "validation_p95_px": 10.653201386959722,
          "rotation_error_deg": 4.640924041374824,
          "center_error_m": 0.028069382676466303
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 11.178768361292331,
          "validation_p95_px": 12.4365676779789,
          "rotation_error_deg": 4.486227889495468,
          "center_error_m": 0.10208952178466522
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.38499372687485,
          "validation_p95_px": 10.112102804123595,
          "rotation_error_deg": 4.842232742710265,
          "center_error_m": 0.12932066523328856
        }
      ],
      "success": true,
      "observations_sha256": "a072a11039496555bad4295c299d61245c3384757d24fa808aef5e6b4b40131a",
      "cameras": [
        {
          "camera_id": 0,
          "invalid_validation_count": 0,
          "validation_rmse_px": 1.9689313000787196e-10,
          "validation_p95_px": 4.1001123501127006e-10,
          "rotation_error_deg": 0.0,
          "center_error_m": 1.7058489968214177e-11,
          "inliers": 120,
          "fit_ms": 0.388065
        },
        {
          "camera_id": 1,
          "invalid_validation_count": 0,
          "validation_rmse_px": 2.1182725452971404e-06,
          "validation_p95_px": 2.8499919949809546e-06,
          "rotation_error_deg": 1.2074182697257333e-06,
          "center_error_m": 8.557701598547105e-08,
          "inliers": 120,
          "fit_ms": 0.196331
        },
        {
          "camera_id": 2,
          "invalid_validation_count": 0,
          "validation_rmse_px": 2.897427189555581e-09,
          "validation_p95_px": 4.172067621220605e-09,
          "rotation_error_deg": 0.0,
          "center_error_m": 2.3187601999870033e-10,
          "inliers": 120,
          "fit_ms": 0.118444
        },
        {
          "camera_id": 3,
          "invalid_validation_count": 0,
          "validation_rmse_px": 1.0149357867665288e-06,
          "validation_p95_px": 1.1857448777056794e-06,
          "rotation_error_deg": 1.2074182697257333e-06,
          "center_error_m": 3.591837726781434e-08,
          "inliers": 120,
          "fit_ms": 0.123313
        }
      ]
    },
    {
      "trial": 2,
      "mount_seed": 20261009,
      "sigma_px": 0.0,
      "outlier_fraction": 0.1,
      "method": "iterative",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": -0.27097312815937435,
          "pitch_deg": -0.6861179651764964,
          "along_body_m": 0.03520825355631871,
          "center_vehicle_m": [
            2.36,
            0.03520825355631871,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": -2.90774132234998,
          "pitch_deg": 3.617461165784224,
          "along_body_m": -0.028069382676466248,
          "center_vehicle_m": [
            0.32193061732353373,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": 4.331163799612122,
          "pitch_deg": -1.1695778186096861,
          "along_body_m": -0.10208952178466521,
          "center_vehicle_m": [
            -2.36,
            -0.10208952178466521,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": 4.577384257622516,
          "pitch_deg": 1.57990476272649,
          "along_body_m": 0.12932066523328864,
          "center_vehicle_m": [
            0.47932066523328865,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 1.3022082123297247,
          "validation_p95_px": 1.5631876572319179,
          "rotation_error_deg": 0.7376878885185812,
          "center_error_m": 0.03520825355631871
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.614596998808949,
          "validation_p95_px": 10.653201386959722,
          "rotation_error_deg": 4.640924041374824,
          "center_error_m": 0.028069382676466303
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 11.178768361292331,
          "validation_p95_px": 12.4365676779789,
          "rotation_error_deg": 4.486227889495468,
          "center_error_m": 0.10208952178466522
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.38499372687485,
          "validation_p95_px": 10.112102804123595,
          "rotation_error_deg": 4.842232742710265,
          "center_error_m": 0.12932066523328856
        }
      ],
      "success": true,
      "observations_sha256": "a8ed6e3ebc53107dfd7b001ff8f3e21fc8d1f7701fac575cf0c9be901b1c1f8e",
      "cameras": [
        {
          "camera_id": 0,
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.097206038278463,
          "validation_p95_px": 9.657965832747292,
          "rotation_error_deg": 4.512931798455427,
          "center_error_m": 0.5757599692720808,
          "inliers": 120,
          "fit_ms": 0.41253
        },
        {
          "camera_id": 1,
          "invalid_validation_count": 0,
          "validation_rmse_px": 2.703343299681416,
          "validation_p95_px": 4.319552430152822,
          "rotation_error_deg": 1.670204388300527,
          "center_error_m": 0.2247870043449962,
          "inliers": 120,
          "fit_ms": 0.167417
        },
        {
          "camera_id": 2,
          "invalid_validation_count": 0,
          "validation_rmse_px": 1.8883133468508595,
          "validation_p95_px": 3.4868283906084807,
          "rotation_error_deg": 1.0418583104728119,
          "center_error_m": 0.14659306762647775,
          "inliers": 120,
          "fit_ms": 0.131389
        },
        {
          "camera_id": 3,
          "invalid_validation_count": 0,
          "validation_rmse_px": 2.061557733631549,
          "validation_p95_px": 2.374680518598201,
          "rotation_error_deg": 1.2042108043905646,
          "center_error_m": 0.09682239519745037,
          "inliers": 120,
          "fit_ms": 0.156015
        }
      ]
    },
    {
      "trial": 2,
      "mount_seed": 20261009,
      "sigma_px": 0.0,
      "outlier_fraction": 0.1,
      "method": "epnp",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": -0.27097312815937435,
          "pitch_deg": -0.6861179651764964,
          "along_body_m": 0.03520825355631871,
          "center_vehicle_m": [
            2.36,
            0.03520825355631871,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": -2.90774132234998,
          "pitch_deg": 3.617461165784224,
          "along_body_m": -0.028069382676466248,
          "center_vehicle_m": [
            0.32193061732353373,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": 4.331163799612122,
          "pitch_deg": -1.1695778186096861,
          "along_body_m": -0.10208952178466521,
          "center_vehicle_m": [
            -2.36,
            -0.10208952178466521,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": 4.577384257622516,
          "pitch_deg": 1.57990476272649,
          "along_body_m": 0.12932066523328864,
          "center_vehicle_m": [
            0.47932066523328865,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 1.3022082123297247,
          "validation_p95_px": 1.5631876572319179,
          "rotation_error_deg": 0.7376878885185812,
          "center_error_m": 0.03520825355631871
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.614596998808949,
          "validation_p95_px": 10.653201386959722,
          "rotation_error_deg": 4.640924041374824,
          "center_error_m": 0.028069382676466303
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 11.178768361292331,
          "validation_p95_px": 12.4365676779789,
          "rotation_error_deg": 4.486227889495468,
          "center_error_m": 0.10208952178466522
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.38499372687485,
          "validation_p95_px": 10.112102804123595,
          "rotation_error_deg": 4.842232742710265,
          "center_error_m": 0.12932066523328856
        }
      ],
      "success": false,
      "observations_sha256": "a8ed6e3ebc53107dfd7b001ff8f3e21fc8d1f7701fac575cf0c9be901b1c1f8e",
      "error": "estimated pose projects training point outside validity"
    },
    {
      "trial": 2,
      "mount_seed": 20261009,
      "sigma_px": 0.0,
      "outlier_fraction": 0.1,
      "method": "sqpnp",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": -0.27097312815937435,
          "pitch_deg": -0.6861179651764964,
          "along_body_m": 0.03520825355631871,
          "center_vehicle_m": [
            2.36,
            0.03520825355631871,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": -2.90774132234998,
          "pitch_deg": 3.617461165784224,
          "along_body_m": -0.028069382676466248,
          "center_vehicle_m": [
            0.32193061732353373,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": 4.331163799612122,
          "pitch_deg": -1.1695778186096861,
          "along_body_m": -0.10208952178466521,
          "center_vehicle_m": [
            -2.36,
            -0.10208952178466521,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": 4.577384257622516,
          "pitch_deg": 1.57990476272649,
          "along_body_m": 0.12932066523328864,
          "center_vehicle_m": [
            0.47932066523328865,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 1.3022082123297247,
          "validation_p95_px": 1.5631876572319179,
          "rotation_error_deg": 0.7376878885185812,
          "center_error_m": 0.03520825355631871
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.614596998808949,
          "validation_p95_px": 10.653201386959722,
          "rotation_error_deg": 4.640924041374824,
          "center_error_m": 0.028069382676466303
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 11.178768361292331,
          "validation_p95_px": 12.4365676779789,
          "rotation_error_deg": 4.486227889495468,
          "center_error_m": 0.10208952178466522
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.38499372687485,
          "validation_p95_px": 10.112102804123595,
          "rotation_error_deg": 4.842232742710265,
          "center_error_m": 0.12932066523328856
        }
      ],
      "success": false,
      "observations_sha256": "a8ed6e3ebc53107dfd7b001ff8f3e21fc8d1f7701fac575cf0c9be901b1c1f8e",
      "error": "estimated pose projects training point outside validity"
    },
    {
      "trial": 2,
      "mount_seed": 20261009,
      "sigma_px": 0.0,
      "outlier_fraction": 0.1,
      "method": "ransac_epnp_lm",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": -0.27097312815937435,
          "pitch_deg": -0.6861179651764964,
          "along_body_m": 0.03520825355631871,
          "center_vehicle_m": [
            2.36,
            0.03520825355631871,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": -2.90774132234998,
          "pitch_deg": 3.617461165784224,
          "along_body_m": -0.028069382676466248,
          "center_vehicle_m": [
            0.32193061732353373,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": 4.331163799612122,
          "pitch_deg": -1.1695778186096861,
          "along_body_m": -0.10208952178466521,
          "center_vehicle_m": [
            -2.36,
            -0.10208952178466521,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": 4.577384257622516,
          "pitch_deg": 1.57990476272649,
          "along_body_m": 0.12932066523328864,
          "center_vehicle_m": [
            0.47932066523328865,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 1.3022082123297247,
          "validation_p95_px": 1.5631876572319179,
          "rotation_error_deg": 0.7376878885185812,
          "center_error_m": 0.03520825355631871
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.614596998808949,
          "validation_p95_px": 10.653201386959722,
          "rotation_error_deg": 4.640924041374824,
          "center_error_m": 0.028069382676466303
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 11.178768361292331,
          "validation_p95_px": 12.4365676779789,
          "rotation_error_deg": 4.486227889495468,
          "center_error_m": 0.10208952178466522
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.38499372687485,
          "validation_p95_px": 10.112102804123595,
          "rotation_error_deg": 4.842232742710265,
          "center_error_m": 0.12932066523328856
        }
      ],
      "success": true,
      "observations_sha256": "a8ed6e3ebc53107dfd7b001ff8f3e21fc8d1f7701fac575cf0c9be901b1c1f8e",
      "cameras": [
        {
          "camera_id": 0,
          "invalid_validation_count": 0,
          "validation_rmse_px": 3.8839272811856705e-09,
          "validation_p95_px": 6.8461615409655325e-09,
          "rotation_error_deg": 0.0,
          "center_error_m": 3.270261211785898e-10,
          "inliers": 108,
          "fit_ms": 0.590427
        },
        {
          "camera_id": 1,
          "invalid_validation_count": 0,
          "validation_rmse_px": 1.932834472150398e-06,
          "validation_p95_px": 2.3760040310725404e-06,
          "rotation_error_deg": 1.7075472925031877e-06,
          "center_error_m": 1.854450891232559e-08,
          "inliers": 108,
          "fit_ms": 0.385459
        },
        {
          "camera_id": 2,
          "invalid_validation_count": 0,
          "validation_rmse_px": 5.78778065163131e-07,
          "validation_p95_px": 9.469908282630033e-07,
          "rotation_error_deg": 0.0,
          "center_error_m": 2.464290638597981e-08,
          "inliers": 108,
          "fit_ms": 0.289929
        },
        {
          "camera_id": 3,
          "invalid_validation_count": 0,
          "validation_rmse_px": 8.479515930505713e-07,
          "validation_p95_px": 1.0619115660453355e-06,
          "rotation_error_deg": 0.0,
          "center_error_m": 2.170812420127728e-08,
          "inliers": 108,
          "fit_ms": 0.289368
        }
      ]
    },
    {
      "trial": 2,
      "mount_seed": 20261009,
      "sigma_px": 0.5,
      "outlier_fraction": 0.0,
      "method": "iterative",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": -0.27097312815937435,
          "pitch_deg": -0.6861179651764964,
          "along_body_m": 0.03520825355631871,
          "center_vehicle_m": [
            2.36,
            0.03520825355631871,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": -2.90774132234998,
          "pitch_deg": 3.617461165784224,
          "along_body_m": -0.028069382676466248,
          "center_vehicle_m": [
            0.32193061732353373,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": 4.331163799612122,
          "pitch_deg": -1.1695778186096861,
          "along_body_m": -0.10208952178466521,
          "center_vehicle_m": [
            -2.36,
            -0.10208952178466521,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": 4.577384257622516,
          "pitch_deg": 1.57990476272649,
          "along_body_m": 0.12932066523328864,
          "center_vehicle_m": [
            0.47932066523328865,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 1.3022082123297247,
          "validation_p95_px": 1.5631876572319179,
          "rotation_error_deg": 0.7376878885185812,
          "center_error_m": 0.03520825355631871
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.614596998808949,
          "validation_p95_px": 10.653201386959722,
          "rotation_error_deg": 4.640924041374824,
          "center_error_m": 0.028069382676466303
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 11.178768361292331,
          "validation_p95_px": 12.4365676779789,
          "rotation_error_deg": 4.486227889495468,
          "center_error_m": 0.10208952178466522
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.38499372687485,
          "validation_p95_px": 10.112102804123595,
          "rotation_error_deg": 4.842232742710265,
          "center_error_m": 0.12932066523328856
        }
      ],
      "success": true,
      "observations_sha256": "d4c1e8216a107eb8c7753b3954b35c530677df2c78cd910774c72536b58c4ceb",
      "cameras": [
        {
          "camera_id": 0,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.18522444196906426,
          "validation_p95_px": 0.27899643092913395,
          "rotation_error_deg": 0.09353037708553905,
          "center_error_m": 0.008715775675521373,
          "inliers": 120,
          "fit_ms": 0.325205
        },
        {
          "camera_id": 1,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.18868477688138396,
          "validation_p95_px": 0.2789044783889618,
          "rotation_error_deg": 0.10337461716112262,
          "center_error_m": 0.003984582390400114,
          "inliers": 120,
          "fit_ms": 0.149634
        },
        {
          "camera_id": 2,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.1968682975381295,
          "validation_p95_px": 0.23249633678460338,
          "rotation_error_deg": 0.10381777610802945,
          "center_error_m": 0.01747828258765214,
          "inliers": 120,
          "fit_ms": 0.185602
        },
        {
          "camera_id": 3,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.16329315148150236,
          "validation_p95_px": 0.1807663315251467,
          "rotation_error_deg": 0.07723816898900181,
          "center_error_m": 0.011756876962450324,
          "inliers": 120,
          "fit_ms": 0.125698
        }
      ]
    },
    {
      "trial": 2,
      "mount_seed": 20261009,
      "sigma_px": 0.5,
      "outlier_fraction": 0.0,
      "method": "epnp",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": -0.27097312815937435,
          "pitch_deg": -0.6861179651764964,
          "along_body_m": 0.03520825355631871,
          "center_vehicle_m": [
            2.36,
            0.03520825355631871,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": -2.90774132234998,
          "pitch_deg": 3.617461165784224,
          "along_body_m": -0.028069382676466248,
          "center_vehicle_m": [
            0.32193061732353373,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": 4.331163799612122,
          "pitch_deg": -1.1695778186096861,
          "along_body_m": -0.10208952178466521,
          "center_vehicle_m": [
            -2.36,
            -0.10208952178466521,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": 4.577384257622516,
          "pitch_deg": 1.57990476272649,
          "along_body_m": 0.12932066523328864,
          "center_vehicle_m": [
            0.47932066523328865,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 1.3022082123297247,
          "validation_p95_px": 1.5631876572319179,
          "rotation_error_deg": 0.7376878885185812,
          "center_error_m": 0.03520825355631871
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.614596998808949,
          "validation_p95_px": 10.653201386959722,
          "rotation_error_deg": 4.640924041374824,
          "center_error_m": 0.028069382676466303
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 11.178768361292331,
          "validation_p95_px": 12.4365676779789,
          "rotation_error_deg": 4.486227889495468,
          "center_error_m": 0.10208952178466522
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.38499372687485,
          "validation_p95_px": 10.112102804123595,
          "rotation_error_deg": 4.842232742710265,
          "center_error_m": 0.12932066523328856
        }
      ],
      "success": true,
      "observations_sha256": "d4c1e8216a107eb8c7753b3954b35c530677df2c78cd910774c72536b58c4ceb",
      "cameras": [
        {
          "camera_id": 0,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.5403536462103185,
          "validation_p95_px": 1.17600254252318,
          "rotation_error_deg": 0.19388142764764632,
          "center_error_m": 0.0398583262864063,
          "inliers": 120,
          "fit_ms": 0.31168
        },
        {
          "camera_id": 1,
          "invalid_validation_count": 0,
          "validation_rmse_px": 1.2842159854523825,
          "validation_p95_px": 2.1553320005324172,
          "rotation_error_deg": 0.36056787870962187,
          "center_error_m": 0.125150583261125,
          "inliers": 120,
          "fit_ms": 0.061857
        },
        {
          "camera_id": 2,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.2652533579203362,
          "validation_p95_px": 0.3823285227645653,
          "rotation_error_deg": 0.09946414620105215,
          "center_error_m": 0.006703155366951614,
          "inliers": 120,
          "fit_ms": 0.054473
        },
        {
          "camera_id": 3,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.6223664722063319,
          "validation_p95_px": 0.9887604249048251,
          "rotation_error_deg": 0.05663642906473639,
          "center_error_m": 0.03531799344507207,
          "inliers": 120,
          "fit_ms": 0.049383
        }
      ]
    },
    {
      "trial": 2,
      "mount_seed": 20261009,
      "sigma_px": 0.5,
      "outlier_fraction": 0.0,
      "method": "sqpnp",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": -0.27097312815937435,
          "pitch_deg": -0.6861179651764964,
          "along_body_m": 0.03520825355631871,
          "center_vehicle_m": [
            2.36,
            0.03520825355631871,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": -2.90774132234998,
          "pitch_deg": 3.617461165784224,
          "along_body_m": -0.028069382676466248,
          "center_vehicle_m": [
            0.32193061732353373,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": 4.331163799612122,
          "pitch_deg": -1.1695778186096861,
          "along_body_m": -0.10208952178466521,
          "center_vehicle_m": [
            -2.36,
            -0.10208952178466521,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": 4.577384257622516,
          "pitch_deg": 1.57990476272649,
          "along_body_m": 0.12932066523328864,
          "center_vehicle_m": [
            0.47932066523328865,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 1.3022082123297247,
          "validation_p95_px": 1.5631876572319179,
          "rotation_error_deg": 0.7376878885185812,
          "center_error_m": 0.03520825355631871
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.614596998808949,
          "validation_p95_px": 10.653201386959722,
          "rotation_error_deg": 4.640924041374824,
          "center_error_m": 0.028069382676466303
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 11.178768361292331,
          "validation_p95_px": 12.4365676779789,
          "rotation_error_deg": 4.486227889495468,
          "center_error_m": 0.10208952178466522
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.38499372687485,
          "validation_p95_px": 10.112102804123595,
          "rotation_error_deg": 4.842232742710265,
          "center_error_m": 0.12932066523328856
        }
      ],
      "success": true,
      "observations_sha256": "d4c1e8216a107eb8c7753b3954b35c530677df2c78cd910774c72536b58c4ceb",
      "cameras": [
        {
          "camera_id": 0,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.1635712431703818,
          "validation_p95_px": 0.291181232608729,
          "rotation_error_deg": 0.07665522692144164,
          "center_error_m": 0.009629146465800202,
          "inliers": 120,
          "fit_ms": 0.192845
        },
        {
          "camera_id": 1,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.2115109210481116,
          "validation_p95_px": 0.2843805920412169,
          "rotation_error_deg": 0.1367833191666483,
          "center_error_m": 0.011803383807989128,
          "inliers": 120,
          "fit_ms": 0.034986
        },
        {
          "camera_id": 2,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.10232086636633911,
          "validation_p95_px": 0.13139908867006542,
          "rotation_error_deg": 0.05922308679490585,
          "center_error_m": 0.006489406648371576,
          "inliers": 120,
          "fit_ms": 0.025819
        },
        {
          "camera_id": 3,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.19778588077502346,
          "validation_p95_px": 0.28065192349214835,
          "rotation_error_deg": 0.08021534508406399,
          "center_error_m": 0.016046836080166414,
          "inliers": 120,
          "fit_ms": 0.02641
        }
      ]
    },
    {
      "trial": 2,
      "mount_seed": 20261009,
      "sigma_px": 0.5,
      "outlier_fraction": 0.0,
      "method": "ransac_epnp_lm",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": -0.27097312815937435,
          "pitch_deg": -0.6861179651764964,
          "along_body_m": 0.03520825355631871,
          "center_vehicle_m": [
            2.36,
            0.03520825355631871,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": -2.90774132234998,
          "pitch_deg": 3.617461165784224,
          "along_body_m": -0.028069382676466248,
          "center_vehicle_m": [
            0.32193061732353373,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": 4.331163799612122,
          "pitch_deg": -1.1695778186096861,
          "along_body_m": -0.10208952178466521,
          "center_vehicle_m": [
            -2.36,
            -0.10208952178466521,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": 4.577384257622516,
          "pitch_deg": 1.57990476272649,
          "along_body_m": 0.12932066523328864,
          "center_vehicle_m": [
            0.47932066523328865,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 1.3022082123297247,
          "validation_p95_px": 1.5631876572319179,
          "rotation_error_deg": 0.7376878885185812,
          "center_error_m": 0.03520825355631871
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.614596998808949,
          "validation_p95_px": 10.653201386959722,
          "rotation_error_deg": 4.640924041374824,
          "center_error_m": 0.028069382676466303
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 11.178768361292331,
          "validation_p95_px": 12.4365676779789,
          "rotation_error_deg": 4.486227889495468,
          "center_error_m": 0.10208952178466522
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.38499372687485,
          "validation_p95_px": 10.112102804123595,
          "rotation_error_deg": 4.842232742710265,
          "center_error_m": 0.12932066523328856
        }
      ],
      "success": true,
      "observations_sha256": "d4c1e8216a107eb8c7753b3954b35c530677df2c78cd910774c72536b58c4ceb",
      "cameras": [
        {
          "camera_id": 0,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.2660175448264207,
          "validation_p95_px": 0.42286162702850183,
          "rotation_error_deg": 0.10060751358603261,
          "center_error_m": 0.020765426863179053,
          "inliers": 109,
          "fit_ms": 0.628349
        },
        {
          "camera_id": 1,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.24717513247603215,
          "validation_p95_px": 0.3310819725862781,
          "rotation_error_deg": 0.14294815666214788,
          "center_error_m": 0.013769733802892807,
          "inliers": 108,
          "fit_ms": 0.354291
        },
        {
          "camera_id": 2,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.4618103749871656,
          "validation_p95_px": 0.7061143476685983,
          "rotation_error_deg": 0.2025509526708858,
          "center_error_m": 0.035206682960775754,
          "inliers": 106,
          "fit_ms": 0.426046
        },
        {
          "camera_id": 3,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.21409977837756142,
          "validation_p95_px": 0.2350559822321576,
          "rotation_error_deg": 0.11005030691737781,
          "center_error_m": 0.018393164778613868,
          "inliers": 114,
          "fit_ms": 0.286743
        }
      ]
    },
    {
      "trial": 2,
      "mount_seed": 20261009,
      "sigma_px": 0.5,
      "outlier_fraction": 0.1,
      "method": "iterative",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": -0.27097312815937435,
          "pitch_deg": -0.6861179651764964,
          "along_body_m": 0.03520825355631871,
          "center_vehicle_m": [
            2.36,
            0.03520825355631871,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": -2.90774132234998,
          "pitch_deg": 3.617461165784224,
          "along_body_m": -0.028069382676466248,
          "center_vehicle_m": [
            0.32193061732353373,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": 4.331163799612122,
          "pitch_deg": -1.1695778186096861,
          "along_body_m": -0.10208952178466521,
          "center_vehicle_m": [
            -2.36,
            -0.10208952178466521,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": 4.577384257622516,
          "pitch_deg": 1.57990476272649,
          "along_body_m": 0.12932066523328864,
          "center_vehicle_m": [
            0.47932066523328865,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 1.3022082123297247,
          "validation_p95_px": 1.5631876572319179,
          "rotation_error_deg": 0.7376878885185812,
          "center_error_m": 0.03520825355631871
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.614596998808949,
          "validation_p95_px": 10.653201386959722,
          "rotation_error_deg": 4.640924041374824,
          "center_error_m": 0.028069382676466303
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 11.178768361292331,
          "validation_p95_px": 12.4365676779789,
          "rotation_error_deg": 4.486227889495468,
          "center_error_m": 0.10208952178466522
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.38499372687485,
          "validation_p95_px": 10.112102804123595,
          "rotation_error_deg": 4.842232742710265,
          "center_error_m": 0.12932066523328856
        }
      ],
      "success": true,
      "observations_sha256": "05358e692bb08803f1f386c7ee0a3c549c93bec1ce93330754d9d6ddf30cf9d0",
      "cameras": [
        {
          "camera_id": 0,
          "invalid_validation_count": 0,
          "validation_rmse_px": 6.875750646237774,
          "validation_p95_px": 11.622636482742932,
          "rotation_error_deg": 3.3461011467988624,
          "center_error_m": 0.6188556546601528,
          "inliers": 120,
          "fit_ms": 0.488003
        },
        {
          "camera_id": 1,
          "invalid_validation_count": 0,
          "validation_rmse_px": 1.5281140023218904,
          "validation_p95_px": 2.1259626925929367,
          "rotation_error_deg": 1.0558042932011324,
          "center_error_m": 0.036286507524451235,
          "inliers": 120,
          "fit_ms": 0.141297
        },
        {
          "camera_id": 2,
          "invalid_validation_count": 0,
          "validation_rmse_px": 1.925032659960055,
          "validation_p95_px": 2.4348387178921915,
          "rotation_error_deg": 1.1886778672334306,
          "center_error_m": 0.06560803819395333,
          "inliers": 120,
          "fit_ms": 0.163109
        },
        {
          "camera_id": 3,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.5014739979173028,
          "validation_p95_px": 0.640225028209756,
          "rotation_error_deg": 0.24911959160828967,
          "center_error_m": 0.0427905985484249,
          "inliers": 120,
          "fit_ms": 0.127131
        }
      ]
    },
    {
      "trial": 2,
      "mount_seed": 20261009,
      "sigma_px": 0.5,
      "outlier_fraction": 0.1,
      "method": "epnp",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": -0.27097312815937435,
          "pitch_deg": -0.6861179651764964,
          "along_body_m": 0.03520825355631871,
          "center_vehicle_m": [
            2.36,
            0.03520825355631871,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": -2.90774132234998,
          "pitch_deg": 3.617461165784224,
          "along_body_m": -0.028069382676466248,
          "center_vehicle_m": [
            0.32193061732353373,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": 4.331163799612122,
          "pitch_deg": -1.1695778186096861,
          "along_body_m": -0.10208952178466521,
          "center_vehicle_m": [
            -2.36,
            -0.10208952178466521,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": 4.577384257622516,
          "pitch_deg": 1.57990476272649,
          "along_body_m": 0.12932066523328864,
          "center_vehicle_m": [
            0.47932066523328865,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 1.3022082123297247,
          "validation_p95_px": 1.5631876572319179,
          "rotation_error_deg": 0.7376878885185812,
          "center_error_m": 0.03520825355631871
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.614596998808949,
          "validation_p95_px": 10.653201386959722,
          "rotation_error_deg": 4.640924041374824,
          "center_error_m": 0.028069382676466303
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 11.178768361292331,
          "validation_p95_px": 12.4365676779789,
          "rotation_error_deg": 4.486227889495468,
          "center_error_m": 0.10208952178466522
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.38499372687485,
          "validation_p95_px": 10.112102804123595,
          "rotation_error_deg": 4.842232742710265,
          "center_error_m": 0.12932066523328856
        }
      ],
      "success": true,
      "observations_sha256": "05358e692bb08803f1f386c7ee0a3c549c93bec1ce93330754d9d6ddf30cf9d0",
      "cameras": [
        {
          "camera_id": 0,
          "invalid_validation_count": 0,
          "validation_rmse_px": 38.587463221213135,
          "validation_p95_px": 81.43989287334045,
          "rotation_error_deg": 8.370809280810452,
          "center_error_m": 2.2676094712158674,
          "inliers": 120,
          "fit_ms": 0.208544
        },
        {
          "camera_id": 1,
          "invalid_validation_count": 0,
          "validation_rmse_px": 32.771463444977265,
          "validation_p95_px": 54.880352802115,
          "rotation_error_deg": 8.76568371678495,
          "center_error_m": 2.59615883301584,
          "inliers": 120,
          "fit_ms": 0.056317
        },
        {
          "camera_id": 2,
          "invalid_validation_count": 0,
          "validation_rmse_px": 10.219510012258128,
          "validation_p95_px": 22.389680864808447,
          "rotation_error_deg": 1.482868826265106,
          "center_error_m": 0.6426854117911622,
          "inliers": 120,
          "fit_ms": 0.049203
        },
        {
          "camera_id": 3,
          "invalid_validation_count": 0,
          "validation_rmse_px": 6.820818224068442,
          "validation_p95_px": 8.557723548618107,
          "rotation_error_deg": 2.3299590753664106,
          "center_error_m": 0.2363551548555997,
          "inliers": 120,
          "fit_ms": 0.046859
        }
      ]
    },
    {
      "trial": 2,
      "mount_seed": 20261009,
      "sigma_px": 0.5,
      "outlier_fraction": 0.1,
      "method": "sqpnp",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": -0.27097312815937435,
          "pitch_deg": -0.6861179651764964,
          "along_body_m": 0.03520825355631871,
          "center_vehicle_m": [
            2.36,
            0.03520825355631871,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": -2.90774132234998,
          "pitch_deg": 3.617461165784224,
          "along_body_m": -0.028069382676466248,
          "center_vehicle_m": [
            0.32193061732353373,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": 4.331163799612122,
          "pitch_deg": -1.1695778186096861,
          "along_body_m": -0.10208952178466521,
          "center_vehicle_m": [
            -2.36,
            -0.10208952178466521,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": 4.577384257622516,
          "pitch_deg": 1.57990476272649,
          "along_body_m": 0.12932066523328864,
          "center_vehicle_m": [
            0.47932066523328865,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 1.3022082123297247,
          "validation_p95_px": 1.5631876572319179,
          "rotation_error_deg": 0.7376878885185812,
          "center_error_m": 0.03520825355631871
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.614596998808949,
          "validation_p95_px": 10.653201386959722,
          "rotation_error_deg": 4.640924041374824,
          "center_error_m": 0.028069382676466303
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 11.178768361292331,
          "validation_p95_px": 12.4365676779789,
          "rotation_error_deg": 4.486227889495468,
          "center_error_m": 0.10208952178466522
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.38499372687485,
          "validation_p95_px": 10.112102804123595,
          "rotation_error_deg": 4.842232742710265,
          "center_error_m": 0.12932066523328856
        }
      ],
      "success": true,
      "observations_sha256": "05358e692bb08803f1f386c7ee0a3c549c93bec1ce93330754d9d6ddf30cf9d0",
      "cameras": [
        {
          "camera_id": 0,
          "invalid_validation_count": 0,
          "validation_rmse_px": 19.399758193251454,
          "validation_p95_px": 37.00619589787353,
          "rotation_error_deg": 6.543057886575553,
          "center_error_m": 1.5971528531622234,
          "inliers": 120,
          "fit_ms": 0.239343
        },
        {
          "camera_id": 1,
          "invalid_validation_count": 0,
          "validation_rmse_px": 4.298858061105351,
          "validation_p95_px": 8.501206557605816,
          "rotation_error_deg": 3.1001471860584227,
          "center_error_m": 0.3588638402337355,
          "inliers": 120,
          "fit_ms": 0.038874
        },
        {
          "camera_id": 2,
          "invalid_validation_count": 0,
          "validation_rmse_px": 3.0305956551288724,
          "validation_p95_px": 5.406778153094374,
          "rotation_error_deg": 1.104973956131995,
          "center_error_m": 0.20384319055068442,
          "inliers": 120,
          "fit_ms": 0.028835
        },
        {
          "camera_id": 3,
          "invalid_validation_count": 0,
          "validation_rmse_px": 1.3333778355164525,
          "validation_p95_px": 2.216699800257809,
          "rotation_error_deg": 0.7411045305184535,
          "center_error_m": 0.11122365881179712,
          "inliers": 120,
          "fit_ms": 0.027452
        }
      ]
    },
    {
      "trial": 2,
      "mount_seed": 20261009,
      "sigma_px": 0.5,
      "outlier_fraction": 0.1,
      "method": "ransac_epnp_lm",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": -0.27097312815937435,
          "pitch_deg": -0.6861179651764964,
          "along_body_m": 0.03520825355631871,
          "center_vehicle_m": [
            2.36,
            0.03520825355631871,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": -2.90774132234998,
          "pitch_deg": 3.617461165784224,
          "along_body_m": -0.028069382676466248,
          "center_vehicle_m": [
            0.32193061732353373,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": 4.331163799612122,
          "pitch_deg": -1.1695778186096861,
          "along_body_m": -0.10208952178466521,
          "center_vehicle_m": [
            -2.36,
            -0.10208952178466521,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": 4.577384257622516,
          "pitch_deg": 1.57990476272649,
          "along_body_m": 0.12932066523328864,
          "center_vehicle_m": [
            0.47932066523328865,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 1.3022082123297247,
          "validation_p95_px": 1.5631876572319179,
          "rotation_error_deg": 0.7376878885185812,
          "center_error_m": 0.03520825355631871
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.614596998808949,
          "validation_p95_px": 10.653201386959722,
          "rotation_error_deg": 4.640924041374824,
          "center_error_m": 0.028069382676466303
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 11.178768361292331,
          "validation_p95_px": 12.4365676779789,
          "rotation_error_deg": 4.486227889495468,
          "center_error_m": 0.10208952178466522
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.38499372687485,
          "validation_p95_px": 10.112102804123595,
          "rotation_error_deg": 4.842232742710265,
          "center_error_m": 0.12932066523328856
        }
      ],
      "success": true,
      "observations_sha256": "05358e692bb08803f1f386c7ee0a3c549c93bec1ce93330754d9d6ddf30cf9d0",
      "cameras": [
        {
          "camera_id": 0,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.19186608922037274,
          "validation_p95_px": 0.25679418275615556,
          "rotation_error_deg": 0.10868680845732955,
          "center_error_m": 0.008364955613036603,
          "inliers": 96,
          "fit_ms": 0.801437
        },
        {
          "camera_id": 1,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.16579359921420814,
          "validation_p95_px": 0.2520255393203661,
          "rotation_error_deg": 0.09370493068356021,
          "center_error_m": 0.018105685403632655,
          "inliers": 94,
          "fit_ms": 0.638688
        },
        {
          "camera_id": 2,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.36422276518744195,
          "validation_p95_px": 0.7040200488381501,
          "rotation_error_deg": 0.1315465016528638,
          "center_error_m": 0.024713065474818652,
          "inliers": 94,
          "fit_ms": 0.606248
        },
        {
          "camera_id": 3,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.17437779381605775,
          "validation_p95_px": 0.25448650771849707,
          "rotation_error_deg": 0.07081450244904008,
          "center_error_m": 0.008427148112730112,
          "inliers": 98,
          "fit_ms": 0.642085
        }
      ]
    },
    {
      "trial": 2,
      "mount_seed": 20261009,
      "sigma_px": 1.0,
      "outlier_fraction": 0.0,
      "method": "iterative",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": -0.27097312815937435,
          "pitch_deg": -0.6861179651764964,
          "along_body_m": 0.03520825355631871,
          "center_vehicle_m": [
            2.36,
            0.03520825355631871,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": -2.90774132234998,
          "pitch_deg": 3.617461165784224,
          "along_body_m": -0.028069382676466248,
          "center_vehicle_m": [
            0.32193061732353373,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": 4.331163799612122,
          "pitch_deg": -1.1695778186096861,
          "along_body_m": -0.10208952178466521,
          "center_vehicle_m": [
            -2.36,
            -0.10208952178466521,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": 4.577384257622516,
          "pitch_deg": 1.57990476272649,
          "along_body_m": 0.12932066523328864,
          "center_vehicle_m": [
            0.47932066523328865,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 1.3022082123297247,
          "validation_p95_px": 1.5631876572319179,
          "rotation_error_deg": 0.7376878885185812,
          "center_error_m": 0.03520825355631871
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.614596998808949,
          "validation_p95_px": 10.653201386959722,
          "rotation_error_deg": 4.640924041374824,
          "center_error_m": 0.028069382676466303
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 11.178768361292331,
          "validation_p95_px": 12.4365676779789,
          "rotation_error_deg": 4.486227889495468,
          "center_error_m": 0.10208952178466522
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.38499372687485,
          "validation_p95_px": 10.112102804123595,
          "rotation_error_deg": 4.842232742710265,
          "center_error_m": 0.12932066523328856
        }
      ],
      "success": true,
      "observations_sha256": "147359e8c96543816a2c0a636596584601f57a996b2242dd2764bac9f10baf95",
      "cameras": [
        {
          "camera_id": 0,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.1954962051326685,
          "validation_p95_px": 0.36271186125648347,
          "rotation_error_deg": 0.06365738856465472,
          "center_error_m": 0.011400074683503115,
          "inliers": 120,
          "fit_ms": 0.367205
        },
        {
          "camera_id": 1,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.1564292211897297,
          "validation_p95_px": 0.27254976466686937,
          "rotation_error_deg": 0.08147514258517015,
          "center_error_m": 0.009757491193567903,
          "inliers": 120,
          "fit_ms": 0.143381
        },
        {
          "camera_id": 2,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.3017437476961247,
          "validation_p95_px": 0.4699796916955707,
          "rotation_error_deg": 0.09802222201964989,
          "center_error_m": 0.021713416307354284,
          "inliers": 120,
          "fit_ms": 0.127751
        },
        {
          "camera_id": 3,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.46773042498994766,
          "validation_p95_px": 0.5516475132632672,
          "rotation_error_deg": 0.29359403032417597,
          "center_error_m": 0.03230704332337836,
          "inliers": 120,
          "fit_ms": 0.124255
        }
      ]
    },
    {
      "trial": 2,
      "mount_seed": 20261009,
      "sigma_px": 1.0,
      "outlier_fraction": 0.0,
      "method": "epnp",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": -0.27097312815937435,
          "pitch_deg": -0.6861179651764964,
          "along_body_m": 0.03520825355631871,
          "center_vehicle_m": [
            2.36,
            0.03520825355631871,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": -2.90774132234998,
          "pitch_deg": 3.617461165784224,
          "along_body_m": -0.028069382676466248,
          "center_vehicle_m": [
            0.32193061732353373,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": 4.331163799612122,
          "pitch_deg": -1.1695778186096861,
          "along_body_m": -0.10208952178466521,
          "center_vehicle_m": [
            -2.36,
            -0.10208952178466521,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": 4.577384257622516,
          "pitch_deg": 1.57990476272649,
          "along_body_m": 0.12932066523328864,
          "center_vehicle_m": [
            0.47932066523328865,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 1.3022082123297247,
          "validation_p95_px": 1.5631876572319179,
          "rotation_error_deg": 0.7376878885185812,
          "center_error_m": 0.03520825355631871
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.614596998808949,
          "validation_p95_px": 10.653201386959722,
          "rotation_error_deg": 4.640924041374824,
          "center_error_m": 0.028069382676466303
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 11.178768361292331,
          "validation_p95_px": 12.4365676779789,
          "rotation_error_deg": 4.486227889495468,
          "center_error_m": 0.10208952178466522
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.38499372687485,
          "validation_p95_px": 10.112102804123595,
          "rotation_error_deg": 4.842232742710265,
          "center_error_m": 0.12932066523328856
        }
      ],
      "success": true,
      "observations_sha256": "147359e8c96543816a2c0a636596584601f57a996b2242dd2764bac9f10baf95",
      "cameras": [
        {
          "camera_id": 0,
          "invalid_validation_count": 0,
          "validation_rmse_px": 2.7545339163199056,
          "validation_p95_px": 5.139235471545517,
          "rotation_error_deg": 0.7917243365950819,
          "center_error_m": 0.19118070407238427,
          "inliers": 120,
          "fit_ms": 0.229584
        },
        {
          "camera_id": 1,
          "invalid_validation_count": 0,
          "validation_rmse_px": 1.7096838876820044,
          "validation_p95_px": 3.019963768855902,
          "rotation_error_deg": 0.6255780228926624,
          "center_error_m": 0.15609029072675715,
          "inliers": 120,
          "fit_ms": 0.056938
        },
        {
          "camera_id": 2,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.8312871348440228,
          "validation_p95_px": 1.245312493762665,
          "rotation_error_deg": 0.2357257688927338,
          "center_error_m": 0.03911326424953095,
          "inliers": 120,
          "fit_ms": 0.049604
        },
        {
          "camera_id": 3,
          "invalid_validation_count": 0,
          "validation_rmse_px": 1.627084680049257,
          "validation_p95_px": 2.215745376954679,
          "rotation_error_deg": 0.530884549902697,
          "center_error_m": 0.04402653633444525,
          "inliers": 120,
          "fit_ms": 0.051187
        }
      ]
    },
    {
      "trial": 2,
      "mount_seed": 20261009,
      "sigma_px": 1.0,
      "outlier_fraction": 0.0,
      "method": "sqpnp",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": -0.27097312815937435,
          "pitch_deg": -0.6861179651764964,
          "along_body_m": 0.03520825355631871,
          "center_vehicle_m": [
            2.36,
            0.03520825355631871,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": -2.90774132234998,
          "pitch_deg": 3.617461165784224,
          "along_body_m": -0.028069382676466248,
          "center_vehicle_m": [
            0.32193061732353373,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": 4.331163799612122,
          "pitch_deg": -1.1695778186096861,
          "along_body_m": -0.10208952178466521,
          "center_vehicle_m": [
            -2.36,
            -0.10208952178466521,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": 4.577384257622516,
          "pitch_deg": 1.57990476272649,
          "along_body_m": 0.12932066523328864,
          "center_vehicle_m": [
            0.47932066523328865,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 1.3022082123297247,
          "validation_p95_px": 1.5631876572319179,
          "rotation_error_deg": 0.7376878885185812,
          "center_error_m": 0.03520825355631871
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.614596998808949,
          "validation_p95_px": 10.653201386959722,
          "rotation_error_deg": 4.640924041374824,
          "center_error_m": 0.028069382676466303
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 11.178768361292331,
          "validation_p95_px": 12.4365676779789,
          "rotation_error_deg": 4.486227889495468,
          "center_error_m": 0.10208952178466522
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.38499372687485,
          "validation_p95_px": 10.112102804123595,
          "rotation_error_deg": 4.842232742710265,
          "center_error_m": 0.12932066523328856
        }
      ],
      "success": true,
      "observations_sha256": "147359e8c96543816a2c0a636596584601f57a996b2242dd2764bac9f10baf95",
      "cameras": [
        {
          "camera_id": 0,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.2282956448536659,
          "validation_p95_px": 0.33560262252117945,
          "rotation_error_deg": 0.09706437345961916,
          "center_error_m": 0.012312639225683783,
          "inliers": 120,
          "fit_ms": 0.202764
        },
        {
          "camera_id": 1,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.5542413275872916,
          "validation_p95_px": 1.080317106671676,
          "rotation_error_deg": 0.5437253154621406,
          "center_error_m": 0.06421268552559657,
          "inliers": 120,
          "fit_ms": 0.034606
        },
        {
          "camera_id": 2,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.24886575655307977,
          "validation_p95_px": 0.3159346120423565,
          "rotation_error_deg": 0.12023755839439061,
          "center_error_m": 0.01849536216496856,
          "inliers": 120,
          "fit_ms": 0.027482
        },
        {
          "camera_id": 3,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.31697115359527944,
          "validation_p95_px": 0.4312786213349912,
          "rotation_error_deg": 0.1921484325878657,
          "center_error_m": 0.02048026187256195,
          "inliers": 120,
          "fit_ms": 0.02639
        }
      ]
    },
    {
      "trial": 2,
      "mount_seed": 20261009,
      "sigma_px": 1.0,
      "outlier_fraction": 0.0,
      "method": "ransac_epnp_lm",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": -0.27097312815937435,
          "pitch_deg": -0.6861179651764964,
          "along_body_m": 0.03520825355631871,
          "center_vehicle_m": [
            2.36,
            0.03520825355631871,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": -2.90774132234998,
          "pitch_deg": 3.617461165784224,
          "along_body_m": -0.028069382676466248,
          "center_vehicle_m": [
            0.32193061732353373,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": 4.331163799612122,
          "pitch_deg": -1.1695778186096861,
          "along_body_m": -0.10208952178466521,
          "center_vehicle_m": [
            -2.36,
            -0.10208952178466521,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": 4.577384257622516,
          "pitch_deg": 1.57990476272649,
          "along_body_m": 0.12932066523328864,
          "center_vehicle_m": [
            0.47932066523328865,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 1.3022082123297247,
          "validation_p95_px": 1.5631876572319179,
          "rotation_error_deg": 0.7376878885185812,
          "center_error_m": 0.03520825355631871
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.614596998808949,
          "validation_p95_px": 10.653201386959722,
          "rotation_error_deg": 4.640924041374824,
          "center_error_m": 0.028069382676466303
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 11.178768361292331,
          "validation_p95_px": 12.4365676779789,
          "rotation_error_deg": 4.486227889495468,
          "center_error_m": 0.10208952178466522
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.38499372687485,
          "validation_p95_px": 10.112102804123595,
          "rotation_error_deg": 4.842232742710265,
          "center_error_m": 0.12932066523328856
        }
      ],
      "success": true,
      "observations_sha256": "147359e8c96543816a2c0a636596584601f57a996b2242dd2764bac9f10baf95",
      "cameras": [
        {
          "camera_id": 0,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.3642593802046557,
          "validation_p95_px": 0.5464687460451557,
          "rotation_error_deg": 0.25833406179142976,
          "center_error_m": 0.027794142565883192,
          "inliers": 89,
          "fit_ms": 1.183049
        },
        {
          "camera_id": 1,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.4457996855263886,
          "validation_p95_px": 0.6097099510864226,
          "rotation_error_deg": 0.36111268746941344,
          "center_error_m": 0.03787384182269879,
          "inliers": 82,
          "fit_ms": 1.370584
        },
        {
          "camera_id": 2,
          "invalid_validation_count": 0,
          "validation_rmse_px": 1.1624431249160534,
          "validation_p95_px": 1.7250694042847354,
          "rotation_error_deg": 0.5362034616217608,
          "center_error_m": 0.08862119281224667,
          "inliers": 81,
          "fit_ms": 1.433162
        },
        {
          "camera_id": 3,
          "invalid_validation_count": 0,
          "validation_rmse_px": 1.0840423418603051,
          "validation_p95_px": 1.5818125258067197,
          "rotation_error_deg": 0.57086208127497,
          "center_error_m": 0.08687453215341852,
          "inliers": 80,
          "fit_ms": 1.454402
        }
      ]
    },
    {
      "trial": 2,
      "mount_seed": 20261009,
      "sigma_px": 1.0,
      "outlier_fraction": 0.1,
      "method": "iterative",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": -0.27097312815937435,
          "pitch_deg": -0.6861179651764964,
          "along_body_m": 0.03520825355631871,
          "center_vehicle_m": [
            2.36,
            0.03520825355631871,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": -2.90774132234998,
          "pitch_deg": 3.617461165784224,
          "along_body_m": -0.028069382676466248,
          "center_vehicle_m": [
            0.32193061732353373,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": 4.331163799612122,
          "pitch_deg": -1.1695778186096861,
          "along_body_m": -0.10208952178466521,
          "center_vehicle_m": [
            -2.36,
            -0.10208952178466521,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": 4.577384257622516,
          "pitch_deg": 1.57990476272649,
          "along_body_m": 0.12932066523328864,
          "center_vehicle_m": [
            0.47932066523328865,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 1.3022082123297247,
          "validation_p95_px": 1.5631876572319179,
          "rotation_error_deg": 0.7376878885185812,
          "center_error_m": 0.03520825355631871
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.614596998808949,
          "validation_p95_px": 10.653201386959722,
          "rotation_error_deg": 4.640924041374824,
          "center_error_m": 0.028069382676466303
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 11.178768361292331,
          "validation_p95_px": 12.4365676779789,
          "rotation_error_deg": 4.486227889495468,
          "center_error_m": 0.10208952178466522
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.38499372687485,
          "validation_p95_px": 10.112102804123595,
          "rotation_error_deg": 4.842232742710265,
          "center_error_m": 0.12932066523328856
        }
      ],
      "success": true,
      "observations_sha256": "843e9b9579e35baaaea6bcb95a3202e24b62aaf4b52bfd19a724bc1d2fc37ef1",
      "cameras": [
        {
          "camera_id": 0,
          "invalid_validation_count": 0,
          "validation_rmse_px": 2.211225659612168,
          "validation_p95_px": 3.3473183977125567,
          "rotation_error_deg": 1.6229744391180314,
          "center_error_m": 0.16734468157468563,
          "inliers": 120,
          "fit_ms": 0.354691
        },
        {
          "camera_id": 1,
          "invalid_validation_count": 0,
          "validation_rmse_px": 1.565197476240076,
          "validation_p95_px": 2.0440962244697056,
          "rotation_error_deg": 0.874117306877066,
          "center_error_m": 0.06577059494123198,
          "inliers": 120,
          "fit_ms": 0.167106
        },
        {
          "camera_id": 2,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.8072368581470236,
          "validation_p95_px": 1.2206562406593509,
          "rotation_error_deg": 0.3605146011827587,
          "center_error_m": 0.03840823362339824,
          "inliers": 120,
          "fit_ms": 0.125998
        },
        {
          "camera_id": 3,
          "invalid_validation_count": 0,
          "validation_rmse_px": 2.165472061303955,
          "validation_p95_px": 3.6194341889229933,
          "rotation_error_deg": 0.574603263023008,
          "center_error_m": 0.17942590366594774,
          "inliers": 120,
          "fit_ms": 0.155074
        }
      ]
    },
    {
      "trial": 2,
      "mount_seed": 20261009,
      "sigma_px": 1.0,
      "outlier_fraction": 0.1,
      "method": "epnp",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": -0.27097312815937435,
          "pitch_deg": -0.6861179651764964,
          "along_body_m": 0.03520825355631871,
          "center_vehicle_m": [
            2.36,
            0.03520825355631871,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": -2.90774132234998,
          "pitch_deg": 3.617461165784224,
          "along_body_m": -0.028069382676466248,
          "center_vehicle_m": [
            0.32193061732353373,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": 4.331163799612122,
          "pitch_deg": -1.1695778186096861,
          "along_body_m": -0.10208952178466521,
          "center_vehicle_m": [
            -2.36,
            -0.10208952178466521,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": 4.577384257622516,
          "pitch_deg": 1.57990476272649,
          "along_body_m": 0.12932066523328864,
          "center_vehicle_m": [
            0.47932066523328865,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 1.3022082123297247,
          "validation_p95_px": 1.5631876572319179,
          "rotation_error_deg": 0.7376878885185812,
          "center_error_m": 0.03520825355631871
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.614596998808949,
          "validation_p95_px": 10.653201386959722,
          "rotation_error_deg": 4.640924041374824,
          "center_error_m": 0.028069382676466303
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 11.178768361292331,
          "validation_p95_px": 12.4365676779789,
          "rotation_error_deg": 4.486227889495468,
          "center_error_m": 0.10208952178466522
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.38499372687485,
          "validation_p95_px": 10.112102804123595,
          "rotation_error_deg": 4.842232742710265,
          "center_error_m": 0.12932066523328856
        }
      ],
      "success": true,
      "observations_sha256": "843e9b9579e35baaaea6bcb95a3202e24b62aaf4b52bfd19a724bc1d2fc37ef1",
      "cameras": [
        {
          "camera_id": 0,
          "invalid_validation_count": 0,
          "validation_rmse_px": 47.837531977394754,
          "validation_p95_px": 102.18432422551375,
          "rotation_error_deg": 11.102869461390114,
          "center_error_m": 3.1732390145454774,
          "inliers": 120,
          "fit_ms": 0.217341
        },
        {
          "camera_id": 1,
          "invalid_validation_count": 0,
          "validation_rmse_px": 5.856287232997332,
          "validation_p95_px": 12.932978364037426,
          "rotation_error_deg": 2.486131049244018,
          "center_error_m": 0.5365819063113128,
          "inliers": 120,
          "fit_ms": 0.056147
        },
        {
          "camera_id": 2,
          "invalid_validation_count": 0,
          "validation_rmse_px": 2.0726421448321046,
          "validation_p95_px": 2.7336797410948086,
          "rotation_error_deg": 1.5783931603599437,
          "center_error_m": 0.1436133358640017,
          "inliers": 120,
          "fit_ms": 0.049945
        },
        {
          "camera_id": 3,
          "invalid_validation_count": 0,
          "validation_rmse_px": 3.4133700233797764,
          "validation_p95_px": 4.95913810444621,
          "rotation_error_deg": 2.0983935612189226,
          "center_error_m": 0.26025254105780604,
          "inliers": 120,
          "fit_ms": 0.047861
        }
      ]
    },
    {
      "trial": 2,
      "mount_seed": 20261009,
      "sigma_px": 1.0,
      "outlier_fraction": 0.1,
      "method": "sqpnp",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": -0.27097312815937435,
          "pitch_deg": -0.6861179651764964,
          "along_body_m": 0.03520825355631871,
          "center_vehicle_m": [
            2.36,
            0.03520825355631871,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": -2.90774132234998,
          "pitch_deg": 3.617461165784224,
          "along_body_m": -0.028069382676466248,
          "center_vehicle_m": [
            0.32193061732353373,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": 4.331163799612122,
          "pitch_deg": -1.1695778186096861,
          "along_body_m": -0.10208952178466521,
          "center_vehicle_m": [
            -2.36,
            -0.10208952178466521,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": 4.577384257622516,
          "pitch_deg": 1.57990476272649,
          "along_body_m": 0.12932066523328864,
          "center_vehicle_m": [
            0.47932066523328865,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 1.3022082123297247,
          "validation_p95_px": 1.5631876572319179,
          "rotation_error_deg": 0.7376878885185812,
          "center_error_m": 0.03520825355631871
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.614596998808949,
          "validation_p95_px": 10.653201386959722,
          "rotation_error_deg": 4.640924041374824,
          "center_error_m": 0.028069382676466303
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 11.178768361292331,
          "validation_p95_px": 12.4365676779789,
          "rotation_error_deg": 4.486227889495468,
          "center_error_m": 0.10208952178466522
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.38499372687485,
          "validation_p95_px": 10.112102804123595,
          "rotation_error_deg": 4.842232742710265,
          "center_error_m": 0.12932066523328856
        }
      ],
      "success": true,
      "observations_sha256": "843e9b9579e35baaaea6bcb95a3202e24b62aaf4b52bfd19a724bc1d2fc37ef1",
      "cameras": [
        {
          "camera_id": 0,
          "invalid_validation_count": 0,
          "validation_rmse_px": 14.709367551191209,
          "validation_p95_px": 33.9105029877158,
          "rotation_error_deg": 5.6791278891976456,
          "center_error_m": 1.1993750956852298,
          "inliers": 120,
          "fit_ms": 0.237139
        },
        {
          "camera_id": 1,
          "invalid_validation_count": 0,
          "validation_rmse_px": 1.2670546223276724,
          "validation_p95_px": 2.1533541092512305,
          "rotation_error_deg": 0.9992295722445986,
          "center_error_m": 0.13516487032614538,
          "inliers": 120,
          "fit_ms": 0.036048
        },
        {
          "camera_id": 2,
          "invalid_validation_count": 0,
          "validation_rmse_px": 1.2848354047028523,
          "validation_p95_px": 2.1962064161009214,
          "rotation_error_deg": 0.7763317832743771,
          "center_error_m": 0.08519298440813183,
          "inliers": 120,
          "fit_ms": 0.027472
        },
        {
          "camera_id": 3,
          "invalid_validation_count": 0,
          "validation_rmse_px": 3.6692486874806254,
          "validation_p95_px": 4.166871324276105,
          "rotation_error_deg": 2.0521265871264283,
          "center_error_m": 0.2644421648294804,
          "inliers": 120,
          "fit_ms": 0.027322
        }
      ]
    },
    {
      "trial": 2,
      "mount_seed": 20261009,
      "sigma_px": 1.0,
      "outlier_fraction": 0.1,
      "method": "ransac_epnp_lm",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": -0.27097312815937435,
          "pitch_deg": -0.6861179651764964,
          "along_body_m": 0.03520825355631871,
          "center_vehicle_m": [
            2.36,
            0.03520825355631871,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": -2.90774132234998,
          "pitch_deg": 3.617461165784224,
          "along_body_m": -0.028069382676466248,
          "center_vehicle_m": [
            0.32193061732353373,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": 4.331163799612122,
          "pitch_deg": -1.1695778186096861,
          "along_body_m": -0.10208952178466521,
          "center_vehicle_m": [
            -2.36,
            -0.10208952178466521,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": 4.577384257622516,
          "pitch_deg": 1.57990476272649,
          "along_body_m": 0.12932066523328864,
          "center_vehicle_m": [
            0.47932066523328865,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 1.3022082123297247,
          "validation_p95_px": 1.5631876572319179,
          "rotation_error_deg": 0.7376878885185812,
          "center_error_m": 0.03520825355631871
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.614596998808949,
          "validation_p95_px": 10.653201386959722,
          "rotation_error_deg": 4.640924041374824,
          "center_error_m": 0.028069382676466303
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 11.178768361292331,
          "validation_p95_px": 12.4365676779789,
          "rotation_error_deg": 4.486227889495468,
          "center_error_m": 0.10208952178466522
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.38499372687485,
          "validation_p95_px": 10.112102804123595,
          "rotation_error_deg": 4.842232742710265,
          "center_error_m": 0.12932066523328856
        }
      ],
      "success": true,
      "observations_sha256": "843e9b9579e35baaaea6bcb95a3202e24b62aaf4b52bfd19a724bc1d2fc37ef1",
      "cameras": [
        {
          "camera_id": 0,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.2790468670504994,
          "validation_p95_px": 0.5281275676754515,
          "rotation_error_deg": 0.15205042680262545,
          "center_error_m": 0.019181883514116035,
          "inliers": 82,
          "fit_ms": 1.912189
        },
        {
          "camera_id": 1,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.3664521898416556,
          "validation_p95_px": 0.6153045890475733,
          "rotation_error_deg": 0.1502868765953834,
          "center_error_m": 0.015921318575002386,
          "inliers": 76,
          "fit_ms": 1.71723
        },
        {
          "camera_id": 2,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.5090311994862791,
          "validation_p95_px": 0.7581490934758152,
          "rotation_error_deg": 0.322414981133839,
          "center_error_m": 0.01665176475265723,
          "inliers": 75,
          "fit_ms": 1.817339
        },
        {
          "camera_id": 3,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.8691918520278692,
          "validation_p95_px": 1.4242891979409147,
          "rotation_error_deg": 0.30343823062708053,
          "center_error_m": 0.07465266585938972,
          "inliers": 75,
          "fit_ms": 1.98168
        }
      ]
    },
    {
      "trial": 3,
      "mount_seed": 20261010,
      "sigma_px": 0.0,
      "outlier_fraction": 0.0,
      "method": "iterative",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": 3.7218766594643142,
          "pitch_deg": -3.753858945725848,
          "along_body_m": -0.1411180073315493,
          "center_vehicle_m": [
            2.36,
            -0.1411180073315493,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": -2.086634904874484,
          "pitch_deg": 1.9015380634696841,
          "along_body_m": -0.11902165000629064,
          "center_vehicle_m": [
            0.23097834999370934,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": -1.4807751204918231,
          "pitch_deg": -1.9646042935235624,
          "along_body_m": -0.04829021155331188,
          "center_vehicle_m": [
            -2.36,
            -0.04829021155331188,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": -0.44574040807293613,
          "pitch_deg": -1.134533964007943,
          "along_body_m": -0.028006283940675628,
          "center_vehicle_m": [
            0.32199371605932436,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.693323256812128,
          "validation_p95_px": 10.695352653693268,
          "rotation_error_deg": 5.285722943856591,
          "center_error_m": 0.14111800733154925
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.0329767369677185,
          "validation_p95_px": 8.846406381832978,
          "rotation_error_deg": 2.823028970799203,
          "center_error_m": 0.11902165000629067
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 4.456635913722232,
          "validation_p95_px": 5.10064801865688,
          "rotation_error_deg": 2.4601118160678097,
          "center_error_m": 0.04829021155331187
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 2.214035850943054,
          "validation_p95_px": 2.591728340862886,
          "rotation_error_deg": 1.2189525564664423,
          "center_error_m": 0.028006283940675614
        }
      ],
      "success": true,
      "observations_sha256": "a5e515d608cab5901a7719079773318637092b25e339b6ff180baa94733fbeca",
      "cameras": [
        {
          "camera_id": 0,
          "invalid_validation_count": 0,
          "validation_rmse_px": 2.4062243108687257e-06,
          "validation_p95_px": 5.433534276912944e-06,
          "rotation_error_deg": 0.0,
          "center_error_m": 1.8532440180846436e-07,
          "inliers": 120,
          "fit_ms": 0.414534
        },
        {
          "camera_id": 1,
          "invalid_validation_count": 0,
          "validation_rmse_px": 4.053397198153691e-06,
          "validation_p95_px": 5.284898654074847e-06,
          "rotation_error_deg": 1.7075472925031877e-06,
          "center_error_m": 8.88010035343999e-08,
          "inliers": 120,
          "fit_ms": 0.15318
        },
        {
          "camera_id": 2,
          "invalid_validation_count": 0,
          "validation_rmse_px": 1.4542958946105508e-06,
          "validation_p95_px": 3.490667382046754e-06,
          "rotation_error_deg": 1.7075472925031877e-06,
          "center_error_m": 8.055919409609596e-08,
          "inliers": 120,
          "fit_ms": 0.129144
        },
        {
          "camera_id": 3,
          "invalid_validation_count": 0,
          "validation_rmse_px": 5.637556584896871e-06,
          "validation_p95_px": 8.13214574708091e-06,
          "rotation_error_deg": 0.0,
          "center_error_m": 2.828225185052736e-07,
          "inliers": 120,
          "fit_ms": 0.089149
        }
      ]
    },
    {
      "trial": 3,
      "mount_seed": 20261010,
      "sigma_px": 0.0,
      "outlier_fraction": 0.0,
      "method": "epnp",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": 3.7218766594643142,
          "pitch_deg": -3.753858945725848,
          "along_body_m": -0.1411180073315493,
          "center_vehicle_m": [
            2.36,
            -0.1411180073315493,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": -2.086634904874484,
          "pitch_deg": 1.9015380634696841,
          "along_body_m": -0.11902165000629064,
          "center_vehicle_m": [
            0.23097834999370934,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": -1.4807751204918231,
          "pitch_deg": -1.9646042935235624,
          "along_body_m": -0.04829021155331188,
          "center_vehicle_m": [
            -2.36,
            -0.04829021155331188,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": -0.44574040807293613,
          "pitch_deg": -1.134533964007943,
          "along_body_m": -0.028006283940675628,
          "center_vehicle_m": [
            0.32199371605932436,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.693323256812128,
          "validation_p95_px": 10.695352653693268,
          "rotation_error_deg": 5.285722943856591,
          "center_error_m": 0.14111800733154925
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.0329767369677185,
          "validation_p95_px": 8.846406381832978,
          "rotation_error_deg": 2.823028970799203,
          "center_error_m": 0.11902165000629067
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 4.456635913722232,
          "validation_p95_px": 5.10064801865688,
          "rotation_error_deg": 2.4601118160678097,
          "center_error_m": 0.04829021155331187
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 2.214035850943054,
          "validation_p95_px": 2.591728340862886,
          "rotation_error_deg": 1.2189525564664423,
          "center_error_m": 0.028006283940675614
        }
      ],
      "success": true,
      "observations_sha256": "a5e515d608cab5901a7719079773318637092b25e339b6ff180baa94733fbeca",
      "cameras": [
        {
          "camera_id": 0,
          "invalid_validation_count": 0,
          "validation_rmse_px": 4.729087625735867e-13,
          "validation_p95_px": 1.1091364791350836e-12,
          "rotation_error_deg": 1.2074182697257333e-06,
          "center_error_m": 3.7246296834893416e-14,
          "inliers": 120,
          "fit_ms": 0.2745
        },
        {
          "camera_id": 1,
          "invalid_validation_count": 0,
          "validation_rmse_px": 3.1589198955788215e-13,
          "validation_p95_px": 5.251650664056174e-13,
          "rotation_error_deg": 0.0,
          "center_error_m": 1.9591897818951655e-14,
          "inliers": 120,
          "fit_ms": 0.062468
        },
        {
          "camera_id": 2,
          "invalid_validation_count": 0,
          "validation_rmse_px": 2.297006381090619e-13,
          "validation_p95_px": 4.979342411453644e-13,
          "rotation_error_deg": 0.0,
          "center_error_m": 1.6162304584474068e-14,
          "inliers": 120,
          "fit_ms": 0.052129
        },
        {
          "camera_id": 3,
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.914791566488948e-13,
          "validation_p95_px": 1.4888434674696088e-12,
          "rotation_error_deg": 0.0,
          "center_error_m": 3.9995813354104877e-14,
          "inliers": 120,
          "fit_ms": 0.059432
        }
      ]
    },
    {
      "trial": 3,
      "mount_seed": 20261010,
      "sigma_px": 0.0,
      "outlier_fraction": 0.0,
      "method": "sqpnp",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": 3.7218766594643142,
          "pitch_deg": -3.753858945725848,
          "along_body_m": -0.1411180073315493,
          "center_vehicle_m": [
            2.36,
            -0.1411180073315493,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": -2.086634904874484,
          "pitch_deg": 1.9015380634696841,
          "along_body_m": -0.11902165000629064,
          "center_vehicle_m": [
            0.23097834999370934,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": -1.4807751204918231,
          "pitch_deg": -1.9646042935235624,
          "along_body_m": -0.04829021155331188,
          "center_vehicle_m": [
            -2.36,
            -0.04829021155331188,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": -0.44574040807293613,
          "pitch_deg": -1.134533964007943,
          "along_body_m": -0.028006283940675628,
          "center_vehicle_m": [
            0.32199371605932436,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.693323256812128,
          "validation_p95_px": 10.695352653693268,
          "rotation_error_deg": 5.285722943856591,
          "center_error_m": 0.14111800733154925
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.0329767369677185,
          "validation_p95_px": 8.846406381832978,
          "rotation_error_deg": 2.823028970799203,
          "center_error_m": 0.11902165000629067
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 4.456635913722232,
          "validation_p95_px": 5.10064801865688,
          "rotation_error_deg": 2.4601118160678097,
          "center_error_m": 0.04829021155331187
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 2.214035850943054,
          "validation_p95_px": 2.591728340862886,
          "rotation_error_deg": 1.2189525564664423,
          "center_error_m": 0.028006283940675614
        }
      ],
      "success": true,
      "observations_sha256": "a5e515d608cab5901a7719079773318637092b25e339b6ff180baa94733fbeca",
      "cameras": [
        {
          "camera_id": 0,
          "invalid_validation_count": 0,
          "validation_rmse_px": 1.5694627546411301e-12,
          "validation_p95_px": 2.910227358508017e-12,
          "rotation_error_deg": 0.0,
          "center_error_m": 1.2092411182768957e-13,
          "inliers": 120,
          "fit_ms": 0.424343
        },
        {
          "camera_id": 1,
          "invalid_validation_count": 0,
          "validation_rmse_px": 1.1414249418377846e-11,
          "validation_p95_px": 1.4154021021387923e-11,
          "rotation_error_deg": 0.0,
          "center_error_m": 1.2338619624788836e-13,
          "inliers": 120,
          "fit_ms": 0.035177
        },
        {
          "camera_id": 2,
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.919731134799047e-13,
          "validation_p95_px": 1.906523214212279e-12,
          "rotation_error_deg": 0.0,
          "center_error_m": 5.0735341693385025e-14,
          "inliers": 120,
          "fit_ms": 0.026149
        },
        {
          "camera_id": 3,
          "invalid_validation_count": 0,
          "validation_rmse_px": 2.480649773246873e-12,
          "validation_p95_px": 3.642873987148008e-12,
          "rotation_error_deg": 0.0,
          "center_error_m": 1.0572028433747991e-13,
          "inliers": 120,
          "fit_ms": 0.023524
        }
      ]
    },
    {
      "trial": 3,
      "mount_seed": 20261010,
      "sigma_px": 0.0,
      "outlier_fraction": 0.0,
      "method": "ransac_epnp_lm",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": 3.7218766594643142,
          "pitch_deg": -3.753858945725848,
          "along_body_m": -0.1411180073315493,
          "center_vehicle_m": [
            2.36,
            -0.1411180073315493,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": -2.086634904874484,
          "pitch_deg": 1.9015380634696841,
          "along_body_m": -0.11902165000629064,
          "center_vehicle_m": [
            0.23097834999370934,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": -1.4807751204918231,
          "pitch_deg": -1.9646042935235624,
          "along_body_m": -0.04829021155331188,
          "center_vehicle_m": [
            -2.36,
            -0.04829021155331188,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": -0.44574040807293613,
          "pitch_deg": -1.134533964007943,
          "along_body_m": -0.028006283940675628,
          "center_vehicle_m": [
            0.32199371605932436,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.693323256812128,
          "validation_p95_px": 10.695352653693268,
          "rotation_error_deg": 5.285722943856591,
          "center_error_m": 0.14111800733154925
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.0329767369677185,
          "validation_p95_px": 8.846406381832978,
          "rotation_error_deg": 2.823028970799203,
          "center_error_m": 0.11902165000629067
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 4.456635913722232,
          "validation_p95_px": 5.10064801865688,
          "rotation_error_deg": 2.4601118160678097,
          "center_error_m": 0.04829021155331187
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 2.214035850943054,
          "validation_p95_px": 2.591728340862886,
          "rotation_error_deg": 1.2189525564664423,
          "center_error_m": 0.028006283940675614
        }
      ],
      "success": true,
      "observations_sha256": "a5e515d608cab5901a7719079773318637092b25e339b6ff180baa94733fbeca",
      "cameras": [
        {
          "camera_id": 0,
          "invalid_validation_count": 0,
          "validation_rmse_px": 1.5043217967490793e-06,
          "validation_p95_px": 2.704493888942404e-06,
          "rotation_error_deg": 0.0,
          "center_error_m": 6.683001450177855e-08,
          "inliers": 120,
          "fit_ms": 0.458878
        },
        {
          "camera_id": 1,
          "invalid_validation_count": 0,
          "validation_rmse_px": 1.0712442270348925e-06,
          "validation_p95_px": 1.7343422119689216e-06,
          "rotation_error_deg": 0.0,
          "center_error_m": 5.863481374480266e-08,
          "inliers": 120,
          "fit_ms": 0.249693
        },
        {
          "camera_id": 2,
          "invalid_validation_count": 0,
          "validation_rmse_px": 8.179003920159904e-10,
          "validation_p95_px": 1.060195264855905e-09,
          "rotation_error_deg": 0.0,
          "center_error_m": 3.026998543450201e-11,
          "inliers": 120,
          "fit_ms": 0.206812
        },
        {
          "camera_id": 3,
          "invalid_validation_count": 0,
          "validation_rmse_px": 6.024342171820879e-07,
          "validation_p95_px": 7.773097730789973e-07,
          "rotation_error_deg": 0.0,
          "center_error_m": 2.337955981328133e-08,
          "inliers": 120,
          "fit_ms": 0.155915
        }
      ]
    },
    {
      "trial": 3,
      "mount_seed": 20261010,
      "sigma_px": 0.0,
      "outlier_fraction": 0.1,
      "method": "iterative",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": 3.7218766594643142,
          "pitch_deg": -3.753858945725848,
          "along_body_m": -0.1411180073315493,
          "center_vehicle_m": [
            2.36,
            -0.1411180073315493,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": -2.086634904874484,
          "pitch_deg": 1.9015380634696841,
          "along_body_m": -0.11902165000629064,
          "center_vehicle_m": [
            0.23097834999370934,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": -1.4807751204918231,
          "pitch_deg": -1.9646042935235624,
          "along_body_m": -0.04829021155331188,
          "center_vehicle_m": [
            -2.36,
            -0.04829021155331188,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": -0.44574040807293613,
          "pitch_deg": -1.134533964007943,
          "along_body_m": -0.028006283940675628,
          "center_vehicle_m": [
            0.32199371605932436,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.693323256812128,
          "validation_p95_px": 10.695352653693268,
          "rotation_error_deg": 5.285722943856591,
          "center_error_m": 0.14111800733154925
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.0329767369677185,
          "validation_p95_px": 8.846406381832978,
          "rotation_error_deg": 2.823028970799203,
          "center_error_m": 0.11902165000629067
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 4.456635913722232,
          "validation_p95_px": 5.10064801865688,
          "rotation_error_deg": 2.4601118160678097,
          "center_error_m": 0.04829021155331187
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 2.214035850943054,
          "validation_p95_px": 2.591728340862886,
          "rotation_error_deg": 1.2189525564664423,
          "center_error_m": 0.028006283940675614
        }
      ],
      "success": true,
      "observations_sha256": "c4d50b1679985edf8d6924b48432cfc1040872de986abe15f7182d7029ca7bb4",
      "cameras": [
        {
          "camera_id": 0,
          "invalid_validation_count": 0,
          "validation_rmse_px": 1.7420054073514752,
          "validation_p95_px": 2.304216180893108,
          "rotation_error_deg": 1.3272317449630255,
          "center_error_m": 0.08871201739966367,
          "inliers": 120,
          "fit_ms": 0.361835
        },
        {
          "camera_id": 1,
          "invalid_validation_count": 0,
          "validation_rmse_px": 6.972715227215475,
          "validation_p95_px": 11.512211544820193,
          "rotation_error_deg": 3.6430553207940934,
          "center_error_m": 0.4441200292211226,
          "inliers": 120,
          "fit_ms": 0.238892
        },
        {
          "camera_id": 2,
          "invalid_validation_count": 0,
          "validation_rmse_px": 8.475525395350749,
          "validation_p95_px": 13.682021738059468,
          "rotation_error_deg": 5.745381414400111,
          "center_error_m": 0.3868719657808233,
          "inliers": 120,
          "fit_ms": 0.230587
        },
        {
          "camera_id": 3,
          "invalid_validation_count": 0,
          "validation_rmse_px": 2.2314568170201032,
          "validation_p95_px": 2.9611545005524063,
          "rotation_error_deg": 1.0495404649713913,
          "center_error_m": 0.07240035853578142,
          "inliers": 120,
          "fit_ms": 0.156546
        }
      ]
    },
    {
      "trial": 3,
      "mount_seed": 20261010,
      "sigma_px": 0.0,
      "outlier_fraction": 0.1,
      "method": "epnp",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": 3.7218766594643142,
          "pitch_deg": -3.753858945725848,
          "along_body_m": -0.1411180073315493,
          "center_vehicle_m": [
            2.36,
            -0.1411180073315493,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": -2.086634904874484,
          "pitch_deg": 1.9015380634696841,
          "along_body_m": -0.11902165000629064,
          "center_vehicle_m": [
            0.23097834999370934,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": -1.4807751204918231,
          "pitch_deg": -1.9646042935235624,
          "along_body_m": -0.04829021155331188,
          "center_vehicle_m": [
            -2.36,
            -0.04829021155331188,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": -0.44574040807293613,
          "pitch_deg": -1.134533964007943,
          "along_body_m": -0.028006283940675628,
          "center_vehicle_m": [
            0.32199371605932436,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.693323256812128,
          "validation_p95_px": 10.695352653693268,
          "rotation_error_deg": 5.285722943856591,
          "center_error_m": 0.14111800733154925
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.0329767369677185,
          "validation_p95_px": 8.846406381832978,
          "rotation_error_deg": 2.823028970799203,
          "center_error_m": 0.11902165000629067
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 4.456635913722232,
          "validation_p95_px": 5.10064801865688,
          "rotation_error_deg": 2.4601118160678097,
          "center_error_m": 0.04829021155331187
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 2.214035850943054,
          "validation_p95_px": 2.591728340862886,
          "rotation_error_deg": 1.2189525564664423,
          "center_error_m": 0.028006283940675614
        }
      ],
      "success": true,
      "observations_sha256": "c4d50b1679985edf8d6924b48432cfc1040872de986abe15f7182d7029ca7bb4",
      "cameras": [
        {
          "camera_id": 0,
          "invalid_validation_count": 0,
          "validation_rmse_px": 13.968343425590296,
          "validation_p95_px": 32.211274353962274,
          "rotation_error_deg": 3.143497025744152,
          "center_error_m": 0.9592011629862487,
          "inliers": 120,
          "fit_ms": 0.210999
        },
        {
          "camera_id": 1,
          "invalid_validation_count": 0,
          "validation_rmse_px": 22.43317339566793,
          "validation_p95_px": 50.6364005631335,
          "rotation_error_deg": 4.967544075237848,
          "center_error_m": 1.8771217015039328,
          "inliers": 120,
          "fit_ms": 0.054764
        },
        {
          "camera_id": 2,
          "invalid_validation_count": 0,
          "validation_rmse_px": 24.958270778636994,
          "validation_p95_px": 31.307297442689478,
          "rotation_error_deg": 15.135750739063193,
          "center_error_m": 0.7401528934879378,
          "inliers": 120,
          "fit_ms": 0.048712
        },
        {
          "camera_id": 3,
          "invalid_validation_count": 0,
          "validation_rmse_px": 74.69490359026344,
          "validation_p95_px": 116.30856427335964,
          "rotation_error_deg": 169.87231055916712,
          "center_error_m": 14.224051889095648,
          "inliers": 120,
          "fit_ms": 0.04749
        }
      ]
    },
    {
      "trial": 3,
      "mount_seed": 20261010,
      "sigma_px": 0.0,
      "outlier_fraction": 0.1,
      "method": "sqpnp",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": 3.7218766594643142,
          "pitch_deg": -3.753858945725848,
          "along_body_m": -0.1411180073315493,
          "center_vehicle_m": [
            2.36,
            -0.1411180073315493,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": -2.086634904874484,
          "pitch_deg": 1.9015380634696841,
          "along_body_m": -0.11902165000629064,
          "center_vehicle_m": [
            0.23097834999370934,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": -1.4807751204918231,
          "pitch_deg": -1.9646042935235624,
          "along_body_m": -0.04829021155331188,
          "center_vehicle_m": [
            -2.36,
            -0.04829021155331188,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": -0.44574040807293613,
          "pitch_deg": -1.134533964007943,
          "along_body_m": -0.028006283940675628,
          "center_vehicle_m": [
            0.32199371605932436,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.693323256812128,
          "validation_p95_px": 10.695352653693268,
          "rotation_error_deg": 5.285722943856591,
          "center_error_m": 0.14111800733154925
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.0329767369677185,
          "validation_p95_px": 8.846406381832978,
          "rotation_error_deg": 2.823028970799203,
          "center_error_m": 0.11902165000629067
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 4.456635913722232,
          "validation_p95_px": 5.10064801865688,
          "rotation_error_deg": 2.4601118160678097,
          "center_error_m": 0.04829021155331187
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 2.214035850943054,
          "validation_p95_px": 2.591728340862886,
          "rotation_error_deg": 1.2189525564664423,
          "center_error_m": 0.028006283940675614
        }
      ],
      "success": false,
      "observations_sha256": "c4d50b1679985edf8d6924b48432cfc1040872de986abe15f7182d7029ca7bb4",
      "error": "estimated pose projects training point outside validity"
    },
    {
      "trial": 3,
      "mount_seed": 20261010,
      "sigma_px": 0.0,
      "outlier_fraction": 0.1,
      "method": "ransac_epnp_lm",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": 3.7218766594643142,
          "pitch_deg": -3.753858945725848,
          "along_body_m": -0.1411180073315493,
          "center_vehicle_m": [
            2.36,
            -0.1411180073315493,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": -2.086634904874484,
          "pitch_deg": 1.9015380634696841,
          "along_body_m": -0.11902165000629064,
          "center_vehicle_m": [
            0.23097834999370934,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": -1.4807751204918231,
          "pitch_deg": -1.9646042935235624,
          "along_body_m": -0.04829021155331188,
          "center_vehicle_m": [
            -2.36,
            -0.04829021155331188,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": -0.44574040807293613,
          "pitch_deg": -1.134533964007943,
          "along_body_m": -0.028006283940675628,
          "center_vehicle_m": [
            0.32199371605932436,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.693323256812128,
          "validation_p95_px": 10.695352653693268,
          "rotation_error_deg": 5.285722943856591,
          "center_error_m": 0.14111800733154925
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.0329767369677185,
          "validation_p95_px": 8.846406381832978,
          "rotation_error_deg": 2.823028970799203,
          "center_error_m": 0.11902165000629067
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 4.456635913722232,
          "validation_p95_px": 5.10064801865688,
          "rotation_error_deg": 2.4601118160678097,
          "center_error_m": 0.04829021155331187
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 2.214035850943054,
          "validation_p95_px": 2.591728340862886,
          "rotation_error_deg": 1.2189525564664423,
          "center_error_m": 0.028006283940675614
        }
      ],
      "success": true,
      "observations_sha256": "c4d50b1679985edf8d6924b48432cfc1040872de986abe15f7182d7029ca7bb4",
      "cameras": [
        {
          "camera_id": 0,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.008497602359625342,
          "validation_p95_px": 0.01199699982533007,
          "rotation_error_deg": 0.00464090826141772,
          "center_error_m": 0.0003696652003836436,
          "inliers": 109,
          "fit_ms": 0.574677
        },
        {
          "camera_id": 1,
          "invalid_validation_count": 0,
          "validation_rmse_px": 1.4949250315084547e-06,
          "validation_p95_px": 1.9685859237056165e-06,
          "rotation_error_deg": 0.0,
          "center_error_m": 5.100568493506401e-08,
          "inliers": 108,
          "fit_ms": 0.318583
        },
        {
          "camera_id": 2,
          "invalid_validation_count": 0,
          "validation_rmse_px": 3.8878532048780226e-10,
          "validation_p95_px": 5.550290633966059e-10,
          "rotation_error_deg": 0.0,
          "center_error_m": 2.1994537394016387e-11,
          "inliers": 108,
          "fit_ms": 0.288265
        },
        {
          "camera_id": 3,
          "invalid_validation_count": 0,
          "validation_rmse_px": 2.305008810293774e-06,
          "validation_p95_px": 2.8967303286764147e-06,
          "rotation_error_deg": 0.0,
          "center_error_m": 2.8787127339534218e-08,
          "inliers": 108,
          "fit_ms": 0.291482
        }
      ]
    },
    {
      "trial": 3,
      "mount_seed": 20261010,
      "sigma_px": 0.5,
      "outlier_fraction": 0.0,
      "method": "iterative",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": 3.7218766594643142,
          "pitch_deg": -3.753858945725848,
          "along_body_m": -0.1411180073315493,
          "center_vehicle_m": [
            2.36,
            -0.1411180073315493,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": -2.086634904874484,
          "pitch_deg": 1.9015380634696841,
          "along_body_m": -0.11902165000629064,
          "center_vehicle_m": [
            0.23097834999370934,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": -1.4807751204918231,
          "pitch_deg": -1.9646042935235624,
          "along_body_m": -0.04829021155331188,
          "center_vehicle_m": [
            -2.36,
            -0.04829021155331188,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": -0.44574040807293613,
          "pitch_deg": -1.134533964007943,
          "along_body_m": -0.028006283940675628,
          "center_vehicle_m": [
            0.32199371605932436,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.693323256812128,
          "validation_p95_px": 10.695352653693268,
          "rotation_error_deg": 5.285722943856591,
          "center_error_m": 0.14111800733154925
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.0329767369677185,
          "validation_p95_px": 8.846406381832978,
          "rotation_error_deg": 2.823028970799203,
          "center_error_m": 0.11902165000629067
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 4.456635913722232,
          "validation_p95_px": 5.10064801865688,
          "rotation_error_deg": 2.4601118160678097,
          "center_error_m": 0.04829021155331187
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 2.214035850943054,
          "validation_p95_px": 2.591728340862886,
          "rotation_error_deg": 1.2189525564664423,
          "center_error_m": 0.028006283940675614
        }
      ],
      "success": true,
      "observations_sha256": "22c8f7659965e448ba0f8bf30864a66fd1f284e1336859bed0ddba71c485b140",
      "cameras": [
        {
          "camera_id": 0,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.3205438716256286,
          "validation_p95_px": 0.6402256853531136,
          "rotation_error_deg": 0.17600865048053838,
          "center_error_m": 0.028254531701436013,
          "inliers": 120,
          "fit_ms": 0.360592
        },
        {
          "camera_id": 1,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.1435715525255123,
          "validation_p95_px": 0.20214707606127602,
          "rotation_error_deg": 0.08436877967641901,
          "center_error_m": 0.006116650679073283,
          "inliers": 120,
          "fit_ms": 0.156426
        },
        {
          "camera_id": 2,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.1056411453973084,
          "validation_p95_px": 0.2205972956156074,
          "rotation_error_deg": 0.020743667755980317,
          "center_error_m": 0.0052170526304368995,
          "inliers": 120,
          "fit_ms": 0.140315
        },
        {
          "camera_id": 3,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.08646133726895351,
          "validation_p95_px": 0.12244353670744783,
          "rotation_error_deg": 0.02646979035562346,
          "center_error_m": 0.0024886184269379474,
          "inliers": 120,
          "fit_ms": 0.093056
        }
      ]
    },
    {
      "trial": 3,
      "mount_seed": 20261010,
      "sigma_px": 0.5,
      "outlier_fraction": 0.0,
      "method": "epnp",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": 3.7218766594643142,
          "pitch_deg": -3.753858945725848,
          "along_body_m": -0.1411180073315493,
          "center_vehicle_m": [
            2.36,
            -0.1411180073315493,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": -2.086634904874484,
          "pitch_deg": 1.9015380634696841,
          "along_body_m": -0.11902165000629064,
          "center_vehicle_m": [
            0.23097834999370934,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": -1.4807751204918231,
          "pitch_deg": -1.9646042935235624,
          "along_body_m": -0.04829021155331188,
          "center_vehicle_m": [
            -2.36,
            -0.04829021155331188,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": -0.44574040807293613,
          "pitch_deg": -1.134533964007943,
          "along_body_m": -0.028006283940675628,
          "center_vehicle_m": [
            0.32199371605932436,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.693323256812128,
          "validation_p95_px": 10.695352653693268,
          "rotation_error_deg": 5.285722943856591,
          "center_error_m": 0.14111800733154925
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.0329767369677185,
          "validation_p95_px": 8.846406381832978,
          "rotation_error_deg": 2.823028970799203,
          "center_error_m": 0.11902165000629067
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 4.456635913722232,
          "validation_p95_px": 5.10064801865688,
          "rotation_error_deg": 2.4601118160678097,
          "center_error_m": 0.04829021155331187
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 2.214035850943054,
          "validation_p95_px": 2.591728340862886,
          "rotation_error_deg": 1.2189525564664423,
          "center_error_m": 0.028006283940675614
        }
      ],
      "success": true,
      "observations_sha256": "22c8f7659965e448ba0f8bf30864a66fd1f284e1336859bed0ddba71c485b140",
      "cameras": [
        {
          "camera_id": 0,
          "invalid_validation_count": 0,
          "validation_rmse_px": 2.701959636070639,
          "validation_p95_px": 6.3516471134757895,
          "rotation_error_deg": 0.7843668688567992,
          "center_error_m": 0.2020414666850007,
          "inliers": 120,
          "fit_ms": 0.234013
        },
        {
          "camera_id": 1,
          "invalid_validation_count": 0,
          "validation_rmse_px": 2.3151651482517317,
          "validation_p95_px": 4.751291898748422,
          "rotation_error_deg": 0.5281866418030028,
          "center_error_m": 0.1922952943916271,
          "inliers": 120,
          "fit_ms": 0.075283
        },
        {
          "camera_id": 2,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.2524368615086288,
          "validation_p95_px": 0.5006221075501774,
          "rotation_error_deg": 0.15149380315917937,
          "center_error_m": 0.016640319249414837,
          "inliers": 120,
          "fit_ms": 0.077747
        },
        {
          "camera_id": 3,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.898012073108702,
          "validation_p95_px": 0.8559567370187415,
          "rotation_error_deg": 0.5351941547628666,
          "center_error_m": 0.040915950823523124,
          "inliers": 120,
          "fit_ms": 0.048011
        }
      ]
    },
    {
      "trial": 3,
      "mount_seed": 20261010,
      "sigma_px": 0.5,
      "outlier_fraction": 0.0,
      "method": "sqpnp",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": 3.7218766594643142,
          "pitch_deg": -3.753858945725848,
          "along_body_m": -0.1411180073315493,
          "center_vehicle_m": [
            2.36,
            -0.1411180073315493,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": -2.086634904874484,
          "pitch_deg": 1.9015380634696841,
          "along_body_m": -0.11902165000629064,
          "center_vehicle_m": [
            0.23097834999370934,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": -1.4807751204918231,
          "pitch_deg": -1.9646042935235624,
          "along_body_m": -0.04829021155331188,
          "center_vehicle_m": [
            -2.36,
            -0.04829021155331188,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": -0.44574040807293613,
          "pitch_deg": -1.134533964007943,
          "along_body_m": -0.028006283940675628,
          "center_vehicle_m": [
            0.32199371605932436,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.693323256812128,
          "validation_p95_px": 10.695352653693268,
          "rotation_error_deg": 5.285722943856591,
          "center_error_m": 0.14111800733154925
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.0329767369677185,
          "validation_p95_px": 8.846406381832978,
          "rotation_error_deg": 2.823028970799203,
          "center_error_m": 0.11902165000629067
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 4.456635913722232,
          "validation_p95_px": 5.10064801865688,
          "rotation_error_deg": 2.4601118160678097,
          "center_error_m": 0.04829021155331187
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 2.214035850943054,
          "validation_p95_px": 2.591728340862886,
          "rotation_error_deg": 1.2189525564664423,
          "center_error_m": 0.028006283940675614
        }
      ],
      "success": true,
      "observations_sha256": "22c8f7659965e448ba0f8bf30864a66fd1f284e1336859bed0ddba71c485b140",
      "cameras": [
        {
          "camera_id": 0,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.14065801265518107,
          "validation_p95_px": 0.2724218486241627,
          "rotation_error_deg": 0.06000486068003653,
          "center_error_m": 0.0140373619379094,
          "inliers": 120,
          "fit_ms": 0.175462
        },
        {
          "camera_id": 1,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.20472189330880733,
          "validation_p95_px": 0.35902079706643425,
          "rotation_error_deg": 0.15479424140864598,
          "center_error_m": 0.023328668162668802,
          "inliers": 120,
          "fit_ms": 0.032732
        },
        {
          "camera_id": 2,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.14180902984537586,
          "validation_p95_px": 0.33444942166962793,
          "rotation_error_deg": 0.062129248129306876,
          "center_error_m": 0.0090748367324984,
          "inliers": 120,
          "fit_ms": 0.026972
        },
        {
          "camera_id": 3,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.44398293464066235,
          "validation_p95_px": 0.6153834601998796,
          "rotation_error_deg": 0.07845433806440215,
          "center_error_m": 0.019425974527727145,
          "inliers": 120,
          "fit_ms": 0.024927
        }
      ]
    },
    {
      "trial": 3,
      "mount_seed": 20261010,
      "sigma_px": 0.5,
      "outlier_fraction": 0.0,
      "method": "ransac_epnp_lm",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": 3.7218766594643142,
          "pitch_deg": -3.753858945725848,
          "along_body_m": -0.1411180073315493,
          "center_vehicle_m": [
            2.36,
            -0.1411180073315493,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": -2.086634904874484,
          "pitch_deg": 1.9015380634696841,
          "along_body_m": -0.11902165000629064,
          "center_vehicle_m": [
            0.23097834999370934,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": -1.4807751204918231,
          "pitch_deg": -1.9646042935235624,
          "along_body_m": -0.04829021155331188,
          "center_vehicle_m": [
            -2.36,
            -0.04829021155331188,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": -0.44574040807293613,
          "pitch_deg": -1.134533964007943,
          "along_body_m": -0.028006283940675628,
          "center_vehicle_m": [
            0.32199371605932436,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.693323256812128,
          "validation_p95_px": 10.695352653693268,
          "rotation_error_deg": 5.285722943856591,
          "center_error_m": 0.14111800733154925
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.0329767369677185,
          "validation_p95_px": 8.846406381832978,
          "rotation_error_deg": 2.823028970799203,
          "center_error_m": 0.11902165000629067
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 4.456635913722232,
          "validation_p95_px": 5.10064801865688,
          "rotation_error_deg": 2.4601118160678097,
          "center_error_m": 0.04829021155331187
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 2.214035850943054,
          "validation_p95_px": 2.591728340862886,
          "rotation_error_deg": 1.2189525564664423,
          "center_error_m": 0.028006283940675614
        }
      ],
      "success": true,
      "observations_sha256": "22c8f7659965e448ba0f8bf30864a66fd1f284e1336859bed0ddba71c485b140",
      "cameras": [
        {
          "camera_id": 0,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.11358744517576465,
          "validation_p95_px": 0.24477957592481497,
          "rotation_error_deg": 0.06909087730827948,
          "center_error_m": 0.010785546078105557,
          "inliers": 105,
          "fit_ms": 0.794543
        },
        {
          "camera_id": 1,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.15557384277632635,
          "validation_p95_px": 0.19226423984450838,
          "rotation_error_deg": 0.11319221451719931,
          "center_error_m": 0.011578209191334824,
          "inliers": 103,
          "fit_ms": 0.426818
        },
        {
          "camera_id": 2,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.2622538866612952,
          "validation_p95_px": 0.6342119197988336,
          "rotation_error_deg": 0.06783917525333476,
          "center_error_m": 0.018075982565679716,
          "inliers": 106,
          "fit_ms": 0.354711
        },
        {
          "camera_id": 3,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.161058827287046,
          "validation_p95_px": 0.2602401311818512,
          "rotation_error_deg": 0.0762612044680875,
          "center_error_m": 0.008117150262661472,
          "inliers": 105,
          "fit_ms": 0.395829
        }
      ]
    },
    {
      "trial": 3,
      "mount_seed": 20261010,
      "sigma_px": 0.5,
      "outlier_fraction": 0.1,
      "method": "iterative",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": 3.7218766594643142,
          "pitch_deg": -3.753858945725848,
          "along_body_m": -0.1411180073315493,
          "center_vehicle_m": [
            2.36,
            -0.1411180073315493,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": -2.086634904874484,
          "pitch_deg": 1.9015380634696841,
          "along_body_m": -0.11902165000629064,
          "center_vehicle_m": [
            0.23097834999370934,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": -1.4807751204918231,
          "pitch_deg": -1.9646042935235624,
          "along_body_m": -0.04829021155331188,
          "center_vehicle_m": [
            -2.36,
            -0.04829021155331188,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": -0.44574040807293613,
          "pitch_deg": -1.134533964007943,
          "along_body_m": -0.028006283940675628,
          "center_vehicle_m": [
            0.32199371605932436,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.693323256812128,
          "validation_p95_px": 10.695352653693268,
          "rotation_error_deg": 5.285722943856591,
          "center_error_m": 0.14111800733154925
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.0329767369677185,
          "validation_p95_px": 8.846406381832978,
          "rotation_error_deg": 2.823028970799203,
          "center_error_m": 0.11902165000629067
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 4.456635913722232,
          "validation_p95_px": 5.10064801865688,
          "rotation_error_deg": 2.4601118160678097,
          "center_error_m": 0.04829021155331187
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 2.214035850943054,
          "validation_p95_px": 2.591728340862886,
          "rotation_error_deg": 1.2189525564664423,
          "center_error_m": 0.028006283940675614
        }
      ],
      "success": true,
      "observations_sha256": "65b82fc840ccd33dfec92387b06ac6f2ebcd0654ad7a71e136b983fb714ffc50",
      "cameras": [
        {
          "camera_id": 0,
          "invalid_validation_count": 0,
          "validation_rmse_px": 2.0280224239728057,
          "validation_p95_px": 2.9745535458807293,
          "rotation_error_deg": 1.686610737550637,
          "center_error_m": 0.08523866597765495,
          "inliers": 120,
          "fit_ms": 0.352717
        },
        {
          "camera_id": 1,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.9840940997063912,
          "validation_p95_px": 1.4431965096857255,
          "rotation_error_deg": 0.6515346151719208,
          "center_error_m": 0.059355711177031444,
          "inliers": 120,
          "fit_ms": 0.134795
        },
        {
          "camera_id": 2,
          "invalid_validation_count": 0,
          "validation_rmse_px": 1.5976507327735663,
          "validation_p95_px": 3.866542394008161,
          "rotation_error_deg": 0.9660827763259144,
          "center_error_m": 0.11875779873190845,
          "inliers": 120,
          "fit_ms": 0.126229
        },
        {
          "camera_id": 3,
          "invalid_validation_count": 0,
          "validation_rmse_px": 1.1024380565160454,
          "validation_p95_px": 1.844969863929494,
          "rotation_error_deg": 0.3910208003345126,
          "center_error_m": 0.04362496687424288,
          "inliers": 120,
          "fit_ms": 0.12152
        }
      ]
    },
    {
      "trial": 3,
      "mount_seed": 20261010,
      "sigma_px": 0.5,
      "outlier_fraction": 0.1,
      "method": "epnp",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": 3.7218766594643142,
          "pitch_deg": -3.753858945725848,
          "along_body_m": -0.1411180073315493,
          "center_vehicle_m": [
            2.36,
            -0.1411180073315493,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": -2.086634904874484,
          "pitch_deg": 1.9015380634696841,
          "along_body_m": -0.11902165000629064,
          "center_vehicle_m": [
            0.23097834999370934,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": -1.4807751204918231,
          "pitch_deg": -1.9646042935235624,
          "along_body_m": -0.04829021155331188,
          "center_vehicle_m": [
            -2.36,
            -0.04829021155331188,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": -0.44574040807293613,
          "pitch_deg": -1.134533964007943,
          "along_body_m": -0.028006283940675628,
          "center_vehicle_m": [
            0.32199371605932436,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.693323256812128,
          "validation_p95_px": 10.695352653693268,
          "rotation_error_deg": 5.285722943856591,
          "center_error_m": 0.14111800733154925
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.0329767369677185,
          "validation_p95_px": 8.846406381832978,
          "rotation_error_deg": 2.823028970799203,
          "center_error_m": 0.11902165000629067
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 4.456635913722232,
          "validation_p95_px": 5.10064801865688,
          "rotation_error_deg": 2.4601118160678097,
          "center_error_m": 0.04829021155331187
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 2.214035850943054,
          "validation_p95_px": 2.591728340862886,
          "rotation_error_deg": 1.2189525564664423,
          "center_error_m": 0.028006283940675614
        }
      ],
      "success": true,
      "observations_sha256": "65b82fc840ccd33dfec92387b06ac6f2ebcd0654ad7a71e136b983fb714ffc50",
      "cameras": [
        {
          "camera_id": 0,
          "invalid_validation_count": 0,
          "validation_rmse_px": 12.030872709920278,
          "validation_p95_px": 29.68462228817479,
          "rotation_error_deg": 5.2269340775859945,
          "center_error_m": 1.0404238720238224,
          "inliers": 120,
          "fit_ms": 0.212773
        },
        {
          "camera_id": 1,
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.297227173394896,
          "validation_p95_px": 20.513747678765746,
          "rotation_error_deg": 2.6432938931039662,
          "center_error_m": 0.9518755710933007,
          "inliers": 120,
          "fit_ms": 0.056217
        },
        {
          "camera_id": 2,
          "invalid_validation_count": 0,
          "validation_rmse_px": 3.883708996799958,
          "validation_p95_px": 4.885310344100288,
          "rotation_error_deg": 1.8558058898580188,
          "center_error_m": 0.1374618338059291,
          "inliers": 120,
          "fit_ms": 0.050425
        },
        {
          "camera_id": 3,
          "invalid_validation_count": 0,
          "validation_rmse_px": 8.192333489630201,
          "validation_p95_px": 11.50961558158267,
          "rotation_error_deg": 0.5451336371286604,
          "center_error_m": 0.3359952946245522,
          "inliers": 120,
          "fit_ms": 0.046488
        }
      ]
    },
    {
      "trial": 3,
      "mount_seed": 20261010,
      "sigma_px": 0.5,
      "outlier_fraction": 0.1,
      "method": "sqpnp",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": 3.7218766594643142,
          "pitch_deg": -3.753858945725848,
          "along_body_m": -0.1411180073315493,
          "center_vehicle_m": [
            2.36,
            -0.1411180073315493,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": -2.086634904874484,
          "pitch_deg": 1.9015380634696841,
          "along_body_m": -0.11902165000629064,
          "center_vehicle_m": [
            0.23097834999370934,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": -1.4807751204918231,
          "pitch_deg": -1.9646042935235624,
          "along_body_m": -0.04829021155331188,
          "center_vehicle_m": [
            -2.36,
            -0.04829021155331188,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": -0.44574040807293613,
          "pitch_deg": -1.134533964007943,
          "along_body_m": -0.028006283940675628,
          "center_vehicle_m": [
            0.32199371605932436,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.693323256812128,
          "validation_p95_px": 10.695352653693268,
          "rotation_error_deg": 5.285722943856591,
          "center_error_m": 0.14111800733154925
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.0329767369677185,
          "validation_p95_px": 8.846406381832978,
          "rotation_error_deg": 2.823028970799203,
          "center_error_m": 0.11902165000629067
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 4.456635913722232,
          "validation_p95_px": 5.10064801865688,
          "rotation_error_deg": 2.4601118160678097,
          "center_error_m": 0.04829021155331187
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 2.214035850943054,
          "validation_p95_px": 2.591728340862886,
          "rotation_error_deg": 1.2189525564664423,
          "center_error_m": 0.028006283940675614
        }
      ],
      "success": true,
      "observations_sha256": "65b82fc840ccd33dfec92387b06ac6f2ebcd0654ad7a71e136b983fb714ffc50",
      "cameras": [
        {
          "camera_id": 0,
          "invalid_validation_count": 0,
          "validation_rmse_px": 4.14401572598908,
          "validation_p95_px": 7.021578689258258,
          "rotation_error_deg": 2.503233380521729,
          "center_error_m": 0.36118539427292234,
          "inliers": 120,
          "fit_ms": 0.186783
        },
        {
          "camera_id": 1,
          "invalid_validation_count": 0,
          "validation_rmse_px": 2.0823093128617867,
          "validation_p95_px": 3.976830852331903,
          "rotation_error_deg": 0.9253184417000315,
          "center_error_m": 0.19073871390612784,
          "inliers": 120,
          "fit_ms": 0.05289
        },
        {
          "camera_id": 2,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.9639312101250485,
          "validation_p95_px": 1.7111038084017673,
          "rotation_error_deg": 0.25742841250966025,
          "center_error_m": 0.06171445660504617,
          "inliers": 120,
          "fit_ms": 0.027913
        },
        {
          "camera_id": 3,
          "invalid_validation_count": 0,
          "validation_rmse_px": 1.9853408997386774,
          "validation_p95_px": 2.747885478163776,
          "rotation_error_deg": 0.2591910893002742,
          "center_error_m": 0.07287809226887093,
          "inliers": 120,
          "fit_ms": 0.025749
        }
      ]
    },
    {
      "trial": 3,
      "mount_seed": 20261010,
      "sigma_px": 0.5,
      "outlier_fraction": 0.1,
      "method": "ransac_epnp_lm",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": 3.7218766594643142,
          "pitch_deg": -3.753858945725848,
          "along_body_m": -0.1411180073315493,
          "center_vehicle_m": [
            2.36,
            -0.1411180073315493,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": -2.086634904874484,
          "pitch_deg": 1.9015380634696841,
          "along_body_m": -0.11902165000629064,
          "center_vehicle_m": [
            0.23097834999370934,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": -1.4807751204918231,
          "pitch_deg": -1.9646042935235624,
          "along_body_m": -0.04829021155331188,
          "center_vehicle_m": [
            -2.36,
            -0.04829021155331188,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": -0.44574040807293613,
          "pitch_deg": -1.134533964007943,
          "along_body_m": -0.028006283940675628,
          "center_vehicle_m": [
            0.32199371605932436,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.693323256812128,
          "validation_p95_px": 10.695352653693268,
          "rotation_error_deg": 5.285722943856591,
          "center_error_m": 0.14111800733154925
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.0329767369677185,
          "validation_p95_px": 8.846406381832978,
          "rotation_error_deg": 2.823028970799203,
          "center_error_m": 0.11902165000629067
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 4.456635913722232,
          "validation_p95_px": 5.10064801865688,
          "rotation_error_deg": 2.4601118160678097,
          "center_error_m": 0.04829021155331187
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 2.214035850943054,
          "validation_p95_px": 2.591728340862886,
          "rotation_error_deg": 1.2189525564664423,
          "center_error_m": 0.028006283940675614
        }
      ],
      "success": true,
      "observations_sha256": "65b82fc840ccd33dfec92387b06ac6f2ebcd0654ad7a71e136b983fb714ffc50",
      "cameras": [
        {
          "camera_id": 0,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.3220114263498365,
          "validation_p95_px": 0.6738468385058091,
          "rotation_error_deg": 0.1307792052275423,
          "center_error_m": 0.031870567781519844,
          "inliers": 93,
          "fit_ms": 0.941311
        },
        {
          "camera_id": 1,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.21703216068087108,
          "validation_p95_px": 0.3212849369884838,
          "rotation_error_deg": 0.14550967021111824,
          "center_error_m": 0.0115719468360575,
          "inliers": 95,
          "fit_ms": 0.617719
        },
        {
          "camera_id": 2,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.19035207529835801,
          "validation_p95_px": 0.41857617946673686,
          "rotation_error_deg": 0.05318271666369258,
          "center_error_m": 0.015332760760862403,
          "inliers": 97,
          "fit_ms": 0.514634
        },
        {
          "camera_id": 3,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.4809289190922934,
          "validation_p95_px": 0.8017890113406867,
          "rotation_error_deg": 0.19080028205094632,
          "center_error_m": 0.025726845877291238,
          "inliers": 95,
          "fit_ms": 0.706687
        }
      ]
    },
    {
      "trial": 3,
      "mount_seed": 20261010,
      "sigma_px": 1.0,
      "outlier_fraction": 0.0,
      "method": "iterative",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": 3.7218766594643142,
          "pitch_deg": -3.753858945725848,
          "along_body_m": -0.1411180073315493,
          "center_vehicle_m": [
            2.36,
            -0.1411180073315493,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": -2.086634904874484,
          "pitch_deg": 1.9015380634696841,
          "along_body_m": -0.11902165000629064,
          "center_vehicle_m": [
            0.23097834999370934,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": -1.4807751204918231,
          "pitch_deg": -1.9646042935235624,
          "along_body_m": -0.04829021155331188,
          "center_vehicle_m": [
            -2.36,
            -0.04829021155331188,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": -0.44574040807293613,
          "pitch_deg": -1.134533964007943,
          "along_body_m": -0.028006283940675628,
          "center_vehicle_m": [
            0.32199371605932436,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.693323256812128,
          "validation_p95_px": 10.695352653693268,
          "rotation_error_deg": 5.285722943856591,
          "center_error_m": 0.14111800733154925
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.0329767369677185,
          "validation_p95_px": 8.846406381832978,
          "rotation_error_deg": 2.823028970799203,
          "center_error_m": 0.11902165000629067
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 4.456635913722232,
          "validation_p95_px": 5.10064801865688,
          "rotation_error_deg": 2.4601118160678097,
          "center_error_m": 0.04829021155331187
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 2.214035850943054,
          "validation_p95_px": 2.591728340862886,
          "rotation_error_deg": 1.2189525564664423,
          "center_error_m": 0.028006283940675614
        }
      ],
      "success": true,
      "observations_sha256": "bfb780f5ffcf6cafa7093741ba4d99d846b07e9ef5789c3e22f25b1c3a453833",
      "cameras": [
        {
          "camera_id": 0,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.35128481995951627,
          "validation_p95_px": 0.6033684771327533,
          "rotation_error_deg": 0.15267295806260636,
          "center_error_m": 0.03250473750792464,
          "inliers": 120,
          "fit_ms": 0.386942
        },
        {
          "camera_id": 1,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.29297857853778037,
          "validation_p95_px": 0.5170321699803379,
          "rotation_error_deg": 0.11815838858608134,
          "center_error_m": 0.018048721137020447,
          "inliers": 120,
          "fit_ms": 0.139624
        },
        {
          "camera_id": 2,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.3445160567174494,
          "validation_p95_px": 0.46128261353707894,
          "rotation_error_deg": 0.25523784432870217,
          "center_error_m": 0.011688999348108004,
          "inliers": 120,
          "fit_ms": 0.12631
        },
        {
          "camera_id": 3,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.263381381787495,
          "validation_p95_px": 0.35309725411949006,
          "rotation_error_deg": 0.12956962871886746,
          "center_error_m": 0.006964603184809357,
          "inliers": 120,
          "fit_ms": 0.087987
        }
      ]
    },
    {
      "trial": 3,
      "mount_seed": 20261010,
      "sigma_px": 1.0,
      "outlier_fraction": 0.0,
      "method": "epnp",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": 3.7218766594643142,
          "pitch_deg": -3.753858945725848,
          "along_body_m": -0.1411180073315493,
          "center_vehicle_m": [
            2.36,
            -0.1411180073315493,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": -2.086634904874484,
          "pitch_deg": 1.9015380634696841,
          "along_body_m": -0.11902165000629064,
          "center_vehicle_m": [
            0.23097834999370934,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": -1.4807751204918231,
          "pitch_deg": -1.9646042935235624,
          "along_body_m": -0.04829021155331188,
          "center_vehicle_m": [
            -2.36,
            -0.04829021155331188,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": -0.44574040807293613,
          "pitch_deg": -1.134533964007943,
          "along_body_m": -0.028006283940675628,
          "center_vehicle_m": [
            0.32199371605932436,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.693323256812128,
          "validation_p95_px": 10.695352653693268,
          "rotation_error_deg": 5.285722943856591,
          "center_error_m": 0.14111800733154925
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.0329767369677185,
          "validation_p95_px": 8.846406381832978,
          "rotation_error_deg": 2.823028970799203,
          "center_error_m": 0.11902165000629067
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 4.456635913722232,
          "validation_p95_px": 5.10064801865688,
          "rotation_error_deg": 2.4601118160678097,
          "center_error_m": 0.04829021155331187
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 2.214035850943054,
          "validation_p95_px": 2.591728340862886,
          "rotation_error_deg": 1.2189525564664423,
          "center_error_m": 0.028006283940675614
        }
      ],
      "success": true,
      "observations_sha256": "bfb780f5ffcf6cafa7093741ba4d99d846b07e9ef5789c3e22f25b1c3a453833",
      "cameras": [
        {
          "camera_id": 0,
          "invalid_validation_count": 0,
          "validation_rmse_px": 2.049735743251556,
          "validation_p95_px": 4.362104618901635,
          "rotation_error_deg": 0.5222236911302027,
          "center_error_m": 0.15118874414994776,
          "inliers": 120,
          "fit_ms": 0.219526
        },
        {
          "camera_id": 1,
          "invalid_validation_count": 0,
          "validation_rmse_px": 1.80338305586354,
          "validation_p95_px": 4.029788543795072,
          "rotation_error_deg": 0.4966136271861517,
          "center_error_m": 0.17711097032609793,
          "inliers": 120,
          "fit_ms": 0.058671
        },
        {
          "camera_id": 2,
          "invalid_validation_count": 0,
          "validation_rmse_px": 1.381618044928353,
          "validation_p95_px": 1.7504242952050868,
          "rotation_error_deg": 0.9715652980275705,
          "center_error_m": 0.06654872456363146,
          "inliers": 120,
          "fit_ms": 0.060504
        },
        {
          "camera_id": 3,
          "invalid_validation_count": 0,
          "validation_rmse_px": 1.8792532864466283,
          "validation_p95_px": 2.115840032790148,
          "rotation_error_deg": 0.6410996837824071,
          "center_error_m": 0.08289869371756092,
          "inliers": 120,
          "fit_ms": 0.048211
        }
      ]
    },
    {
      "trial": 3,
      "mount_seed": 20261010,
      "sigma_px": 1.0,
      "outlier_fraction": 0.0,
      "method": "sqpnp",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": 3.7218766594643142,
          "pitch_deg": -3.753858945725848,
          "along_body_m": -0.1411180073315493,
          "center_vehicle_m": [
            2.36,
            -0.1411180073315493,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": -2.086634904874484,
          "pitch_deg": 1.9015380634696841,
          "along_body_m": -0.11902165000629064,
          "center_vehicle_m": [
            0.23097834999370934,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": -1.4807751204918231,
          "pitch_deg": -1.9646042935235624,
          "along_body_m": -0.04829021155331188,
          "center_vehicle_m": [
            -2.36,
            -0.04829021155331188,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": -0.44574040807293613,
          "pitch_deg": -1.134533964007943,
          "along_body_m": -0.028006283940675628,
          "center_vehicle_m": [
            0.32199371605932436,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.693323256812128,
          "validation_p95_px": 10.695352653693268,
          "rotation_error_deg": 5.285722943856591,
          "center_error_m": 0.14111800733154925
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.0329767369677185,
          "validation_p95_px": 8.846406381832978,
          "rotation_error_deg": 2.823028970799203,
          "center_error_m": 0.11902165000629067
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 4.456635913722232,
          "validation_p95_px": 5.10064801865688,
          "rotation_error_deg": 2.4601118160678097,
          "center_error_m": 0.04829021155331187
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 2.214035850943054,
          "validation_p95_px": 2.591728340862886,
          "rotation_error_deg": 1.2189525564664423,
          "center_error_m": 0.028006283940675614
        }
      ],
      "success": true,
      "observations_sha256": "bfb780f5ffcf6cafa7093741ba4d99d846b07e9ef5789c3e22f25b1c3a453833",
      "cameras": [
        {
          "camera_id": 0,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.34319016366019334,
          "validation_p95_px": 0.6276874430665773,
          "rotation_error_deg": 0.1831340861892832,
          "center_error_m": 0.03520413715135126,
          "inliers": 120,
          "fit_ms": 0.241507
        },
        {
          "camera_id": 1,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.3181626115768975,
          "validation_p95_px": 0.5023781392009246,
          "rotation_error_deg": 0.29159342632896745,
          "center_error_m": 0.030520677750336008,
          "inliers": 120,
          "fit_ms": 0.035538
        },
        {
          "camera_id": 2,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.9625156434867356,
          "validation_p95_px": 2.1712341252715515,
          "rotation_error_deg": 0.6055379129215354,
          "center_error_m": 0.06633487932300823,
          "inliers": 120,
          "fit_ms": 0.027382
        },
        {
          "camera_id": 3,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.21251592733438945,
          "validation_p95_px": 0.2955734739967691,
          "rotation_error_deg": 0.10760151039913934,
          "center_error_m": 0.009495852424553365,
          "inliers": 120,
          "fit_ms": 0.065995
        }
      ]
    },
    {
      "trial": 3,
      "mount_seed": 20261010,
      "sigma_px": 1.0,
      "outlier_fraction": 0.0,
      "method": "ransac_epnp_lm",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": 3.7218766594643142,
          "pitch_deg": -3.753858945725848,
          "along_body_m": -0.1411180073315493,
          "center_vehicle_m": [
            2.36,
            -0.1411180073315493,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": -2.086634904874484,
          "pitch_deg": 1.9015380634696841,
          "along_body_m": -0.11902165000629064,
          "center_vehicle_m": [
            0.23097834999370934,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": -1.4807751204918231,
          "pitch_deg": -1.9646042935235624,
          "along_body_m": -0.04829021155331188,
          "center_vehicle_m": [
            -2.36,
            -0.04829021155331188,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": -0.44574040807293613,
          "pitch_deg": -1.134533964007943,
          "along_body_m": -0.028006283940675628,
          "center_vehicle_m": [
            0.32199371605932436,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.693323256812128,
          "validation_p95_px": 10.695352653693268,
          "rotation_error_deg": 5.285722943856591,
          "center_error_m": 0.14111800733154925
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.0329767369677185,
          "validation_p95_px": 8.846406381832978,
          "rotation_error_deg": 2.823028970799203,
          "center_error_m": 0.11902165000629067
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 4.456635913722232,
          "validation_p95_px": 5.10064801865688,
          "rotation_error_deg": 2.4601118160678097,
          "center_error_m": 0.04829021155331187
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 2.214035850943054,
          "validation_p95_px": 2.591728340862886,
          "rotation_error_deg": 1.2189525564664423,
          "center_error_m": 0.028006283940675614
        }
      ],
      "success": true,
      "observations_sha256": "bfb780f5ffcf6cafa7093741ba4d99d846b07e9ef5789c3e22f25b1c3a453833",
      "cameras": [
        {
          "camera_id": 0,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.6744132767949154,
          "validation_p95_px": 1.1370214299468626,
          "rotation_error_deg": 0.2849247661809424,
          "center_error_m": 0.04176846095322702,
          "inliers": 70,
          "fit_ms": 2.985651
        },
        {
          "camera_id": 1,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.3685216508354266,
          "validation_p95_px": 0.48901551836918705,
          "rotation_error_deg": 0.2956037849359705,
          "center_error_m": 0.02625928697361429,
          "inliers": 81,
          "fit_ms": 1.302285
        },
        {
          "camera_id": 2,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.4886270196080758,
          "validation_p95_px": 0.7081475671867569,
          "rotation_error_deg": 0.2234464504768199,
          "center_error_m": 0.01636235546294016,
          "inliers": 85,
          "fit_ms": 0.988822
        },
        {
          "camera_id": 3,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.8141190355972804,
          "validation_p95_px": 0.9499371470227641,
          "rotation_error_deg": 0.34626506144782904,
          "center_error_m": 0.034913359173701995,
          "inliers": 78,
          "fit_ms": 1.545916
        }
      ]
    },
    {
      "trial": 3,
      "mount_seed": 20261010,
      "sigma_px": 1.0,
      "outlier_fraction": 0.1,
      "method": "iterative",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": 3.7218766594643142,
          "pitch_deg": -3.753858945725848,
          "along_body_m": -0.1411180073315493,
          "center_vehicle_m": [
            2.36,
            -0.1411180073315493,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": -2.086634904874484,
          "pitch_deg": 1.9015380634696841,
          "along_body_m": -0.11902165000629064,
          "center_vehicle_m": [
            0.23097834999370934,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": -1.4807751204918231,
          "pitch_deg": -1.9646042935235624,
          "along_body_m": -0.04829021155331188,
          "center_vehicle_m": [
            -2.36,
            -0.04829021155331188,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": -0.44574040807293613,
          "pitch_deg": -1.134533964007943,
          "along_body_m": -0.028006283940675628,
          "center_vehicle_m": [
            0.32199371605932436,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.693323256812128,
          "validation_p95_px": 10.695352653693268,
          "rotation_error_deg": 5.285722943856591,
          "center_error_m": 0.14111800733154925
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.0329767369677185,
          "validation_p95_px": 8.846406381832978,
          "rotation_error_deg": 2.823028970799203,
          "center_error_m": 0.11902165000629067
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 4.456635913722232,
          "validation_p95_px": 5.10064801865688,
          "rotation_error_deg": 2.4601118160678097,
          "center_error_m": 0.04829021155331187
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 2.214035850943054,
          "validation_p95_px": 2.591728340862886,
          "rotation_error_deg": 1.2189525564664423,
          "center_error_m": 0.028006283940675614
        }
      ],
      "success": true,
      "observations_sha256": "2c74f059de84fba74657e587eb634d5e843ece9731ac860b94b286ddc98ac581",
      "cameras": [
        {
          "camera_id": 0,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.2956767181376609,
          "validation_p95_px": 0.3897809104282556,
          "rotation_error_deg": 0.24082787305264527,
          "center_error_m": 0.019644553724227964,
          "inliers": 120,
          "fit_ms": 0.361044
        },
        {
          "camera_id": 1,
          "invalid_validation_count": 0,
          "validation_rmse_px": 4.365079283008352,
          "validation_p95_px": 5.535822245335202,
          "rotation_error_deg": 2.8605228377204455,
          "center_error_m": 0.21989350384789627,
          "inliers": 120,
          "fit_ms": 0.177636
        },
        {
          "camera_id": 2,
          "invalid_validation_count": 0,
          "validation_rmse_px": 1.0072347088187368,
          "validation_p95_px": 1.4266233793472032,
          "rotation_error_deg": 0.6683633480743594,
          "center_error_m": 0.04750604709130268,
          "inliers": 120,
          "fit_ms": 0.128493
        },
        {
          "camera_id": 3,
          "invalid_validation_count": 0,
          "validation_rmse_px": 5.7692175796443115,
          "validation_p95_px": 8.694384187740752,
          "rotation_error_deg": 1.9083474535301934,
          "center_error_m": 0.21825985452279878,
          "inliers": 120,
          "fit_ms": 0.284899
        }
      ]
    },
    {
      "trial": 3,
      "mount_seed": 20261010,
      "sigma_px": 1.0,
      "outlier_fraction": 0.1,
      "method": "epnp",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": 3.7218766594643142,
          "pitch_deg": -3.753858945725848,
          "along_body_m": -0.1411180073315493,
          "center_vehicle_m": [
            2.36,
            -0.1411180073315493,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": -2.086634904874484,
          "pitch_deg": 1.9015380634696841,
          "along_body_m": -0.11902165000629064,
          "center_vehicle_m": [
            0.23097834999370934,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": -1.4807751204918231,
          "pitch_deg": -1.9646042935235624,
          "along_body_m": -0.04829021155331188,
          "center_vehicle_m": [
            -2.36,
            -0.04829021155331188,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": -0.44574040807293613,
          "pitch_deg": -1.134533964007943,
          "along_body_m": -0.028006283940675628,
          "center_vehicle_m": [
            0.32199371605932436,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.693323256812128,
          "validation_p95_px": 10.695352653693268,
          "rotation_error_deg": 5.285722943856591,
          "center_error_m": 0.14111800733154925
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.0329767369677185,
          "validation_p95_px": 8.846406381832978,
          "rotation_error_deg": 2.823028970799203,
          "center_error_m": 0.11902165000629067
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 4.456635913722232,
          "validation_p95_px": 5.10064801865688,
          "rotation_error_deg": 2.4601118160678097,
          "center_error_m": 0.04829021155331187
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 2.214035850943054,
          "validation_p95_px": 2.591728340862886,
          "rotation_error_deg": 1.2189525564664423,
          "center_error_m": 0.028006283940675614
        }
      ],
      "success": true,
      "observations_sha256": "2c74f059de84fba74657e587eb634d5e843ece9731ac860b94b286ddc98ac581",
      "cameras": [
        {
          "camera_id": 0,
          "invalid_validation_count": 0,
          "validation_rmse_px": 5.6961041430181245,
          "validation_p95_px": 13.92051010136478,
          "rotation_error_deg": 0.8695412799539752,
          "center_error_m": 0.3809800914018326,
          "inliers": 120,
          "fit_ms": 0.212883
        },
        {
          "camera_id": 1,
          "invalid_validation_count": 0,
          "validation_rmse_px": 61.271277196757694,
          "validation_p95_px": 120.61327128073458,
          "rotation_error_deg": 15.539616387515359,
          "center_error_m": 4.9250442183250955,
          "inliers": 120,
          "fit_ms": 0.055324
        },
        {
          "camera_id": 2,
          "invalid_validation_count": 0,
          "validation_rmse_px": 10.24102901136149,
          "validation_p95_px": 13.089595156849429,
          "rotation_error_deg": 4.543097784948581,
          "center_error_m": 0.23809007835881954,
          "inliers": 120,
          "fit_ms": 0.049544
        },
        {
          "camera_id": 3,
          "invalid_validation_count": 0,
          "validation_rmse_px": 20.163351682843956,
          "validation_p95_px": 24.956840365009885,
          "rotation_error_deg": 3.2600703342004227,
          "center_error_m": 0.727190947253293,
          "inliers": 120,
          "fit_ms": 0.047179
        }
      ]
    },
    {
      "trial": 3,
      "mount_seed": 20261010,
      "sigma_px": 1.0,
      "outlier_fraction": 0.1,
      "method": "sqpnp",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": 3.7218766594643142,
          "pitch_deg": -3.753858945725848,
          "along_body_m": -0.1411180073315493,
          "center_vehicle_m": [
            2.36,
            -0.1411180073315493,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": -2.086634904874484,
          "pitch_deg": 1.9015380634696841,
          "along_body_m": -0.11902165000629064,
          "center_vehicle_m": [
            0.23097834999370934,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": -1.4807751204918231,
          "pitch_deg": -1.9646042935235624,
          "along_body_m": -0.04829021155331188,
          "center_vehicle_m": [
            -2.36,
            -0.04829021155331188,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": -0.44574040807293613,
          "pitch_deg": -1.134533964007943,
          "along_body_m": -0.028006283940675628,
          "center_vehicle_m": [
            0.32199371605932436,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.693323256812128,
          "validation_p95_px": 10.695352653693268,
          "rotation_error_deg": 5.285722943856591,
          "center_error_m": 0.14111800733154925
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.0329767369677185,
          "validation_p95_px": 8.846406381832978,
          "rotation_error_deg": 2.823028970799203,
          "center_error_m": 0.11902165000629067
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 4.456635913722232,
          "validation_p95_px": 5.10064801865688,
          "rotation_error_deg": 2.4601118160678097,
          "center_error_m": 0.04829021155331187
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 2.214035850943054,
          "validation_p95_px": 2.591728340862886,
          "rotation_error_deg": 1.2189525564664423,
          "center_error_m": 0.028006283940675614
        }
      ],
      "success": true,
      "observations_sha256": "2c74f059de84fba74657e587eb634d5e843ece9731ac860b94b286ddc98ac581",
      "cameras": [
        {
          "camera_id": 0,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.7255799297808453,
          "validation_p95_px": 1.3492592344347474,
          "rotation_error_deg": 0.5091839723142887,
          "center_error_m": 0.0575375532547225,
          "inliers": 120,
          "fit_ms": 0.178448
        },
        {
          "camera_id": 1,
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.566035962352711,
          "validation_p95_px": 15.412801829405074,
          "rotation_error_deg": 2.635085111813601,
          "center_error_m": 0.7652053071421474,
          "inliers": 120,
          "fit_ms": 0.052198
        },
        {
          "camera_id": 2,
          "invalid_validation_count": 0,
          "validation_rmse_px": 3.110959731558314,
          "validation_p95_px": 6.386404606432844,
          "rotation_error_deg": 0.7386289352403921,
          "center_error_m": 0.20973496877859515,
          "inliers": 120,
          "fit_ms": 0.027512
        },
        {
          "camera_id": 3,
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.077809290962033,
          "validation_p95_px": 11.864521393466598,
          "rotation_error_deg": 0.7117538778174688,
          "center_error_m": 0.4198467401750155,
          "inliers": 120,
          "fit_ms": 0.027642
        }
      ]
    },
    {
      "trial": 3,
      "mount_seed": 20261010,
      "sigma_px": 1.0,
      "outlier_fraction": 0.1,
      "method": "ransac_epnp_lm",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": 3.7218766594643142,
          "pitch_deg": -3.753858945725848,
          "along_body_m": -0.1411180073315493,
          "center_vehicle_m": [
            2.36,
            -0.1411180073315493,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": -2.086634904874484,
          "pitch_deg": 1.9015380634696841,
          "along_body_m": -0.11902165000629064,
          "center_vehicle_m": [
            0.23097834999370934,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": -1.4807751204918231,
          "pitch_deg": -1.9646042935235624,
          "along_body_m": -0.04829021155331188,
          "center_vehicle_m": [
            -2.36,
            -0.04829021155331188,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": -0.44574040807293613,
          "pitch_deg": -1.134533964007943,
          "along_body_m": -0.028006283940675628,
          "center_vehicle_m": [
            0.32199371605932436,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.693323256812128,
          "validation_p95_px": 10.695352653693268,
          "rotation_error_deg": 5.285722943856591,
          "center_error_m": 0.14111800733154925
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.0329767369677185,
          "validation_p95_px": 8.846406381832978,
          "rotation_error_deg": 2.823028970799203,
          "center_error_m": 0.11902165000629067
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 4.456635913722232,
          "validation_p95_px": 5.10064801865688,
          "rotation_error_deg": 2.4601118160678097,
          "center_error_m": 0.04829021155331187
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 2.214035850943054,
          "validation_p95_px": 2.591728340862886,
          "rotation_error_deg": 1.2189525564664423,
          "center_error_m": 0.028006283940675614
        }
      ],
      "success": true,
      "observations_sha256": "2c74f059de84fba74657e587eb634d5e843ece9731ac860b94b286ddc98ac581",
      "cameras": [
        {
          "camera_id": 0,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.371431641860472,
          "validation_p95_px": 0.5689713925930329,
          "rotation_error_deg": 0.2585722027346755,
          "center_error_m": 0.02740918982242478,
          "inliers": 69,
          "fit_ms": 3.010197
        },
        {
          "camera_id": 1,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.6219113610690796,
          "validation_p95_px": 1.2400738686835802,
          "rotation_error_deg": 0.3168889010245089,
          "center_error_m": 0.06461241651348391,
          "inliers": 81,
          "fit_ms": 1.333725
        },
        {
          "camera_id": 2,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.7017609510944917,
          "validation_p95_px": 1.1186999474279937,
          "rotation_error_deg": 0.3100365349721706,
          "center_error_m": 0.026039478473547754,
          "inliers": 67,
          "fit_ms": 3.292451
        },
        {
          "camera_id": 3,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.4336510177784954,
          "validation_p95_px": 0.4957566692093815,
          "rotation_error_deg": 0.2666095215021134,
          "center_error_m": 0.013216684069528414,
          "inliers": 76,
          "fit_ms": 1.714164
        }
      ]
    },
    {
      "trial": 4,
      "mount_seed": 20261011,
      "sigma_px": 0.0,
      "outlier_fraction": 0.0,
      "method": "iterative",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": -0.7485131926940056,
          "pitch_deg": -3.8149387175399028,
          "along_body_m": -0.053820486667521414,
          "center_vehicle_m": [
            2.36,
            -0.053820486667521414,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": 2.257788899322877,
          "pitch_deg": -0.46677172659118593,
          "along_body_m": -0.11214938007803424,
          "center_vehicle_m": [
            0.23785061992196574,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": 4.373705802630779,
          "pitch_deg": 2.5426688055305844,
          "along_body_m": 0.054640099912275364,
          "center_vehicle_m": [
            -2.36,
            0.054640099912275364,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": 1.4452945144608176,
          "pitch_deg": -3.5454072400025485,
          "along_body_m": 0.05113499442617225,
          "center_vehicle_m": [
            0.40113499442617223,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.141429724369046,
          "validation_p95_px": 8.779539340019946,
          "rotation_error_deg": 3.8876499859434057,
          "center_error_m": 0.053820486667521414
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 3.9024676610153626,
          "validation_p95_px": 4.673160198155526,
          "rotation_error_deg": 2.3055277843421194,
          "center_error_m": 0.11214938007803421
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 10.234454194289702,
          "validation_p95_px": 10.809009804181652,
          "rotation_error_deg": 5.058787132746043,
          "center_error_m": 0.054640099912275336
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.278672321477505,
          "validation_p95_px": 8.339288689612562,
          "rotation_error_deg": 3.828592711859514,
          "center_error_m": 0.05113499442617231
        }
      ],
      "success": true,
      "observations_sha256": "a6616909f9738eb98b11b7ed89e94599cbeaac576e70195183e09b8634fa239d",
      "cameras": [
        {
          "camera_id": 0,
          "invalid_validation_count": 0,
          "validation_rmse_px": 3.003341549098996e-06,
          "validation_p95_px": 3.2991940045220916e-06,
          "rotation_error_deg": 0.0,
          "center_error_m": 2.2060933962047174e-08,
          "inliers": 120,
          "fit_ms": 0.351275
        },
        {
          "camera_id": 1,
          "invalid_validation_count": 0,
          "validation_rmse_px": 2.317459918645608e-06,
          "validation_p95_px": 3.215980867263706e-06,
          "rotation_error_deg": 0.0,
          "center_error_m": 3.331909247552863e-08,
          "inliers": 120,
          "fit_ms": 0.136438
        },
        {
          "camera_id": 2,
          "invalid_validation_count": 0,
          "validation_rmse_px": 1.549455660893007e-06,
          "validation_p95_px": 2.3437630146000904e-06,
          "rotation_error_deg": 0.0,
          "center_error_m": 5.801202909127684e-08,
          "inliers": 120,
          "fit_ms": 0.125457
        },
        {
          "camera_id": 3,
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.852424446643004e-07,
          "validation_p95_px": 1.2196408149769555e-06,
          "rotation_error_deg": 1.2074182697257333e-06,
          "center_error_m": 2.2331351664297268e-08,
          "inliers": 120,
          "fit_ms": 0.123274
        }
      ]
    },
    {
      "trial": 4,
      "mount_seed": 20261011,
      "sigma_px": 0.0,
      "outlier_fraction": 0.0,
      "method": "epnp",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": -0.7485131926940056,
          "pitch_deg": -3.8149387175399028,
          "along_body_m": -0.053820486667521414,
          "center_vehicle_m": [
            2.36,
            -0.053820486667521414,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": 2.257788899322877,
          "pitch_deg": -0.46677172659118593,
          "along_body_m": -0.11214938007803424,
          "center_vehicle_m": [
            0.23785061992196574,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": 4.373705802630779,
          "pitch_deg": 2.5426688055305844,
          "along_body_m": 0.054640099912275364,
          "center_vehicle_m": [
            -2.36,
            0.054640099912275364,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": 1.4452945144608176,
          "pitch_deg": -3.5454072400025485,
          "along_body_m": 0.05113499442617225,
          "center_vehicle_m": [
            0.40113499442617223,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.141429724369046,
          "validation_p95_px": 8.779539340019946,
          "rotation_error_deg": 3.8876499859434057,
          "center_error_m": 0.053820486667521414
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 3.9024676610153626,
          "validation_p95_px": 4.673160198155526,
          "rotation_error_deg": 2.3055277843421194,
          "center_error_m": 0.11214938007803421
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 10.234454194289702,
          "validation_p95_px": 10.809009804181652,
          "rotation_error_deg": 5.058787132746043,
          "center_error_m": 0.054640099912275336
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.278672321477505,
          "validation_p95_px": 8.339288689612562,
          "rotation_error_deg": 3.828592711859514,
          "center_error_m": 0.05113499442617231
        }
      ],
      "success": true,
      "observations_sha256": "a6616909f9738eb98b11b7ed89e94599cbeaac576e70195183e09b8634fa239d",
      "cameras": [
        {
          "camera_id": 0,
          "invalid_validation_count": 0,
          "validation_rmse_px": 1.0949507361713291e-12,
          "validation_p95_px": 1.7138632176690605e-12,
          "rotation_error_deg": 0.0,
          "center_error_m": 9.983443872367701e-14,
          "inliers": 120,
          "fit_ms": 0.21136
        },
        {
          "camera_id": 1,
          "invalid_validation_count": 0,
          "validation_rmse_px": 8.177109055404686e-13,
          "validation_p95_px": 1.2336133791080107e-12,
          "rotation_error_deg": 0.0,
          "center_error_m": 2.4856167880340933e-14,
          "inliers": 120,
          "fit_ms": 0.054473
        },
        {
          "camera_id": 2,
          "invalid_validation_count": 0,
          "validation_rmse_px": 8.579687071194105e-13,
          "validation_p95_px": 1.496099621103432e-12,
          "rotation_error_deg": 1.2074182697257333e-06,
          "center_error_m": 3.476196094478361e-14,
          "inliers": 120,
          "fit_ms": 0.049875
        },
        {
          "camera_id": 3,
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.613497043931263e-13,
          "validation_p95_px": 1.3015354111199803e-12,
          "rotation_error_deg": 0.0,
          "center_error_m": 3.8231025960008005e-14,
          "inliers": 120,
          "fit_ms": 0.048131
        }
      ]
    },
    {
      "trial": 4,
      "mount_seed": 20261011,
      "sigma_px": 0.0,
      "outlier_fraction": 0.0,
      "method": "sqpnp",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": -0.7485131926940056,
          "pitch_deg": -3.8149387175399028,
          "along_body_m": -0.053820486667521414,
          "center_vehicle_m": [
            2.36,
            -0.053820486667521414,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": 2.257788899322877,
          "pitch_deg": -0.46677172659118593,
          "along_body_m": -0.11214938007803424,
          "center_vehicle_m": [
            0.23785061992196574,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": 4.373705802630779,
          "pitch_deg": 2.5426688055305844,
          "along_body_m": 0.054640099912275364,
          "center_vehicle_m": [
            -2.36,
            0.054640099912275364,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": 1.4452945144608176,
          "pitch_deg": -3.5454072400025485,
          "along_body_m": 0.05113499442617225,
          "center_vehicle_m": [
            0.40113499442617223,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.141429724369046,
          "validation_p95_px": 8.779539340019946,
          "rotation_error_deg": 3.8876499859434057,
          "center_error_m": 0.053820486667521414
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 3.9024676610153626,
          "validation_p95_px": 4.673160198155526,
          "rotation_error_deg": 2.3055277843421194,
          "center_error_m": 0.11214938007803421
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 10.234454194289702,
          "validation_p95_px": 10.809009804181652,
          "rotation_error_deg": 5.058787132746043,
          "center_error_m": 0.054640099912275336
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.278672321477505,
          "validation_p95_px": 8.339288689612562,
          "rotation_error_deg": 3.828592711859514,
          "center_error_m": 0.05113499442617231
        }
      ],
      "success": true,
      "observations_sha256": "a6616909f9738eb98b11b7ed89e94599cbeaac576e70195183e09b8634fa239d",
      "cameras": [
        {
          "camera_id": 0,
          "invalid_validation_count": 0,
          "validation_rmse_px": 1.1582260705485638e-12,
          "validation_p95_px": 1.976061485266321e-12,
          "rotation_error_deg": 0.0,
          "center_error_m": 7.650413460764944e-14,
          "inliers": 120,
          "fit_ms": 0.20066
        },
        {
          "camera_id": 1,
          "invalid_validation_count": 0,
          "validation_rmse_px": 3.7612801922204964e-12,
          "validation_p95_px": 5.01362711869176e-12,
          "rotation_error_deg": 0.0,
          "center_error_m": 5.303515314174411e-14,
          "inliers": 120,
          "fit_ms": 0.0316
        },
        {
          "camera_id": 2,
          "invalid_validation_count": 0,
          "validation_rmse_px": 2.3413926734823165e-12,
          "validation_p95_px": 3.5471364010428753e-12,
          "rotation_error_deg": 0.0,
          "center_error_m": 7.959217048812222e-14,
          "inliers": 120,
          "fit_ms": 0.025037
        },
        {
          "camera_id": 3,
          "invalid_validation_count": 0,
          "validation_rmse_px": 5.887422933369118e-13,
          "validation_p95_px": 8.880361617906904e-13,
          "rotation_error_deg": 0.0,
          "center_error_m": 5.124999207551095e-14,
          "inliers": 120,
          "fit_ms": 0.023845
        }
      ]
    },
    {
      "trial": 4,
      "mount_seed": 20261011,
      "sigma_px": 0.0,
      "outlier_fraction": 0.0,
      "method": "ransac_epnp_lm",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": -0.7485131926940056,
          "pitch_deg": -3.8149387175399028,
          "along_body_m": -0.053820486667521414,
          "center_vehicle_m": [
            2.36,
            -0.053820486667521414,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": 2.257788899322877,
          "pitch_deg": -0.46677172659118593,
          "along_body_m": -0.11214938007803424,
          "center_vehicle_m": [
            0.23785061992196574,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": 4.373705802630779,
          "pitch_deg": 2.5426688055305844,
          "along_body_m": 0.054640099912275364,
          "center_vehicle_m": [
            -2.36,
            0.054640099912275364,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": 1.4452945144608176,
          "pitch_deg": -3.5454072400025485,
          "along_body_m": 0.05113499442617225,
          "center_vehicle_m": [
            0.40113499442617223,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.141429724369046,
          "validation_p95_px": 8.779539340019946,
          "rotation_error_deg": 3.8876499859434057,
          "center_error_m": 0.053820486667521414
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 3.9024676610153626,
          "validation_p95_px": 4.673160198155526,
          "rotation_error_deg": 2.3055277843421194,
          "center_error_m": 0.11214938007803421
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 10.234454194289702,
          "validation_p95_px": 10.809009804181652,
          "rotation_error_deg": 5.058787132746043,
          "center_error_m": 0.054640099912275336
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.278672321477505,
          "validation_p95_px": 8.339288689612562,
          "rotation_error_deg": 3.828592711859514,
          "center_error_m": 0.05113499442617231
        }
      ],
      "success": true,
      "observations_sha256": "a6616909f9738eb98b11b7ed89e94599cbeaac576e70195183e09b8634fa239d",
      "cameras": [
        {
          "camera_id": 0,
          "invalid_validation_count": 0,
          "validation_rmse_px": 1.2618546988605829e-09,
          "validation_p95_px": 1.7489741870874382e-09,
          "rotation_error_deg": 1.2074182697257333e-06,
          "center_error_m": 1.2853222795963836e-10,
          "inliers": 120,
          "fit_ms": 0.388355
        },
        {
          "camera_id": 1,
          "invalid_validation_count": 0,
          "validation_rmse_px": 2.933213915118572e-06,
          "validation_p95_px": 4.800994838548727e-06,
          "rotation_error_deg": 1.7075472925031877e-06,
          "center_error_m": 1.313741374651881e-07,
          "inliers": 120,
          "fit_ms": 0.185351
        },
        {
          "camera_id": 2,
          "invalid_validation_count": 0,
          "validation_rmse_px": 6.257307136324636e-07,
          "validation_p95_px": 1.2293925248425373e-06,
          "rotation_error_deg": 0.0,
          "center_error_m": 3.089303564117256e-08,
          "inliers": 120,
          "fit_ms": 0.141588
        },
        {
          "camera_id": 3,
          "invalid_validation_count": 0,
          "validation_rmse_px": 5.795776179758312e-07,
          "validation_p95_px": 9.620099923481224e-07,
          "rotation_error_deg": 0.0,
          "center_error_m": 3.8346766746133784e-08,
          "inliers": 120,
          "fit_ms": 0.119636
        }
      ]
    },
    {
      "trial": 4,
      "mount_seed": 20261011,
      "sigma_px": 0.0,
      "outlier_fraction": 0.1,
      "method": "iterative",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": -0.7485131926940056,
          "pitch_deg": -3.8149387175399028,
          "along_body_m": -0.053820486667521414,
          "center_vehicle_m": [
            2.36,
            -0.053820486667521414,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": 2.257788899322877,
          "pitch_deg": -0.46677172659118593,
          "along_body_m": -0.11214938007803424,
          "center_vehicle_m": [
            0.23785061992196574,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": 4.373705802630779,
          "pitch_deg": 2.5426688055305844,
          "along_body_m": 0.054640099912275364,
          "center_vehicle_m": [
            -2.36,
            0.054640099912275364,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": 1.4452945144608176,
          "pitch_deg": -3.5454072400025485,
          "along_body_m": 0.05113499442617225,
          "center_vehicle_m": [
            0.40113499442617223,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.141429724369046,
          "validation_p95_px": 8.779539340019946,
          "rotation_error_deg": 3.8876499859434057,
          "center_error_m": 0.053820486667521414
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 3.9024676610153626,
          "validation_p95_px": 4.673160198155526,
          "rotation_error_deg": 2.3055277843421194,
          "center_error_m": 0.11214938007803421
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 10.234454194289702,
          "validation_p95_px": 10.809009804181652,
          "rotation_error_deg": 5.058787132746043,
          "center_error_m": 0.054640099912275336
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.278672321477505,
          "validation_p95_px": 8.339288689612562,
          "rotation_error_deg": 3.828592711859514,
          "center_error_m": 0.05113499442617231
        }
      ],
      "success": true,
      "observations_sha256": "10c62b2191316c5426e6694804cb4cdad8dd349a89efe8d9275256d8750ca7f5",
      "cameras": [
        {
          "camera_id": 0,
          "invalid_validation_count": 0,
          "validation_rmse_px": 1.4464166927390516,
          "validation_p95_px": 2.2034814983622555,
          "rotation_error_deg": 0.686194289803863,
          "center_error_m": 0.08613780257535585,
          "inliers": 120,
          "fit_ms": 0.358599
        },
        {
          "camera_id": 1,
          "invalid_validation_count": 0,
          "validation_rmse_px": 1.2738275557871588,
          "validation_p95_px": 1.7406893140621529,
          "rotation_error_deg": 0.8677567533024874,
          "center_error_m": 0.0595660951649452,
          "inliers": 120,
          "fit_ms": 0.170403
        },
        {
          "camera_id": 2,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.7776126488920103,
          "validation_p95_px": 1.035249820986513,
          "rotation_error_deg": 0.4697944071800815,
          "center_error_m": 0.0527958980468377,
          "inliers": 120,
          "fit_ms": 0.160453
        },
        {
          "camera_id": 3,
          "invalid_validation_count": 0,
          "validation_rmse_px": 3.673117833858841,
          "validation_p95_px": 4.729046647846,
          "rotation_error_deg": 2.5248966135531603,
          "center_error_m": 0.2767600276819506,
          "inliers": 120,
          "fit_ms": 0.177045
        }
      ]
    },
    {
      "trial": 4,
      "mount_seed": 20261011,
      "sigma_px": 0.0,
      "outlier_fraction": 0.1,
      "method": "epnp",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": -0.7485131926940056,
          "pitch_deg": -3.8149387175399028,
          "along_body_m": -0.053820486667521414,
          "center_vehicle_m": [
            2.36,
            -0.053820486667521414,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": 2.257788899322877,
          "pitch_deg": -0.46677172659118593,
          "along_body_m": -0.11214938007803424,
          "center_vehicle_m": [
            0.23785061992196574,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": 4.373705802630779,
          "pitch_deg": 2.5426688055305844,
          "along_body_m": 0.054640099912275364,
          "center_vehicle_m": [
            -2.36,
            0.054640099912275364,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": 1.4452945144608176,
          "pitch_deg": -3.5454072400025485,
          "along_body_m": 0.05113499442617225,
          "center_vehicle_m": [
            0.40113499442617223,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.141429724369046,
          "validation_p95_px": 8.779539340019946,
          "rotation_error_deg": 3.8876499859434057,
          "center_error_m": 0.053820486667521414
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 3.9024676610153626,
          "validation_p95_px": 4.673160198155526,
          "rotation_error_deg": 2.3055277843421194,
          "center_error_m": 0.11214938007803421
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 10.234454194289702,
          "validation_p95_px": 10.809009804181652,
          "rotation_error_deg": 5.058787132746043,
          "center_error_m": 0.054640099912275336
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.278672321477505,
          "validation_p95_px": 8.339288689612562,
          "rotation_error_deg": 3.828592711859514,
          "center_error_m": 0.05113499442617231
        }
      ],
      "success": true,
      "observations_sha256": "10c62b2191316c5426e6694804cb4cdad8dd349a89efe8d9275256d8750ca7f5",
      "cameras": [
        {
          "camera_id": 0,
          "invalid_validation_count": 0,
          "validation_rmse_px": 2.3514636069272252,
          "validation_p95_px": 4.612584218564079,
          "rotation_error_deg": 0.4387315214909497,
          "center_error_m": 0.19344579512008098,
          "inliers": 120,
          "fit_ms": 0.220126
        },
        {
          "camera_id": 1,
          "invalid_validation_count": 0,
          "validation_rmse_px": 51.31982266644571,
          "validation_p95_px": 102.3192134477635,
          "rotation_error_deg": 9.63530250414779,
          "center_error_m": 3.48046954951539,
          "inliers": 120,
          "fit_ms": 0.060605
        },
        {
          "camera_id": 2,
          "invalid_validation_count": 0,
          "validation_rmse_px": 4.404336703864841,
          "validation_p95_px": 7.78182053016659,
          "rotation_error_deg": 0.9995995407565234,
          "center_error_m": 0.15267065265872376,
          "inliers": 120,
          "fit_ms": 0.049914
        },
        {
          "camera_id": 3,
          "invalid_validation_count": 0,
          "validation_rmse_px": 6.051671754527352,
          "validation_p95_px": 9.937431048340894,
          "rotation_error_deg": 4.754577749285939,
          "center_error_m": 0.5712082708683134,
          "inliers": 120,
          "fit_ms": 0.04732
        }
      ]
    },
    {
      "trial": 4,
      "mount_seed": 20261011,
      "sigma_px": 0.0,
      "outlier_fraction": 0.1,
      "method": "sqpnp",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": -0.7485131926940056,
          "pitch_deg": -3.8149387175399028,
          "along_body_m": -0.053820486667521414,
          "center_vehicle_m": [
            2.36,
            -0.053820486667521414,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": 2.257788899322877,
          "pitch_deg": -0.46677172659118593,
          "along_body_m": -0.11214938007803424,
          "center_vehicle_m": [
            0.23785061992196574,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": 4.373705802630779,
          "pitch_deg": 2.5426688055305844,
          "along_body_m": 0.054640099912275364,
          "center_vehicle_m": [
            -2.36,
            0.054640099912275364,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": 1.4452945144608176,
          "pitch_deg": -3.5454072400025485,
          "along_body_m": 0.05113499442617225,
          "center_vehicle_m": [
            0.40113499442617223,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.141429724369046,
          "validation_p95_px": 8.779539340019946,
          "rotation_error_deg": 3.8876499859434057,
          "center_error_m": 0.053820486667521414
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 3.9024676610153626,
          "validation_p95_px": 4.673160198155526,
          "rotation_error_deg": 2.3055277843421194,
          "center_error_m": 0.11214938007803421
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 10.234454194289702,
          "validation_p95_px": 10.809009804181652,
          "rotation_error_deg": 5.058787132746043,
          "center_error_m": 0.054640099912275336
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.278672321477505,
          "validation_p95_px": 8.339288689612562,
          "rotation_error_deg": 3.828592711859514,
          "center_error_m": 0.05113499442617231
        }
      ],
      "success": true,
      "observations_sha256": "10c62b2191316c5426e6694804cb4cdad8dd349a89efe8d9275256d8750ca7f5",
      "cameras": [
        {
          "camera_id": 0,
          "invalid_validation_count": 0,
          "validation_rmse_px": 1.3128361658649164,
          "validation_p95_px": 2.1735559404458753,
          "rotation_error_deg": 0.24416106961256662,
          "center_error_m": 0.09974061808529437,
          "inliers": 120,
          "fit_ms": 0.175903
        },
        {
          "camera_id": 1,
          "invalid_validation_count": 0,
          "validation_rmse_px": 4.475295348156023,
          "validation_p95_px": 10.570006731888986,
          "rotation_error_deg": 1.277784402426839,
          "center_error_m": 0.40770689851880865,
          "inliers": 120,
          "fit_ms": 0.035207
        },
        {
          "camera_id": 2,
          "invalid_validation_count": 0,
          "validation_rmse_px": 3.515198201044259,
          "validation_p95_px": 6.240082712874742,
          "rotation_error_deg": 0.9200789702973581,
          "center_error_m": 0.2643465605763235,
          "inliers": 120,
          "fit_ms": 0.027222
        },
        {
          "camera_id": 3,
          "invalid_validation_count": 0,
          "validation_rmse_px": 11.331741316091758,
          "validation_p95_px": 18.93731187777995,
          "rotation_error_deg": 4.058381170474191,
          "center_error_m": 1.0948004421005464,
          "inliers": 120,
          "fit_ms": 0.02646
        }
      ]
    },
    {
      "trial": 4,
      "mount_seed": 20261011,
      "sigma_px": 0.0,
      "outlier_fraction": 0.1,
      "method": "ransac_epnp_lm",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": -0.7485131926940056,
          "pitch_deg": -3.8149387175399028,
          "along_body_m": -0.053820486667521414,
          "center_vehicle_m": [
            2.36,
            -0.053820486667521414,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": 2.257788899322877,
          "pitch_deg": -0.46677172659118593,
          "along_body_m": -0.11214938007803424,
          "center_vehicle_m": [
            0.23785061992196574,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": 4.373705802630779,
          "pitch_deg": 2.5426688055305844,
          "along_body_m": 0.054640099912275364,
          "center_vehicle_m": [
            -2.36,
            0.054640099912275364,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": 1.4452945144608176,
          "pitch_deg": -3.5454072400025485,
          "along_body_m": 0.05113499442617225,
          "center_vehicle_m": [
            0.40113499442617223,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.141429724369046,
          "validation_p95_px": 8.779539340019946,
          "rotation_error_deg": 3.8876499859434057,
          "center_error_m": 0.053820486667521414
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 3.9024676610153626,
          "validation_p95_px": 4.673160198155526,
          "rotation_error_deg": 2.3055277843421194,
          "center_error_m": 0.11214938007803421
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 10.234454194289702,
          "validation_p95_px": 10.809009804181652,
          "rotation_error_deg": 5.058787132746043,
          "center_error_m": 0.054640099912275336
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.278672321477505,
          "validation_p95_px": 8.339288689612562,
          "rotation_error_deg": 3.828592711859514,
          "center_error_m": 0.05113499442617231
        }
      ],
      "success": true,
      "observations_sha256": "10c62b2191316c5426e6694804cb4cdad8dd349a89efe8d9275256d8750ca7f5",
      "cameras": [
        {
          "camera_id": 0,
          "invalid_validation_count": 0,
          "validation_rmse_px": 1.0834935869348774e-06,
          "validation_p95_px": 1.5958965066605113e-06,
          "rotation_error_deg": 0.0,
          "center_error_m": 2.570014666494579e-08,
          "inliers": 108,
          "fit_ms": 0.599054
        },
        {
          "camera_id": 1,
          "invalid_validation_count": 0,
          "validation_rmse_px": 2.1283334579206873e-06,
          "validation_p95_px": 3.2353792070425397e-06,
          "rotation_error_deg": 1.7075472925031877e-06,
          "center_error_m": 8.865013317165517e-08,
          "inliers": 108,
          "fit_ms": 0.392603
        },
        {
          "camera_id": 2,
          "invalid_validation_count": 0,
          "validation_rmse_px": 1.0346656317273807e-09,
          "validation_p95_px": 1.4514669311965525e-09,
          "rotation_error_deg": 0.0,
          "center_error_m": 6.781746531860201e-11,
          "inliers": 108,
          "fit_ms": 0.361594
        },
        {
          "camera_id": 3,
          "invalid_validation_count": 0,
          "validation_rmse_px": 1.2065867433420377e-06,
          "validation_p95_px": 1.566146232065342e-06,
          "rotation_error_deg": 0.0,
          "center_error_m": 2.98902318810024e-08,
          "inliers": 108,
          "fit_ms": 0.304446
        }
      ]
    },
    {
      "trial": 4,
      "mount_seed": 20261011,
      "sigma_px": 0.5,
      "outlier_fraction": 0.0,
      "method": "iterative",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": -0.7485131926940056,
          "pitch_deg": -3.8149387175399028,
          "along_body_m": -0.053820486667521414,
          "center_vehicle_m": [
            2.36,
            -0.053820486667521414,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": 2.257788899322877,
          "pitch_deg": -0.46677172659118593,
          "along_body_m": -0.11214938007803424,
          "center_vehicle_m": [
            0.23785061992196574,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": 4.373705802630779,
          "pitch_deg": 2.5426688055305844,
          "along_body_m": 0.054640099912275364,
          "center_vehicle_m": [
            -2.36,
            0.054640099912275364,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": 1.4452945144608176,
          "pitch_deg": -3.5454072400025485,
          "along_body_m": 0.05113499442617225,
          "center_vehicle_m": [
            0.40113499442617223,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.141429724369046,
          "validation_p95_px": 8.779539340019946,
          "rotation_error_deg": 3.8876499859434057,
          "center_error_m": 0.053820486667521414
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 3.9024676610153626,
          "validation_p95_px": 4.673160198155526,
          "rotation_error_deg": 2.3055277843421194,
          "center_error_m": 0.11214938007803421
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 10.234454194289702,
          "validation_p95_px": 10.809009804181652,
          "rotation_error_deg": 5.058787132746043,
          "center_error_m": 0.054640099912275336
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.278672321477505,
          "validation_p95_px": 8.339288689612562,
          "rotation_error_deg": 3.828592711859514,
          "center_error_m": 0.05113499442617231
        }
      ],
      "success": true,
      "observations_sha256": "13066a37e807e4237377e5ea56151ea414e01e402a7d02eee2d7d91a6a055f7d",
      "cameras": [
        {
          "camera_id": 0,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.1102211092599048,
          "validation_p95_px": 0.13337403254416574,
          "rotation_error_deg": 0.05133655904625399,
          "center_error_m": 0.001901918857495213,
          "inliers": 120,
          "fit_ms": 0.380249
        },
        {
          "camera_id": 1,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.07765173176773132,
          "validation_p95_px": 0.17320385992941784,
          "rotation_error_deg": 0.015092643992285583,
          "center_error_m": 0.005164944449338203,
          "inliers": 120,
          "fit_ms": 0.134575
        },
        {
          "camera_id": 2,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.16114596152776733,
          "validation_p95_px": 0.19609219597653596,
          "rotation_error_deg": 0.10299579583654182,
          "center_error_m": 0.01040350264385732,
          "inliers": 120,
          "fit_ms": 0.125027
        },
        {
          "camera_id": 3,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.30037633404691255,
          "validation_p95_px": 0.4356161474740723,
          "rotation_error_deg": 0.23616244295165637,
          "center_error_m": 0.021373204560036267,
          "inliers": 120,
          "fit_ms": 0.130597
        }
      ]
    },
    {
      "trial": 4,
      "mount_seed": 20261011,
      "sigma_px": 0.5,
      "outlier_fraction": 0.0,
      "method": "epnp",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": -0.7485131926940056,
          "pitch_deg": -3.8149387175399028,
          "along_body_m": -0.053820486667521414,
          "center_vehicle_m": [
            2.36,
            -0.053820486667521414,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": 2.257788899322877,
          "pitch_deg": -0.46677172659118593,
          "along_body_m": -0.11214938007803424,
          "center_vehicle_m": [
            0.23785061992196574,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": 4.373705802630779,
          "pitch_deg": 2.5426688055305844,
          "along_body_m": 0.054640099912275364,
          "center_vehicle_m": [
            -2.36,
            0.054640099912275364,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": 1.4452945144608176,
          "pitch_deg": -3.5454072400025485,
          "along_body_m": 0.05113499442617225,
          "center_vehicle_m": [
            0.40113499442617223,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.141429724369046,
          "validation_p95_px": 8.779539340019946,
          "rotation_error_deg": 3.8876499859434057,
          "center_error_m": 0.053820486667521414
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 3.9024676610153626,
          "validation_p95_px": 4.673160198155526,
          "rotation_error_deg": 2.3055277843421194,
          "center_error_m": 0.11214938007803421
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 10.234454194289702,
          "validation_p95_px": 10.809009804181652,
          "rotation_error_deg": 5.058787132746043,
          "center_error_m": 0.054640099912275336
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.278672321477505,
          "validation_p95_px": 8.339288689612562,
          "rotation_error_deg": 3.828592711859514,
          "center_error_m": 0.05113499442617231
        }
      ],
      "success": true,
      "observations_sha256": "13066a37e807e4237377e5ea56151ea414e01e402a7d02eee2d7d91a6a055f7d",
      "cameras": [
        {
          "camera_id": 0,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.8366394220873241,
          "validation_p95_px": 1.612402634913428,
          "rotation_error_deg": 0.23358042207911922,
          "center_error_m": 0.0835705828778222,
          "inliers": 120,
          "fit_ms": 0.208335
        },
        {
          "camera_id": 1,
          "invalid_validation_count": 0,
          "validation_rmse_px": 1.7475191145513143,
          "validation_p95_px": 3.93276340803476,
          "rotation_error_deg": 0.30729770210812896,
          "center_error_m": 0.1075473740802757,
          "inliers": 120,
          "fit_ms": 0.055305
        },
        {
          "camera_id": 2,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.29774519229054364,
          "validation_p95_px": 0.4164117721907555,
          "rotation_error_deg": 0.18659969362958118,
          "center_error_m": 0.021272477355812083,
          "inliers": 120,
          "fit_ms": 0.049854
        },
        {
          "camera_id": 3,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.620288304514946,
          "validation_p95_px": 0.9592634932250325,
          "rotation_error_deg": 0.17470976175428674,
          "center_error_m": 0.027289537388903735,
          "inliers": 120,
          "fit_ms": 0.047921
        }
      ]
    },
    {
      "trial": 4,
      "mount_seed": 20261011,
      "sigma_px": 0.5,
      "outlier_fraction": 0.0,
      "method": "sqpnp",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": -0.7485131926940056,
          "pitch_deg": -3.8149387175399028,
          "along_body_m": -0.053820486667521414,
          "center_vehicle_m": [
            2.36,
            -0.053820486667521414,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": 2.257788899322877,
          "pitch_deg": -0.46677172659118593,
          "along_body_m": -0.11214938007803424,
          "center_vehicle_m": [
            0.23785061992196574,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": 4.373705802630779,
          "pitch_deg": 2.5426688055305844,
          "along_body_m": 0.054640099912275364,
          "center_vehicle_m": [
            -2.36,
            0.054640099912275364,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": 1.4452945144608176,
          "pitch_deg": -3.5454072400025485,
          "along_body_m": 0.05113499442617225,
          "center_vehicle_m": [
            0.40113499442617223,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.141429724369046,
          "validation_p95_px": 8.779539340019946,
          "rotation_error_deg": 3.8876499859434057,
          "center_error_m": 0.053820486667521414
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 3.9024676610153626,
          "validation_p95_px": 4.673160198155526,
          "rotation_error_deg": 2.3055277843421194,
          "center_error_m": 0.11214938007803421
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 10.234454194289702,
          "validation_p95_px": 10.809009804181652,
          "rotation_error_deg": 5.058787132746043,
          "center_error_m": 0.054640099912275336
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.278672321477505,
          "validation_p95_px": 8.339288689612562,
          "rotation_error_deg": 3.828592711859514,
          "center_error_m": 0.05113499442617231
        }
      ],
      "success": true,
      "observations_sha256": "13066a37e807e4237377e5ea56151ea414e01e402a7d02eee2d7d91a6a055f7d",
      "cameras": [
        {
          "camera_id": 0,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.15910927824306048,
          "validation_p95_px": 0.2881806342478594,
          "rotation_error_deg": 0.02509874608194676,
          "center_error_m": 0.008036284768674753,
          "inliers": 120,
          "fit_ms": 0.183066
        },
        {
          "camera_id": 1,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.4176410177108486,
          "validation_p95_px": 0.9564991271106633,
          "rotation_error_deg": 0.18314112897198065,
          "center_error_m": 0.037765950852076235,
          "inliers": 120,
          "fit_ms": 0.032692
        },
        {
          "camera_id": 2,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.2926522944020223,
          "validation_p95_px": 0.5010069033040733,
          "rotation_error_deg": 0.14264641106031342,
          "center_error_m": 0.022782017033008042,
          "inliers": 120,
          "fit_ms": 0.027292
        },
        {
          "camera_id": 3,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.24624224012260598,
          "validation_p95_px": 0.44640613597687273,
          "rotation_error_deg": 0.12717154630312627,
          "center_error_m": 0.021377459214017392,
          "inliers": 120,
          "fit_ms": 0.025558
        }
      ]
    },
    {
      "trial": 4,
      "mount_seed": 20261011,
      "sigma_px": 0.5,
      "outlier_fraction": 0.0,
      "method": "ransac_epnp_lm",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": -0.7485131926940056,
          "pitch_deg": -3.8149387175399028,
          "along_body_m": -0.053820486667521414,
          "center_vehicle_m": [
            2.36,
            -0.053820486667521414,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": 2.257788899322877,
          "pitch_deg": -0.46677172659118593,
          "along_body_m": -0.11214938007803424,
          "center_vehicle_m": [
            0.23785061992196574,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": 4.373705802630779,
          "pitch_deg": 2.5426688055305844,
          "along_body_m": 0.054640099912275364,
          "center_vehicle_m": [
            -2.36,
            0.054640099912275364,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": 1.4452945144608176,
          "pitch_deg": -3.5454072400025485,
          "along_body_m": 0.05113499442617225,
          "center_vehicle_m": [
            0.40113499442617223,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.141429724369046,
          "validation_p95_px": 8.779539340019946,
          "rotation_error_deg": 3.8876499859434057,
          "center_error_m": 0.053820486667521414
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 3.9024676610153626,
          "validation_p95_px": 4.673160198155526,
          "rotation_error_deg": 2.3055277843421194,
          "center_error_m": 0.11214938007803421
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 10.234454194289702,
          "validation_p95_px": 10.809009804181652,
          "rotation_error_deg": 5.058787132746043,
          "center_error_m": 0.054640099912275336
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.278672321477505,
          "validation_p95_px": 8.339288689612562,
          "rotation_error_deg": 3.828592711859514,
          "center_error_m": 0.05113499442617231
        }
      ],
      "success": true,
      "observations_sha256": "13066a37e807e4237377e5ea56151ea414e01e402a7d02eee2d7d91a6a055f7d",
      "cameras": [
        {
          "camera_id": 0,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.14737838678432202,
          "validation_p95_px": 0.22548168117391998,
          "rotation_error_deg": 0.13780497981008186,
          "center_error_m": 0.008236388303297882,
          "inliers": 115,
          "fit_ms": 0.480229
        },
        {
          "camera_id": 1,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.12545506689299743,
          "validation_p95_px": 0.17059083389921298,
          "rotation_error_deg": 0.045196695064787525,
          "center_error_m": 0.0025329024669714954,
          "inliers": 111,
          "fit_ms": 0.472875
        },
        {
          "camera_id": 2,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.1748188557163314,
          "validation_p95_px": 0.22645912334249857,
          "rotation_error_deg": 0.11087821853463452,
          "center_error_m": 0.010844650320678152,
          "inliers": 111,
          "fit_ms": 0.324003
        },
        {
          "camera_id": 3,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.2913367470973046,
          "validation_p95_px": 0.459871254979636,
          "rotation_error_deg": 0.14675624637887197,
          "center_error_m": 0.019729959850940153,
          "inliers": 103,
          "fit_ms": 0.417059
        }
      ]
    },
    {
      "trial": 4,
      "mount_seed": 20261011,
      "sigma_px": 0.5,
      "outlier_fraction": 0.1,
      "method": "iterative",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": -0.7485131926940056,
          "pitch_deg": -3.8149387175399028,
          "along_body_m": -0.053820486667521414,
          "center_vehicle_m": [
            2.36,
            -0.053820486667521414,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": 2.257788899322877,
          "pitch_deg": -0.46677172659118593,
          "along_body_m": -0.11214938007803424,
          "center_vehicle_m": [
            0.23785061992196574,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": 4.373705802630779,
          "pitch_deg": 2.5426688055305844,
          "along_body_m": 0.054640099912275364,
          "center_vehicle_m": [
            -2.36,
            0.054640099912275364,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": 1.4452945144608176,
          "pitch_deg": -3.5454072400025485,
          "along_body_m": 0.05113499442617225,
          "center_vehicle_m": [
            0.40113499442617223,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.141429724369046,
          "validation_p95_px": 8.779539340019946,
          "rotation_error_deg": 3.8876499859434057,
          "center_error_m": 0.053820486667521414
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 3.9024676610153626,
          "validation_p95_px": 4.673160198155526,
          "rotation_error_deg": 2.3055277843421194,
          "center_error_m": 0.11214938007803421
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 10.234454194289702,
          "validation_p95_px": 10.809009804181652,
          "rotation_error_deg": 5.058787132746043,
          "center_error_m": 0.054640099912275336
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.278672321477505,
          "validation_p95_px": 8.339288689612562,
          "rotation_error_deg": 3.828592711859514,
          "center_error_m": 0.05113499442617231
        }
      ],
      "success": true,
      "observations_sha256": "93c2e92a7e6b9eabd54fccbf96a08d958b30bb12e1c53eeaed9522e8675b3089",
      "cameras": [
        {
          "camera_id": 0,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.9679953819310471,
          "validation_p95_px": 1.5379523310066385,
          "rotation_error_deg": 0.4299676215043321,
          "center_error_m": 0.059466032691241885,
          "inliers": 120,
          "fit_ms": 0.390098
        },
        {
          "camera_id": 1,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.8586554984616825,
          "validation_p95_px": 1.3229946021801615,
          "rotation_error_deg": 0.5589034121630921,
          "center_error_m": 0.020739020462769205,
          "inliers": 120,
          "fit_ms": 0.134334
        },
        {
          "camera_id": 2,
          "invalid_validation_count": 0,
          "validation_rmse_px": 3.174059102132708,
          "validation_p95_px": 4.129237436959521,
          "rotation_error_deg": 2.167287898536922,
          "center_error_m": 0.22379034548255303,
          "inliers": 120,
          "fit_ms": 0.222591
        },
        {
          "camera_id": 3,
          "invalid_validation_count": 0,
          "validation_rmse_px": 6.8322547054249405,
          "validation_p95_px": 10.074572667766693,
          "rotation_error_deg": 4.455615846900447,
          "center_error_m": 0.3212691513806296,
          "inliers": 120,
          "fit_ms": 0.253781
        }
      ]
    },
    {
      "trial": 4,
      "mount_seed": 20261011,
      "sigma_px": 0.5,
      "outlier_fraction": 0.1,
      "method": "epnp",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": -0.7485131926940056,
          "pitch_deg": -3.8149387175399028,
          "along_body_m": -0.053820486667521414,
          "center_vehicle_m": [
            2.36,
            -0.053820486667521414,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": 2.257788899322877,
          "pitch_deg": -0.46677172659118593,
          "along_body_m": -0.11214938007803424,
          "center_vehicle_m": [
            0.23785061992196574,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": 4.373705802630779,
          "pitch_deg": 2.5426688055305844,
          "along_body_m": 0.054640099912275364,
          "center_vehicle_m": [
            -2.36,
            0.054640099912275364,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": 1.4452945144608176,
          "pitch_deg": -3.5454072400025485,
          "along_body_m": 0.05113499442617225,
          "center_vehicle_m": [
            0.40113499442617223,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.141429724369046,
          "validation_p95_px": 8.779539340019946,
          "rotation_error_deg": 3.8876499859434057,
          "center_error_m": 0.053820486667521414
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 3.9024676610153626,
          "validation_p95_px": 4.673160198155526,
          "rotation_error_deg": 2.3055277843421194,
          "center_error_m": 0.11214938007803421
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 10.234454194289702,
          "validation_p95_px": 10.809009804181652,
          "rotation_error_deg": 5.058787132746043,
          "center_error_m": 0.054640099912275336
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.278672321477505,
          "validation_p95_px": 8.339288689612562,
          "rotation_error_deg": 3.828592711859514,
          "center_error_m": 0.05113499442617231
        }
      ],
      "success": false,
      "observations_sha256": "93c2e92a7e6b9eabd54fccbf96a08d958b30bb12e1c53eeaed9522e8675b3089",
      "error": "estimated pose projects training point outside validity"
    },
    {
      "trial": 4,
      "mount_seed": 20261011,
      "sigma_px": 0.5,
      "outlier_fraction": 0.1,
      "method": "sqpnp",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": -0.7485131926940056,
          "pitch_deg": -3.8149387175399028,
          "along_body_m": -0.053820486667521414,
          "center_vehicle_m": [
            2.36,
            -0.053820486667521414,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": 2.257788899322877,
          "pitch_deg": -0.46677172659118593,
          "along_body_m": -0.11214938007803424,
          "center_vehicle_m": [
            0.23785061992196574,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": 4.373705802630779,
          "pitch_deg": 2.5426688055305844,
          "along_body_m": 0.054640099912275364,
          "center_vehicle_m": [
            -2.36,
            0.054640099912275364,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": 1.4452945144608176,
          "pitch_deg": -3.5454072400025485,
          "along_body_m": 0.05113499442617225,
          "center_vehicle_m": [
            0.40113499442617223,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.141429724369046,
          "validation_p95_px": 8.779539340019946,
          "rotation_error_deg": 3.8876499859434057,
          "center_error_m": 0.053820486667521414
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 3.9024676610153626,
          "validation_p95_px": 4.673160198155526,
          "rotation_error_deg": 2.3055277843421194,
          "center_error_m": 0.11214938007803421
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 10.234454194289702,
          "validation_p95_px": 10.809009804181652,
          "rotation_error_deg": 5.058787132746043,
          "center_error_m": 0.054640099912275336
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.278672321477505,
          "validation_p95_px": 8.339288689612562,
          "rotation_error_deg": 3.828592711859514,
          "center_error_m": 0.05113499442617231
        }
      ],
      "success": false,
      "observations_sha256": "93c2e92a7e6b9eabd54fccbf96a08d958b30bb12e1c53eeaed9522e8675b3089",
      "error": "estimated pose projects training point outside validity"
    },
    {
      "trial": 4,
      "mount_seed": 20261011,
      "sigma_px": 0.5,
      "outlier_fraction": 0.1,
      "method": "ransac_epnp_lm",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": -0.7485131926940056,
          "pitch_deg": -3.8149387175399028,
          "along_body_m": -0.053820486667521414,
          "center_vehicle_m": [
            2.36,
            -0.053820486667521414,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": 2.257788899322877,
          "pitch_deg": -0.46677172659118593,
          "along_body_m": -0.11214938007803424,
          "center_vehicle_m": [
            0.23785061992196574,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": 4.373705802630779,
          "pitch_deg": 2.5426688055305844,
          "along_body_m": 0.054640099912275364,
          "center_vehicle_m": [
            -2.36,
            0.054640099912275364,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": 1.4452945144608176,
          "pitch_deg": -3.5454072400025485,
          "along_body_m": 0.05113499442617225,
          "center_vehicle_m": [
            0.40113499442617223,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.141429724369046,
          "validation_p95_px": 8.779539340019946,
          "rotation_error_deg": 3.8876499859434057,
          "center_error_m": 0.053820486667521414
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 3.9024676610153626,
          "validation_p95_px": 4.673160198155526,
          "rotation_error_deg": 2.3055277843421194,
          "center_error_m": 0.11214938007803421
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 10.234454194289702,
          "validation_p95_px": 10.809009804181652,
          "rotation_error_deg": 5.058787132746043,
          "center_error_m": 0.054640099912275336
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.278672321477505,
          "validation_p95_px": 8.339288689612562,
          "rotation_error_deg": 3.828592711859514,
          "center_error_m": 0.05113499442617231
        }
      ],
      "success": true,
      "observations_sha256": "93c2e92a7e6b9eabd54fccbf96a08d958b30bb12e1c53eeaed9522e8675b3089",
      "cameras": [
        {
          "camera_id": 0,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.21345623001824157,
          "validation_p95_px": 0.4194107977685292,
          "rotation_error_deg": 0.15864711651825064,
          "center_error_m": 0.018362228570350782,
          "inliers": 95,
          "fit_ms": 0.985415
        },
        {
          "camera_id": 1,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.19477612553753634,
          "validation_p95_px": 0.4529494878197735,
          "rotation_error_deg": 0.0730326666236348,
          "center_error_m": 0.013465431306162123,
          "inliers": 101,
          "fit_ms": 0.5813
        },
        {
          "camera_id": 2,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.14711999278261695,
          "validation_p95_px": 0.16909841343584397,
          "rotation_error_deg": 0.07917561393709222,
          "center_error_m": 0.0024925017394244508,
          "inliers": 99,
          "fit_ms": 0.528531
        },
        {
          "camera_id": 3,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.0876287826342523,
          "validation_p95_px": 0.15569856833445692,
          "rotation_error_deg": 0.050379472614796285,
          "center_error_m": 0.007001162088907066,
          "inliers": 97,
          "fit_ms": 0.519934
        }
      ]
    },
    {
      "trial": 4,
      "mount_seed": 20261011,
      "sigma_px": 1.0,
      "outlier_fraction": 0.0,
      "method": "iterative",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": -0.7485131926940056,
          "pitch_deg": -3.8149387175399028,
          "along_body_m": -0.053820486667521414,
          "center_vehicle_m": [
            2.36,
            -0.053820486667521414,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": 2.257788899322877,
          "pitch_deg": -0.46677172659118593,
          "along_body_m": -0.11214938007803424,
          "center_vehicle_m": [
            0.23785061992196574,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": 4.373705802630779,
          "pitch_deg": 2.5426688055305844,
          "along_body_m": 0.054640099912275364,
          "center_vehicle_m": [
            -2.36,
            0.054640099912275364,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": 1.4452945144608176,
          "pitch_deg": -3.5454072400025485,
          "along_body_m": 0.05113499442617225,
          "center_vehicle_m": [
            0.40113499442617223,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.141429724369046,
          "validation_p95_px": 8.779539340019946,
          "rotation_error_deg": 3.8876499859434057,
          "center_error_m": 0.053820486667521414
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 3.9024676610153626,
          "validation_p95_px": 4.673160198155526,
          "rotation_error_deg": 2.3055277843421194,
          "center_error_m": 0.11214938007803421
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 10.234454194289702,
          "validation_p95_px": 10.809009804181652,
          "rotation_error_deg": 5.058787132746043,
          "center_error_m": 0.054640099912275336
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.278672321477505,
          "validation_p95_px": 8.339288689612562,
          "rotation_error_deg": 3.828592711859514,
          "center_error_m": 0.05113499442617231
        }
      ],
      "success": true,
      "observations_sha256": "be18e90ec4295a3c971d358b6bc14cf12f690307f38d5ec53393d6a3568a0a47",
      "cameras": [
        {
          "camera_id": 0,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.2421337034270735,
          "validation_p95_px": 0.3685498444280129,
          "rotation_error_deg": 0.11728341183135785,
          "center_error_m": 0.01696458972000259,
          "inliers": 120,
          "fit_ms": 0.357176
        },
        {
          "camera_id": 1,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.3844269384577003,
          "validation_p95_px": 0.5341778665983475,
          "rotation_error_deg": 0.15584158263578837,
          "center_error_m": 0.010495082954601014,
          "inliers": 120,
          "fit_ms": 0.135767
        },
        {
          "camera_id": 2,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.355880990074457,
          "validation_p95_px": 0.5627092871773293,
          "rotation_error_deg": 0.14038213053555287,
          "center_error_m": 0.02489454557543364,
          "inliers": 120,
          "fit_ms": 0.125547
        },
        {
          "camera_id": 3,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.1889185430821345,
          "validation_p95_px": 0.2936226002353712,
          "rotation_error_deg": 0.08908472240879152,
          "center_error_m": 0.011277039750885945,
          "inliers": 120,
          "fit_ms": 0.123744
        }
      ]
    },
    {
      "trial": 4,
      "mount_seed": 20261011,
      "sigma_px": 1.0,
      "outlier_fraction": 0.0,
      "method": "epnp",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": -0.7485131926940056,
          "pitch_deg": -3.8149387175399028,
          "along_body_m": -0.053820486667521414,
          "center_vehicle_m": [
            2.36,
            -0.053820486667521414,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": 2.257788899322877,
          "pitch_deg": -0.46677172659118593,
          "along_body_m": -0.11214938007803424,
          "center_vehicle_m": [
            0.23785061992196574,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": 4.373705802630779,
          "pitch_deg": 2.5426688055305844,
          "along_body_m": 0.054640099912275364,
          "center_vehicle_m": [
            -2.36,
            0.054640099912275364,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": 1.4452945144608176,
          "pitch_deg": -3.5454072400025485,
          "along_body_m": 0.05113499442617225,
          "center_vehicle_m": [
            0.40113499442617223,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.141429724369046,
          "validation_p95_px": 8.779539340019946,
          "rotation_error_deg": 3.8876499859434057,
          "center_error_m": 0.053820486667521414
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 3.9024676610153626,
          "validation_p95_px": 4.673160198155526,
          "rotation_error_deg": 2.3055277843421194,
          "center_error_m": 0.11214938007803421
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 10.234454194289702,
          "validation_p95_px": 10.809009804181652,
          "rotation_error_deg": 5.058787132746043,
          "center_error_m": 0.054640099912275336
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.278672321477505,
          "validation_p95_px": 8.339288689612562,
          "rotation_error_deg": 3.828592711859514,
          "center_error_m": 0.05113499442617231
        }
      ],
      "success": true,
      "observations_sha256": "be18e90ec4295a3c971d358b6bc14cf12f690307f38d5ec53393d6a3568a0a47",
      "cameras": [
        {
          "camera_id": 0,
          "invalid_validation_count": 0,
          "validation_rmse_px": 1.9099246789402753,
          "validation_p95_px": 3.2686267829866473,
          "rotation_error_deg": 0.3787242246480218,
          "center_error_m": 0.16494488200146243,
          "inliers": 120,
          "fit_ms": 0.525234
        },
        {
          "camera_id": 1,
          "invalid_validation_count": 0,
          "validation_rmse_px": 1.8839034053360293,
          "validation_p95_px": 4.454087630838639,
          "rotation_error_deg": 0.6768058429635743,
          "center_error_m": 0.15372644304913127,
          "inliers": 120,
          "fit_ms": 0.070914
        },
        {
          "camera_id": 2,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.6929106616975421,
          "validation_p95_px": 1.386693054698957,
          "rotation_error_deg": 0.18305085489879774,
          "center_error_m": 0.04985676810200968,
          "inliers": 120,
          "fit_ms": 0.053521
        },
        {
          "camera_id": 3,
          "invalid_validation_count": 0,
          "validation_rmse_px": 1.509826527961167,
          "validation_p95_px": 2.7665489540842993,
          "rotation_error_deg": 0.33029746085459255,
          "center_error_m": 0.07681708781270662,
          "inliers": 120,
          "fit_ms": 0.05278
        }
      ]
    },
    {
      "trial": 4,
      "mount_seed": 20261011,
      "sigma_px": 1.0,
      "outlier_fraction": 0.0,
      "method": "sqpnp",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": -0.7485131926940056,
          "pitch_deg": -3.8149387175399028,
          "along_body_m": -0.053820486667521414,
          "center_vehicle_m": [
            2.36,
            -0.053820486667521414,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": 2.257788899322877,
          "pitch_deg": -0.46677172659118593,
          "along_body_m": -0.11214938007803424,
          "center_vehicle_m": [
            0.23785061992196574,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": 4.373705802630779,
          "pitch_deg": 2.5426688055305844,
          "along_body_m": 0.054640099912275364,
          "center_vehicle_m": [
            -2.36,
            0.054640099912275364,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": 1.4452945144608176,
          "pitch_deg": -3.5454072400025485,
          "along_body_m": 0.05113499442617225,
          "center_vehicle_m": [
            0.40113499442617223,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.141429724369046,
          "validation_p95_px": 8.779539340019946,
          "rotation_error_deg": 3.8876499859434057,
          "center_error_m": 0.053820486667521414
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 3.9024676610153626,
          "validation_p95_px": 4.673160198155526,
          "rotation_error_deg": 2.3055277843421194,
          "center_error_m": 0.11214938007803421
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 10.234454194289702,
          "validation_p95_px": 10.809009804181652,
          "rotation_error_deg": 5.058787132746043,
          "center_error_m": 0.054640099912275336
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.278672321477505,
          "validation_p95_px": 8.339288689612562,
          "rotation_error_deg": 3.828592711859514,
          "center_error_m": 0.05113499442617231
        }
      ],
      "success": true,
      "observations_sha256": "be18e90ec4295a3c971d358b6bc14cf12f690307f38d5ec53393d6a3568a0a47",
      "cameras": [
        {
          "camera_id": 0,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.31618211162310345,
          "validation_p95_px": 0.5658906996682558,
          "rotation_error_deg": 0.13908327282905403,
          "center_error_m": 0.027909794668825632,
          "inliers": 120,
          "fit_ms": 0.184318
        },
        {
          "camera_id": 1,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.3846699615788713,
          "validation_p95_px": 0.739515935558651,
          "rotation_error_deg": 0.11518825401080289,
          "center_error_m": 0.027408465068075174,
          "inliers": 120,
          "fit_ms": 0.033764
        },
        {
          "camera_id": 2,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.5401869381007047,
          "validation_p95_px": 1.091294180175602,
          "rotation_error_deg": 0.2329324323235724,
          "center_error_m": 0.042531051216470606,
          "inliers": 120,
          "fit_ms": 0.027021
        },
        {
          "camera_id": 3,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.3366139842780801,
          "validation_p95_px": 0.4719521068720858,
          "rotation_error_deg": 0.2818017434666996,
          "center_error_m": 0.028030831901313173,
          "inliers": 120,
          "fit_ms": 0.025769
        }
      ]
    },
    {
      "trial": 4,
      "mount_seed": 20261011,
      "sigma_px": 1.0,
      "outlier_fraction": 0.0,
      "method": "ransac_epnp_lm",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": -0.7485131926940056,
          "pitch_deg": -3.8149387175399028,
          "along_body_m": -0.053820486667521414,
          "center_vehicle_m": [
            2.36,
            -0.053820486667521414,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": 2.257788899322877,
          "pitch_deg": -0.46677172659118593,
          "along_body_m": -0.11214938007803424,
          "center_vehicle_m": [
            0.23785061992196574,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": 4.373705802630779,
          "pitch_deg": 2.5426688055305844,
          "along_body_m": 0.054640099912275364,
          "center_vehicle_m": [
            -2.36,
            0.054640099912275364,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": 1.4452945144608176,
          "pitch_deg": -3.5454072400025485,
          "along_body_m": 0.05113499442617225,
          "center_vehicle_m": [
            0.40113499442617223,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.141429724369046,
          "validation_p95_px": 8.779539340019946,
          "rotation_error_deg": 3.8876499859434057,
          "center_error_m": 0.053820486667521414
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 3.9024676610153626,
          "validation_p95_px": 4.673160198155526,
          "rotation_error_deg": 2.3055277843421194,
          "center_error_m": 0.11214938007803421
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 10.234454194289702,
          "validation_p95_px": 10.809009804181652,
          "rotation_error_deg": 5.058787132746043,
          "center_error_m": 0.054640099912275336
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.278672321477505,
          "validation_p95_px": 8.339288689612562,
          "rotation_error_deg": 3.828592711859514,
          "center_error_m": 0.05113499442617231
        }
      ],
      "success": true,
      "observations_sha256": "be18e90ec4295a3c971d358b6bc14cf12f690307f38d5ec53393d6a3568a0a47",
      "cameras": [
        {
          "camera_id": 0,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.504380032994205,
          "validation_p95_px": 1.0474343447956014,
          "rotation_error_deg": 0.3388621198883903,
          "center_error_m": 0.0426140125824512,
          "inliers": 82,
          "fit_ms": 1.618293
        },
        {
          "camera_id": 1,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.5991000794954167,
          "validation_p95_px": 1.2074474007352303,
          "rotation_error_deg": 0.2926433924751912,
          "center_error_m": 0.051758594232728876,
          "inliers": 85,
          "fit_ms": 1.020682
        },
        {
          "camera_id": 2,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.6617556371476253,
          "validation_p95_px": 1.1985083701726291,
          "rotation_error_deg": 0.24514762922252456,
          "center_error_m": 0.05293502462641065,
          "inliers": 82,
          "fit_ms": 1.402264
        },
        {
          "camera_id": 3,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.5062350606222378,
          "validation_p95_px": 0.7628171086394531,
          "rotation_error_deg": 0.3569969063086223,
          "center_error_m": 0.0417684115262459,
          "inliers": 80,
          "fit_ms": 1.658408
        }
      ]
    },
    {
      "trial": 4,
      "mount_seed": 20261011,
      "sigma_px": 1.0,
      "outlier_fraction": 0.1,
      "method": "iterative",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": -0.7485131926940056,
          "pitch_deg": -3.8149387175399028,
          "along_body_m": -0.053820486667521414,
          "center_vehicle_m": [
            2.36,
            -0.053820486667521414,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": 2.257788899322877,
          "pitch_deg": -0.46677172659118593,
          "along_body_m": -0.11214938007803424,
          "center_vehicle_m": [
            0.23785061992196574,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": 4.373705802630779,
          "pitch_deg": 2.5426688055305844,
          "along_body_m": 0.054640099912275364,
          "center_vehicle_m": [
            -2.36,
            0.054640099912275364,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": 1.4452945144608176,
          "pitch_deg": -3.5454072400025485,
          "along_body_m": 0.05113499442617225,
          "center_vehicle_m": [
            0.40113499442617223,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.141429724369046,
          "validation_p95_px": 8.779539340019946,
          "rotation_error_deg": 3.8876499859434057,
          "center_error_m": 0.053820486667521414
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 3.9024676610153626,
          "validation_p95_px": 4.673160198155526,
          "rotation_error_deg": 2.3055277843421194,
          "center_error_m": 0.11214938007803421
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 10.234454194289702,
          "validation_p95_px": 10.809009804181652,
          "rotation_error_deg": 5.058787132746043,
          "center_error_m": 0.054640099912275336
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.278672321477505,
          "validation_p95_px": 8.339288689612562,
          "rotation_error_deg": 3.828592711859514,
          "center_error_m": 0.05113499442617231
        }
      ],
      "success": true,
      "observations_sha256": "179926e399912155c999832047198748be1d634c1c9105306bac393031b49999",
      "cameras": [
        {
          "camera_id": 0,
          "invalid_validation_count": 0,
          "validation_rmse_px": 3.847237577192655,
          "validation_p95_px": 7.641553230963883,
          "rotation_error_deg": 3.142023854411785,
          "center_error_m": 0.3280214199534149,
          "inliers": 120,
          "fit_ms": 0.418492
        },
        {
          "camera_id": 1,
          "invalid_validation_count": 0,
          "validation_rmse_px": 1.132203589943729,
          "validation_p95_px": 2.177007374763272,
          "rotation_error_deg": 0.6027586978866301,
          "center_error_m": 0.06786363073908766,
          "inliers": 120,
          "fit_ms": 0.139013
        },
        {
          "camera_id": 2,
          "invalid_validation_count": 0,
          "validation_rmse_px": 2.1800443978218893,
          "validation_p95_px": 2.6859423928984136,
          "rotation_error_deg": 1.254061824260134,
          "center_error_m": 0.09367312884387748,
          "inliers": 120,
          "fit_ms": 0.158259
        },
        {
          "camera_id": 3,
          "invalid_validation_count": 0,
          "validation_rmse_px": 1.476308782470985,
          "validation_p95_px": 1.7151185767865291,
          "rotation_error_deg": 1.066291984248476,
          "center_error_m": 0.09035477308827045,
          "inliers": 120,
          "fit_ms": 0.122912
        }
      ]
    },
    {
      "trial": 4,
      "mount_seed": 20261011,
      "sigma_px": 1.0,
      "outlier_fraction": 0.1,
      "method": "epnp",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": -0.7485131926940056,
          "pitch_deg": -3.8149387175399028,
          "along_body_m": -0.053820486667521414,
          "center_vehicle_m": [
            2.36,
            -0.053820486667521414,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": 2.257788899322877,
          "pitch_deg": -0.46677172659118593,
          "along_body_m": -0.11214938007803424,
          "center_vehicle_m": [
            0.23785061992196574,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": 4.373705802630779,
          "pitch_deg": 2.5426688055305844,
          "along_body_m": 0.054640099912275364,
          "center_vehicle_m": [
            -2.36,
            0.054640099912275364,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": 1.4452945144608176,
          "pitch_deg": -3.5454072400025485,
          "along_body_m": 0.05113499442617225,
          "center_vehicle_m": [
            0.40113499442617223,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.141429724369046,
          "validation_p95_px": 8.779539340019946,
          "rotation_error_deg": 3.8876499859434057,
          "center_error_m": 0.053820486667521414
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 3.9024676610153626,
          "validation_p95_px": 4.673160198155526,
          "rotation_error_deg": 2.3055277843421194,
          "center_error_m": 0.11214938007803421
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 10.234454194289702,
          "validation_p95_px": 10.809009804181652,
          "rotation_error_deg": 5.058787132746043,
          "center_error_m": 0.054640099912275336
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.278672321477505,
          "validation_p95_px": 8.339288689612562,
          "rotation_error_deg": 3.828592711859514,
          "center_error_m": 0.05113499442617231
        }
      ],
      "success": true,
      "observations_sha256": "179926e399912155c999832047198748be1d634c1c9105306bac393031b49999",
      "cameras": [
        {
          "camera_id": 0,
          "invalid_validation_count": 0,
          "validation_rmse_px": 9.848462456726125,
          "validation_p95_px": 21.82425931901702,
          "rotation_error_deg": 4.074040222342635,
          "center_error_m": 0.802095547952527,
          "inliers": 120,
          "fit_ms": 0.234774
        },
        {
          "camera_id": 1,
          "invalid_validation_count": 0,
          "validation_rmse_px": 16.532080041652247,
          "validation_p95_px": 34.596216112775636,
          "rotation_error_deg": 2.1313301780192586,
          "center_error_m": 0.8865151953410354,
          "inliers": 120,
          "fit_ms": 0.061587
        },
        {
          "camera_id": 2,
          "invalid_validation_count": 0,
          "validation_rmse_px": 21.961460791456393,
          "validation_p95_px": 34.832517742051,
          "rotation_error_deg": 7.376195780581069,
          "center_error_m": 0.6044567523230904,
          "inliers": 120,
          "fit_ms": 0.055054
        },
        {
          "camera_id": 3,
          "invalid_validation_count": 0,
          "validation_rmse_px": 5.578878896920162,
          "validation_p95_px": 8.579849580488537,
          "rotation_error_deg": 1.7167571293680972,
          "center_error_m": 0.4126575843876165,
          "inliers": 120,
          "fit_ms": 0.048211
        }
      ]
    },
    {
      "trial": 4,
      "mount_seed": 20261011,
      "sigma_px": 1.0,
      "outlier_fraction": 0.1,
      "method": "sqpnp",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": -0.7485131926940056,
          "pitch_deg": -3.8149387175399028,
          "along_body_m": -0.053820486667521414,
          "center_vehicle_m": [
            2.36,
            -0.053820486667521414,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": 2.257788899322877,
          "pitch_deg": -0.46677172659118593,
          "along_body_m": -0.11214938007803424,
          "center_vehicle_m": [
            0.23785061992196574,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": 4.373705802630779,
          "pitch_deg": 2.5426688055305844,
          "along_body_m": 0.054640099912275364,
          "center_vehicle_m": [
            -2.36,
            0.054640099912275364,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": 1.4452945144608176,
          "pitch_deg": -3.5454072400025485,
          "along_body_m": 0.05113499442617225,
          "center_vehicle_m": [
            0.40113499442617223,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.141429724369046,
          "validation_p95_px": 8.779539340019946,
          "rotation_error_deg": 3.8876499859434057,
          "center_error_m": 0.053820486667521414
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 3.9024676610153626,
          "validation_p95_px": 4.673160198155526,
          "rotation_error_deg": 2.3055277843421194,
          "center_error_m": 0.11214938007803421
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 10.234454194289702,
          "validation_p95_px": 10.809009804181652,
          "rotation_error_deg": 5.058787132746043,
          "center_error_m": 0.054640099912275336
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.278672321477505,
          "validation_p95_px": 8.339288689612562,
          "rotation_error_deg": 3.828592711859514,
          "center_error_m": 0.05113499442617231
        }
      ],
      "success": true,
      "observations_sha256": "179926e399912155c999832047198748be1d634c1c9105306bac393031b49999",
      "cameras": [
        {
          "camera_id": 0,
          "invalid_validation_count": 0,
          "validation_rmse_px": 10.751194362591784,
          "validation_p95_px": 22.209808872640707,
          "rotation_error_deg": 3.2901884914572737,
          "center_error_m": 0.9170072852574653,
          "inliers": 120,
          "fit_ms": 0.227991
        },
        {
          "camera_id": 1,
          "invalid_validation_count": 0,
          "validation_rmse_px": 3.0500429856031923,
          "validation_p95_px": 7.053587973006093,
          "rotation_error_deg": 1.3817588738545576,
          "center_error_m": 0.24180136409857106,
          "inliers": 120,
          "fit_ms": 0.044685
        },
        {
          "camera_id": 2,
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.106798959073209,
          "validation_p95_px": 13.713957483049299,
          "rotation_error_deg": 3.6885562768751026,
          "center_error_m": 0.5318533870423618,
          "inliers": 120,
          "fit_ms": 0.029385
        },
        {
          "camera_id": 3,
          "invalid_validation_count": 0,
          "validation_rmse_px": 4.637641914080257,
          "validation_p95_px": 10.400335470563515,
          "rotation_error_deg": 2.284128562834475,
          "center_error_m": 0.44221525731950145,
          "inliers": 120,
          "fit_ms": 0.02644
        }
      ]
    },
    {
      "trial": 4,
      "mount_seed": 20261011,
      "sigma_px": 1.0,
      "outlier_fraction": 0.1,
      "method": "ransac_epnp_lm",
      "mounts": [
        {
          "camera_id": 0,
          "yaw_deg": -0.7485131926940056,
          "pitch_deg": -3.8149387175399028,
          "along_body_m": -0.053820486667521414,
          "center_vehicle_m": [
            2.36,
            -0.053820486667521414,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 1,
          "yaw_deg": 2.257788899322877,
          "pitch_deg": -0.46677172659118593,
          "along_body_m": -0.11214938007803424,
          "center_vehicle_m": [
            0.23785061992196574,
            -1.1000000000000003,
            1.12
          ]
        },
        {
          "camera_id": 2,
          "yaw_deg": 4.373705802630779,
          "pitch_deg": 2.5426688055305844,
          "along_body_m": 0.054640099912275364,
          "center_vehicle_m": [
            -2.36,
            0.054640099912275364,
            0.8500000000000001
          ]
        },
        {
          "camera_id": 3,
          "yaw_deg": 1.4452945144608176,
          "pitch_deg": -3.5454072400025485,
          "along_body_m": 0.05113499442617225,
          "center_vehicle_m": [
            0.40113499442617223,
            1.1000000000000003,
            1.12
          ]
        }
      ],
      "before_cameras": [
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.141429724369046,
          "validation_p95_px": 8.779539340019946,
          "rotation_error_deg": 3.8876499859434057,
          "center_error_m": 0.053820486667521414
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 3.9024676610153626,
          "validation_p95_px": 4.673160198155526,
          "rotation_error_deg": 2.3055277843421194,
          "center_error_m": 0.11214938007803421
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 10.234454194289702,
          "validation_p95_px": 10.809009804181652,
          "rotation_error_deg": 5.058787132746043,
          "center_error_m": 0.054640099912275336
        },
        {
          "invalid_validation_count": 0,
          "validation_rmse_px": 7.278672321477505,
          "validation_p95_px": 8.339288689612562,
          "rotation_error_deg": 3.828592711859514,
          "center_error_m": 0.05113499442617231
        }
      ],
      "success": true,
      "observations_sha256": "179926e399912155c999832047198748be1d634c1c9105306bac393031b49999",
      "cameras": [
        {
          "camera_id": 0,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.7378162256813825,
          "validation_p95_px": 1.2427901977622047,
          "rotation_error_deg": 0.30010845224884464,
          "center_error_m": 0.045049406775512904,
          "inliers": 69,
          "fit_ms": 3.017972
        },
        {
          "camera_id": 1,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.640013443752348,
          "validation_p95_px": 1.199024634756484,
          "rotation_error_deg": 0.2977303160687439,
          "center_error_m": 0.041586415063963655,
          "inliers": 76,
          "fit_ms": 2.166581
        },
        {
          "camera_id": 2,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.4195638213433195,
          "validation_p95_px": 0.6071337129722583,
          "rotation_error_deg": 0.23088682167840113,
          "center_error_m": 0.008680178797535852,
          "inliers": 72,
          "fit_ms": 2.229379
        },
        {
          "camera_id": 3,
          "invalid_validation_count": 0,
          "validation_rmse_px": 0.6895546095398595,
          "validation_p95_px": 1.1757789776863505,
          "rotation_error_deg": 0.3343554815331292,
          "center_error_m": 0.060463186983292005,
          "inliers": 69,
          "fit_ms": 2.74247
        }
      ]
    }
  ],
  "limitations": [
    "known exact intrinsics; noncoplanar XYZ/UV supplied, no image detector",
    "UV synthesized from truth; visibility/occlusion not checked",
    "validation UV is clean; train noise/outliers controlled",
    "illustrations are actual Blender input/GLES output; not proof of image-based calibration",
    "RANSAC threshold and LM objective use normalized pinhole coordinates",
    "not all calibration families; no server calibration job API yet"
  ]
}
```
