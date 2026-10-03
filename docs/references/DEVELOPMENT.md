# Разработка и графический конвейер

> Описания перенесены из исходных материалов. Ссылки и применимость к выбранным версиям предстоит проверить при чтении; перенос не означает, что источник уже изучен.

Каталог: [[references/README|Источники и порядок чтения]].

## S13

**Varun Ravi Kumar and others. Surround View Fisheye Camera Perception for Automated Driving: Overview Survey and Challenges**

[Открыть источник](https://arxiv.org/abs/2205.13281)

**Уровень:** Средний обзорный

**Формат:** Обзорная научная статья, английский язык

**Разделы главы:** 1.1, 1.2, 1.3 и 1.6

Обзор систем на основе четырех автомобильных fisheye-камер. Статья систематизирует модели камер, задачи ближнего обзора, требования к точности и характерные проблемы сильной дисторсии. Для данной диссертации полезны разделы о геометрии и калибровке; части, посвященные нейросетевому восприятию, можно использовать только для общего контекста.

**Что изучить:**

- назначение четырех fisheye-камер
- особенности ближней зоны
- классификация моделей проекции
- проблемы калибровки
- ограничения Surround View

## S15

**ISO IEC IEEE 29148:2018. Systems and Software Engineering Life Cycle Processes Requirements Engineering**

[Открыть источник](https://www.iso.org/standard/72089.html)

**Уровень:** Базовый методический

**Формат:** Международный стандарт, английский язык

**Разделы главы:** 2.1, 2.2 и 2.3

Стандарт задает правила формирования, анализа и проверки требований к программным и программно-аппаратным системам. Он помогает отделить функции системы от ограничений реализации и сформулировать проверяемые требования. Для диплома достаточно использовать структуру требований и свойства качественной формулировки, не воспроизводя полный промышленный процесс.

**Что изучить:**

- функциональные и нефункциональные требования
- ограничения и допущения
- проверяемость требования
- критерии приемки
- трассировка требований на испытания

## S16

**Guide to the Software Engineering Body of Knowledge SWEBOK Guide Version 4**

[Открыть источник](https://www.computer.org/education/bodies-of-knowledge/software-engineering)

**Уровень:** Базовый — средний методический

**Формат:** Официальное руководство IEEE Computer Society, английский язык

**Разделы главы:** 2.1, 2.2, 2.3, 2.8, 2.9 и 2.10

Руководство систематизирует процессы работы с требованиями, архитектурой, проектированием, тестированием и качеством программного обеспечения. В аналитической главе оно полезно для обоснования перехода от требований к архитектурным решениям и программе экспериментов. Читать следует выборочно разделы Software Requirements, Software Design, Software Testing и Software Quality.

**Что изучить:**

- анализ требований
- архитектурное проектирование
- качество программного обеспечения
- планирование испытаний
- связь требований и тестов

## S17

**Simon Brown. The C4 Model for Visualising Software Architecture**

[Открыть источник](https://c4model.com/)

**Уровень:** Базовый практический

**Формат:** Официальное руководство и видео, английский язык

**Разделы главы:** 2.8

C4 Model предлагает четыре уровня архитектурного описания: контекст, контейнеры, компоненты и код. Для проекта достаточно первых трех уровней и отдельной диаграммы развертывания. Источник поможет единообразно показать связи между sv-client, sv-server, sv-configurator, sv-simulator, целевым устройством и рабочей станцией.

**Что изучить:**

- System Context diagram
- Container diagram
- Component diagram
- границы ответственности компонентов
- подписи связей и протоколов

## S24

**Eigen. Getting Started and Geometry Module**

[Открыть источник](https://eigen.tuxfamily.org/dox/GettingStarted.html)

**Уровень:** Базовый — средний практический

**Формат:** Официальная документация библиотеки C++, английский язык

**Разделы главы:** 3.2 и 3.3

Eigen предоставляет типы матриц, векторов, преобразований и разложений, удобные для независимого математического ядра. Документация нужна для аккуратной реализации переходов между системами координат камеры, автомобиля, поверхности и виртуальной камеры. Следует отдельно изучить Geometry Module и правила выравнивания данных.

**Что изучить:**

- Matrix и Vector
- Affine и Isometry transforms
- AngleAxis и Quaternion
- решение линейных систем
- передача матриц в OpenGL

## S25

**Ceres Solver. Nonlinear Least Squares Tutorial**

[Открыть источник](https://ceres-solver.readthedocs.io/latest/nnls_tutorial.html)

**Уровень:** Средний практический

**Формат:** Официальное руководство, английский язык

**Разделы главы:** 3.3 и 3.4

Руководство показывает формулирование нелинейной задачи наименьших квадратов, вычисление производных и применение робастных функций потерь. Ceres можно использовать в настольной утилите калибровки или в исследовательском прототипе для уточнения положения камер по ошибке репроекции и расхождению перекрывающихся проекций. Возможность сборки на целевой платформе следует проверять отдельно.

**Что изучить:**

- ResidualBlock
- CostFunction
- automatic differentiation
- robust loss functions
- ограничение и фиксация параметров

## S26

**Khronos Group. OpenGL ES Registry**

[Открыть источник](https://registry.khronos.org/OpenGL/index_es.php)

**Уровень:** Средний практический

**Формат:** Официальные спецификации и справочные страницы, английский язык

**Разделы главы:** 3.6 и 3.11

Официальный набор спецификаций OpenGL ES разных версий. Источник требуется для реализации переносимого GPU-конвейера без зависимости от возможностей настольной видеокарты. Перед разработкой необходимо определить фактическую версию OpenGL ES и расширения целевого устройства, после чего ограничить шейдеры и форматы текстур этим набором возможностей.

**Что изучить:**

- буферы вершин и индексов
- текстуры и sampler
- framebuffer objects
- форматы пикселей
- ограничения конкретной версии OpenGL ES

## S27

**Khronos Group. OpenGL ES Shading Language Specification**

[Открыть источник](https://registry.khronos.org/OpenGL/specs/es/3.2/GLSL_ES_Specification_3.20.html)

**Уровень:** Средний — углубленный практический

**Формат:** Официальная спецификация GLSL ES, английский язык

**Разделы главы:** 3.6

Спецификация языка шейдеров нужна для корректной реализации проекции fisheye-кадров на сетку, преобразования цветовых форматов и смешивания камер. В проекте основная часть вычислений может выполняться fragment shader, а геометрия параметрической поверхности — vertex shader. Использовать следует версию GLSL ES, поддержанную реальным устройством.

**Что изучить:**

- vertex и fragment shaders
- uniform и varying данные
- выборка нескольких текстур
- precision qualifiers
- смешивание по весовым маскам

## S29

**Linux Kernel Documentation. Video for Linux API Version 2**

[Открыть источник](https://docs.kernel.org/userspace-api/media/v4l/v4l2.html)

**Уровень:** Средний системный

**Формат:** Официальная документация ядра Linux, английский язык

**Разделы главы:** 3.5 и 3.11

V4L2 является низкоуровневым интерфейсом захвата видео в Linux. Документация необходима, если sv-server должен получать кадры непосредственно от USB- или CSI-камер. Особое внимание следует уделить потоковому вводу, буферам, временным меткам, форматам YUV и возможностям минимизации копирования памяти.

**Что изучить:**

- device capabilities
- streaming I/O
- memory mapped buffers
- timestamps
- single и multi planar formats

## S30

**GStreamer. Application Development Manual**

[Открыть источник](https://gstreamer.freedesktop.org/documentation/application-development/)

**Уровень:** Средний практический

**Формат:** Официальное руководство, английский язык

**Разделы главы:** 3.5, 3.9 и 3.10

Руководство объясняет устройство мультимедийного pipeline, элементы, pads, caps, buffers и обработку событий. GStreamer можно использовать для приема четырех потоков от камер или симулятора, декодирования, назначения временных меток и передачи кадров вычислительному ядру. Необходимо заранее проверить наличие требуемых плагинов в целевой сборке Авроры.

**Что изучить:**

- pipeline и elements
- caps negotiation
- GstBuffer и metadata
- appsink и appsrc
- обработка ошибок и изменения состояния

## S31

**GStreamer. Clocks and Synchronization**

[Открыть источник](https://gstreamer.freedesktop.org/documentation/application-development/advanced/clocks.html)

**Уровень:** Средний — углубленный системный

**Формат:** Официальное руководство, английский язык

**Разделы главы:** 3.5 и 3.10

Материал описывает общий GstClock, временные метки буферов, running time и механизм синхронизации потоков. Для системы из четырех камер это основной источник по выбору кадров, относящихся к одному моменту времени. На его основе можно определить допустимый временной разброс и политику ожидания, повторения или отбрасывания кадров.

**Что изучить:**

- GstClock
- PTS и DTS
- base time и running time
- синхронизация live sources
- политика обработки опоздавших кадров

## S32

**Blender Manual. Cameras and Panoramic Cameras**

[Открыть источник](https://docs.blender.org/manual/en/latest/render/cycles/object_settings/cameras.html)

**Уровень:** Базовый — средний практический

**Формат:** Официальное руководство, английский язык

**Разделы главы:** 3.9

Документация описывает обычные и панорамные камеры Blender, включая fisheye equidistant, fisheye equisolid и полиномиальную fisheye-модель. Источник нужен для построения sv-simulator с четырьмя виртуальными камерами и известными эталонными параметрами. Следует выбрать модель, наиболее близкую используемой модели OpenCV, и зафиксировать способ преобразования параметров.

**Что изучить:**

- панорамные типы камер
- поле зрения
- fisheye equidistant и equisolid
- положение камер относительно автомобиля
- рендеринг синхронных кадров

## S33

**Blender Python API. Camera**

[Открыть источник](https://docs.blender.org/api/current/bpy.types.Camera.html)

**Уровень:** Средний практический

**Формат:** Официальная API-документация, английский язык

**Разделы главы:** 3.9

API позволяет программно создавать и настраивать камеры, менять их положение, задавать fisheye-проекцию и запускать пакетный рендеринг. В проекте он нужен для воспроизводимой генерации наборов данных, автоматического внесения смещений камер и сохранения истинных параметров каждого эксперимента.

**Что изучить:**

- создание и настройка Camera
- panorama_type
- матрицы объектов
- пакетный рендеринг
- сохранение метаданных эксперимента

## S34

**GoogleTest User Guide**

[Открыть источник](https://google.github.io/googletest/)

**Уровень:** Базовый практический

**Формат:** Официальное руководство, английский язык

**Разделы главы:** 3.2, 3.3, 3.4 и 3.10

Документация фреймворка модульного тестирования C++. Тесты следует создавать одновременно с математическим ядром: проверять прямую и обратную проекцию, преобразования координат, сериализацию конфигурации и обработку ошибочных данных. Для начала достаточно GoogleTest Primer, затем потребуются параметризованные тесты и fixtures.

**Что изучить:**

- базовые assertions
- test fixtures
- parameterized tests
- проверка чисел с плавающей точкой
- разделение тестов и платформенного кода

## S44

**Surround view camera system for ADAS on TI’s TDAx SoCs**

[Открыть источник](https://www.ti.com/lit/wp/spry270a/spry270a.pdf)


**Библиографическая запись из прежней документации:**

Surround view camera system for ADAS on TI’s TDAx SoCs / V. Appia, H. Hariyani, S. Sivasankaran [et al.]. – Texas Instruments, 2015. – 18 p. – SPRY270A. – URL: [https://www.ti.com/lit/wp/spry270a/spry270a.pdf](https://www.ti.com/lit/wp/spry270a/spry270a.pdf) (дата обращения: 19.09.2026). – Текст : электронный.

## S45

**Qt Quick**

[Открыть источник](https://doc.qt.io/archives/qt-5.15/qtquick-index.html)


**Библиографическая запись из прежней документации:**

Qt Quick / The Qt Company. – Текст : электронный // Qt 5.15 Documentation. – URL: [https://doc.qt.io/archives/qt-5.15/qtquick-index.html](https://doc.qt.io/archives/qt-5.15/qtquick-index.html) (дата обращения: 19.09.2026).

## S46

**EGL_KHR_surfaceless_context**

[Открыть источник](https://registry.khronos.org/EGL/extensions/KHR/EGL_KHR_surfaceless_context.txt)


**Библиографическая запись из прежней документации:**

EGL_KHR_surfaceless_context / Khronos Group. – Текст : электронный // Khronos EGL Registry. – URL: [https://registry.khronos.org/EGL/extensions/KHR/EGL_KHR_surfaceless_context.txt](https://registry.khronos.org/EGL/extensions/KHR/EGL_KHR_surfaceless_context.txt) (дата обращения: 19.09.2026).

## S47

**Qt Quick Scene Graph**

[Открыть источник](https://doc.qt.io/archives/qt-5.15/qtquick-visualcanvas-scenegraph.html)


**Библиографическая запись из прежней документации:**

Qt Quick Scene Graph / The Qt Company. – Текст : электронный // Qt Quick 5.15 Documentation. – URL: [https://doc.qt.io/archives/qt-5.15/qtquick-visualcanvas-scenegraph.html](https://doc.qt.io/archives/qt-5.15/qtquick-visualcanvas-scenegraph.html) (дата обращения: 19.09.2026).

## S49

**QQuickWindow Class**

[Открыть источник](https://doc.qt.io/archives/qt-5.15/qquickwindow.html)


**Библиографическая запись из прежней документации:**

QQuickWindow Class / The Qt Company. – Текст : электронный // Qt Quick 5.15 Documentation. – URL: [https://doc.qt.io/archives/qt-5.15/qquickwindow.html](https://doc.qt.io/archives/qt-5.15/qquickwindow.html) (дата обращения: 19.09.2026).

## S50

**EGL_EXT_image_dma_buf_import**

[Открыть источник](https://registry.khronos.org/EGL/extensions/EXT/EGL_EXT_image_dma_buf_import.txt)


**Библиографическая запись из прежней документации:**

EGL_EXT_image_dma_buf_import / Khronos Group. – Текст : электронный // Khronos EGL Registry. – URL: [https://registry.khronos.org/EGL/extensions/EXT/EGL_EXT_image_dma_buf_import.txt](https://registry.khronos.org/EGL/extensions/EXT/EGL_EXT_image_dma_buf_import.txt) (дата обращения: 19.09.2026).
