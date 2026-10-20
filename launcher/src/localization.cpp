#include "localization.h"

#include <array>

#include "locales/de.h"
#include "locales/es.h"
#include "locales/fr.h"
#include "locales/it.h"
#include "locales/ru.h"

namespace {

Localization::Language g_current_language = Localization::Language::English;

const std::array<Localization::LanguageOption, 7> kLanguages = {{
    {Localization::Language::English, "en", "English"},
    {Localization::Language::Portuguese, "pt_BR", "Português"},
    {Localization::Language::French, "fr", "Français"},
    {Localization::Language::German, "de", "Deutsch"},
    {Localization::Language::Spanish, "es", "Español"},
    {Localization::Language::Italian, "it", "Italiano"},
    {Localization::Language::Russian, "ru", "Русский"},
}};

// English string table (default)
const char* const kStringsEn[] = {
    "Naruto: The Broken Bond",  // WindowTitle
    "PLAY",  // TabPlay
    "SETTINGS",  // TabSettings
    "GAME FILES",  // TabGameFiles
    "Quality",  // GroupQuality
    "Controls",  // GroupControls
    "Aspect Ratio",  // AspectLabel
    "16:9 - Standard (original)",  // Aspect16x9
    "21:9 - UltraWide",  // Aspect21x9
    "32:9 - Super UltraWide",  // Aspect32x9
    "Dynamically adjusts 3D camera FOV horizontally without character stretching or distortion.",  // AspectDescription
    "Resolution",  // ResolutionLabel
    "Renderer",  // RendererLabel
    "Texture Filtering",  // TextureFilteringLabel
    "Default",  // AnisoDefault
    "FXAA",  // FxaaLabel
    "Off",  // FxaaOff
    "Normal",  // FxaaNormal
    "Extreme",  // FxaaExtreme
    "Post-Processing",  // PostProcessingLabel
    "Bilinear (default)",  // PostBilinear
    "CAS (sharpen)",  // PostCas
    "FSR EASU (upscale)",  // PostFsr
    "Dithering",  // DitherLabel
    "FPS Overlay",  // FpsOverlayLabel
    "Fullscreen",  // FullscreenLabel
    "VSync",  // VsyncLabel
    "Keep enabled to maintain accurate game animation speeds and physics.",  // VsyncDescription
    "Use SDL controller backend (Xbox / PlayStation controllers)",  // SdlInputLabel
    "Controller",  // ControllerLabel
    "No controller detected (Keyboard & Mouse)",  // NoControllerDetected
    "Skip intro videos (Ubisoft logos)",  // SkipIntroLabel
    "PLAY GAME",  // BtnPlay
    "Reset to Recommended",  // BtnResetRecommended
    "Open Game Folder",  // BtnOpenFolder
    "Settings are applied when you click PLAY.",  // SettingsAppliedNotice
    "Game ready",  // StatusReady
    "Game files not found",  // StatusNotFound
    "Select your extracted Broken Bond game folder in GAME FILES.",  // FilesMissingDesc
    "(None found)",  // NoneDetected
    "Select Extracted Game Folder...",  // BtnSelectFolder
    "Reset to Default Path",  // BtnResetDefault
    "Path reset to default.",  // PathResetMsg
    "Settings are saved when you play or close the launcher.",  // FooterNotice
    "Game launched successfully!",  // LaunchSuccessMsg
    "Failed to start process: ",  // LaunchFailedMsg
    "Game executable not found at: ",  // ExeNotFoundMsg
    "Path updated: ",  // PathUpdatedMsg
    "Your journey starts here.",  // PlayHeading
    "Return to the Hidden Leaf Village.",  // PlayIntro
    "Game files detected. You're ready to play.",  // ReadyDetail
    "Manage game files",  // ManageFiles
    "Connect your original game files to the PC port.",  // FilesIntro
    "Game folder",  // FolderHeading
    "Select the folder containing default.xex. Your original files are never moved or deleted.",  // FolderHint
    "Need to extract your game?",  // SetupHeading
    "Extract your original Xbox 360 disc image first, then select the folder containing default.xex.",  // SetupHint
    "Recommended settings applied.",  // RecommendedApplied
    "Place broken_bond.exe next to this launcher.",  // ExeMissing
    "PC port not found",  // ExeMissingStatus
    "Game files detected",  // FilesDetected
    // --- Game language ---
    "Game",  // GroupGame
    "Game language",  // GameLanguageLabel
    "English",  // GameLanguageEnglish
    "French",  // GameLanguageFrench
    "German",  // GameLanguageGerman
    "Spanish",  // GameLanguageSpanish
    "Italian",  // GameLanguageItalian
    "Custom (%u)",  // GameLanguageCustom
    "Applied on the next launch. Available text and voices depend on your game files.",  // GameLanguageDescription
    "Launcher interface language",  // LauncherLanguageTooltip
    // --- Renderer ---
    "Xenos (emulated GPU)",  // RendererXenos
    "Native (Direct3D 12)",  // RendererNative
    "Xenos emulates the Xbox 360 GPU and is the default. Native Direct3D 12 is available in builds that include it; its options apply only when Native is selected.",  // RendererDescription
    "Not available with the native renderer. Choose Xenos under Renderer to use it.",  // XenosOnlyReason
    "MSAA",  // MsaaLabel
    "4x (original)",  // MsaaOriginal
    "Multisampling of the game's 3D scenes. 8x needs a stronger GPU.",  // MsaaDescription
    "Transparency AA",  // TransparencyAaLabel
    "Smooths the edges of foliage, fences and other cut-out textures (alpha to coverage).",  // TransparencyAaDescription
    "Frame rate",  // FrameRateLabel
    "30 is the game's original. Above it, the game's logic keeps its pace and the frames in between are interpolated. With VSync on, it stops at the monitor's rate; high rates need a faster CPU.",  // FrameRateDescription
    "Brightness",  // BrightnessLabel
    "50 is the game's original brightness.",  // BrightnessDescription
    "Frame latency",  // FrameLatencyLabel
    "1 (lowest)",  // FrameLatencyLowest
    "Frames queued ahead of the display. 1 has the lowest input lag; 2 or 3 can smooth uneven frame times.",  // FrameLatencyDescription
    "Triple buffering",  // TripleBufferingLabel
    "Three display buffers instead of two.",  // TripleBufferingDescription
    "Syncs presentation with the display. With the native renderer, the game speed does not depend on VSync.",  // VsyncNativeDescription
    "Graphics card",  // GraphicsDeviceLabel
    "Automatic",  // GraphicsDeviceAuto
    "The graphics card the game runs on. Automatic leaves the choice to the renderer. On laptops with two graphics cards, pick the dedicated one (NVIDIA or AMD).",  // GraphicsDeviceDescription
    "Display",  // GroupDisplay
    "Advanced",  // GroupAdvanced
    "Xenos only",  // XenosOnlyTag
    "Native only",  // NativeOnlyTag
    "Only with the native renderer. Choose Native under Renderer to use it.",  // NativeOnlyReason
    "The resolution the game draws at, in multiples of the original 1280×720. Higher values look sharper and need a stronger graphics card.",  // ResolutionDescription
    "Anisotropic filtering keeps textures sharp on surfaces seen at an angle, such as floors and roads.",  // TextureFilteringDescription
    "Smooths jagged edges by filtering the final image. Extreme smooths more and can blur fine detail.",  // FxaaDescription
    "How the image is fitted to the screen. CAS sharpens it, FSR upscales it with sharpening, and Bilinear keeps it soft.",  // PostProcessingDescription
    "Adds fine noise that hides color banding in gradients.",  // DitherDescription
    "Shows the frame rate and frame time in a corner of the screen. F1 toggles it in game.",  // FpsOverlayDescription
    "Runs the game in fullscreen. Off, it runs in a window.",  // FullscreenDescription
    "Skips the logos shown when the game starts. The opening movie still plays.",  // SkipIntroDescription
    "Reads controllers through SDL, which recognizes Xbox, PlayStation and other controllers. Off, the game uses XInput, which only recognizes Xbox-compatible controllers.",  // SdlInputDescription
    "The controller connected now, as SDL names it.",  // ControllerDescription
    "Use keyboard and mouse",  // KeyboardMouseLabel
    "The keyboard and mouse stand in for an Xbox controller and work alongside a connected controller. With the mouse on the camera, the cursor stays inside the game window while it has focus; Alt+Tab releases it. The game still shows the controller's buttons.",  // KeyboardMouseDescription
    "Mouse moves the camera",  // MouseCameraLabel
    "Moves the camera (the right stick) with the mouse. Off, the camera moves only with the keys of the right stick.",  // MouseCameraDescription
    "Sensitivity",  // MouseSensitivityLabel
    "How fast the camera follows the mouse. 100% is the default.",  // MouseSensitivityDescription
    "Keys",  // KeysLabel
    "The keys and mouse buttons for each controller input, two per input.",  // KeysDescription
    "Map keys…",  // MapKeysButton
    "Map keys",  // MapKeysTitle
    "Controls › Keyboard and mouse",  // MapKeysPath
    "Buttons",  // KeysButtonsHeading
    "Left stick",  // LeftStickName
    "Right stick",  // RightStickName
    "D-pad",  // DpadName
    "+ mouse",  // KeysPlusMouse
    "Up",  // DirectionUp
    "Down",  // DirectionDown
    "Left",  // DirectionLeft
    "Right",  // DirectionRight
    "Click and press the new key (5 s). Right-click clears. A key already in use leaves its previous input.",  // KeysHint
    "Press… %d",  // KeyCapturePrompt
    "Restore default keys",  // KeysRestoreDefaults
    "Done",  // KeysDone
    "%s left %s.",  // KeyMovedStatus
    "%s is used by the game and cannot be mapped.",  // KeyReservedStatus
    "Default keys restored.",  // KeysRestoredStatus
    "Space",  // KeySpaceName
    "Left mouse",  // MouseLeftName
    "Right mouse",  // MouseRightName
    "Middle mouse",  // MouseMiddleName
    "Could not save settings. Check the configuration file and folder write permissions.",  // SaveFailed
    "Unavailable",  // UnavailableTag
    "Ultrawide is not available yet: Broken Bond camera projection and Xenos presentation still need validation. The game uses 16:9.",  // AspectUnavailableReason
};

// Portuguese string table (pt-BR)
const char* const kStringsPtBr[] = {
    "Naruto: The Broken Bond",  // WindowTitle
    "JOGAR",  // TabPlay
    "CONFIGURAÇÕES",  // TabSettings
    "ARQUIVOS",  // TabGameFiles
    "Qualidade",  // GroupQuality
    "Controles",  // GroupControls
    "Proporção de tela",  // AspectLabel
    "16:9 - Padrão (original)",  // Aspect16x9
    "21:9 - UltraWide",  // Aspect21x9
    "32:9 - Super UltraWide",  // Aspect32x9
    "Ajusta dinamicamente o FOV da camera 3D horizontalmente sem esticar os personagens.",  // AspectDescription
    "Resolução",  // ResolutionLabel
    "Renderizador",  // RendererLabel
    "Filtro de texturas",  // TextureFilteringLabel
    "Padrão",  // AnisoDefault
    "FXAA",  // FxaaLabel
    "Desligado",  // FxaaOff
    "Normal",  // FxaaNormal
    "Extremo",  // FxaaExtreme
    "Pós-processamento",  // PostProcessingLabel
    "Bilinear (padrão)",  // PostBilinear
    "CAS (nitidez)",  // PostCas
    "FSR EASU (upscaling)",  // PostFsr
    "Dithering",  // DitherLabel
    "Overlay de FPS",  // FpsOverlayLabel
    "Tela cheia",  // FullscreenLabel
    "VSync",  // VsyncLabel
    "Mantenha ativado para garantir a velocidade correta das animações e da física do jogo.",  // VsyncDescription
    "Usar controles SDL (Xbox / PlayStation)",  // SdlInputLabel
    "Controle",  // ControllerLabel
    "Nenhum controle conectado",  // NoControllerDetected
    "Pular vídeos de abertura",  // SkipIntroLabel
    "JOGAR AGORA",  // BtnPlay
    "Usar recomendadas",  // BtnResetRecommended
    "Abrir pasta do jogo",  // BtnOpenFolder
    "As alterações serão aplicadas ao iniciar o jogo.",  // SettingsAppliedNotice
    "Jogo pronto",  // StatusReady
    "Arquivos do jogo não encontrados",  // StatusNotFound
    "Selecione a pasta extraída de Broken Bond em ARQUIVOS.",  // FilesMissingDesc
    "(Nenhuma encontrada)",  // NoneDetected
    "Selecionar pasta do jogo...",  // BtnSelectFolder
    "Usar pasta padrão",  // BtnResetDefault
    "Caminho restaurado para o padrão.",  // PathResetMsg
    "Configurações salvas ao iniciar ou fechar o launcher.",  // FooterNotice
    "Jogo iniciado com sucesso!",  // LaunchSuccessMsg
    "Falha ao iniciar processo: ",  // LaunchFailedMsg
    "Executável do jogo não encontrado em: ",  // ExeNotFoundMsg
    "Caminho atualizado: ",  // PathUpdatedMsg
    "Sua jornada começa aqui.",  // PlayHeading
    "Volte para a Vila Oculta da Folha.",  // PlayIntro
    "Arquivos do jogo detectados. Tudo pronto para jogar.",  // ReadyDetail
    "Gerenciar arquivos do jogo",  // ManageFiles
    "Conecte os arquivos do seu jogo original ao port de PC.",  // FilesIntro
    "Pasta do jogo",  // FolderHeading
    "Selecione a pasta que contém default.xex. Seus arquivos originais nunca são movidos ou excluídos.",  // FolderHint
    "Precisa extrair seu jogo?",  // SetupHeading
    "Extraia a imagem do seu disco original de Xbox 360 e selecione a pasta que contém default.xex.",  // SetupHint
    "Configurações recomendadas aplicadas.",  // RecommendedApplied
    "Coloque broken_bond.exe ao lado deste launcher.",  // ExeMissing
    "Port de PC não encontrado",  // ExeMissingStatus
    "Arquivos do jogo detectados",  // FilesDetected
    // --- Game language ---
    "Jogo",  // GroupGame
    "Idioma do jogo",  // GameLanguageLabel
    "Inglês",  // GameLanguageEnglish
    "Francês",  // GameLanguageFrench
    "Alemão",  // GameLanguageGerman
    "Espanhol",  // GameLanguageSpanish
    "Italiano",  // GameLanguageItalian
    "Personalizado (%u)",  // GameLanguageCustom
    "Aplicado ao iniciar o jogo. Os textos e as vozes disponíveis dependem dos arquivos do jogo.",  // GameLanguageDescription
    "Idioma da interface do launcher",  // LauncherLanguageTooltip
    // --- Renderer ---
    "Xenos (GPU emulada)",  // RendererXenos
    "Nativo (Direct3D 12)",  // RendererNative
    "Xenos emula a GPU do Xbox 360 e é o padrão. Direct3D 12 nativo está disponível nas compilações que o incluem; suas opções só se aplicam quando Nativo está selecionado.",  // RendererDescription
    "Indisponível com o renderizador nativo. Escolha Xenos em Renderizador para usar.",  // XenosOnlyReason
    "MSAA",  // MsaaLabel
    "4x (original)",  // MsaaOriginal
    "Multiamostragem das cenas 3D do jogo. 8x exige uma GPU mais forte.",  // MsaaDescription
    "AA de transparência",  // TransparencyAaLabel
    "Suaviza as bordas de folhagens, cercas e outras texturas recortadas (alpha to coverage).",  // TransparencyAaDescription
    "Taxa de quadros",  // FrameRateLabel
    "30 é a taxa original do jogo. Acima disso, a lógica mantém o ritmo original e os quadros intermediários são interpolados. Com o VSync ligado, para na taxa do monitor; taxas altas exigem uma CPU mais rápida.",  // FrameRateDescription
    "Brilho",  // BrightnessLabel
    "50 é o brilho original do jogo.",  // BrightnessDescription
    "Latência de quadros",  // FrameLatencyLabel
    "1 (mínima)",  // FrameLatencyLowest
    "Quadros na fila antes da tela. 1 tem o menor atraso de comando; 2 ou 3 podem suavizar tempos de quadro irregulares.",  // FrameLatencyDescription
    "Buffer triplo",  // TripleBufferingLabel
    "Três buffers de exibição em vez de dois.",  // TripleBufferingDescription
    "Sincroniza a apresentação com a tela. No renderizador nativo, a velocidade do jogo não depende do VSync.",  // VsyncNativeDescription
    "Placa de vídeo",  // GraphicsDeviceLabel
    "Automática",  // GraphicsDeviceAuto
    "A placa de vídeo em que o jogo roda. Automática deixa a escolha para o renderizador. Em notebooks com duas placas, escolha a dedicada (NVIDIA ou AMD).",  // GraphicsDeviceDescription
    "Tela",  // GroupDisplay
    "Avançado",  // GroupAdvanced
    "só no Xenos",  // XenosOnlyTag
    "só no Nativo",  // NativeOnlyTag
    "Só com o renderizador nativo. Escolha Nativo em Renderizador para usar.",  // NativeOnlyReason
    "A resolução em que o jogo desenha, em múltiplos dos 1280×720 originais. Valores maiores deixam a imagem mais nítida e exigem uma placa de vídeo mais forte.",  // ResolutionDescription
    "A filtragem anisotrópica mantém nítidas as texturas vistas de lado, como chão e estradas.",  // TextureFilteringDescription
    "Suaviza bordas serrilhadas filtrando a imagem final. Extremo suaviza mais e pode borrar detalhes finos.",  // FxaaDescription
    "Como a imagem é ajustada à tela. CAS aumenta a nitidez, FSR amplia com nitidez e Bilinear deixa a imagem suave.",  // PostProcessingDescription
    "Adiciona um ruído fino que esconde faixas de cor em degradês.",  // DitherDescription
    "Mostra a taxa e o tempo de quadro num canto da tela. F1 liga e desliga durante o jogo.",  // FpsOverlayDescription
    "Roda o jogo em tela cheia. Desligado, roda numa janela.",  // FullscreenDescription
    "Pula os logos exibidos ao abrir o jogo. O vídeo de abertura continua.",  // SkipIntroDescription
    "Lê o controle pelo SDL, que reconhece controles de Xbox, PlayStation e outros. Desligado, o jogo usa o XInput, que só reconhece controles compatíveis com Xbox.",  // SdlInputDescription
    "O controle conectado agora, com o nome que o SDL dá a ele.",  // ControllerDescription
    "Usar teclado e mouse",  // KeyboardMouseLabel
    "O teclado e o mouse fazem as vezes de um controle de Xbox e funcionam junto com um controle conectado. Com o mouse na câmera, o cursor fica preso na janela do jogo enquanto ela está em foco; Alt+Tab o libera. O jogo continua mostrando os botões do controle.",  // KeyboardMouseDescription
    "Mouse move a câmera",  // MouseCameraLabel
    "Move a câmera (o analógico direito) com o mouse. Desligado, a câmera só se move pelas teclas do analógico direito.",  // MouseCameraDescription
    "Sensibilidade",  // MouseSensitivityLabel
    "A velocidade com que a câmera segue o mouse. 100% é o padrão.",  // MouseSensitivityDescription
    "Teclas",  // KeysLabel
    "As teclas e os botões do mouse de cada comando do controle, dois por comando.",  // KeysDescription
    "Mapear teclas…",  // MapKeysButton
    "Mapear teclas",  // MapKeysTitle
    "Controles › Teclado e mouse",  // MapKeysPath
    "Botões",  // KeysButtonsHeading
    "Analógico esquerdo",  // LeftStickName
    "Analógico direito",  // RightStickName
    "Direcional",  // DpadName
    "+ mouse",  // KeysPlusMouse
    "Cima",  // DirectionUp
    "Baixo",  // DirectionDown
    "Esquerda",  // DirectionLeft
    "Direita",  // DirectionRight
    "Clique e pressione a nova tecla (5 s). Botão direito limpa. Tecla já usada sai do comando anterior.",  // KeysHint
    "Pressione… %d",  // KeyCapturePrompt
    "Restaurar teclas padrão",  // KeysRestoreDefaults
    "Concluir",  // KeysDone
    "%s saiu de %s.",  // KeyMovedStatus
    "%s é usada pelo jogo e não pode ser mapeada.",  // KeyReservedStatus
    "Teclas padrão restauradas.",  // KeysRestoredStatus
    "Espaço",  // KeySpaceName
    "Mouse esq.",  // MouseLeftName
    "Mouse dir.",  // MouseRightName
    "Mouse meio",  // MouseMiddleName
    "Não foi possível salvar. Verifique o arquivo de configuração e a permissão de escrita na pasta.",  // SaveFailed
    "Indisponível",  // UnavailableTag
    "Ultrawide ainda não está disponível: a projeção da câmera de Broken Bond e a apresentação do Xenos precisam ser validadas. O jogo usa 16:9.",  // AspectUnavailableReason
};

static_assert(std::size(kStringsEn) == static_cast<size_t>(TextId::Count));
static_assert(std::size(kStringsPtBr) == static_cast<size_t>(TextId::Count));
static_assert(std::size(localization_strings::kFr) == static_cast<size_t>(TextId::Count));
static_assert(std::size(localization_strings::kDe) == static_cast<size_t>(TextId::Count));
static_assert(std::size(localization_strings::kEs) == static_cast<size_t>(TextId::Count));
static_assert(std::size(localization_strings::kIt) == static_cast<size_t>(TextId::Count));
static_assert(std::size(localization_strings::kRu) == static_cast<size_t>(TextId::Count));
static_assert(std::size(kLanguages) == static_cast<size_t>(Localization::Language::Count));

const char* const* const kStringTables[] = {
    kStringsEn, kStringsPtBr,
    localization_strings::kFr, localization_strings::kDe,
    localization_strings::kEs, localization_strings::kIt,
    localization_strings::kRu,
};
static_assert(std::size(kStringTables) == std::size(kLanguages));

}  // namespace

const std::array<Localization::LanguageOption, 7>& Localization::GetLanguages() {
  return kLanguages;
}

void Localization::SetLanguage(Localization::Language lang) {
  g_current_language = static_cast<size_t>(lang) < kLanguages.size() ? lang : Language::English;
}

Localization::Language Localization::GetLanguage() {
  return g_current_language;
}

void Localization::SetLanguageCode(const std::string& code) {
  const std::string normalized = (code == "pt-BR" || code == "pt") ? "pt_BR" : code;
  for (const auto& option : kLanguages) {
    if (normalized == option.code) {
      SetLanguage(option.language);
      return;
    }
  }
  SetLanguage(Language::English);
}

std::string Localization::GetLanguageCode() {
  return kLanguages[static_cast<size_t>(g_current_language)].code;
}

const char* Localization::Get(TextId id) {
  const size_t index = static_cast<size_t>(id);
  if (index >= static_cast<size_t>(TextId::Count)) {
    return "";
  }

  return kStringTables[static_cast<size_t>(g_current_language)][index];
}
