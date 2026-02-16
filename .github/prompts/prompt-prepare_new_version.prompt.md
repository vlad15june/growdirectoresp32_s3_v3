---
agent: agent
---
Увеличить версию проекта, обновить файл VERSION
/Users/arsenarutiunian/Projects/GrowDirector/esp32/PlatformIO IDE/growdirectoresp32_S3_v3/VERSION

Проанализируй все изменения с помощью git diff и составь релиз нотс для новой версии. Релиз нотс должен быть кратким, информативным и структурированным по категориям (например, "Added", "Changed", "Fixed"). Учитывай только изменения, которые были внесены с момента изменения файла VERSION. коммиты изменения файла VERSION имают имя, например, #ver 2.1.22.
/Users/arsenarutiunian/Projects/GrowDirector/esp32/PlatformIO IDE/growdirectoresp32_S3_v3/ReleaseNotes.md


Правила:
1. в секцию  ### Fixed добавлять только существенные поломки, на каждый баг одна строка с номером тикета, его брать из сообщения коммита, например, GDV1-3261.
пример строки которая добавлена в релиз нотс:
- **Sensor Type Synchronization**: GDV1-3261 Fixed critical bug where sensor types weren't being updated when server changed parameters


ВАЖНО!
после добавления сделай комит с соответствующим сообщением, например, #ver 2.1.22