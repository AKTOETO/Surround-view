# Aurora SDK, RPM и запуск проверки на устройстве

Проверка источников: 05.10.2026, актуальные просмотренные страницы — ОС Аврора 5.2.1. Фактические SDK, выпуск устройства и hardware пока **не предоставлены**. Здесь подготовлен путь переноса версии 0.6.0; native RPM на Linux проверяет упаковку, но не подтверждает ABI, зависимости и установку Авроры. GPU-профиль включает купол/цилиндр/куб с полом, fusion и native render-oracles. CPU/GPU Linux RPM 0.6.0, native source fixture и development consumers проверены ([[validation/SOURCES_SMOKE]]); целевая сборка SDK остаётся открытой. Свидетельства: [[validation/CLIENT_SMOKE]]. Полный список Linux-команд — [[engineering/BUILD]], критерии и данные — [[engineering/PLATFORM_TEST]].

## Два RPM-профиля

| Spec | Состав | Для чего |
|---|---|---|
| `packaging/rpm/surround-view.spec` | Core, OpenCV tools, native report, EGL renderer/bench/server, client library/probe; Qt client опционально | Проверка GPU и прототипа |
| `packaging/rpm/surround-view-cpu.spec` | Core, OpenCV tools, native CPU report, client library/probe | Проверка toolchain/модели при недоступном EGL |

Они устанавливают совпадающие пути и конфликтуют; одновременно устанавливается один профиль. Все исполняемые файлы — архитектурные, `BuildArch: noarch` не используется. `%install` получает только CMake install layout; runtime scriptlets, запуск тестов в `%check`, сеть из CMake отсутствуют. Два spec-файла нужны для разных составов пакета, а не как копии одной инструкции.

