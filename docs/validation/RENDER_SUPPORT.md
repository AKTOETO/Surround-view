# Native local-support mask

Проверено 13.10.2026. Реализован opt-in C++ renderer result, не новая SV01 subscription. Формат и пример использования: [[engineering/RENDERING#Native маска локальной поддержки]]. Основание — [[validation/VISIBILITY_FAILURE]] и [[architecture/UNSUPPORTED_SURFACES]].

## Что проверено

21/21 native GTest на Mesa llvmpipe и NVIDIA GeForce RTX 5070 Ti. Из них 11 новых support tests: семь алгоритмов и четыре contract controls. Tracked summary с именами cases, hashes XML/binary и source fingerprint: [[validation/baselines/render_support_v1.json]]. Бинарник собран при modified working tree поверх указанного revision; fingerprint фиксирует native исходники, а не только HEAD. Полные XML — generated `artifacts/render-support-v1/{mesa,nvidia}.xml`.

| Проверка | Результат |
|---|---|
| edge_feather/angular_feather/hard_best_angle + четыре hybrid modes | Counts совпали с source status; hybrid G совпал с actual post-smoothing weights |
| Black RGB | Valid samples имеют положительную поддержку независимо от яркости |
| Missing input, behind camera, outside image | Не создают eligibility |
| Background, footprint, depth-tested ego model | Counts нулевые, path/alpha соответствуют raster domain |
| No input / повторное использование destination | Нет fusion path; выключенный запрос очищает mask |
| RGBA до/во время/после capture | Побайтное равенство, без дополнительных source uploads |
| Zero edge weights | GPU feather fallback; hard_best_angle выбирает одну камеру; hybrid выбирает fusion при eligibility>0 |
| Positive weights с суммой ниже GPU threshold | G>0 совместим с gpu_fallback |
| graph_cut_multi_band seam smoothing | Returned blend_weights равны full inspection, отличаются от decision weights; цвет unchanged |
| Нет destination / не-color diagnostic | Явный invalid_argument |

Zero-weight control намеренно задаёт degenerate intrinsics напрямую renderer; threshold control задаёт edge_width_px выше публичного bound. Это known-answer arithmetic fixtures, а не допустимые пользовательские конфигурации или демонстрация качества калибровки. Seven-algorithm raster fixture использует 96×64 плоский carrier и одну passing camera; post-smoothing control отдельно использует две перекрывающиеся маски. Старые seven-carrier tests в том же бинарнике проверяют geometry positions/parity, **не** означают полноту support-mask trials на всех носителях.

Полная сборка прошла. Дополнительная CTest регрессия9/9: core, render_modes, source_correspondence, native_fusion_parity, render_fusion_parity, replay_ipc, client_transports, fusion_boundary, research_scenarios. Это выбранный набор, не весь CTest suite.

## Повторение

```bash
cmake --build build -j 4
__EGL_VENDOR_LIBRARY_FILENAMES=/usr/share/glvnd/egl_vendor.d/50_mesa.json \
  LIBGL_ALWAYS_SOFTWARE=1 EGL_PLATFORM=surfaceless \
  build/sv-carrier-inspection-tests \
  --gtest_output=xml:artifacts/render-support-v1/mesa.xml
__EGL_VENDOR_LIBRARY_FILENAMES=/usr/share/glvnd/egl_vendor.d/10_nvidia.json \
  EGL_PLATFORM=surfaceless build/sv-carrier-inspection-tests \
  --gtest_output=xml:artifacts/render-support-v1/nvidia.xml
__EGL_VENDOR_LIBRARY_FILENAMES=/usr/share/glvnd/egl_vendor.d/50_mesa.json \
  ctest --test-dir build --output-on-failure \
  -R '^(core|render_modes|source_correspondence|native_fusion_parity|render_fusion_parity|replay_ipc|client_transports|fusion_boundary|research_scenarios)$'
```

## Граница результата

Новый формат отделяет local eligibility, local positive weights и выбранную ветку. Он не устраняет source-surface mismatch, не восстанавливает скрытые поверхности, не измеряет effective signed/nonlocal multiband support. Fallback RGB и frozen collectors/plans не менялись. Native overhead timer добавлен; benchmark при исследовательских разрешениях ещё нужен. Требуются matched occlusion/geometry captures, typed server/library delivery с frame/config/input identity, limits и отказами, затем independent scene/appearance holdout. На Aurora это ещё не проверено.
