# Архив и происхождение материалов

Архив хранит прежние постановки и исходные идеи. Действующая тема — [[research/TOPIC]], текущие решения — [[architecture/DECISIONS]], порядок работ — [[planning/ROADMAP]].

## Что объединено

- «Диплом 2.docx» и оформленный план по Авроре содержали один паспорт, одинаковые задачи и четыре раздела. Паспорт перенесён в `research/TOPIC`, главы — в `planning/THESIS`, работы — в `planning/ROADMAP`. Уникальные авторские заметки: сравнить способы представления четырёх камер, калибровку после механического смещения, задержки и работу на реальном устройстве; сгенерированный план требовал проверки. Эти условия включены в рабочую постановку и TODO.
- Список литературы Word и повторяющиеся библиографии объединены в [[references/README]]; повтор OpenCV calibration для глав 1 и 4 стал одной записью с двумя применениями.
- Краткое описание Markdown, его DOCX и PDF содержали один текст. Его историческая версия включена в [[archive/CONCEPTS]], текущая постановка хранится только в [[research/TOPIC]].
- Две разные концепции сохранены разделами [[archive/CONCEPTS]]; они не были побитовыми дубликатами.
- [[archive/EXPLORATION]] сохраняет исходный выбор тем и альтернативных направлений.
- [[archive/AUDIT]] сохраняет анализ от 18.09.2026. [[archive/BASELINE_PLAN]] — прежний календарь Linux-стенда на 26 недель.
- Отдельные `.puml` встроены в [[architecture/SYSTEM]]. Учтена локальная правка файла последовательности.

## Решения при объединении

Аврора и методики калибровки — основная постановка по указанию пользователя. Linux используется для первого проверочного стенда. Сравнение сеток стало дополнительным опытом. `sv-ui` переименован в `sv-client`; номера `UI-*` сохранены для трассировки. Старые ID требований не потеряны; новые функции калибровки и целевой платформы получили отдельные ID.

Прежние версии Qt/API и численные цели сохраняются только как desktop-кандидаты и проектные предложения. Сроки 26 недель и 12 месяцев не смешиваются в действующий календарь. Утверждения о достигнутой новизне, точности и производительности не приняты без измерений.

## Реестр преобразования

Перед преобразованием была создана временная резервная копия исходников вне проекта. Долгосрочное текстовое содержание находится в этом хранилище; работа с документацией не зависит от резервной копии. Бинарные экспорты и отдельные диаграммы исключены из рабочего набора по переходу на Markdown.

