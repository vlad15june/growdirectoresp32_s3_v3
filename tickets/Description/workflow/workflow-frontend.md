# Complex Rules V1 — План реализации: Фронтенд

> Ссылка: [workflow-system-overview.md](workflow-system-overview.md)  
> **Не путать** с существующим Workflow API — данный документ описывает **сложные правила (Complex Rules)** — цепочки обычных Rules с зависимостями.

---

## План работ

1. Навигация и структура экранов
2. Список ComplexRule (главный экран)
3. Экран редактирования / деталей — структура карточек
4. Карточка Trigger — конфигурация
5. Карточка Steps — конфигурация шагов
6. Карточки Active Hours и Notifications
7. Дашборд — реал-тайм состояние
8. Удаление и принудительная остановка
9. Валидация и обработка ошибок
10. API-интеграция

---

## 1. Навигация и структура экранов

### Структура

ComplexRule — отдельная секция в боковой навигации (не вложена в Rules).

```
Сайдбар:
  ├── Climate
  ├── Equipment
  └── Complex Rules  ← НОВЫЙ пункт
        ├── [+ New ComplexRule]
        ├── Active:
        │     ├── ● ComplexRule A  (зелёная точка)
        │     └── ● ComplexRule B
        └── Inactive:
              └── ○ ComplexRule C
```

### Экраны

| Экран | Описание |
|-------|----------|
| **ComplexRuleListScreen** | Список всех ComplexRule с состоянием. Точка входа. |
| **ComplexRuleEditScreen** | Единый экран создания **и** редактирования **и** просмотра деталей. Открывается при тапе на элемент списка или при нажатии «+ New». |

**Нет отдельных экранов** для создания vs просмотра vs редактирования — это один и тот же экран с inline-редактированием.

---

## 2. Список ComplexRule

### Экран: ComplexRuleListScreen

Отображает все ComplexRule пользователя с текущим состоянием.

### Карточка ComplexRule

| Элемент | Описание |
|---------|----------|
| Название | Имя ComplexRule |
| Режим | Иконка Schedule (часы) или Sensor (датчик) |
| Состояние | Цветной бейдж: Idle / Running / Completed / Error / Disabled / Synchronizing |
| Количество шагов | «3 шага» |
| Приоритет | Число (если есть конфликты) |
| Триггер | Краткое описание: «07:00» или «CO₂ < 800» |

### Цвета состояний

| Состояние | Цвет | Описание |
|-----------|------|----------|
| `Idle` | Серый (`#F0F0F0` / `#666`) | Ожидает триггера |
| `Running` | Синий (`#DBEAFE` / `#1D4ED8`) | Выполняется |
| `Completed` | Зелёный (`#D1FAE5` / `#065F46`) | Завершён, ждёт следующий цикл |
| `Error` | Красный (`#FEE2E2` / `#991B1B`) | Ошибка |
| `Synchronizing` | Янтарный (`#FEF3C7` / `#92400E`) | Синхронизация правил |
| `Disabled` | Серый (toggle выключен) | ComplexRule отключён |

### Статусы шагов (Step-level)

| Состояние | Цвет | Описание |
|-----------|------|----------|
| `Pending` | Серый | Ожидает своей очереди |
| `Running` | Синий | Шаг выполняется сейчас |
| `Done` | Зелёный | Шаг завершён |
| `Error` | Красный | Шаг в ошибке |

### Действия

- Tap → переход на экран деталей
- Long press / swipe → Enable/Disable, Delete
- FAB (кнопка «+») → создание нового ComplexRule

---

## 3. Экран редактирования / деталей — структура карточек

Один экран для создания, редактирования и просмотра состояния ComplexRule.

### Header

```
[Название (inline edit)] ✎  |  3 steps · Sensor mode  |  Priority [1▾]
                                [● Idle ▾]  |  [■ Terminate]  [Disabled] ◯
```

