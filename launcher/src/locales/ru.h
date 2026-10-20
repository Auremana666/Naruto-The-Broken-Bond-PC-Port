#pragma once

namespace localization_strings {

inline constexpr const char* kRu[] = {
    "Naruto: The Broken Bond",  // WindowTitle
    "ИГРАТЬ",  // TabPlay
    "НАСТРОЙКИ",  // TabSettings
    "ФАЙЛЫ ИГРЫ",  // TabGameFiles
    "Качество",  // GroupQuality
    "Управление",  // GroupControls
    "Соотношение сторон",  // AspectLabel
    "16:9 - Стандартное (оригинал)",  // Aspect16x9
    "21:9 - UltraWide",  // Aspect21x9
    "32:9 - Super UltraWide",  // Aspect32x9
    "Изменяет горизонтальный угол обзора 3D-камеры без растяжения и искажения персонажей.",  // AspectDescription
    "Разрешение",  // ResolutionLabel
    "Рендерер",  // RendererLabel
    "Фильтрация текстур",  // TextureFilteringLabel
    "По умолчанию",  // AnisoDefault
    "FXAA",  // FxaaLabel
    "Отключено",  // FxaaOff
    "Обычный",  // FxaaNormal
    "Экстремальный",  // FxaaExtreme
    "Постобработка",  // PostProcessingLabel
    "Билинейная (по умолчанию)",  // PostBilinear
    "CAS (резкость)",  // PostCas
    "FSR EASU (масштабирование)",  // PostFsr
    "Дизеринг",  // DitherLabel
    "Счётчик FPS",  // FpsOverlayLabel
    "Полный экран",  // FullscreenLabel
    "VSync",  // VsyncLabel
    "Оставьте включённым для правильной скорости анимации и физики игры.",  // VsyncDescription
    "Использовать SDL для геймпадов (Xbox / PlayStation)",  // SdlInputLabel
    "Геймпад",  // ControllerLabel
    "Геймпад не найден (клавиатура и мышь)",  // NoControllerDetected
    "Пропускать вступительные ролики (логотипы Ubisoft)",  // SkipIntroLabel
    "ИГРАТЬ",  // BtnPlay
    "Рекомендуемые",  // BtnResetRecommended
    "Открыть папку игры",  // BtnOpenFolder
    "Настройки применяются при запуске игры.",  // SettingsAppliedNotice
    "Игра готова",  // StatusReady
    "Файлы игры не найдены",  // StatusNotFound
    "Выберите распакованную папку Broken Bond на вкладке ФАЙЛЫ ИГРЫ.",  // FilesMissingDesc
    "(Папка не найдена)",  // NoneDetected
    "Выбрать папку извлечённой игры...",  // BtnSelectFolder
    "Восстановить путь",  // BtnResetDefault
    "Путь по умолчанию восстановлен.",  // PathResetMsg
    "Настройки сохраняются при запуске игры или закрытии лаунчера.",  // FooterNotice
    "Игра запущена!",  // LaunchSuccessMsg
    "Не удалось запустить процесс: ",  // LaunchFailedMsg
    "Исполняемый файл игры не найден в: ",  // ExeNotFoundMsg
    "Путь обновлён: ",  // PathUpdatedMsg
    "Ваше путешествие начинается здесь.",  // PlayHeading
    "Вернитесь в Деревню Скрытого Листа.",  // PlayIntro
    "Файлы игры найдены. Можно играть.",  // ReadyDetail
    "Управление файлами игры",  // ManageFiles
    "Укажите оригинальные файлы для версии на ПК.",  // FilesIntro
    "Папка игры",  // FolderHeading
    "Выберите папку с default.xex. Оригинальные файлы не перемещаются и не удаляются.",  // FolderHint
    "Нужно распаковать игру?",  // SetupHeading
    "Распакуйте образ собственного диска Xbox 360 и выберите папку с default.xex.",  // SetupHint
    "Рекомендуемые настройки применены.",  // RecommendedApplied
    "Поместите broken_bond.exe рядом с программой запуска.",  // ExeMissing
    "Версия для ПК не найдена",  // ExeMissingStatus
    "Файлы игры найдены",  // FilesDetected
    "Игра",  // GroupGame
    "Язык игры",  // GameLanguageLabel
    "Английский",  // GameLanguageEnglish
    "Французский",  // GameLanguageFrench
    "Немецкий",  // GameLanguageGerman
    "Испанский",  // GameLanguageSpanish
    "Итальянский",  // GameLanguageItalian
    "Другой (%u)",  // GameLanguageCustom
    "Применяется при запуске игры. Доступные тексты и озвучка зависят от файлов игры.",  // GameLanguageDescription
    "Язык интерфейса лаунчера",  // LauncherLanguageTooltip
    "Xenos (эмуляция GPU)",  // RendererXenos
    "Нативный (Direct3D 12)",  // RendererNative
    "Выберите нативный графический API. Доступны только API, включённые в эту сборку. Выбор применяется при следующем запуске игры.",  // RendererDescription
    "Недоступно с нативным рендерером. Чтобы включить, выберите Xenos в пункте «Рендерер».",  // XenosOnlyReason
    "MSAA",  // MsaaLabel
    "4x (оригинал)",  // MsaaOriginal
    "Мультисэмплинг 3D-сцен игры. Для 8x нужна более мощная видеокарта.",  // MsaaDescription
    "Сглаживание прозрачности",  // TransparencyAaLabel
    "Сглаживает края листвы, заборов и других вырезанных текстур (alpha to coverage).",  // TransparencyAaDescription
    "Частота кадров",  // FrameRateLabel
    "30 — исходная частота игры. Выше логика игры сохраняет свой темп, а промежуточные кадры интерполируются. С VSync частота не выше частоты монитора; высокие значения требуют быстрого процессора.",  // FrameRateDescription
    "Яркость",  // BrightnessLabel
    "50 — исходная яркость игры.",  // BrightnessDescription
    "Задержка кадров",  // FrameLatencyLabel
    "1 (минимальная)",  // FrameLatencyLowest
    "Кадры в очереди перед выводом на экран. 1 даёт наименьшую задержку ввода; 2 или 3 могут сгладить неровное время кадра.",  // FrameLatencyDescription
    "Тройная буферизация",  // TripleBufferingLabel
    "Три буфера вывода вместо двух.",  // TripleBufferingDescription
    "Синхронизирует вывод с экраном. С нативным рендерером скорость игры не зависит от VSync.",  // VsyncNativeDescription
    "Видеокарта",  // GraphicsDeviceLabel
    "Автоматически",  // GraphicsDeviceAuto
    "Видеокарта, на которой работает игра. «Автоматически» оставляет выбор рендереру. На ноутбуках с двумя видеокартами выберите дискретную (NVIDIA или AMD).",  // GraphicsDeviceDescription
    "Экран",  // GroupDisplay
    "Дополнительно",  // GroupAdvanced
    "только Xenos",  // XenosOnlyTag
    "только нативный",  // NativeOnlyTag
    "Только с нативным рендерером. Чтобы включить, выберите «Нативный» в пункте «Рендерер».",  // NativeOnlyReason
    "Разрешение, в котором рисуется игра, кратное исходным 1280×720. Чем выше значение, тем чётче изображение и тем мощнее нужна видеокарта.",  // ResolutionDescription
    "Анизотропная фильтрация сохраняет чёткость текстур на поверхностях под углом, например на полу и дорогах.",  // TextureFilteringDescription
    "Сглаживает зубчатые края, фильтруя итоговое изображение. «Экстремальный» сглаживает сильнее и может размыть мелкие детали.",  // FxaaDescription
    "Как изображение подгоняется под экран. CAS повышает резкость, FSR масштабирует с повышением резкости, а билинейная фильтрация оставляет картинку мягкой.",  // PostProcessingDescription
    "Добавляет мелкий шум, скрывающий цветовые полосы в градиентах.",  // DitherDescription
    "Показывает частоту и время кадра в углу экрана. F1 включает и выключает его в игре.",  // FpsOverlayDescription
    "Запускает игру в полноэкранном режиме. Если выключено, игра работает в окне.",  // FullscreenDescription
    "Пропускает логотипы при запуске игры. Вступительный ролик сохраняется.",  // SkipIntroDescription
    "Читает геймпады через SDL, который распознаёт геймпады Xbox, PlayStation и другие. Если выключено, игра использует XInput, который распознаёт только Xbox-совместимые геймпады.",  // SdlInputDescription
    "Подключённый сейчас геймпад, под именем, которое ему даёт SDL.",  // ControllerDescription
    "Клавиатура и мышь",  // KeyboardMouseLabel
    "Клавиатура и мышь заменяют геймпад Xbox и работают вместе с подключённым геймпадом. Если мышь управляет камерой, курсор остаётся в окне игры, пока оно активно; Alt+Tab освобождает его. Игра по-прежнему показывает кнопки геймпада.",  // KeyboardMouseDescription
    "Мышь управляет камерой",  // MouseCameraLabel
    "Камера (правый стик) двигается мышью. Если выключено, камера двигается только клавишами правого стика.",  // MouseCameraDescription
    "Чувствительность",  // MouseSensitivityLabel
    "Как быстро камера следует за мышью. 100% — значение по умолчанию.",  // MouseSensitivityDescription
    "Клавиши",  // KeysLabel
    "Клавиши и кнопки мыши для каждой кнопки геймпада, по две на кнопку.",  // KeysDescription
    "Назначить клавиши…",  // MapKeysButton
    "Назначение клавиш",  // MapKeysTitle
    "Управление › Клавиатура и мышь",  // MapKeysPath
    "Кнопки",  // KeysButtonsHeading
    "Левый стик",  // LeftStickName
    "Правый стик",  // RightStickName
    "Крестовина",  // DpadName
    "+ мышь",  // KeysPlusMouse
    "Вверх",  // DirectionUp
    "Вниз",  // DirectionDown
    "Влево",  // DirectionLeft
    "Вправо",  // DirectionRight
    "Нажмите на поле, затем новую клавишу (5 с). Правый щелчок очищает. Занятая клавиша снимается с прежней кнопки.",  // KeysHint
    "Нажмите… %d",  // KeyCapturePrompt
    "Вернуть клавиши по умолчанию",  // KeysRestoreDefaults
    "Готово",  // KeysDone
    "%s снята с кнопки %s.",  // KeyMovedStatus
    "%s используется игрой и не может быть назначена.",  // KeyReservedStatus
    "Клавиши по умолчанию восстановлены.",  // KeysRestoredStatus
    "Пробел",  // KeySpaceName
    "ЛКМ",  // MouseLeftName
    "ПКМ",  // MouseRightName
    "СКМ",  // MouseMiddleName
    "Не удалось сохранить. Проверьте файл настроек и права на запись в папку.",  // SaveFailed
    "Недоступно",  // UnavailableTag
    "Для ultrawide ещё требуется проверить проекцию камеры Broken Bond и вывод изображения. Игра использует 16:9.",  // AspectUnavailableReason
};

}  // namespace localization_strings
