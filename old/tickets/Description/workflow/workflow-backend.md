# Complex Rules V1 — План реализации: Бэкенд

> Ссылка: [workflow-system-overview.md](workflow-system-overview.md)  
> **Не путать** с существующим Workflow API (`/api/v1/Modules/{moduleUid}/Workflows`) — это отдельная фича с графовой моделью. Данный документ описывает **сложные правила (Complex Rules)** — цепочки обычных Rules с зависимостями.

---

## План работ

1. Модель данных ComplexRule
2. Конвертация ComplexRule → Rules (ядро логики)
3. CRUD API для ComplexRule
4. Конфликт-менеджмент устройств
5. Вычисление blockedByComplexRuleUids
6. Агрегация состояний правил → состояние ComplexRule
7. Жизненный цикл (Enable/Disable, Completed→Idle)
8. API для фронтенда (дашборд, история)
9. Расширение модели Rule для передачи на модуль
10. Тесты

---

## 1. Модель данных ComplexRule

### Сущность ComplexRule

| Поле | Тип | Описание |
|------|-----|----------|
| `uid` | `string` | Уникальный идентификатор |
| `name` | `string` | Название (от пользователя) |
| `mode` | `enum` | `Schedule` или `Sensor` |
| `priority` | `int` | Приоритет (меньше = выше). Уникальный среди конфликтующих ComplexRule. |
| `state` | `enum` | `Disabled`, `Synchronizing`, `Idle`, `Running`, `Completed`, `Error` |
| `activeHoursStart` | `time?` | Начало Active Hours (опционально) |
| `activeHoursEnd` | `time?` | Конец Active Hours (опционально) |
| `isEnabled` | `bool` | Включён/выключен |
| `createdAt` | `datetime` | Дата создания |
| `updatedAt` | `datetime` | Дата изменения |

### Шаги = обычные Rules

Отдельной сущности «ComplexRuleStep» нет. Шаг ComplexRule — это обычное правило (Rule) с заполненными полями зависимости (`complexRuleUid`, `stepNumber`, `dependsOnRuleUid` и т.д.).

Бэкенд находит все шаги ComplexRule простым запросом: `Rules WHERE complexRuleUid = X ORDER BY stepNumber`.

### Связь

```
ComplexRule 1 ──────── N Rule (с заполненным complexRuleUid)
```

---

## 2. Конвертация ComplexRule → Rules

Это **ключевая логика бэкенда**. При создании/редактировании ComplexRule бэкенд генерирует из шагов набор стандартных правил.

### Алгоритм конвертации

```
Для каждого шага из запроса:
  │
  ├── Создать Rule со стандартными полями:
  │     ruleType, deviceUid, conditions, schedule, timing...
  │
  ├── Добавить поля зависимости:
  │     complexRuleUid = complexRule.uid
  │     complexRulePriority = complexRule.priority
  │     stepNumber = порядковый номер шага
  │
  ├── stepNumber == 1?
  │     ├── Да → dependsOnRuleUid = пусто
  │     │         dependsOnDeviceUid = пусто
  │     │         blockedByComplexRuleUids = вычислить (см. раздел 5)
  │     │
  │     │         Триггер ComplexRule → условие первого правила:
  │     │           Schedule: время запуска → поле schedule
  │     │           Sensor: пороги → поле conditions
  │     │
  │     └── Нет → dependsOnRuleUid = UID правила предыдущего шага
  │               dependsOnDeviceUid = deviceUid предыдущего шага
  │               blockedByComplexRuleUids = пусто
  │
  ├── Active Hours → записать во ВСЕ правила цепочки
  │
  └── Сохранить Rule
```

### Маппинг режимов

| ComplexRule Mode | Типы правил в шагах | Действия шагов |
|---------------|---------------------|----------------|
| **Schedule** | schedule, loop | On, Off (schedule), Pulse (loop) |
| **Sensor** | sensor, loop, step | Regular, Stepper Motor, Loop |

