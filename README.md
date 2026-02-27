# brainflayer

**Высокопроизводительный инструмент для проверки Bitcoin/Ethereum brainwallet-фраз**

Brainflayer берёт текстовые фразы (или другие входные данные) из словаря, хэширует их для получения приватных ключей, вычисляет соответствующие публичные ключи по кривой secp256k1, формирует hash160 адресов и проверяет их по блум-фильтру (и, опционально, по отсортированному файлу хэшей).

---

## Описание

### Как это работает

Brainflayer реализует следующий конвейер:

```
фраза/строка
    │
    ▼
[хэш-функция] ──────── sha256 / sha3 / keccak / warp / bwio / bv2 / rush / camp2
    │
    ▼
приватный ключ (32 байта)
    │
    ▼
[secp256k1] ─────────── умножение точки на кривой (батч-режим)
    │
    ▼
публичный ключ (65 байт, несжатый)
    │
    ├─── Bitcoin несжатый (u): SHA256 → RIPEMD-160
    ├─── Bitcoin сжатый   (c): SHA256 → RIPEMD-160 сжатого ключа
    ├─── Ethereum          (e): Keccak-256 → последние 160 бит
    └─── X-координата      (x): старшие 160 бит координаты X
    │
    ▼
hash160 (20 байт)
    │
    ▼
[блум-фильтр] ──────── есть ли совпадение?
    │
    ▼
[опц. точная проверка] ─ бинарный поиск по отсортированному файлу (-f)
    │
    ▼
вывод: hash160:тип:режим:фраза
```

Блум-фильтр имеет размер **512 МиБ** и использует **20 хэш-функций**, что обеспечивает крайне низкий процент ложных срабатываний при большом числе загруженных адресов.

---

## Зависимости

### Системные пакеты

```bash
# Debian / Ubuntu
sudo apt-get install build-essential libssl-dev libgmp-dev

# Fedora / RHEL
sudo dnf install gcc make openssl-devel gmp-devel
```

| Зависимость | Назначение |
|---|---|
| `gcc` | Компилятор C (требуется поддержка gnu99) |
| `make` | Система сборки |
| `libssl-dev` | OpenSSL — SHA-256, RIPEMD-160, PBKDF2 |
| `libgmp-dev` | GNU MP — арифметика больших чисел для secp256k1 |
| `librt` | POSIX real-time (обычно входит в состав glibc) |

### Встроенные подмодули (git submodules)

| Подмодуль | Назначение |
|---|---|
| `secp256k1/` | Оптимизированная библиотека для вычислений на эллиптической кривой (Bitcoin Core) |
| `scrypt-jane/` | Реализация scrypt для WarpWallet, brainwallet.io, brainv2 |

---

## Сборка

```bash
git clone https://github.com/komyaka/brainflayer
cd brainflayer
git submodule update --init
make
```

После успешной сборки в директории появятся исполняемые файлы:
`brainflayer`, `hexln`, `hex2blf`, `blfchk`, `ecmtabgen`, `filehex`

---

## Инструменты

### `brainflayer` — основной инструмент

Читает строки из stdin (или файла `-i`), применяет выбранный алгоритм хэширования, вычисляет публичные ключи и адреса, проверяет по блум-фильтру. При совпадении выводит результат.

### `hex2blf` — создание блум-фильтра

```
hex2blf <hashfile.hex> <bloomfile.blf>
```

Читает файл с hex-кодированными hash160 (по одному на строку, 40 символов) и строит блум-фильтр размером 512 МиБ. Если файл блум-фильтра уже существует — дополняет его.

### `blfchk` — проверка по блум-фильтру

```
blfchk <bloomfile.blf> [hashfile.hex]
```

Читает hex-кодированные hash160 из stdin и выводит те, которые присутствуют в блум-фильтре. Если задан второй аргумент — дополнительно проверяет по отсортированному файлу (устраняет ложные срабатывания).

### `hexln` — преобразование строк в hex

```
hexln
```

Читает stdin строка за строкой, каждую строку (без символа переноса) выводит как hex-строку. Корректно обрабатывает CRLF, LF и CR. Полезен для передачи произвольных бинарных данных в brainflayer через `-x`.

### `ecmtabgen` — генерация таблицы умножения EC

```
ecmtabgen <window_size> <tablefile.tab>
```

Предварительно вычисляет и сохраняет на диск таблицу умножения точки генератора secp256k1. Используется с флагом `-m` для ускорения старта brainflayer.

### `filehex` — чтение hex-файлов

Утилита для работы с hex-файлами (внутреннее использование).

---

## Подготовка блум-фильтра

Для работы brainflayer требуется блум-фильтр, содержащий hash160-адреса, которые нужно найти.

