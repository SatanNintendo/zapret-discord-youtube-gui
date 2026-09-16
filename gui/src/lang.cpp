/*
 * Zapret GUI — lang.cpp
 * RU / EN string tables. The compile-time size checks guarantee the
 * enum, the RU table and the EN table never drift apart.
 */
#include <windows.h>
#include "lang.h"

int g_lang = ZG_LANG_RU;

void zg_lang_set(int lang)
{
    if (lang < 0 || lang >= ZG_LANG_COUNT) lang = ZG_LANG_RU;
    g_lang = lang;
}

const wchar_t* zg_str(int id)
{
    extern const wchar_t* const TBL_RU[];
    extern const wchar_t* const TBL_EN[];
    if (id < 0 || id >= ZG_STR_COUNT) return L"?";
    return (g_lang == ZG_LANG_EN) ? TBL_EN[id] : TBL_RU[id];
}

/* ------------------------------------------------------------------ */
/* Russian                                                             */
/* ------------------------------------------------------------------ */
const wchar_t* const TBL_RU[ZG_STR_COUNT] = {
    /* window / general */
    L"Zapret GUI — Discord и YouTube",
    L"Обход блокировок Discord и YouTube",
    L"Проверить обновления",
    L"Очистить",
    L"НАСТРОЙКИ",
    L"ЖУРНАЛ СОБЫТИЙ",
    L"Диагностика",
    L"Обновить hosts",
    L"Обновить IPSet",
    L"Тесты",
    L"ВКЛЮЧИТЬ ОБХОД",
    L"ВЫКЛЮЧИТЬ ОБХОД",
    L"ВЫБРАТЬ ПАПКУ ZAPRET",

    /* hero status */
    L"ОБХОД ВКЛЮЧЁН",
    L"ОБХОД ВЫКЛЮЧЕН",
    L"ФАЙЛЫ ZAPRET НЕ НАЙДЕНЫ",
    L"укажите папку zapret — нажмите «Выбрать папку zapret» ниже",
    L"работает как служба — стратегия: %s",
    L"запущен из Zapret GUI — стратегия: %s",
    L"winws.exe запущен вручную (вне Zapret GUI)",
    L"служба zapret установлена, но не запущена",
    L"нажмите кнопку ниже, чтобы включить обход",

    /* cards */
    L"ПРОЦЕСС WINWS",
    L"работает",
    L"не запущен",
    L"СЛУЖБА ZAPRET",
    L"запущена",
    L"установлена",
    L"не установлена",
    L"ДРАЙВЕР WINDIVERT",
    L"активен",
    L"не активен",

    /* settings rows */
    L"Стратегия обхода",
    L"Игровой фильтр",
    L"Автозапуск с Windows (служба)",
    L"Проверять обновления zapret",
    L"Фильтр IP-списков (IPSet)",
    L"Язык интерфейса",
    L"Тема оформления",

    /* theme names */
    L"Тёмная",
    L"Светлая",
    L"Полночь",

    /* tray */
    L"Zapret GUI — обход включён",
    L"Zapret GUI — обход выключен",
    L"Zapret GUI — файлы zapret не найдены",
    L"Zapret GUI",
    L"Программа свернута в трей и продолжает работать. Полный выход — правой кнопкой по значку.",
    L"Открыть Zapret GUI",
    L"Полный выход",

    /* missing-folder dialog */
    L"Zapret GUI — файлы zapret не найдены",
    L"Файлы zapret не найдены",
    L"ZapretGUI.exe запущен из папки:\n%s\n\n"
    L"Там нет bin\\winws.exe.\n\n"
    L"Папка zapret — это распакованный архив zapret-discord-youtube:\n"
    L"внутри неё лежат bin\\, lists\\ и файлы general*.bat.\n"
    L"Копировать ZapretGUI.exe внутрь не обязательно —\n"
    L"можно просто указать эту папку.",
    L"Указать папку zapret…",
    L"Найти папку автоматически",
    L"Продолжить без zapret",
    L"Файлы zapret не найдены: рядом с ZapretGUI.exe нет bin\\winws.exe.\n\n"
    L"ZapretGUI.exe запущен из папки:\n%s\n\n"
    L"«Да» — указать папку zapret вручную\n"
    L"«Нет» — повторить автоматический поиск\n"
    L"«Отмена» — продолжить без zapret",
    L"Укажите папку zapret — где лежат bin\\winws.exe и general*.bat",
    L"В выбранной папке нет bin\\winws.exe.\n\n"
    L"Выберите папку, в которую распакован zapret —\n"
    L"внутри неё должны быть папки bin и lists\n"
    L"и файлы general*.bat.",
    L"Автоматический поиск не нашёл папку zapret.\n\n"
    L"Нажмите «ВЫБРАТЬ ПАПКУ ZAPRET» и укажите папку,\n"
    L"в которую распакован zapret (внутри — bin\\, lists\\,\n"
    L"файлы general*.bat).",

    /* log messages */
    L"Zapret GUI %s запущен (exe: %s)",
    L"Рядом с ZapretGUI.exe нет bin\\winws.exe — папка zapret не найдена",
    L"Автопоиск проверил: %s",
    L"Используется сохранённая папка zapret: %s",
    L"Папка zapret найдена автоматически: %s",
    L"Папка zapret: %s",
    L"Версия zapret: %s · найдено стратегий: %d",
    L"Текущая стратегия: %s",
    L"В выбранной папке нет bin\\winws.exe: %s",
    L"Повторный автопоиск папки zapret…",
    L"Папка zapret не найдена. Проверены: %s",
    L"Останавливаю обход…",
    L"Служба zapret остановлена, обход выключен",
    L"Обход выключен (winws.exe остановлен)",
    L"Не выбрана стратегия обхода",
    L"Запускаю стратегию: %s",
    L"Обход запущен (winws.exe работает)",
    L"winws.exe не запустился — проверьте стратегию",
    L"Открываю диагностику (консоль)…",
    L"Запускаю тесты стратегий (PowerShell)…",
    L"Не выбрана стратегия для службы",
    L"Устанавливаю службу zapret (%s)…",
    L"Удаляю службы zapret / WinDivert…",
    L"Служба zapret установлена и запущена (%s)",
    L"Ошибка установки службы: %s",
    L"Службы удалены (zapret, WinDivert)",
    L"Ошибка удаления службы: %s",
    L"Автопроверка обновлений включена",
    L"Автопроверка обновлений выключена",
    L"Не удалось изменить настройку автообновлений",
    L"Стратегия выбрана: %s",
    L"Для применения перезапустите обход",
    L"Игровой фильтр: %s",
    L"Не удалось изменить игровой фильтр",
    L"Перезапустите обход, чтобы применить изменения",
    L"Фильтр IPSet: %s",
    L"Не удалось переключить IPSet: нет резервной копии списка. Нажмите «Обновить IPSet»",
    L"GUI закрыт — обход продолжает работать",
    L"Свернуто в трей — программа продолжает работать",
    L"Язык интерфейса: Русский",
    L"Interface language: English",
    L"Тема: %s",

    /* core errors */
    L"bin\\winws.exe не найден — укажите папку zapret (кнопка под статусом)",
    L"Служба zapret уже запущена. Выключите её (тумблер «Автозапуск» или кнопка остановки), затем запускайте обход вручную",
    L"winws.exe уже запущен — сначала остановите текущий обход",
    L"Не удалось разобрать файл стратегии %s",
    L"Не удалось запустить winws.exe (код %lu)",
    L"bin\\winws.exe не найден (папка: %s)",
    L"Нет доступа к диспетчеру служб (код %lu)",
    L"Служба zapret уже существует — сначала удалите её",
    L"Не удалось создать службу (код %lu)",
    L"Служба создана, но не запустилась (код %lu)",
    L"(поиск не выполнялся)",

    /* network messages */
    L"Проверка обновлений zapret…",
    L"Не удалось получить версию из репозитория (проверка пропущена)",
    L"Установлена последняя версия: %s",
    L"Доступна новая версия: %s (установлена %s). Открываю страницу релиза…",
    L"Обновление списка IPSet…",
    L"Не удалось скачать список IPSet (код %lu)",
    L"Не удалось записать lists\\ipset-all.txt",
    L"Список IPSet обновлён (%d строк, %lu байт)",
    L"Проверка файла hosts…",
    L"Не удалось скачать hosts из репозитория (код %lu)",
    L"Не удалось создать временный файл",
    L"Не удалось прочитать системный hosts",
    L"Первая строка из репозитория не найдена в hosts",
    L"Последняя строка из репозитория не найдена в hosts",
    L"Hosts требует обновления: скопируйте строки из открывшегося файла",
    L"в %ls (файл hosts — открыть от имени администратора)",
    L"Hosts актуален",
};

