# Урок 9. OTA и версии: FIRMWARE_VERSION, обновление, bootType

## Теория

Обновление по воздуху — штатный путь доставки: модуль сам качает образ по HTTPS
и перезагружается в новый. Опоры механизма:

- **Версия из файла.** `VERSION` в корне репозитория — источник истины. Скрипт
  `scripts/firmware_version.py` (pre-build) подставляет `-DFIRMWARE_VERSION`, поэтому
  даже голый `pio run` собирает прошивку с правильной версией. CI/артефакты именуются
  по той же версии.
- **Поток обновления.** Команда `UpdateFirmware` (module command, options: url, version)
  -> `FirmwareUpdateService` качает образ через `HttpsOTAUpdate` ->
  режим `CONFIGURED_NEED_RESTART` -> ребут -> загрузка новой версии.
- **bootType.** Причина перезагрузки пишется в EEPROM перед ребутом (`BOOT_TYPE_*`):
  user, technical, firmware-update, sensor-chng... Следующая загрузка читает флаг
  и сообщает серверу в structure (`bootType`), а также решает, писать ли событие
  в OTA-log. Не терять bootType при рефакторинге ребутов — реальная регрессия
  (см. тикет GDV1-3797).
- **Отложенное обновление.** `PendingFirmwareUpdateStore` (SPIFFS) переживает ребут:
  обновление докатывается после загрузки (`resumeKeptUpdate`).
- **Контроль сборки.** `scripts/build_manifest.py` (post-build) пишет `build_manifest.json`:
  версии всех пакетов, флаги, git commit, sha256 бинарника. Два билда одного коммита
  должны давать идентичный манифест — это ловит "плывущие" зависимости.

## Разбор в прошивке

- `VERSION`, `scripts/firmware_version.py`, `scripts/build_manifest.py`.
- `src/Application/Services/FirmwareUpdateService.h` и `ConfirmRunningFirmwareUseCase.h`.
- `src/Infrastructure/Adapters/Ota/` — partition control, image writer.
- `src/Infrastructure/Configuration/PendingFirmwareUpdateStore.h`.
- `src/Core/Domain/Enums/...` — `BootTypeEnum`, `getBootTypeName()`.

## Задание

Прокачайте пайплайн версии:

1. Соберите `pio run -e esp32-s3-devkitm-1`, найдите в `build_manifest.json` свой
   `git commit` и `firmware sha256`. Пересоберите без изменений — манифест не должен
   измениться.
2. Поднимите `VERSION` на одну патч-версию (как `#ver` коммит по конвенции репозитория)
   и убедитесь по логу загрузки, что структура уходит с новой версией.
3. Подумайте: куда бы вы добавили запись "обновление отклонено по причине X"
   (bootType? отдельный флаг? ответ module_command?) — и почему.

Правило безопасности из AGENTS.md: OTA-ссылки и подпись проверяем особенно внимательно —
затрагивая этот код, не ослабляйте проверки.