Это **лабораторные CLI-пакеты**, предназначенные для согласованной исследовательской среды устройства. Опциональный Qt Quick client пока desktop prototype: здесь нет AuroraApp/Silica lifecycle, launcher/manifest и подтверждённого application security profile. Для публичного приложения понадобится отдельная интеграция. Список допустимых API и проверку профиля сверять с [публичными API Авроры](https://developer.auroraos.ru/doc/software_development/reference/public_api); наличие Boost/OpenCV/GLM на ПК не гарантирует их доступность в target repository.

Поле License отражает пока непубличный исследовательский код и CC0 фотоисточник. Это упаковочная метка, не выбранная открытая лицензия исходников. До распространения закрепить фактическую лицензию проекта и соответствующие файлы; CC0 происхождение фото уже описано в [[engineering/SCENE]].

## Подготовить целевую среду

Установить согласованные tooling/target по [инструкции Platform SDK](https://developer.auroraos.ru/doc/sdk/psdk/setup), войти в SDK/Build Engine. Получить реальные имена:

```sh
sdk-assistant list
SV_TARGET='имя_установленного_target_из_списка'
sb2 -t "$SV_TARGET" rpm -q gcc-c++ cmake ninja boost-devel glm-devel opencv-devel
sb2 -t "$SV_TARGET" pkg-config --modversion openssl egl glesv2
sb2 -t "$SV_TARGET" rpm --eval '%cmake'
sb2 -t "$SV_TARGET" rpm --eval '%ninja_build'
sb2 -t "$SV_TARGET" rpm --eval '%ninja_install'
```

Переменная `SV_TARGET` — шаблон для замены, а не существующий комплект. Не угадывать точный номер релиза по версии страницы документации. В [[validation/ACCEPTANCE]] записать выпуск/архитектуру/SDK/Qt/драйвер и результат проверки пакетов. Названия `boost-devel`, `glm-devel`, `opencv-devel` в spec — ожидаемые package names; если target использует другие имена, проверить реальные provider packages и скорректировать `BuildRequires` в отдельном коммите.

Boost должен предоставлять именно JSON ≥1.75, GLM — CMake config, OpenCV — нужные modules из [[engineering/BUILD]], OpenSSL — Crypto. Проверять target-пакеты, не host `/usr/include` и не x86_64 `.so`. Если зависимости отсутствуют, сначала подготовить их RPM под тот же target или согласовать иной dependency profile; **не добавлять FetchContent**. Установка development-пакетов относится к подготовке SDK. Автоматический `mb2 installdeps` может обращаться к target repositories; для полностью offline сборки предварительно установить все зависимости и использовать `mb2 -n`.

Макросы `%cmake -GNinja`, `%ninja_build`, `%ninja_install` соответствуют [официальному CMake/Ninja примеру](https://developer.auroraos.ru/doc/5.1.4/sdk/app_development/work/create/create_new_project). Их фактическое раскрытие проверять в выбранном SDK: host-макросы из `tests/rpm_host.macros` **никогда не загружать в Aurora SDK**, они предназначены только для native packaging smoke на ПК.

## Зафиксировать исходники

Для проверки самого spec и CMake install layout на ПК существует `tools/verify_host_rpm.py`:

```sh
python3 tools/verify_host_rpm.py --profile gpu --output artifacts/rpm-host-gpu
python3 tools/verify_host_rpm.py --profile cpu --output artifacts/rpm-host-cpu
```

Требуются native RPM-tools (включая `rpm2archive`), `bsdtar` и development-пакеты Linux из [[engineering/BUILD]]. Скрипт использует собственные **host-only** macros и `--nodeps`, поскольку Linux-стенд может иметь пакетную базу другого дистрибутива. Проверяет payload paths/ELF requires, распаковывает пакет и запускает core/native/scene smoke из installed layout, для GPU также renderer. Сохраняет log/listing и `INSTALLED_REPORT.md` в artifacts; RPM database тоже локальная. Это не проверяет целевые BuildRequires, ABI Авроры и её security profile. `--toolchain-root` позволяет использовать распакованные RPM-tools без системной установки; `--inspect-only` повторяет проверку уже собранных RPM без пересборки.

На ПК после коммитов и при чистом рабочем дереве:

```sh
python3 tools/make_source_archive.py --output artifacts/sources
sha256sum artifacts/sources/surround-view-0.6.0.tar.gz
```

Скрипт берёт только **Git HEAD**, не локальные build/data/private files, добавляет `.source-revision`, нормализует tar/gzip metadata. Экспорт незакоммиченного дерева запрещён; одинаковая revision даёт одинаковый архив. Перенести архив и spec в рабочее пространство SDK. Ни файлы ключей подписи, ни большие traces в Source0 не входят.

Есть два режима; выбрать один:

1. **mb2 из checkout**: сборка инструмента разработчика по конкретному spec, удобная для итераций.
2. **rpmbuild из Source0**: явная сборка неизменного release archive, удобная для связи отчёта с доставленным исходником.

Официальная [инструкция mb2](https://developer.auroraos.ru/doc/sdk/tools/mb2) задаёт выбор target/spec; нестандартный путь `packaging/rpm` поэтому передаётся явно:

```sh
mb2 -s packaging/rpm/surround-view.spec -t "$SV_TARGET" -n -X build
# CPU-профиль вместо GPU:
mb2 -s packaging/rpm/surround-view-cpu.spec -t "$SV_TARGET" -n -X build
```

Команды альтернативны. `-n` применим после установки зависимостей; `-X` сохраняет явно заданную 0.6.0 вместо автоматического суффикса версии. Результаты mb2 находятся в `RPMS/` выбранного рабочего каталога. В IDE явно выбрать нужный spec. По умолчанию client=0; для лабораторного Qt 5 client при release rpmbuild передать `--define 'sv_with_client 1'` и подтвердить runtime Qt/QML modules.

## Явная release-сборка Source0

В SDK-каталоге проекта, где уже лежит экспортированный архив:

```sh
mkdir -p artifacts/rpmbuild/SOURCES artifacts/rpmbuild/SPECS
cp artifacts/sources/surround-view-0.6.0.tar.gz artifacts/rpmbuild/SOURCES/
cp packaging/rpm/surround-view.spec artifacts/rpmbuild/SPECS/
SV_RPM_TOP="$PWD/artifacts/rpmbuild"
sb2 -t "$SV_TARGET" rpmbuild -bb --define "_topdir $SV_RPM_TOP" \
  "$SV_RPM_TOP/SPECS/surround-view.spec"
```

Для CPU заменить имя spec на `surround-view-cpu.spec`, оставив Source0 `surround-view-0.6.0.tar.gz`. В этом режиме результаты в `artifacts/rpmbuild/RPMS/<архитектура>/`. **Не использовать `--nodeps` для целевой сборки**: необходимо подтвердить целевые BuildRequires. RPM автоматически формирует runtime ELF dependencies; проверить:

```sh
rpm -qp --requires 'путь_к_полученному.rpm'
rpm -qpl 'путь_к_полученному.rpm'
```

SV_AURORA отключает host Python-tests, spec задаёт Release, client OFF, BUILD_TESTING OFF. `sv-core-tests` всё равно упаковывается благодаря `SV_PLATFORM_TEST=ON`. Не запускать aarch64/armv7hl benchmark на ПК как native; корректность кросс-сборки и hardware performance — разные проверки.

## Валидировать, подписать, установить

В исследовательской среде выяснить допустимый профиль пакета. Инструмент [rpm-validator](https://developer.auroraos.ru/doc/sdk/tools/rpm_validator) проверяет зависимости и структуру относительно профиля; этот проект пока не имеет подтверждённого профиля сертификации:

```sh
rpm-validator -p 'подтверждённый_профиль' 'путь_к_пакету.rpm'
```

Если требуется подпись, использовать выданные именно для выбранной среды сертификаты и [rpmsign-external](https://developer.auroraos.ru/doc/sdk/tools/rpmsign_external):

```sh
rpmsign-external sign --key 'путь_к_ключу.pem' \
  --cert 'путь_к_сертификату.pem' 'путь_к_пакету.rpm'
```

Ключи остаются вне проекта. Установка из developer-среды следует [инструкции Platform SDK](https://developer.auroraos.ru/doc/sdk/psdk/build):

```sh
scp 'путь_к_пакету.rpm' defaultuser@DEVICE_IP:~/
ssh defaultuser@DEVICE_IP
devel-su
pkcon install-local 'имя_пакета.rpm'
exit
```

Заменить пользователя/IP на реальные. Пакеты зависимостей того же target должны быть доступны до установки. После выхода из повышенной сессии запускать квалификацию как пользователь устройства, в среде с доступным графическим backend. Установка RPM на устройство в данной работе пока не выполнялась.

## Проверить на устройстве и забрать данные

```sh
sv-core-tests /usr/share/surround-view/configs/synthetic.json
sv-source-tests /usr/share/surround-view/configs/synthetic.json
sv-platform-test --config /usr/share/surround-view/configs/synthetic.json \
  --output "$HOME/sv-results/run-01" --label Aurora-device \
  --egl-platform default --require-gpu
```

При недоступном EGL сначала сохранить failed report, затем отдельно выполнить `--cpu-only` в новом каталоге. Диагностика OpenCV и таймеров входит в native report; отсутствие GPU не маскируется нулевыми временами. `passed` не доказывает physical display latency и работоспособность датчиков.

На ПК:

```sh
mkdir -p artifacts/aurora
scp -r defaultuser@DEVICE_IP:~/sv-results/run-01 artifacts/aurora/
python3 tools/compare_reports.py docs/validation/baselines/PC_RTX.md \
  artifacts/aurora/run-01/REPORT.md --output artifacts/aurora-vs-pc.md --strict
```

Если код/профиль отличается, повторить PC baseline из тех же исходников. Для более полного hardware паспорта отдельно записать SoC/GPU, RAM, display resolution/refresh, storage, governor/thermal mode, температуру, фоновые процессы и аппаратные timestamps. Незаполненные показатели Авроры остаются «нет данных», а не копируются из RTX/Mesa.

## Что ещё подтвердить

- Фактический SDK и target ABI; все package versions/provider names.
- Раскрытие macros и успешную **целевую** RPM-сборку без скачивания из CMake.
- Installation/runtime dependencies, исследовательский профиль и доступ к EGL.
- Native criteria и сравнимые render/upload/readback времена на железе.
- Qt/AuroraApp lifecycle и собственный launcher для будущего приложения.
- Реальный camera adapter: разрешения, timestamps, синхронизация и formats.

Схема переноса и подписи изображений находятся в [[diploma/03_PROTOTYPE_IMPLEMENTATION]]; результаты двух PC backend — в [[diploma/04_EXPERIMENTAL_STUDY]].

Оба research spec 0.6.0 включают static `sv-client-lib`/`sv-wire`, публичные headers и installed CMake export. GUI остаётся optional. Это позволяет собирать отдельного клиента из установленного пакета; инструкция — [[engineering/CLIENT_LIBRARY]]. UDP и target ABI этой упаковкой не подтверждаются.

В spec `CMAKE_INSTALL_LIBDIR=%{_lib}` согласует относительный lib/lib64 с архитектурным RPM macro. Это сохраняет relocation installed CMake package и предотвращает расхождение с `%{_libdir}` в `%files`. Host verifier дополнительно проверяет development files и собирает отдельного consumer из распакованного RPM.
