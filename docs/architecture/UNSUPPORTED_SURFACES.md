# Политика unsupported surfaces

Статус: native local-support mask реализована и проверена по [[validation/RENDER_SUPPORT]]. Доставка через wire/typed client API и новая политика заполнения пока являются проектом. Обычный RGBA и существующий wire contract сохранены.

## Что существует

Server shader использует camera eligibility и веса fusion. При отсутствии positive contribution rectangular carrier получает opaque нейтральный fallback, dome — градиент. `fusion.diagnostic=coverage` показывает observed valid-camera count/4 вместо final color; `weights` — распределение весов. Native inspection/probe сохраняет per-camera statuses. Это не parallel typed subscription и не confidence о правильности scene surface. Требуемое разделение network products описано в [[engineering/PROTOCOL_IMPLEMENTED]] и [[architecture/CLIENT_SERVER_MODEL]].

## Различать три понятия

- Sample eligibility: camera projection/source constraints и объявленный occluder predicate пропустили sample. Он может относиться к другой поверхности.
- Positive fusion support: хотя бы один sample реально имеет положительный вес/вклад. Eligible source у границы с нулевым весом не гарантирует вклад.
- Scene observability/correspondence: исходные данные действительно наблюдают выбранную scene point. В текущем опыте это известно из independent Blender truth; в продукте такая готовая маска отсутствует.

Финальный кадр не должен обозначать fallback, interpolation или temporal/prior fill как новое прямое наблюдение. Название coverage не должно обещать geometry confidence.

## Реализованный native этап

`RenderInspectionRequest::support=true` возвращает `RenderInspection::support` в том же вызове renderer, что final RGBA. Формат `render_support_v1`: top-left CV_8UC4, R — число eligible cameras, G — число положительных локальных весов, B — фактически выбранная ветка output, A — признак raster geometry. Коды и ограничения: [[engineering/RENDERING#Native маска локальной поддержки]]. Запрос разрешён только для `diagnostic=color`; ego/background получают нулевые counts. Hybrid использует actual post-smoothing base weights и отдельные path codes. Это не scene visibility, не confidence и не effective multiband support.

Known-answer и RGBA parity проверены для семи алгоритмов на Mesa/NVIDIA. Отдельные controls различают zero weights, hard selection и fallback threshold. Путь маски не требует capture всех signed bands. Исторические frozen collectors/plans не менялись. Следующий этап — доставка в серверную сессию и библиотеку, с проверкой identities, limits и отказов.

## Предлагаемый сетевой этап

Передавать native маску как явно рекламируемый diagnostic product через server и sv-client-lib, с форматом, размерами, frame/input/config identifiers и правилами unsupported capability. Native кодирование зафиксировано; имена сетевых команд, подписки и wire framing ещё не реализованы. Client сможет показывать unsupported region отдельным стилем, а исследования — считать его без сегментации fallback RGB. Отдельно учитывать ego/background/carrier policy и отмену final-frame subscription; существующий протокол пока не реализует эту схему.

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

Native known-answer проверки no-input, projection invalid, eligible-but-zero-weight, positive support, ego/background и RGBA parity выполнены; supplied-box predicate использует ту же функцию проекции shader. Расширить matched captures на occlusion/geometry perturbations и измерить overhead при исследовательских разрешениях. Для wire ещё нужны форматы/limits/revision identity, typed library acceptance/rejection и сценарий восстановления настроек. Никакой pixel mask без source-depth evidence не называть truth observability. Для новых reconstruction/temporal policies нужны отдельные capabilities, data provenance, invalidation/history tests и независимые scene/pose controls.

Для multiband signed/nonlocal contributions локальный positive base weight не равен поддержке конечного pixel: требуется отдельное определение effective support по уровням/доменам и соответствующие oracle tests. Предлагаемая base-weight mask должна называться и интерпретироваться именно как локальная маска; переносить на неё semantic confidence или финальную observed validity всех алгоритмов нельзя. Audit §4.78 относится к edge_feather, не устанавливает это определение для hybrid paths.