### Шаг 1. Получить список hash160 Bitcoin-адресов

Источники:
- **[Utxodump](https://github.com/in3rsha/bitcoin-utxo-dump)** — дамп всех UTXO (только активные адреса)
- **[Blockchair](https://blockchair.com/dumps)** — готовые дампы адресов
- **Собственный Bitcoin-узел** — через `bitcoin-cli`

Файл должен содержать hex-кодированные hash160, по одному на строку:

```
751e76e8199196f454a1b5e2f8e1d18a1ab89cae
89abcdefabbaabbaabbaabbaabbaabbaabbaabba
...
```

### Шаг 2. Создать блум-фильтр

```bash
hex2blf addresses.hex bitcoin.blf
```

Пример вывода:
```
[*] Loading hash160s from 'addresses.hex'   100.0%
[*] Loaded 48234567 hashes, false positive rate: ~2.154e-04 (1 in ~4642)
[*] Writing bloom filter to 'bitcoin.blf'...
[+] Success!
```

> **Примечание:** Блум-фильтр всегда имеет фиксированный размер 512 МиБ, независимо от числа загруженных адресов.

---

## Формат вывода

Каждая найденная фраза выводится в формате:

```
<hash160_hex>:<тип>:<режим_входа>:<исходная_строка>
```

| Поле | Описание |
|---|---|
| `hash160_hex` | 40-символьный hex hash160 найденного адреса |
| `тип` | `u` (несжатый Bitcoin), `c` (сжатый Bitcoin), `e` (Ethereum), `x` (x-координата) |
| `режим_входа` | `sha256`, `sha3`, `keccak`, `warp`, `bwio`, `bv2`, `rush`, `camp2`, `priv` |
| `исходная_строка` | Исходная фраза или приватный ключ |

Пример:
```
751e76e8199196f454a1b5e2f8e1d18a1ab89cae:u:sha256:correct horse battery staple
b10c007eb6169c3a190411d97d0d31509f3d2f14:c:sha256:correct horse battery staple
```

---

## Примеры использования

### Базовый перебор Bitcoin brainwallet (SHA-256)

Проверить фразы из словаря по блум-фильтру (по умолчанию проверяются несжатые `u` и сжатые `c` адреса):

```bash
brainflayer -b bitcoin.blf -i wordlist.txt
```

Читать из stdin:
```bash
cat wordlist.txt | brainflayer -b bitcoin.blf
```

Сохранить результаты в файл:
```bash
brainflayer -b bitcoin.blf -i wordlist.txt -o found.txt
```

---

### Подробный режим (`-v`)

Отображать прогресс и скорость перебора:

```bash
brainflayer -b bitcoin.blf -i wordlist.txt -v
```

Пример вывода в stderr:
```
[*]  12345 p/s (avg:  11230 p/s)  processed:   1234500  found: 0
```

---

### Только один тип адреса

Проверять только сжатые Bitcoin-адреса:
```bash
brainflayer -b bitcoin.blf -i wordlist.txt -c c
```

Проверять только несжатые:
```bash
brainflayer -b bitcoin.blf -i wordlist.txt -c u
```

Проверять несжатые, сжатые и Ethereum-адреса одновременно:
```bash
brainflayer -b bitcoin.blf -i wordlist.txt -c uce
```

---

### Ethereum-адреса (Keccak-256)

Использовать Keccak-256 как алгоритм хэширования и проверять Ethereum-адреса:

```bash
# Старый формат (ethercamp / старый ethaddress.org)
brainflayer -b ethereum.blf -t keccak -c e -i wordlist.txt

# Новый ethercamp (2031 прохода Keccak-256)
brainflayer -b ethereum.blf -t camp2 -c e -i wordlist.txt
```

---

### WarpWallet с солью (`-t warp -s`)

WarpWallet объединяет scrypt и PBKDF2. Соль передаётся через `-s` (обычно email):

```bash
# Фразы в wordlist, соль фиксирована
brainflayer -b bitcoin.blf -t warp -s user@example.com -i wordlist.txt

# Соли в wordlist (email-адреса), фраза фиксирована (-p)
brainflayer -b bitcoin.blf -t warp -p "correct horse battery staple" -i emails.txt
```

> **Внимание:** WarpWallet очень медленный (scrypt с N=2^18). Ожидайте не более 1–10 ключей в секунду.

Аналогичный синтаксис для brainwallet.io (`-t bwio`) и brainv2 (`-t bv2`):
```bash
brainflayer -b bitcoin.blf -t bwio -s user@example.com -i wordlist.txt
# bv2 - КРАЙНЕ МЕДЛЕННЫЙ
brainflayer -b bitcoin.blf -t bv2  -s user@example.com -i wordlist.txt
```

---

### Режим SHA-3 (`-t sha3`)

```bash
brainflayer -b bitcoin.blf -t sha3 -i wordlist.txt
```

---

### Инкрементальный перебор приватных ключей (`-I`)

Перебирать приватные ключи последовательно начиная с заданного значения:

```bash
# Начать с ключа 0x1 и перебрать 1 000 000 ключей
brainflayer -b bitcoin.blf -I 0000000000000000000000000000000000000000000000000000000000000001 -N 1000000 -v

# Начать с произвольного места
brainflayer -b bitcoin.blf -I 8000000000000000000000000000000000000000000000000000000000000000
```

Этот режим очень быстрый — ключи не хэшируются, а инкрементируются напрямую.

---

### Распределение нагрузки между процессами (`-n K/N`)

Разделить словарь на N частей и обрабатывать только K-ю часть (K начинается с 1):

```bash
# Запустить 4 параллельных процесса:
brainflayer -b bitcoin.blf -i wordlist.txt -n 1/4 &
brainflayer -b bitcoin.blf -i wordlist.txt -n 2/4 &
brainflayer -b bitcoin.blf -i wordlist.txt -n 3/4 &
brainflayer -b bitcoin.blf -i wordlist.txt -n 4/4 &
```

---

### Пропуск строк (`-k`, `-N`)

Пропустить первые 1 000 000 строк:
```bash
brainflayer -b bitcoin.blf -i wordlist.txt -k 1000000
```

Обработать только 500 000 строк:
```bash
brainflayer -b bitcoin.blf -i wordlist.txt -N 500000
```

Комбинирование — обработать строки с 1 000 001 по 1 500 000:
```bash
brainflayer -b bitcoin.blf -i wordlist.txt -k 1000000 -N 500000
```

---

### Hex-кодированный ввод (`-x`)

Если входные данные — hex-строки (например, бинарные данные, пропущенные через `hexln`):

```bash
# Преобразовать обычные фразы в hex и передать в brainflayer
cat wordlist.txt | hexln | brainflayer -b bitcoin.blf -x

# Передать сырые приватные ключи напрямую (-t priv требует -x)
cat privkeys.hex | brainflayer -b bitcoin.blf -t priv -x
```

---

### Rushwallet (`-t rush -r`)

Rushwallet использует фиксированный фрагмент URL:

```bash
brainflayer -b bitcoin.blf -t rush -r "#fragment_here" -i wordlist.txt
```

---

### Предвычисленная таблица EC (`ecmtabgen` + `-m`)

Создание таблицы (один раз):
```bash
# Размер окна 16 (значение по умолчанию): ~192 МиБ файл, быстрый старт
ecmtabgen 16 ecmult_table_w16.tab

# Размер окна 12: ~12 МиБ файл, немного медленнее
ecmtabgen 12 ecmult_table_w12.tab
```

Использование таблицы:
```bash
brainflayer -b bitcoin.blf -m ecmult_table_w16.tab -i wordlist.txt
```

> При использовании `-m` параметр `-w` игнорируется — размер окна берётся из файла таблицы. Это ускоряет запуск при многократном перезапуске инструмента.

---

### Параметр батча (`-B`)

Размер батча для батч-вычислений аффинных преобразований (должен быть степенью двойки):

```bash
# По умолчанию: 1024 (оптимальный баланс производительности и кэша)
brainflayer -b bitcoin.blf -i wordlist.txt -B 1024

# Максимум: 4096
brainflayer -b bitcoin.blf -i wordlist.txt -B 4096

# Маленький батч для экономии памяти
brainflayer -b bitcoin.blf -i wordlist.txt -B 64
```

---

### Точная проверка по файлу (`-f`)

Блум-фильтр может давать ложные срабатывания. Для точной верификации добавьте отсортированный файл hash160:

```bash
# Создать отсортированный файл (требуется однократно)
sort addresses.hex > addresses_sorted.hex

# Использовать блум-фильтр для быстрой предфильтрации + точная проверка
brainflayer -b bitcoin.blf -f addresses_sorted.hex -i wordlist.txt
```

`-f` выполняет бинарный поиск по файлу для проверки каждого кандидата из блум-фильтра — все ложные срабатывания отфильтровываются.

---

### Использование `hexln`

```bash
# Каждая строка словаря → hex
echo "correct horse battery staple" | hexln
# → 636f72726563742068...(hex)

# Пайп в brainflayer
cat wordlist.txt | hexln | brainflayer -b bitcoin.blf -x
```

---

### Использование `hex2blf`

```bash
# Создать новый блум-фильтр
hex2blf addresses.hex bitcoin.blf

# Дополнить существующий блум-фильтр новыми адресами
hex2blf new_addresses.hex bitcoin.blf
```

---

### Использование `blfchk`

```bash
# Проверить список hash160 по блум-фильтру
cat hashes_to_check.hex | blfchk bitcoin.blf

# Проверить с устранением ложных срабатываний
cat hashes_to_check.hex | blfchk bitcoin.blf addresses_sorted.hex
```

---

### Дополнительный режим вывода (`-a`)

Дописывать результаты в конец файла (не перезаписывать):

```bash
brainflayer -b bitcoin.blf -i wordlist.txt -o found.txt -a
```

---

## Тестирование

```bash
# Запустить все тесты (нормализация строк + E2E тесты brainflayer + round-trip блум-фильтра)
make test

# Проверка памяти (требуется valgrind)
make memcheck

# Сборка с AddressSanitizer и запуск тестов
make sanitize

# Бенчмарк (генерирует тестовый словарь и замеряет производительность)
make bench

# Бенчмарк с кастомным размером батча
make bench BENCH_B=4096
```

---

## Производительность

### Ключевые параметры

| Параметр | Флаг | Описание |
|---|---|---|
| Размер батча | `-B` | Число ключей, вычисляемых одновременно. По умолчанию 1024, максимум 4096. Батч-вычисления амортизируют стоимость аффинных преобразований. |
| Размер окна | `-w` | Размер окна для таблицы EC-умножения. По умолчанию 16. Памяти: ~3 × 2^w КиБ при старте, ~2^w КиБ после построения. |
| Предвычисленная таблица | `-m` | Загрузить готовую таблицу EC-умножения из файла вместо построения при каждом запуске. |

### Рекомендации

- **Для максимальной производительности** (sha256, keccak): используйте `-B 1024` или `-B 2048`
- **Для параллельного запуска** нескольких процессов: делите задачу через `-n K/N`
- **WarpWallet/bwio/bv2** работают значительно медленнее из-за применения scrypt/PBKDF2 — это ожидаемо и не является ошибкой
- **Инкрементальный режим** (`-I`) и **rushwallet** (`-t rush`) — наиболее быстрые режимы

---

## Все флаги brainflayer

```
Использование: brainflayer [ПАРАМЕТР]...

 -a                          открыть файл вывода в режиме дозаписи
 -b FILE                     проверять совпадения по блум-фильтру FILE
 -f FILE                     верифицировать совпадения по отсортированным hash160 в FILE
 -i FILE                     читать из FILE вместо stdin
 -o FILE                     писать в FILE вместо stdout
 -c TYPES                    типы адресов для вычисления hash160 (по умолчанию: 'uc')
                              u - несжатый Bitcoin-адрес
                              c - сжатый Bitcoin-адрес
                              e - Ethereum-адрес
                              x - старшие биты координаты X публичного ключа
 -t TYPE                     тип входных данных:
                              sha256 (по умолчанию) — классический brainwallet
                              sha3   — sha3-256
                              priv   — сырые приватные ключи (требует -x)
                              warp   — WarpWallet (поддерживает -s или -p)
                              bwio   — brainwallet.io (поддерживает -s или -p)
                              bv2    — brainv2 (поддерживает -s или -p) ОЧЕНЬ МЕДЛЕННЫЙ
                              rush   — rushwallet (требует -r) БЫСТРЫЙ
                              keccak — keccak256 (ethercamp / старый ethaddress)
                              camp2  — keccak256 × 2031 (новый ethercamp)
 -x                          считать входные данные hex-кодированными
 -s SALT                     использовать SALT для солёных типов (по умолчанию: нет)
 -p PASSPHRASE               использовать PASSPHRASE для солёных типов;
                              входные данные будут считаться солями
 -r FRAGMENT                 использовать FRAGMENT для взлома rushwallet
 -I HEXPRIVKEY               инкрементальный режим перебора ключей начиная с HEXPRIVKEY
                              (поддерживает -n) БЫСТРЫЙ
 -k K                        пропустить первые K строк ввода
 -N N                        остановиться после N строк или ключей
 -n K/N                      использовать только K-ю из каждых N строк ввода
 -B BATCH_SIZE               размер батча для аффинных преобразований
                              должен быть степенью двойки (по умолчанию: 1024, максимум: 4096)
 -w WINDOW_SIZE              размер окна для таблицы ecmult (по умолчанию: 16)
                              ~3 × 2^w КиБ памяти при старте, ~2^w КиБ после построения
 -m FILE                     загрузить таблицу ecmult из FILE
                              инструмент ecmtabgen может построить такую таблицу
 -v                          подробный режим — отображать прогресс
 -h                          показать эту справку
```

---

## Лицензия

Copyright (c) 2015 Ryan Castellucci, All Rights Reserved.
