# Доставка native support mask через SV01

Проверено 13.10.2026. Native формат [[validation/RENDER_SUPPORT]] доставляется одним выбранным продуктом через sv-server и sv-client-lib. Контракт: [[engineering/PROTOCOL_IMPLEMENTED#Выбор data product: output_selection_v1 / render_support_v1]], сценарий С12: [[engineering/PROTOCOL_SCENARIOS]]. Baseline с code/test/binary provenance: [[validation/baselines/output_products_v1.json]].

## Проверки

Полная сборка и Mesa CTest **55/55** (28.02s). Пять новых C++ decoder/view tests и три новых live integration tests входят в suite. Те же три integration tests прошли на NVIDIA RTX5070Ti (3.555s). CPU-profile client-probe/decoder собраны; client_output прошёл1/1 CTest (пять GTest cases), ldd consumer не содержит OpenCV/Qt. Installed external CMake consumer собирается/запускается в прежнем client_transports test; новый --support mode проверен отдельными live tests для in-tree binary.

| Контроль | Результат |
|---|---|
| Seven fusion modes, source/config/view одинаковы | Complete wire payload побайтно равен native mask для каждого режима на Mesa/NVIDIA |
| Header provenance | inputs/frame_set/upload_count сохраняются при paused output switch; config/state/applied command identity проверены |
| Default, catalog, selector | final_frame по умолчанию, catalog read-only, selector увеличивает state revision без config revision |
| Invalid/unknown product и color conflict в обе стороны | Reject до mutation, settings/revisions сохраняются |
| Lease active | catalog разрешён; configure_output отвергается даже owner; lease restore завершается |
| Возврат final_frame | Исходный RGBA восстанавливается побайтно после возврата исходного fusion mode |
| Persistence | Config file не изменяется |
| Held diagnostic frame | Control остаётся жив, release timeout закрывает data, reconnect возвращает final_frame |
| Десять немедленных reconnect после selector ACK | Ни одна новая generation не получает прежний diagnostic product |
| Typed C++ consumer | Unix и TCP loopback принимают/декодируют маску и освобождают slot |
| Malformed metadata/provenance/pixels | Invalid dimensions/stride/schema/identities, large camera IDs, unknown paths/counts/domains rejected |
| Owning view | Payload не копируется; provenance сохраняется; bounds проверяются; владение живёт независимо от исходного shared_ptr |

Pixel decoder сохраняет важные negative cases native формата: GPU fallback с положительными weights допустим; hybrid_fusion с zero weights допустим. Final/legacy frame без output_product сохраняет прежнюю общую transport validation. Hybrid inspection дополнительно удерживает samples/ego/fallback; 4 bytes/pixel маски не описывают весь peak memory overhead. Схема маски проверяется до callback и повторно при construction view; это linear scan payload, overhead нужно измерять отдельно.

Live fixture — один generated frame/96×64, фиксированный view и прежняя calibration. В parity matrix используется default pyramid_boundary=zero и binary_pairs. Это verification product transport, а не independent scene-quality oracle, не полное исследование всех geometry/pyramid/solver комбинаций и не two-host испытание. Future protocol commands и одновременно несколько продуктов тестами не объявляются реализованными.

## Воспроизведение

```bash
cmake --build build -j 4
__EGL_VENDOR_LIBRARY_FILENAMES=/usr/share/glvnd/egl_vendor.d/50_mesa.json \
  ctest --test-dir build --output-on-failure -j 4
__EGL_VENDOR_LIBRARY_FILENAMES=/usr/share/glvnd/egl_vendor.d/10_nvidia.json \
  EGL_PLATFORM=surfaceless python3 tests/test_output_products.py build configs/synthetic.json
cmake --build build-cpu --target sv-client-probe sv-client-output-tests -j 4
ctest --test-dir build-cpu -R '^client_output$' --output-on-failure
```

build-cpu — ранее сконфигурированная SV_GPU=OFF сборка. Новый test-only sv-support-wire-probe нужен для native/wire comparison и не устанавливается как продуктовый инструмент. Historical frozen collectors/plans не изменялись. Raw full/CPU CTest logs скопированы в generated artifacts/output-products-v1; tracked baseline хранит их hashes и hashes binaries/tests. Тестовый бинарник собран из modified tree поверх build_source_revision; implementation_commit фиксирует соответствующий code snapshot. NVIDIA результат — отдельный прямой unittest run, не вся CTest suite на NVIDIA.

## Следующий этап

GUI selector/palette без JSON, renderer/transport overhead при исследовательских разрешениях, matched occlusion/geometry captures и independent natural-appearance/correspondence holdout. Общий registry остальных diagnostics, parallel subscriptions, multiple active clients и control-only mode остаются отдельными задачами. Устройство Aurora/two-host/реальные камеры здесь не проверялись.
