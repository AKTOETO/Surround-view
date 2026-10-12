# Matched hybrid source correspondence и знаковые полосы

Дата:12.10.2026. Follow-up [[validation/DISPLACED_CORRESPONDENCE]], диплом §4.67. Case rig101-near-z080, frames0/1, пять carriers, true calibration,800×800 inputs и640×360 output. Шесть fusion/boundary variants, **60 native условий** в10 replay invocations. План `configs/research/hybrid-correspondence-plan.json`, baseline `docs/validation/baselines/hybrid_correspondence_v1.json`, raw root `artifacts/hybrid-correspondence-v1`.

## Фиксация и native изменения

`8b30597` добавляет opt-in `FusionInspection::level_contributions`: четыре signed weighted linear-RGB bands на каждом native pyramid level, до реконструкции/clamp. Арифметика fusion и обычный renderer не заменены. `sv-render-fusion-probe` поддерживает `CONFIG MANIFEST OUTPUT [FRAME_INDEX] [--color-only]`, записывает реальные source paths/hashes/timestamp, effective fusion settings, carrier positions и hashes/bytes всех exported products. По умолчанию сохраняет прежние18 combinations первого frame; `--color-only` даёт6.

План и independent collector зафиксированы `bdf988d` до capture. Native source revision `8b30597943f0cb9bd6d89d493d8225c73969764e`, source fingerprint `8453b30d9553eac3c5070d0f7deeea7a829c7935394cd0fbc7644ed5eb10706e`. CMake configure и build выполнены после фиксации native API. Сохранён exact executable SHA256 `f72d131c8b71835d3df1961ac422144bf6b2c5ad271b3ab2c9c575e5c4604c2c`; product source/shader hashes также frozen. Device: NVIDIA GeForce RTX5070Ti/PCIe/SSE2. Это новая сборка; её binary hash не должен совпадать с прежним edge probe.

Шесть combinations: seam_distance_feather/zero, graph_cut_seam/zero, multi_band/zero, graph_cut_multi_band/zero, multi_band/normalized, graph_cut_multi_band/normalized. Native defaults: pyramid_levels4, smoothness_weight0.1, edge_width24px, binary_pairs solver. Nonmultiband inspection имеет один level. Alpha-expansion/GPU-only fusion в этой серии не проверяются.

## Условия парности и oracle

Для **всех60** условий проверено полное совпадение regions, carrier positions и actual source UV/status с соответствующим прежним edge-feather frame/carrier. Float matrices сравниваются с учётом NaN invalid pixels. Hybrid fallback RGBA SHA256 также совпал с прежним ordinary edge output. Включение inspection не меняет native RGBA, uploads4 и mesh_builds1. Config/manifest/input/file hashes проверены.

Это позволяет повторно использовать independent Blender scene/source hits §§4.62–4.66 **на прежних pixel domains**: expected target, source-visible expected target, fixed edge RGB proxy. Новые hybrid chroma pixels вне этих domains не получают surface label автоматически. Нового Blender RGB/ray capture не выполнялось.

Independent SciPy `zoom(order=1, grid_mode=False)` reconstructs каждый camera contribution из signed native bands с исходной цепочкой dimensions. Max error по всем60 условиям1.490116119e−8 linear RGB, меньше frozen2e−6. Band coefficients не смешиваются с RGB identity. Sum camera contributions, clamp/encode, coverage fallback и ego composition реконструируют обычный native RGBA с max error1 RGB8 LSB, в пределах frozen1LSB. Native level weights неотрицательны и нормированы (sum0 либо1 с max tolerance2e−6). Negative band values сохраняются; их нельзя интерпретировать как opacity.

## Результат: геометрия не восстановлена смешиванием

В таблице — число raw magenta proxy pixels full frame, без component filtering. Последняя колонка относится к **всем пяти carriers и двум frames** данного варианта; plane/bowl показаны отдельно для компактности. Полные60 строк и SHA находятся в baseline.