### При редактировании ComplexRule

- Удалить все правила с `complexRuleUid` этого ComplexRule
- Пересоздать правила по новой конфигурации
- Пересчитать `blockedByComplexRuleUids` для ВСЕХ затронутых ComplexRule (не только текущего)
- Отправить новые правила на модули через стандартный механизм

---

## 3. CRUD API для ComplexRule

### Эндпоинты

| Метод | Путь | Описание |
|-------|------|----------|
| `POST` | `/api/v1/ComplexRule` | Создать ComplexRule |
| `GET` | `/api/v1/ComplexRule` | Список ComplexRule (с состояниями) |
| `GET` | `/api/v1/ComplexRule/{uid}` | Детали ComplexRule |
| `PUT` | `/api/v1/ComplexRule/{uid}` | Обновить ComplexRule |
| `DELETE` | `/api/v1/ComplexRule/{uid}` | Удалить ComplexRule |
| `POST` | `/api/v1/ComplexRule/{uid}/enable` | Включить ComplexRule |
| `POST` | `/api/v1/ComplexRule/{uid}/disable` | Выключить ComplexRule |

### Создание (POST)

Входные данные — шаги это обычные Rules (структура `AddRuleDto`) + `stepNumber` + `moduleUid`:
```json
{
  "name": "Фертигация",
  "mode": "Schedule",
  "priority": 1,
  "activeHoursStart": "06:00",
  "activeHoursEnd": "22:00",
  "steps": [
    {
      "stepNumber": 1,
      "moduleUid": "module-A",
      "deviceUid": "valve-1",
      "ruleType": "schedule",
      "isRuleOn": true,
      "schedule": [
        { "startAt": "2026-04-09T07:00:00Z", "endAt": "2026-04-09T07:30:00Z" }
      ],
      "daysOfWeek": [1, 2, 3, 4, 5]
    },
    {
      "stepNumber": 2,
      "moduleUid": "module-A",
      "deviceUid": "pump-1",
      "ruleType": "loop",
      "isRuleOn": true,
      "workingTime": 30,
      "waitingTime": 10
    }
  ]
}
```

> **Важно**: каждый элемент `steps` — это стандартный `AddRuleDto` (те же поля, что в `POST /api/v1/Rules`) плюс `stepNumber` и `moduleUid`. Бэкенд при сохранении дополняет каждое правило полями зависимости (`complexRuleUid`, `dependsOnRuleUid` и т.д.) — см. раздел 2.

> **Не путать** с существующим Workflow API (`/api/v1/Modules/{moduleUid}/Workflows`) — это отдельная фича с графовой моделью.

Логика:
1. Валидация (см. раздел 4 — конфликты)
2. Каждый step сохраняется как обычный Rule через стандартную логику создания правил
3. Бэкенд добавляет поля зависимости: `complexRuleUid`, `stepNumber`, `dependsOnRuleUid`, `blockedByComplexRuleUids` (раздел 2, 5)
4. Сохранить ComplexRule + Rules (с complexRuleUid)
5. Отправить правила на модули
6. Ждать синхронизации → обновить state

### Удаление (DELETE)

1. Удалить все правила с `complexRuleUid` этого ComplexRule с модулей
2. Пересчитать `blockedByComplexRuleUids` у ComplexRule, которые ссылались на удалённый
3. Удалить правила + ComplexRule

---

## 4. Конфликт-менеджмент устройств

### При создании/редактировании ComplexRule бэкенд проверяет каждое устройство:

```
Для каждого deviceUid в шагах ComplexRule:
  │
  ├── Устройство используется в обычном правиле (не ComplexRule)?
  │     └── Да → ОШИБКА. Нельзя сохранить.
  │           Фронтенду вернуть: device "valve-1" занято правилом "Rule X"
  │
  ├── Устройство используется в другом ComplexRule?
  │     ├── Тот же приоритет? → ОШИБКА. Приоритеты должны быть разные.
  │     └── Разный приоритет? → OK, но пометить конфликт (жёлтый)
  │
  └── Устройство свободно? → OK
```

