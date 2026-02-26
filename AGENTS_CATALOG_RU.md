# Каталог агентов (10)

Профили лежат в `.github/agents/`. Делегирование — строго через `runSubagent()` под управлением `orchestrator`.

| Агент | Назначение | Обычно пишет |
|---|---|---|
| orchestrator | Координация фаз, гейтов, роутинг по триггерам, REDO циклы | STATUS.md (координация) |
| issue-analyst | Воспроизведение бага, root cause | STATUS.md (REPRO/ROOT CAUSE) |
| architect | Scope/дизайн/интерфейсы/риски/команды запуска | STATUS.md (план/дизайн) |
| coder | Реализация (фича/фикс/рефактор), запуск тестов | Код + STATUS.md (лог) |
| refactor-maintainer | Поддерживаемость/рефактор без изменения поведения | Код + STATUS.md |
| qa-test | Тест-план/регрессии/стабильность | STATUS.md (TEST PLAN) (+ тесты по разрешению) |
| security-review | Ревью безопасности | STATUS.md (SECURITY REVIEW) |
| performance-review | Ревью производительности | STATUS.md (PERFORMANCE REVIEW) |
| dx-ci | CI/сборка/линт/формат/инструменты | CI/конфиги + STATUS.md (BUILD/CI) |
| docs-writer | Документация, run steps, миграционные заметки | docs + STATUS.md (DOCS) |
| auditor | Финальная проверка VERIFIED/REDO | STATUS.md (AUDIT) |
