# Политика unsupported surfaces

Статус: проект по результатам [[validation/VISIBILITY_FAILURE]], не реализованный новый wire/API contract. Совместимость существующего отображения сохраняется до отдельного native изменения.

## Что существует

Server shader использует camera eligibility и веса fusion. При отсутствии positive contribution rectangular carrier получает opaque нейтральный fallback, dome — градиент. `fusion.diagnostic=coverage` показывает observed valid-camera count/4 вместо final color; `weights` — распределение весов. Native inspection/probe сохраняет per-camera statuses. Это не parallel typed subscription и не confidence о правильности scene surface. Требуемое разделение network products описано в [[engineering/PROTOCOL_IMPLEMENTED]] и [[architecture/CLIENT_SERVER_MODEL]].

## Различать три понятия

- Sample eligibility: camera projection/source constraints и объявленный occluder predicate пропустили sample. Он может относиться к другой поверхности.
- Positive fusion support: хотя бы один sample реально имеет положительный вес/вклад. Eligible source у границы с нулевым весом не гарантирует вклад.
- Scene observability/correspondence: исходные данные действительно наблюдают выбранную scene point. В текущем опыте это известно из independent Blender truth; в продукте такая готовая маска отсутствует.

Финальный кадр не должен обозначать fallback, interpolation или temporal/prior fill как новое прямое наблюдение. Название coverage не должно обещать geometry confidence.

## Предлагаемый native этап

Сформировать отдельно camera-validity count и positive-weight support mask в том же frame/geometry revision, что final RGBA. Передавать их как явно рекламируемые diagnostic products через server и sv-client-lib, с форматом, размерами, frame/input/config identifiers и правилами unsupported capability. Это проект API, точные product names и кодирование ещё не зафиксированы. Client может показывать unsupported region отдельным стилем, а исследования — считать его без сегментации fallback RGB. Отдельно учитывать ego/background/carrier policy и отмену final-frame subscription; существующий протокол пока не реализует всю эту схему.

```plantuml
@startuml
participant "sv-server renderer" as Render
participant "sv-server session
(proposed diagnostics)" as Server
participant "sv-client-lib
(proposed typed products)" as Lib
participant "example client" as Client
Client -> Lib: subscribe requested diagnostic products
Lib -> Server: capability-checked request
Server -> Render: render with support export
Render --> Server: RGBA + eligibility + positive support
shared frame/config/input identity
Server --> Lib: requested products / explicit unsupported error
Lib --> Client: typed data with provenance
Client -> Client: display fallback as unsupported
separately from observed image
note over Server,Client
This sequence is a proposed extension.
Existing coverage view replaces color;
parallel typed products are not implemented.
end note
@enduml
```

Рисунок — Предлагаемая доставка support diagnostics; наличие диаграммы не означает реализованного протокола.

## Проверки до включения

Разделить no-input, projection invalid, occlusion rejected, eligible-but-zero-weight и positive support; сохранить RGBA parity при выключенном export и проверить форматы/limits/revision identity. Проверить GPU и hybrid semantics, ordinary/inspection paths, ego/background и memory overhead. Добавить typed library acceptance/rejection и сценарий восстановления настроек. Никакой pixel mask без source-depth evidence не называть truth observability. Для новых reconstruction/temporal policies нужны отдельные capabilities, data provenance, invalidation/history tests и независимые scene/pose controls.

Для multiband signed/nonlocal contributions локальный positive base weight не равен поддержке конечного pixel: требуется отдельное определение effective support по уровням/доменам и соответствующие oracle tests. Предлагаемая base-weight mask должна называться и интерпретироваться именно как локальная маска; переносить на неё semantic confidence или финальную observed validity всех алгоритмов нельзя. Audit §4.78 относится к edge_feather, не устанавливает это определение для hybrid paths.