| Элемент | Описание |
|---------|----------|
| Название | Inline-редактируемый input. Hover — серый фон. Focus — синяя подчёркивающая линия. |
| Priority | Dropdown: 1–10. |
| Status selector | Dropdown с цветовыми классами: Idle / Running / Completed / Error / Synchronizing. |
| Enable/Disable toggle | Переключатель вкл/выкл. |
| Terminate button | Красная рамка. При нажатии — confirmation dialog: «Stop ComplexRule? All devices will be switched off immediately.» |

### Карточки (collapsible)

1. **Trigger** — всегда открыта по умолчанию. Бейдж: `Sensor` (синий) или `Schedule` (синий).
2. **Active Hours** — свёрнута по умолчанию. В заголовке: итоговый диапазон + Enable toggle.
3. **Notifications** — свёрнута по умолчанию. В заголовке: краткий итог включённых.
4. **Steps** — без collapse. Кнопка `+ Add Step` в заголовке.

### Кнопка Save

Кнопка «Save Changes» в самом низу страницы. Тёмный фон (`#1C1C1C`).

---

## 4. Карточка Trigger — конфигурация

### Режим (Sensor / Schedule)

Сегмент-переключатель вверху карточки. Переключение меняет содержимое карточки и тип шагов в Steps.

---

### Sensor-режим

**Список датчиков триггера** (до 3 датчиков):

Каждая строка:

| Поле | Описание |
|------|----------|
| Sensor type | Dropdown: Temperature / Humidity / CO₂ / Light / pH / EC / ORP |
| Operator | Dropdown: Higher than / Lower than / Equal to / Between / Not between |
| Value | Number input |
| Unit | Read-only: °C / % / ppm / lux / pH / mS/cm / mV |

При 2+ датчиках — появляется **Logic toggle**: `AND` (синий) / `OR` (оранжевый).
- AND: all conditions must be met simultaneously
- OR: any one condition is sufficient

**Activation delay**: число секунд. Условие должно выполняться непрерывно это время, прежде чем ComplexRule запустится.

Кнопка `+ Add Sensor` (максимум 3).

---

### Schedule-режим

**Тип расписания**:

| Тип | Описание |
|-----|----------|
| **Trigger at** | Запуск только в указанные точные моменты времени. Чипы с временем (HH:MM). `+ Add time`. |
| **Repeat** | Повтор с фиксированным интервалом: каждые N минут / часов. |

**Repeat on** (дни недели): кнопки Mo / Tu / We / Th / Fr / Sa / Su.

---

## 5. Карточка Steps — конфигурация шагов

Описание под заголовком:
- **Sensor mode**: «After the trigger fires, sensor value is checked at each step. Steps can run in parallel — next step activates when the previous one has started and its sensor condition is met.»
- **Schedule mode**: «After the trigger fires, steps execute in order. Command is sent to the device and the next step activates once the command is accepted.»

---

### Sensor mode — сетка шага

Каждый шаг = карточка (`step-card`). Карточка может содержать несколько строк — по одной на каждый датчик триггера.

**Колонки строки:**

| # | Колонка | Содержимое |
|---|---------|------------|
| 1 | Drag | Иконка перетаскивания (⠿) |
| 2 | № | Номер шага (только в первой строке) |
| 3 | Device | Dropdown выбора устройства (только в первой строке) |
| 4 | Sensor | Dropdown выбора датчика (из датчиков триггера) |
| 5 | From | Number input (жёлтая рамка) — порог включения |
| 6 | Current | Read-only зелёный бокс — текущее значение датчика |
| 7 | Until | Number input — порог выключения |
| 8 | (spacer) | — |
| 9 | Action Type | Dropdown (только в первой строке): Regular / Stepper Motor / Loop |
| 10 | Params | Поля параметров (только в первой строке) |
| 11 | Status | Бейдж статуса шага (только в первой строке) |
| 12 | Delete | × (первая строка — удалить шаг; последующие — удалить строку датчика) |

**Если датчиков 2+** — между строками показывается AND/OR-пилюля (наследует глобальную логику триггера).