### API для фронтенда: статус устройств

| Метод | Путь | Описание |
|-------|------|----------|
| `GET` | `/api/v1/ComplexRule/devices/availability` | Статус доступности устройств |

Ответ:
```json
{
  "devices": [
    { "deviceUid": "valve-1", "status": "free" },
    { "deviceUid": "pump-1", "status": "blocked", "blockedBy": "Rule X" },
    { "deviceUid": "fan-1", "status": "shared", "sharedWith": ["wf-002"] }
  ]
}
```

---

## 5. Вычисление blockedByComplexRuleUids

### Когда вычислять

- При создании ComplexRule
- При редактировании ComplexRule (пересчитать для всех затронутых)
- При удалении ComplexRule (пересчитать для тех, кто ссылался на удалённый)

### Алгоритм

```
Для ComplexRule X:
  │
  ├── Собрать все deviceUid из всех шагов ComplexRule X
  │
  ├── Найти все другие ComplexRule, которые используют хотя бы
  │   одно из этих устройств
  │
  ├── Из найденных оставить только те, у которых priority < X.priority
  │   (меньше = выше приоритет)
  │
  └── Список их UID → blockedByComplexRuleUids
        │
        └── Записать в правило ПЕРВОГО шага ComplexRule X
```

### Пересчёт при изменении любого ComplexRule

При создании/редактировании/удалении ComplexRule Y:
1. Найти все ComplexRule, которые делят устройства с Y
2. Пересчитать `blockedByComplexRuleUids` для каждого из них
3. Обновить правила первых шагов на модулях

---

## 6. Агрегация состояний правил → состояние ComplexRule

### Источник данных

Бэкенд уже получает `POST /api/v1/Hardware/update` от каждого модуля каждые 5 секунд. В `DeviceDto` есть `ruleUid`.

### Логика агрегации

```
При получении hardware/update:
  │
  ├── Для каждого DeviceDto с ruleUid:
  │     └── Найти Rule по uid == ruleUid, проверить complexRuleUid
  │           └── Если complexRuleUid не пустой → обновить состояние шага
  │
  └── Для каждого ComplexRule, у которого обновился шаг:
        │
        ├── Все правила Synchronized, ни одно не активно → Idle
        ├── Хотя бы одно правило активно → Running
        ├── Все шаги завершены → Completed
        ├── Любое правило в Error → Error
        └── Не все синхронизированы → Synchronizing
```

### Переход Completed → Idle

Бэкенд отслеживает по времени:

- **Schedule**: Completed → Idle при наступлении 00:00
- **Sensor с Active Hours**: Completed → Idle когда Active Hours закончились
- **Sensor без Active Hours**: Completed → Idle при наступлении 00:00

Реализация: фоновый job/scheduler проверяет Completed ComplexRule и переводит в Idle при наступлении условия.

---

## 7. Жизненный цикл — Enable/Disable

### Enable ComplexRule

1. Включить все правила с `complexRuleUid` этого ComplexRule (выставить `isEnabled = true`)
2. Отправить правила на модули
3. State → `Synchronizing`
4. Ждать подтверждения синхронизации от каждого модуля (существующий механизм)
5. Все синхронизированы → State → `Idle`
6. Таймаут 5 мин без полной синхронизации → State → `Error`

### Disable ComplexRule

1. Выключить все правила цепочки
2. Отправить обновление на модули
3. State → `Disabled`

---

## 8. API для фронтенда — дашборд и история

### Дашборд

| Метод | Путь | Описание |
|-------|------|----------|
| `GET` | `/api/v1/ComplexRule/dashboard` | Все ComplexRule с текущими состояниями |

Ответ содержит для каждого ComplexRule:
- `state` — текущее состояние
- `currentStep` — номер активного шага (если Running)
- `totalSteps` — общее количество шагов
- `lastRunAt` — время последнего запуска
- `nextRunAt` — время следующего запуска (для Schedule)