/* ------------------------------------------------------------------ */
/* English                                                             */
/* ------------------------------------------------------------------ */
const wchar_t* const TBL_EN[ZG_STR_COUNT] = {
    /* window / general */
    L"Zapret GUI — Discord & YouTube",
    L"Bypass for blocked Discord and YouTube",
    L"Check for updates",
    L"Clear",
    L"SETTINGS",
    L"EVENT LOG",
    L"Diagnostics",
    L"Update hosts",
    L"Update IPSet",
    L"Tests",
    L"ENABLE BYPASS",
    L"DISABLE BYPASS",
    L"SELECT ZAPRET FOLDER",

    /* hero status */
    L"BYPASS ENABLED",
    L"BYPASS DISABLED",
    L"ZAPRET FILES NOT FOUND",
    L"select the zapret folder — press \"Select zapret folder\" below",
    L"running as a service — strategy: %s",
    L"launched from Zapret GUI — strategy: %s",
    L"winws.exe is running manually (outside Zapret GUI)",
    L"the zapret service is installed but not running",
    L"press the button below to enable the bypass",

    /* cards */
    L"WINWS PROCESS",
    L"running",
    L"not running",
    L"ZAPRET SERVICE",
    L"running",
    L"installed",
    L"not installed",
    L"WINDIVERT DRIVER",
    L"active",
    L"inactive",

    /* settings rows */
    L"Bypass strategy",
    L"Game filter",
    L"Start with Windows (service)",
    L"Check for zapret updates",
    L"IP list filter (IPSet)",
    L"Language",
    L"Theme",

    /* theme names */
    L"Dark",
    L"Light",
    L"Midnight",

    /* tray */
    L"Zapret GUI — bypass enabled",
    L"Zapret GUI — bypass disabled",
    L"Zapret GUI — zapret files not found",
    L"Zapret GUI",
    L"The app is minimized to the tray and keeps running. Right-click the tray icon to exit completely.",
    L"Open Zapret GUI",
    L"Exit completely",

    /* missing-folder dialog */
    L"Zapret GUI — zapret files not found",
    L"Zapret files not found",
    L"ZapretGUI.exe was launched from:\n%s\n\n"
    L"There is no bin\\winws.exe there.\n\n"
    L"The zapret folder is the unpacked zapret-discord-youtube archive:\n"
    L"it contains bin\\, lists\\ and general*.bat files.\n"
    L"You don't have to copy ZapretGUI.exe into it —\n"
    L"you can simply select that folder.",
    L"Select the zapret folder…",
    L"Find the folder automatically",
    L"Continue without zapret",
    L"Zapret files not found: no bin\\winws.exe next to ZapretGUI.exe.\n\n"
    L"ZapretGUI.exe was launched from:\n%s\n\n"
    L"\"Yes\" — select the zapret folder manually\n"
    L"\"No\"  — repeat the automatic search\n"
    L"\"Cancel\" — continue without zapret",
    L"Select the zapret folder — the one containing bin\\winws.exe and general*.bat",
    L"The selected folder has no bin\\winws.exe.\n\n"
    L"Select the folder where zapret was unpacked —\n"
    L"it must contain the bin and lists subfolders\n"
    L"and general*.bat files.",
    L"The automatic search could not find the zapret folder.\n\n"
    L"Press \"SELECT ZAPRET FOLDER\" and pick the folder\n"
    L"where zapret was unpacked (it contains bin\\, lists\\,\n"
    L"general*.bat files).",

    /* log messages */
    L"Zapret GUI %s started (exe: %s)",
    L"No bin\\winws.exe next to ZapretGUI.exe — zapret folder not found",
    L"Autosearch checked: %s",
    L"Using the saved zapret folder: %s",
    L"Zapret folder found automatically: %s",
    L"zapret folder: %s",
    L"zapret version: %s · strategies found: %d",
    L"Current strategy: %s",
    L"The selected folder has no bin\\winws.exe: %s",
    L"Re-running the zapret folder autosearch…",
    L"Zapret folder not found. Checked: %s",
    L"Stopping the bypass…",
    L"The zapret service was stopped, bypass disabled",
    L"Bypass disabled (winws.exe stopped)",
    L"No bypass strategy selected",
    L"Launching strategy: %s",
    L"Bypass started (winws.exe is running)",
    L"winws.exe did not start — check the strategy",
    L"Opening diagnostics (console)…",
    L"Running strategy tests (PowerShell)…",
    L"No strategy selected for the service",
    L"Installing the zapret service (%s)…",
    L"Removing zapret / WinDivert services…",
    L"The zapret service was installed and started (%s)",
    L"Service installation error: %s",
    L"Services removed (zapret, WinDivert)",
    L"Service removal error: %s",
    L"Update auto-check enabled",
    L"Update auto-check disabled",
    L"Failed to change the update check setting",
    L"Strategy selected: %s",
    L"Restart the bypass to apply",
    L"Game filter: %s",
    L"Failed to change the game filter",
    L"Restart the bypass to apply the changes",
    L"IPSet filter: %s",
    L"Failed to switch IPSet: no list backup. Press \"Update IPSet\"",
    L"GUI closed — the bypass keeps running",
    L"Minimized to tray — the app keeps running",
    L"Interface language: Russian",
    L"Interface language: English",
    L"Theme: %s",

    /* core errors */
    L"bin\\winws.exe not found — select the zapret folder (button under the status)",
    L"The zapret service is already running. Turn it off (the \"Autostart\" toggle or the stop button) before launching the bypass manually",
    L"winws.exe is already running — stop the current bypass first",
    L"Failed to parse strategy file %s",
    L"Failed to start winws.exe (code %lu)",
    L"bin\\winws.exe not found (folder: %s)",
    L"No access to the service manager (code %lu)",
    L"The zapret service already exists — remove it first",
    L"Failed to create the service (code %lu)",
    L"The service was created but failed to start (code %lu)",
    L"(no search performed)",

    /* network messages */
    L"Checking for zapret updates…",
    L"Could not fetch the version from the repository (check skipped)",
    L"You are on the latest version: %s",
    L"New version available: %s (installed: %s). Opening the release page…",
    L"Updating the IPSet list…",
    L"Failed to download the IPSet list (code %lu)",
    L"Failed to write lists\\ipset-all.txt",
    L"IPSet list updated (%d lines, %lu bytes)",
    L"Checking the hosts file…",
    L"Failed to download hosts from the repository (code %lu)",
    L"Failed to create a temporary file",
    L"Failed to read the system hosts file",
    L"The first repository line is missing from hosts",
    L"The last repository line is missing from hosts",
    L"Hosts needs an update: copy the lines from the file that just opened",
    L"into %ls (the hosts file — open it as administrator)",
    L"Hosts is up to date",
};

