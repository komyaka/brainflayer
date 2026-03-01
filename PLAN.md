# Детальный план развития brainflayer

> Версия: 1.0 | Дата: 2026-03-01 | Ветка: `copilot/fix-code-errors-and-performance`

---

## Контекст

Предыдущий этап (2026-02-26 — 2026-02-27) завершён с аудитором VERIFIED:

| Фаза              | Итог     |
|-------------------|----------|
| Исправление CR/LF | VERIFIED |
| Утечки памяти     | VERIFIED |
| Тесты (make test) | VERIFIED |
| CI (GitHub Actions) | VERIFIED |
| Windows совместимость | VERIFIED |
| Документация (README) | VERIFIED |
| Security review   | VERIFIED |
| Performance review | VERIFIED (только анализ) |

**Что НЕ реализовано** — три пункта из performance review:

| ID      | Суть                                      | Файл              |
|---------|-------------------------------------------|-------------------|
| PERF-01 | Batch size (Bopt) не подобран под кэш i5  | `brainflayer.c`   |
| PERF-02 | `hsearchf` использует fseek/fread, нет mmap-пути | `hsearchf.c` |
| PERF-03 | Режим camp/keccak (2031 раундов) без документации ограничений | `brainflayer.c` |

---

## Фаза 1 — Оптимизация batch size (PERF-01)

### Цель
Подобрать значение `Bopt` по умолчанию, которое вписывается в L2/L3 кэш Intel i5 при SHA256-режиме, и убрать per-line realloc для unhex-буфера.

### Конкретные изменения

| # | Файл | Изменение |
|---|------|-----------|
| 1a | `brainflayer.c` | Снизить значение `B` по умолчанию с 4096 до 1024 (если не задан `-B`). |
| 1b | `brainflayer.c` | Перед main-loop добавить `fstat` на входной файл, если `-i FILE`, и выделить unhex-буфер фиксированным размером вместо realloc-on-each-line. |
| 2 | `Makefile` | Добавить переменную `BENCH_COMPARE` и цель `bench-compare` для запуска двух прогонов (`-B 512` и `-B 1024`) с выводом строк/с. |
| 3 | `README.md` | Добавить в секцию «Производительность» таблицу результатов bench-compare (Ubuntu 24 / Intel i5). |

### Критерии приёмки

- `make bench BENCH_B=1024` завершается быстрее или равно текущему `make bench` (baseline BENCH_B=4096).
- `make test` проходит без ошибок.
- Линт (`gcc -Wall -Wextra ... -pedantic`) чист.

### Команды проверки

```bash
# сравнить два значения Bopt
make bench-compare

# убедиться что тесты не сломаны
make test

# quick perf snapshot (если доступен perf)
perf stat -r 3 -- taskset -c 0-3 ./brainflayer -t sha256 -B 1024 -i bench/bench_dict.txt >/dev/null
```

### Риски

| Риск | Вероятность | Смягчение |
|------|-------------|-----------|
| fstat недоступен для stdin | Средняя | Использовать realloc-путь только когда `fileno(in) != STDIN_FILENO` |
| Разные i5 поколения дают разные результаты | Средняя | Фиксировать CPU-модель в README; пересчитать при необходимости |

---

## Фаза 2 — mmap-путь в hsearchf (PERF-02)

### Цель
Заменить `fseek + fread` в функции бинарного/интерполяционного поиска `hsearchf` на доступ через `mmap` — устранить syscall-нагрузку при bloom-попадании.

### Конкретные изменения

| # | Файл | Изменение |
|---|------|-----------|
| 1 | `hsearchf.h` | Добавить поле `void *map` и `size_t maplen` в `struct hsearch` (или отдельный контекст). Объявить функции `hsearchf_open_mmap` / `hsearchf_close_mmap`. |
| 2 | `hsearchf.c` | Реализовать `hsearchf_open_mmap`: вызов `mmap(MAP_SHARED, PROT_READ)` + `posix_madvise(MADV_RANDOM)`; реализовать `hsearchf_close_mmap` с `munmap`. Заменить тело поиска: вместо `fseek + fread` — прямое обращение к `(uint8_t*)map + offset`. |
| 3 | `brainflayer.c` | Если `hsearchf_open_mmap` возвращает успех — использовать mmap-путь; иначе — fallback на `fseek/fread`. |
| 4 | `tests/normalize_test.c` (или новый `tests/hsearchf_test.c`) | Добавить тест: создать временную таблицу из 10 записей, выполнить `hsearchf_open_mmap`, сделать 3 поиска (1 miss, 2 hits), проверить результат, закрыть. |
| 5 | `Makefile` | Добавить цель `bench-hsearch` (сравнение `fseek` vs `mmap` при 100k bloom-попаданиях). |

### Критерии приёмки

- `hsearchf_open_mmap` корректно работает на Linux и возвращает `NULL` на Windows (где используется `mmapf.c` с `CreateFileMapping`).
- Поиск через mmap и `fseek` выдают идентичные результаты (проверяется новым тестом).
- `make test` проходит. Нет утечек при `make memcheck`.

### Команды проверки

```bash
make test
make memcheck
make bench-hsearch   # новая цель; выводит lookup/s для fseek и mmap
```

### Риски