| Исходник | Куда перенесено содержание | SHA-256 исходника |
|---|---|---|
| `Краткое_описание_темы_3D_Surround_View.docx` | [[archive/CONCEPTS]] | `a3b04613c5af9c92210912c98ccfcb942670b89bd8d523280e202b514772e92c` |
| `Краткое_описание_темы_3D_Surround_View.pdf` | [[archive/CONCEPTS]] | `83e951b4b39d61134f2af6ef93a89417718b15a665fa828edcd84625751abdcd` |
| `Диплом 2.docx` | [[research/TOPIC]], [[planning/THESIS]], [[planning/ROADMAP]] | `543a23b078bf99b54355f19280137d43ae6c489fdf04342feb76a9defa2a1e70` |
| `Источники_для_диссертации_Aurora_Surround_View.docx` | [[references/README]] | `df939d8f08bc2d5a931959b810f6e2b584cd194715e7104c6492db4c5adac882` |
| `Концепция_магистерской_диссертации_3D_Surround_View.docx` | [[archive/CONCEPTS]] | `38b3573dc16d391b81551710705d5cceaa577b5775cdcf393a94498389c8379b` |
| `Описание.docx` | [[archive/EXPLORATION]] | `948472b231158fbed8990d051454c4e864c8e8294850403ba90eb0bda0c407e0` |
| `План_магистерской_диссертации_Аврора_Surround_View.docx` | [[research/TOPIC]], [[planning/THESIS]], [[planning/ROADMAP]] | `9c5a5bc56b353ad0411e6c8d65cf0b46ce1c75c29e8f8520cff3861bb0d1ad25` |
| `docs/COORDINATE_SYSTEMS.md` | [[architecture/MATHEMATICS]] | `3e21309381688aa375dc309dd6529ba0d776f1749dcd29a9772a9c989ab56c4c` |
| `docs/MASTER_PLAN.md` | [[archive/BASELINE_PLAN]], [[planning/ROADMAP]] | `f3ca0023ae5061f55b4968c502a120c9f7fb208a6386b648e179c748068d6261` |
| `docs/REQUIREMENTS.md` | [[requirements/SYSTEM]] | `dd8f335936b6a8431ef096e0e4c6c7a5babc401d2c547290f3d6341b95484500` |
| `docs/RESEARCH_PLAN.md` | [[research/EXPERIMENTS]] | `aec237afe56c3aa0a49649429841d4c4f1bd7d17e82f841201378943d2ae8802` |
| `docs/SYSTEM_ARCHITECTURE.md` | [[architecture/SYSTEM]], [[architecture/DECISIONS]] | `f901471494f88424b191a4426467ef77c78eb86842322c7ae3da42fb387832cf` |
| `docs/VALIDATION.md` | [[validation/ACCEPTANCE]] | `673da4bd8c08034a765c62d4ea3cb9c7b3e146fc510f73897c53fb9bcab8c545` |
| `docs/Аудит_и_план_3D_Surround_View.md` | [[archive/AUDIT]] | `2bb6e483b46745cede052d1c7753a4d8d6ed57e757cca14ffc3f1a5b91797926` |
| `docs/Концепция_магистерской_диссертации_3D_Surround_View.docx` | [[archive/CONCEPTS]] | `9171c24ff2225623e69c6ec4efcca2134ac511d0e3360ebc1def730a794f25f9` |
| `docs/Краткое_описание_темы_3D_Surround_View.md` | [[archive/CONCEPTS]], [[research/TOPIC]] | `ddc96f441de8eae3014958d42818f8fa78fde766295bffddf75ad213f19a8588` |
| `docs/diagrams/components.puml` | [[architecture/SYSTEM]] | `a4600c556715f47deee9fbb214ddc207602706e02706d7b6133e486508989d0c` |
| `docs/diagrams/server-sequence.puml` | [[architecture/SYSTEM]] | `4d3cc16d47aa4db4018ec03fc92cfd6e7f049c49f640d25b84eb84a5b8c07f19` |
| `docs/diagrams/system-context.puml` | [[architecture/SYSTEM]] | `80c7c1d1b1578533c00807945151aa3a9c3f1f1ae927039c9d7bcd6d3fb87113` |
| `docs/requirements/CONFIGURATION.md` | [[requirements/CONFIGURATION]] | `52d8c4970c69164cf1301f7ca703e2470673eab22c47877349b363b7e5ecc9cc` |
| `docs/requirements/NFR.md` | [[requirements/NFR]] | `80820633663e13cd6d048446f2aabcf672d2fa6a13bb3d8cb1eb6f863d3354c7` |
| `docs/requirements/OPS.md` | [[requirements/OPS]] | `012e8d278dbb60696d4904beb008883cd6b89f509a66a54b40a401eaf5306c63` |
| `docs/requirements/PROTOCOL.md` | [[requirements/PROTOCOL]] | `044e661fa8166f8fcf3ad9227c871d5c8d8e987977903fc32a107866105401c4` |
| `docs/requirements/SERVER.md` | [[requirements/SERVER]] | `ed23f37a6103ffdf6fa34c9f92e2f577cbbd6c43f22aae7fe3232aeccae15d54` |
| `docs/requirements/SIMULATOR.md` | [[requirements/SIMULATOR]] | `820f0e56dff0b4770390f64f252713407f6280c6e649b6415333b837adb0f733` |
| `docs/requirements/UI.md` | [[requirements/CLIENT]] | `bf2b14a22f45795668b763c4c968732a323c3a3d63f5e540d96bc57b5c61a61d` |