### История выполнений

| Метод | Путь | Описание |
|-------|------|----------|
| `GET` | `/api/v1/ComplexRule/{uid}/history` | История запусков ComplexRule |

Каждая запись:
- Время старта, время завершения
- Финальный статус (Completed / Error)
- На каком шаге произошла ошибка (если Error)

### Реал-тайм обновления

- SignalR / WebSocket для push-уведомлений о смене состояния ComplexRule
- Фронтенд подписывается на ComplexRule UID и получает обновления

---

## 9. Расширение модели Rule для передачи на модуль

### Текущий механизм

Бэкенд уже отправляет правила на модули через:
- `POST /api/v1/Hardware/Rules/state` (MQTT notification → модуль забирает по HTTP)
- Модуль запрашивает `GET /api/v1/Hardware/rulelist`

### Что добавить в JSON правила

В существующую сериализацию Rule → JSON добавить новые поля:

```json
{
  ...существующие поля правила...,
  "dependsOnRuleUid": "rule-xyz-789",
  "dependsOnDeviceUid": "pump-1",
  "stepNumber": 2,
  "complexRuleUid": "cr-001",
  "complexRulePriority": 1,
  "blockedByComplexRuleUids": ["cr-002"]
}
```

Для обычных правил (не ComplexRule) эти поля отсутствуют или пустые.

---

## 10. Тесты

### Unit-тесты

| Тест | Описание |
|------|----------|
| Конвертация ComplexRule → Rules | Правильно генерируются правила из шагов, проставляются dependsOn, stepNumber |
| Триггер → условие правила | Schedule-триггер корректно маппится в schedule правила; Sensor-триггер → conditions |
| blockedByComplexRuleUids | Правильно вычисляется при создании/редактировании/удалении ComplexRule |
| Агрегация состояний | Running когда хотя бы одно правило активно; Completed когда все Done |
| Completed → Idle | Переход при 00:00 (Schedule) и конце Active Hours (Sensor) |
| Конфликт-менеджмент | Блокировка при устройстве в обычном правиле; жёлтый при shared; ошибка при одинаковом приоритете |

### Интеграционные тесты

| Тест | Описание |
|------|----------|
| CRUD ComplexRule | Создание → правила на модулях; Удаление → правила удалены |
| Enable/Disable | Правила включаются/выключаются; состояние Synchronizing → Idle |
| Mock hardware/update | Отправка DeviceDto с ruleUid → корректная агрегация статуса ComplexRule |

---

## Порядок реализации (рекомендация)

```
Фаза 1: Модель и CRUD
  ├── Модель ComplexRule + расширение Rule + миграции БД
  ├── CRUD API (без конвертации)
  └── Тесты модели

Фаза 2: Конвертация
  ├── Конвертация ComplexRule → Rules
  ├── Отправка правил на модули
  ├── Расширение JSON правила новыми полями
  └── Тесты конвертации

Фаза 3: Мониторинг
  ├── Агрегация состояний из hardware/update
  ├── Жизненный цикл (Enable/Disable, Completed→Idle)
  ├── Дашборд API
  └── Тесты агрегации

Фаза 4: Конфликты и приоритеты
  ├── Конфликт-менеджмент устройств
  ├── Вычисление blockedByComplexRuleUids
  ├── API доступности устройств
  └── Тесты конфликтов

Фаза 5: История и реал-тайм
  ├── История выполнений
  ├── Push-уведомления (SignalR)
  └── Интеграционные тесты
```

---

## Что НЕ меняется

- `POST /api/v1/Hardware/update` — модули отправляют те же данные
- `GET /api/v1/Hardware/rulelist` — тот же механизм, правила с новыми полями
- MQTT-протокол — те же топики, те же нотификации
- Существующие правила — работают как раньше
- Синхронизация правил — тот же процесс
