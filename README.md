# GameX

Редактор пам'яті локальних процесів на Linux. Знайдіть у грі число (гроші, життя, набої), відсійте зайві адреси, підставте своє значення і за потреби заморозьте його.

**Межі:** лише процеси вашого користувача; без мережевого читу, без приховування процесу, без обходу античиту. Запис у пам'ять — через `process_vm_readv` / `process_vm_writev`.

## Збірка і встановлення

Мова — C++20. Інтерфейс — Qt Widgets. Ядро (`gamex_core`) від Qt не залежить.

### Залежності

```bash
sudo apt install g++ cmake ninja-build
# системний Qt (один із варіантів):
sudo apt install qt6-base-dev
# опційно — мнемоніка в «Хто пише…»:
sudo apt install libcapstone-dev
```

Повідомлення cmake `Capstone not found` — не помилка: без Capstone збірка йде нормально, лише без тексту інструкцій у watchpoint.

### Збірка проти системного Qt

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/gamex_core_tests   # має надрукувати ok
./build/gamex              # запуск без встановлення
```

### Збірка проти Qt з інсталера (~/Qt/…)

Якщо використовуєте offline-набір (наприклад 6.12), **обов’язково** вкажіть префікс — інакше після install підхопиться системний Qt іншої версії (`version Qt_6.12 not found`):

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$HOME/.local" \
  -DGAMEX_QT_ROOT="$HOME/Qt/6.12.0/gcc_64"
cmake --build build
cmake --install build
```

У бінарник вшивається `RUNPATH` на `$GAMEX_QT_ROOT/lib`, тож окремий `LD_LIBRARY_PATH` не потрібен.

Тимчасовий обхід, якщо вже встановили без RPATH:

```bash
LD_LIBRARY_PATH="$HOME/Qt/6.12.0/gcc_64/lib" gamex
```

### Встановлення

У домашній каталог (без root; переконайтесь, що `$HOME/.local/bin` у `PATH`):

```bash
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$HOME/.local"
# за потреби додайте -DGAMEX_QT_ROOT=...
cmake --build build
cmake --install build
gamex
```

У `/usr/local` (потрібен sudo):

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
sudo cmake --install build
```

Встановлюється: `bin/gamex`, ярлик `share/applications/gamex.desktop`, іконки `share/icons/hicolor/…`, документація `share/doc/gamex/` (`LICENSE`, `NOTICE`, `README.md`).

### Видалення

З `$HOME/.local`:

```bash
rm -f "$HOME/.local/bin/gamex" \
  "$HOME/.local/share/applications/gamex.desktop" \
  "$HOME/.local/share/icons/hicolor/"*/apps/gamex.png \
  "$HOME/.local/share/icons/hicolor/scalable/apps/gamex.svg"
rm -rf "$HOME/.local/share/doc/gamex"
```

З `/usr/local`:

```bash
sudo rm -f /usr/local/bin/gamex \
  /usr/local/share/applications/gamex.desktop \
  /usr/local/share/icons/hicolor/*/apps/gamex.png \
  /usr/local/share/icons/hicolor/scalable/apps/gamex.svg
sudo rm -rf /usr/local/share/doc/gamex
```

## Доступ до пам'яті

На типовому Ubuntu `kernel.yama.ptrace_scope = 1`: читати можна нащадків. Кнопка **Запустити** стартує гру як дочірній процес — це основний робочий шлях. Підключення до вже відкритої гри ядро часто блокує.

## Як користуватись

1. **Запустити** або **Обрати процес**.
2. Ввести значення з екрана (або вираз `100*10`, або шаблон `DE ?? AD`), натиснути **Шукати**.
3. Змінити параметр у грі → **Відсіяти** (наприклад «Змінилось»).
4. **Додати обране** вниз → правити комірку «Значення → у гру».

У меню **Довідка → Як користуватись…** (F1) — повний опис кнопок і режимів.

## Можливості

| Що         | Коротко                                                              |
| ---------- | -------------------------------------------------------------------- |
| Пошук      | Рівність, порівняння, діапазон, невідомий знімок, відсів; фон + Стоп |
| Типи       | 1–8 байтів (є i24), float, UTF-8, байти; LE/BE                       |
| Таблиця    | Заморозка (4 режими), групи, F5–F8, файл `.gxt`                      |
| ASLR       | `module+зсув`, пошук статичних вказівників                           |
| Формули    | Вирази в полі пошуку, M1–M4, маска `??`                              |
| Структура  | Кілька полів зі зсувами, один прохід                                 |
| Hex        | Перегляд і запис байтів навколо адреси                               |
| Watchpoint | «Хто пише…» через ptrace (лише процес з **Запустити**)               |
| Емулятори  | Профілі NES/SNES/GBA/NDS/PS1/N64/…: гостьова адреса ↔ RAM у `maps`   |

## Каталоги

```text
src/core   пам'ять, сканер, таблиця, заморозка, watchpoint, профілі емуляторів
src/ui     вікно Qt
tests      перевірки ядра без Qt
```

## Ліцензія

GameX розповсюджується на умовах **GNU GPL версії 3** (або пізнішої, на ваш вибір). Текст — у [LICENSE](LICENSE).

Сторонні залежності (Qt, опційний Capstone тощо) описані в [NOTICE](NOTICE). Дотримуйтесь і їхніх умов.

Інструмент для власних однокористувацьких ігор і навчальних програм. Використання проти чужих правил сервісу або законодавства — на вашій відповідальності.