Ссылка `+ [SensorName] row` в конце карточки — добавить строку датчика в шаг (если в триггере есть неиспользованные датчики).

**Параметры по типу действия:**

| Action Type | Параметры |
|-------------|-----------|
| **Regular** | — (нет параметров) |
| **Stepper Motor** | `Run (s)`, `Wait (s)` |
| **Loop** | `Times`, `On (s)`, `Off (s)` |

**Состояния карточки шага:**

| Состояние | Стиль |
|-----------|-------|
| Running | Синяя левая граница |
| Done | Полупрозрачный (opacity 0.7) |
| Error | Красная левая граница |
| Conflict (устройство shared) | Жёлтая левая граница + banner внутри |
| Rule-blocked (устройство занято Rule) | Красная левая граница + banner внутри |

---

### Schedule mode — сетка шага

**Колонки:**

| # | Колонка | Содержимое |
|---|---------|------------|
| 1 | Drag | ⠿ |
| 2 | № | Номер шага |
| 3 | Device | Dropdown устройства |
| 4 | (spacer) | — |
| 5 | Action | Dropdown: On / Off / Pulse |
| 6 | Params | Поля параметров |
| 7 | Status | Бейдж |
| 8 | Delete | × |

**Параметры по действию:**

| Action | Параметры |
|--------|-----------|
| **On** | — |
| **Off** | — |
| **Pulse** | `On (s)`, `Off (s)`, `Cycles` |

---

## 6. Карточки Active Hours и Notifications

### Active Hours

Collapsible карточка. В заголовке: итоговый диапазон + Enable toggle (клик по toggle не раскрывает карточку).

| Поле | Тип |
|------|-----|
| From | Time picker |
| Until | Time picker |
| Active Days | Кнопки Mo/Tu/We/Th/Fr/Sa/Su |

### Notifications

Collapsible карточка. В заголовке: краткий итог (например, «Push · 3 on»).

| Событие | Умолчание |
|---------|-----------|
| On completion | Вкл |
| On failure | Вкл |
| On device conflict | Вкл |

Каждое событие — отдельный toggle.

---

## 7. Дашборд — реал-тайм состояние

### Обновление состояния

Два механизма:
1. **Polling**: `GET /api/v1/ComplexRule` (список) каждые 5–10 секунд — обновлять статусы и badge шагов.
2. **Push** (если SignalR доступен): подписка на обновления ComplexRule.

### Обновление на экране списка

- Обновить status-бейдж ComplexRule
- Обновить счётчик активных шагов

### Обновление на ComplexRuleEditScreen

- Обновить Status selector
- Обновить badge каждого шага (Pending / Running / Done / Error)
- При переходе в Error — показать уведомление на экране

---

## 8. Удаление и принудительная остановка

### Terminate (принудительная остановка)

- Кнопка в header экрана. Доступна всегда.
- Confirmation dialog: «Stop ComplexRule? All devices turned on by this ComplexRule will be switched off immediately.»
- После подтверждения: все шаги → Done, статус → Completed.

### Удаление

- Доступно с экрана списка (swipe/long press → Delete).
- Confirmation: «Удалить ComplexRule "[Название]"? Все связанные правила будут удалены с модулей.»
- `DELETE /api/v1/ComplexRule/{uid}`
- Во время Running — сначала Terminate, затем Delete.

### Редактирование

- Редактирование происходит inline на том же экране (ComplexRuleEditScreen).
- Все поля доступны для изменения в любое время.
- «Save Changes» → `PUT /api/v1/ComplexRule/{uid}`.
- Бэкенд пересоздаёт правила на модулях → статус переходит в Synchronizing.

---

## 9. Валидация и обработка ошибок

### Валидация на фронтенде

| Проверка | Когда | Сообщение |
|----------|-------|-----------|
| Название пустое | Save | «Введите название» |
| Нет шагов | Save | «Добавьте хотя бы один шаг» |
| Устройство не выбрано | Save | «Выберите устройство для шага N» |
| Устройство заблокировано (rule) | Dropdown | Option disabled, выбрать нельзя |
| Одинаковый приоритет | Save (если shared devices) | «Приоритет N уже занят ComplexRule "X"» |
| Триггер не задан | Save | «Выберите время/условие запуска» |
| Датчиков триггера нет (Sensor mode) | Save | «Добавьте хотя бы один датчик триггера» |