| Method / boundary | Plane f0 | Bowl f0 | Plane f1 | Bowl f1 | Expected target overlap, все10 carrier/frame |
|---|---:|---:|---:|---:|---:|---:|
| seam_distance_feather / zero | 4095 | 4405 | 5388 | 5603 | 0 |
| graph_cut_seam / zero | 1039 | 1162 | 3789 | 3927 | 0 |
| multi_band / zero | 4830 | 6250 | 5374 | 5597 | 0 |
| graph_cut_multi_band / zero | 1308 | 1458 | 4097 | 4241 | 0 |
| multi_band / normalized | 4840 | 6310 | 5374 | 5597 | 0 |
| graph_cut_multi_band / normalized | 1308 | 1458 | 4097 | 4241 | 0 |

Во всех60 случаях raw target-color overlap с expected target mask равен0; local target-ray blend weight также0 в expected ROI. Source-visible conditional pixels76/69 сохранены. Graph-cut уменьшает displaced proxy в frame0, но в frame1 его median local target-ray weight на fixed edge proxy меняется с0 до1. Поэтому меньшее число цветных pixels не доказывает качество восстановления объекта; independent expected position остаётся пустой по этому proxy.

Новые proxy pixels вне старых ray domains присутствуют у каждого family: суммарно131 для seam EDT,369 для cut,16 для MB/zero,264 для cut+MB/zero,183 для MB/normalized,264 для cut+MB/normalized (коррелированные carrier/frame суммы). Не выдавать их за уже классифицированные geometric ghosts. Нужны новые ray casts/footprints.

## Нелокальные contributions

Таблица считает pixels source-visible expected ROI, где хотя бы одна **локально недопустимая** camera имеет reconstructed contribution с max abs channel>1e−6. Это signed linear RGB до clamp/composition, не opacity или target identity.

| Plane variant | f0: pixels /76 | f0 max abs linear RGB | f1: pixels /69 | f1 max abs linear RGB |
|---|---:|---:|---:|---:|
| seam_distance_feather / zero | 0 | 0.000000000 | 0 | 0.000000000 |
| graph_cut_seam / zero | 0 | 0.000000000 | 0 | 0.000000000 |
| multi_band / zero | 76 | 0.001903697 | 29 | 0.000018542 |
| graph_cut_multi_band / zero | 76 | 0.001437428 | 10 | 0.000011461 |
| multi_band / normalized | 76 | 0.008976149 | 68 | 0.000396502 |
| graph_cut_multi_band / normalized | 76 | 0.006959576 | 53 | 0.000361177 |

У seam EDT/cut contributions локальны. У multiband присутствует цветовой вклад cameras, чей local projection invalid. Normalized-support здесь увеличивает nonlocal magnitude/count, но это не доказательство худшего RGB качества: измеряется поддержка reconstructed bands в конкретном ROI, а не общий quality metric. Nonlocal contribution не возвращает correct surface correspondence; raw expected target-color overlap остаётся0.

Для диагностики каждого уровня сохраняются native normalized coefficients и signed weighted bands. Mean abs propagated band используется только для описания масштаба; signed cancellation означает, что суммы magnitude не являются долями результата. Native source validity/weight0 не доказывают отсутствие ненулевого pyramid contribution.

## Воспроизведение

Предпосылки: прежние raw source/Blender/edge/displaced artifacts, GLES/NVIDIA для точного paired capture, system C++ dependencies, NumPy/SciPy/Pillow/Matplotlib для независимого audit. C++ API/compiler и product pipeline остаются native; Python не является renderer.

```sh
cmake --build build --target sv-render-fusion-probe sv-fusion-boundary-tests sv-server -j4
ctest --test-dir build -R '^fusion_boundary$' --output-on-failure
__EGL_VENDOR_LIBRARY_FILENAMES=/usr/share/glvnd/egl_vendor.d/50_mesa.json \
  ctest --test-dir build -R '^render_fusion_parity$' --output-on-failure
python3 tests/test_source_correspondence.py
MPLCONFIGDIR=/tmp/sv-matplotlib python3 docs/diploma/plot_hybrid_correspondence.py \
  --output-report artifacts/hybrid-correspondence-v1/reaudit.json
cmp docs/validation/baselines/hybrid_correspondence_v1.json artifacts/hybrid-correspondence-v1/reaudit.json
```