/* compile-time integrity checks */
typedef char zg_tbl_ru_size_check[(sizeof(TBL_RU) / sizeof(TBL_RU[0]) == ZG_STR_COUNT) ? 1 : -1];
typedef char zg_tbl_en_size_check[(sizeof(TBL_EN) / sizeof(TBL_EN[0]) == ZG_STR_COUNT) ? 1 : -1];

/* ------------------------------------------------------------------ */
/* per-language label sets                                              */
/* ------------------------------------------------------------------ */
static const wchar_t* const GAME_RU[4] = {
    L"Отключён", L"Включён (UDP)", L"Включён (TCP)", L"Включён (TCP и UDP)"
};
static const wchar_t* const GAME_EN[4] = {
    L"Off", L"Enabled (UDP)", L"Enabled (TCP)", L"Enabled (TCP & UDP)"
};
static const wchar_t* const IPSET_RU[3] = {
    L"Загружен (список)", L"Отключён (none)", L"Любой IP (any)"
};
static const wchar_t* const IPSET_EN[3] = {
    L"Loaded (list)", L"Disabled (none)", L"Any IP (any)"
};
static const wchar_t* const LANG_NAMES[2] = { L"Русский", L"English" };

const wchar_t* const* zg_game_labels(void)   { return (g_lang == ZG_LANG_EN) ? GAME_EN : GAME_RU; }
const wchar_t* const* zg_ipset_labels(void)  { return (g_lang == ZG_LANG_EN) ? IPSET_EN : IPSET_RU; }
const wchar_t* const* zg_lang_names(void)    { return LANG_NAMES; }
int zg_lang_name_count(void)                 { return 2; }