### Ошибки от бэкенда

| Ошибка | Отображение |
|--------|-------------|
| Конфликт устройств | Alert с описанием конфликта |
| Синхронизация не удалась | Бейдж Error + сообщение |
| ComplexRule в Error (runtime) | Push-уведомление + бейдж на экране |

---

## 10. API-интеграция

### Device availability — 3 статуса

Запрос `GET /api/v1/ComplexRule/devices/availability` возвращает для каждого устройства один из трёх статусов:

| Статус | Цвет в dropdown | Поведение |
|--------|-----------------|-----------|
| `available` | Стандартный | Выбрать можно, без предупреждений |
| `conflict` | Жёлтый (`color:#B45309`) | Выбрать можно. Внутри step-карточки — жёлтый conflict banner: «Device shared with [ComplexRule name] — priority determines control.» |
| `rule` | Красный (`color:#A32D2D`) | **Выбрать нельзя** (option disabled). Внутри step-карточки — красный banner: «Device assigned to [Rule name] — unavailable for this ComplexRule.» |

### Сервисный слой

Создать `ComplexRuleService` / `ComplexRuleApiClient`:

| Метод | Эндпоинт | Описание |
|-------|----------|----------|
| `getComplexRules()` | `GET /api/v1/ComplexRule` | Список с состояниями |
| `getComplexRule(uid)` | `GET /api/v1/ComplexRule/{uid}` | Детали |
| `createComplexRule(data)` | `POST /api/v1/ComplexRule` | Создание |
| `updateComplexRule(uid, data)` | `PUT /api/v1/ComplexRule/{uid}` | Обновление |
| `deleteComplexRule(uid)` | `DELETE /api/v1/ComplexRule/{uid}` | Удаление |
| `enableComplexRule(uid)` | `POST /api/v1/ComplexRule/{uid}/enable` | Включение |
| `disableComplexRule(uid)` | `POST /api/v1/ComplexRule/{uid}/disable` | Выключение |
| `terminateComplexRule(uid)` | `POST /api/v1/ComplexRule/{uid}/terminate` | Принудительная остановка |
| `getDeviceAvailability()` | `GET /api/v1/ComplexRule/devices/availability` | Доступность устройств |

### Стейт-менеджмент

- Хранить список ComplexRule и их состояния
- Обновлять при получении данных с бэкенда (polling/push)
- После Save — перезапрашивать getComplexRule(uid) чтобы получить актуальное состояние

---

## Порядок реализации (рекомендация)

```
Фаза 1: Каркас
  ├── Боковая навигация — новый пункт Complex Rules
  ├── ComplexRuleListScreen (список с состояниями)
  └── ComplexRuleService (API-клиент)

Фаза 2: Edit screen — статика
  ├── Header (название, priority, status, toggle, terminate)
  ├── Trigger card — Sensor mode
  ├── Trigger card — Schedule mode
  └── Active Hours card

Фаза 3: Steps
  ├── Sensor mode steps grid (device + sensor rows + params)
  ├── Schedule mode steps grid
  ├── Device availability (3 статуса: available/conflict/rule)
  └── Conflict banners внутри step-карточек

Фаза 4: Сохранение и управление
  ├── Save Changes → PUT/POST
  ├── Notifications card
  ├── Terminate с confirmation dialog
  └── Delete с confirmation

Фаза 5: Реал-тайм
  ├── Polling статусов шагов
  ├── Step status badges (Pending/Running/Done/Error)
  └── Push-уведомления (если SignalR)
```

---

## Что НЕ меняется

- Экраны Rules — без изменений
- Существующая навигация — Complex Rules добавляется рядом
- Device management — без изменений
- Sensor screens — без изменений