Integration test открывает локальный IPC socket; первый sandbox запуск остановился на bind: Operation not permitted. Повтор с разрешённым IPC прошёл. Итог: render_fusion_parity + fusion_boundary CTest2/2, C++ fusion GTest7/7, independent correspondence controls9/9. Новый integration test проверяет настоящий second frame с отличающимся RGB,6 color variants/settings/file hashes и отказ invalid/out-of-range/overflow indices до создания каталога. Legacy18-case carrier/composition/SV01 checks сохранены.

Новый native GTest для constant fields проверяет zero fine bands и known coarse weighted values; reset/all-invalid bands проверены. Read-only SciPy oracle проверяет signed band reconstruction на60 реальных captures. Повторный audit побитно воспроизводит baseline.

Для повторного capture скопируйте план и задайте новый output_root. Используйте сохранённый binary, иначе оформляйте отдельную версию native provenance и обновляйте binary/source hashes. Пример из корня проекта (новый каталог должен отсутствовать):

```python
from pathlib import Path
import hashlib, json, os, shutil, subprocess
root=Path.cwd(); path=root/'artifacts/hybrid-repeat-plan.json'
plan=json.loads((root/'configs/research/hybrid-correspondence-plan.json').read_text())
plan['output_root']='artifacts/hybrid-repeat'
with path.open('x') as stream:
    stream.write(json.dumps(plan,indent=2)+'\n')
for name,digest in plan['frozen_files'].items():
    assert hashlib.sha256((root/name).read_bytes()).hexdigest()==digest, name
binary=root/'artifacts/hybrid-correspondence-v1/sv-render-fusion-probe'
assert hashlib.sha256(binary.read_bytes()).hexdigest()==plan['binary_sha256']
output=root/plan['output_root']; output.mkdir(exist_ok=False)
shutil.copy2(binary,output/binary.name)
record={'binary_sha256':plan['binary_sha256'],
        'plan_sha256':hashlib.sha256(path.read_bytes()).hexdigest(),'cases':[]}
for frame in plan['frames']:
    for carrier in plan['carriers']:
        argv=[str(output/binary.name),str(root/plan['config_root']/carrier/'config.json'),
              str(root/plan['manifest']),str(output/f'frame{frame}'/carrier),str(frame),'--color-only']
        subprocess.run(argv,check=True,timeout=240,env=dict(os.environ,
            __EGL_VENDOR_LIBRARY_FILENAMES='/usr/share/glvnd/egl_vendor.d/10_nvidia.json',EGL_PLATFORM='surfaceless'))
        record['cases'].append({'frame':frame,'carrier':carrier,'argv':argv,'returncode':0})
        (output/'invocations.json').write_text(json.dumps(record,indent=2)+'\n')
```

Audit новой серии: `python3 docs/diploma/plot_hybrid_correspondence.py --plan artifacts/hybrid-repeat-plan.json --output-report artifacts/hybrid-repeat/summary.json --figures artifacts/hybrid-repeat/figures`. Копия плана/output меняют hashes, это ожидаемо. Capture требует same device для paired audit; Mesa regression fixtures не заменяют NVIDIA series.

## Следующий этап

Matched hybrid attribution pilot завершён, но исправление height/occlusion failure не продемонстрировано. Следующий контроль — four-texel surface footprints и source camera resampling для сохранённых point-ray/RGB mismatches, включая новые hybrid proxy pixels. Затем проверяемая mitigation на unchanged inputs/independent truth. Другие scenes/mount seeds/optics, long clips, alpha-expansion, GPU-only contribution capture, triangle precision/coverage и bounded server/library diagnostic delivery остаются открытыми; physical/Aurora validation отсутствует.

Следующий bounded four-texel контроль прежних edge mismatches выполнен: [[validation/SOURCE_FOOTPRINT]], §4.68.1024/1164 neighboring target support,140 upstream RGB-center residual. Новые hybrid domains этим опытом не классифицированы.

Upstream footprint follow-up прежних edge residuals выполнен: [[validation/CUBE_FOOTPRINT]], §4.69.135 cube-center/5 area-only support, RGB reconstruction0LSB. Новые hybrid proxy coordinates пока требуют отдельного truth.

Follow-up новых hybrid domains выполнен: [[validation/HYBRID_DOMAIN]], §4.70. Все547 unique coordinates/1227 condition pixels получили independent virtual/source/texel labels; local target central-ray support0,698 texel support/529 без него. Full spatial origin signed bands не установлен.