| Риск | Вероятность | Смягчение |
|------|-------------|-----------|
| `mmap` на больших таблицах (>4 ГиБ) требует 64-bit поинтеров | Низкая | Проверять `st_size <= SIZE_MAX`; документировать в README |
| Windows `CreateFileMapping` / `MapViewOfFile` уже реализован в `mmapf.c` | Средняя | Переиспользовать `mmapf_open` вместо прямого POSIX mmap для кросс-платформенности |
| `posix_madvise` отсутствует на macOS | Низкая | Обернуть в `#ifdef POSIX_MADV_RANDOM` |

---

## Фаза 3 — Документирование и ограничения camp/keccak (PERF-03)

### Цель
Задокументировать, что режим `camp` (~2031 Keccak-раундов на кандидата) предназначен только для dedicated-прогонов с малым Bopt, и автоматически снижать Bopt до безопасного значения когда активен режим camp.

### Конкретные изменения

| # | Файл | Изменение |
|---|------|-----------|
| 1 | `brainflayer.c` | Добавить проверку: если `type == CAMP && Bopt > 256` — вывести предупреждение (`fprintf(stderr, "warning: ...")`) и снизить `Bopt` до 256. |
| 2 | `README.md` | Добавить в описание режима camp2 секцию «⚠ Предупреждение о производительности» с объяснением 2031 раундов и рекомендацией `-B 256`. |
| 3 | `tests/normalize_test.c` | Добавить тест: запустить `./brainflayer -t camp2 -B 1024 ...` через `popen`, убедиться что вывод `stderr` содержит «warning». |

### Критерии приёмки

- При `brainflayer -t camp2 -B 512` в stderr выводится предупреждение и `Bopt` снижается до 256.
- `make test` проходит.
- README содержит секцию предупреждения для camp режима.

### Команды проверки

```bash
echo "test" | ./brainflayer -t camp2 -B 512 2>&1 | grep -i warning
make test
```

---

## Фаза 4 — Тестирование и CI

### Цели
- Добавить `hsearchf_test` в тест-набор.
- Убедиться что CI (`.github/workflows/ci.yml`) запускает новые тесты.
- Проверить `make sanitize` на новом коде.

### Конкретные изменения

| # | Файл | Изменение |
|---|------|-----------|
| 1 | `Makefile` | Добавить `tests/hsearchf_test$(EXT)` в `TESTS`; обновить `test` и `sanitize` цели. |
| 2 | `.github/workflows/ci.yml` | Добавить шаг `make bench BENCH_B=1024` (только измерение, не gate) для отслеживания регрессий перф в PR. |

### Критерии приёмки

- `make test` запускает и normalize_test, и hsearchf_test.
- CI проходит без ошибок на Ubuntu 24 и Windows MSYS2.

---

## Фаза 5 — Обновление документации

### Цели
- Обновить `README.md` с результатами bench-compare и bench-hsearch.
- Добавить раздел «Известные ограничения» (camp mode, Windows mmap).
- Обновить `CHANGELOG.md`.

### Конкретные изменения

| # | Файл | Изменение |
|---|------|-----------|
| 1 | `README.md` | Секции: «Производительность» (bench-compare + bench-hsearch таблицы), «Режим camp2» (предупреждение), «Известные ограничения». |
| 2 | `CHANGELOG.md` | Записи для Фаз 1–4. |
| 3 | `STATUS.md` | Обновить Acceptance Criteria и Orchestrator Log. |

---

## Итоговая последовательность агентов

```
orchestrator
  → coder          (Фаза 1: PERF-01 batch size)
  → auditor        (gate: make test + bench-compare)
  → coder          (Фаза 2: PERF-02 hsearchf mmap)
  → security-review (новый mmap path)
  → auditor        (gate: make test + make memcheck)
  → coder          (Фаза 3: PERF-03 camp warning)
  → coder          (Фаза 4: CI обновление)
  → docs-writer    (Фаза 5: README/CHANGELOG)
  → auditor        (финальный: VERIFIED)
```

---

## Временные оценки

| Фаза | Сложность | Оценка (агент-часы) |
|------|-----------|---------------------|
| Фаза 1 | Средняя | 1–2 |
| Фаза 2 | Высокая | 3–4 |
| Фаза 3 | Низкая | 0.5–1 |
| Фаза 4 | Низкая | 0.5–1 |
| Фаза 5 | Низкая | 0.5–1 |
| **Итого** | | **5.5–9** |

---

## Зависимости и предусловия

- `git submodule update --init` (secp256k1, scrypt-jane) — уже выполнено.
- Окружение: Ubuntu 24.04, gcc 13.3.0, OpenSSL 3.0, GMP, valgrind (опционально).
- Windows: MSYS2/MinGW64 с `ws2_32`, `crypto`, `gmp` — поддерживается через `win_compat.h`.

---

## Метрики успеха

| Метрика | Базовый уровень | Цель |
|---------|-----------------|------|
| `make bench BENCH_B=1024` (строк/с) | ~1.67M/с (0.60 с на 1M строк) | ≥ 1.67M/с (не регрессировать) |
| `bench-hsearch` lookup/с (mmap vs fseek) | — (нет baseline) | +10% при hit rate ≥ 0.1% |
| `make test` — количество тестов | 10 | ≥ 12 (+ hsearchf_test, camp warning) |
| CI время (Ubuntu) | ~3 мин | ≤ 5 мин (с bench шагом) |
