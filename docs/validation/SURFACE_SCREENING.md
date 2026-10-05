# Screening геометрии и fusion: Linux 0.4.0

Дата: 06.10.2026. Реализация — `4b6e4b4`, сравнительный стенд — `616380c`. Это подготовительный опыт E-STITCH-01, не завершённое исследование качества. Методика и команды: [[engineering/RENDERING]], источник: [[engineering/BLENDER]].

## Происхождение

- Manifest SHA-256: `d91af117739c24ec52f173369692ad9793804ed1837fecd65f6751f5b7eb96bc`.
- Config SHA-256: `cdf90d578d165f63734678a0709e9bc3a71199e13ba7095771bd351e4d1a9bca`.
- Binary SHA-256: `9553b6f69ea947ddc288622db4a95b1b830c9f8c51e5a6104fea111b0bb96b42`.
- Script SHA-256: `11a6e115ff9a3c0ade0dc238fbd3b0669c965f813280b8ffa63260d6f0fd9f30`.
- Report SHA-256: `9c0a515ebe2822b8323572b297937262fa5d4d13621f6d8fd6ba4fbba0158af2`.
- Первичные локальные результаты: `artifacts/surface-screen-v04-final/{report.json,REPORT.md}`; 70 config/render cases. Большие generated outputs исключены из Git; рисунки и этот компактный отчёт сохраняются в документации.

На всех 70 cases renderer: **llvmpipe (LLVM 22.1.8, 256 bits)**. Четыре разнесённые Blender-камеры, только первый набор записи, 960×540 RGBA. Два ракурса: azimuth=0.8 rad, distance=8.5 m, elevation=1.0/0.35 rad. Каждый bench использует 1 warmup и 3 measurements. Такой sample count годится для smoke, не для performance ranking. Длины сторон/радиусы 12 м; сетки имеют разные бюджеты.

## Покрытие при низком ракурсе

| Носитель | Треугольники | 0 камер | 1 камера | 2 камеры | Маска | За геометрией |
|---|---:|---:|---:|---:|---:|---:|
| plane | 8712 | 296 | 180669 | 151625 | 12098 | 173712 |
| bowl | 8712 | 296 | 188486 | 168053 | 12098 | 149467 |
| dome | 24320 | 296 | 265387 | 240619 | 12098 | 0 |
| cylinder | 24320 | 296 | 265035 | 240971 | 12098 | 0 |
| cube | 12552 | 296 | 264962 | 241044 | 12098 | 0 |

Сумма категорий в каждой строке — 518400 пикселей. Три/четыре камеры в этих двух ракурсах не наблюдают ни одного пикселя. На oblique-ракурсе outside_surface: plane 10386, bowl 1533, dome/cylinder/cube 0. Это не доказательство покрытия всей поверхности или отсутствия occlusion: считаются только видимые в output точки и валидность проекции.

Замкнутые оболочки устраняют экран за геометрией. Они сохраняют 296 ненаблюдаемых пикселей при low-view; в color эти области получают fallback. Различия 1/2-camera coverage не устанавливают лучший carrier: меняются геометрия точек и дискретизация. Hard-вариант убирает межкамерное усреднение, но может давать резкий шов. Для ранжирования нужны metric markers, depth/visibility truth, фотометрические возмущения, видео и общий resource budget.

![Носители и fusion](../diploma/figures/experiments/04_surface_fusion_screen.png)

*Рисунок V.1 — Actual GLES readbacks пяти носителей и трёх fusion-режимов на одном Blender-наборе; общий low-view. Неравные сетки, без оценки seam/ghosting error.*

![Диагностика](../diploma/figures/experiments/04_surface_diagnostics.png)

*Рисунок V.2 — Coverage и palette contributions edge-feather. Gray показывает число валидных камер, magenta — footprint ТС; blue-gray фон plane/bowl находится за геометрией. Скрипт рядом с главой: `diploma/plot_screening.py`.*

## Квалификация реализации

| Проверка | Результат | Граница |
|---|---|---|
| Release CTest | 8/8 | Включая render_modes и прежние integration tests |
| CPU Debug ASan/UBSan CTest | 6/6 | GPU/Qt выключены |
| Native build suite | 25 pass, 0 fail, 0 skip | 3 iterations, 1 warmup, 1 repeat |
| Installed Linux suite | 25 pass, 0 fail, 0 skip | Запущен из CMake install prefix |
| Enclosure GPU oracle | 36 допустимых ракурсов | 3 формы × 3 elevation × 4 azimuth; empty-input diagnostic |

Native build revision: `616380c4550d3c7c60dba4c13dc95cbef21330c1`; source fingerprint `27b57186112759ef8d9a131695ed65f0db7f9a4b8792d032b422e38fa69cafe7`.

Native/installed первичные JSON: `artifacts/platform-v04-final/report.json`, `artifacts/platform-v04-installed/report.json`. Оба — короткие проверки. RPM 0.4.0 не пересобран; исторические Linux RPM smoke остаются отдельными результатами. SDK, устройство Авроры, real camera capture и sustained performance ещё не проверены. Для нового аппаратного baseline использовать полные параметры [[engineering/PLATFORM_TEST]] и одну source revision на ПК/устройстве.
