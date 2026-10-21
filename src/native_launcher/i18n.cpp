#include "i18n.h"
#include <cstring>
#include <map>
#ifdef __vita__
#include <psp2/apputil.h>
#include <psp2/system_param.h>
#endif

namespace hardrpg {
namespace {
int current = English;
const char *const names[languageCount] = {"English", "Español", "Português (Brasil)"};
const char *const codes[languageCount] = {"en", "es", "pt-BR"};

// English, Spanish, Brazilian Portuguese. "{}" stands for a value filled in
// when shown (a name, a number, a path). A text missing here, or with an
// empty translation, shows in English. Keep translations about as long as
// the English: most of them sit in fixed boxes on a 960x544 screen.
struct Text {
  const char *en, *es, *pt;
};
const Text table[] = {
    // Buttons on the hint bar.
    {"Select", "Elegir", "Escolher"},
    {"Back", "Atrás", "Voltar"},
    {"Change", "Cambiar", "Mudar"},
    {"Use default", "Usar predeterminado", "Usar padrão"},
    {"Reset", "Restablecer", "Redefinir"},
    {"Choose", "Elegir", "Escolher"},
    {"Automatic", "Automático", "Automático"},
    {"Check again", "Comprobar otra vez", "Verificar de novo"},
    {"Continue", "Continuar", "Continuar"},
    {"Scroll", "Desplazar", "Rolar"},
    {"Stop testing", "Dejar de probar", "Parar teste"},
    {"Assign", "Asignar", "Atribuir"},
    {"Cancel", "Cancelar", "Cancelar"},
    {"Tap twice to assign", "Toca dos veces para asignar", "Toque duas vezes para atribuir"},
    {"Map zone", "Asignar zona", "Mapear zona"},
    {"Left and right: mode or zone", "Izquierda y derecha: modo o zona", "Esquerda e direita: modo ou zona"},
    {"Save report", "Guardar informe", "Salvar relatório"},
    {"Choose RTP", "Elegir RTP", "Escolher RTP"},
    {"Open", "Abrir", "Abrir"},
    {"Use this folder", "Usar esta carpeta", "Usar esta pasta"},
    {"Add games here", "Añadir juegos de aquí", "Adicionar jogos daqui"},
    {"Close", "Cerrar", "Fechar"},
    {"Answer", "Responder", "Responder"},
    {"Release notes", "Notas de la versión", "Notas da versão"},
    {"Browse", "Explorar", "Procurar"},
    {"Remove", "Quitar", "Remover"},
    {"Type key", "Escribir clave", "Digitar chave"},
    {"Remove key", "Quitar clave", "Remover chave"},
    {"View", "Ver", "Ver"},
    {"Save", "Guardar", "Salvar"},
    {"Keep current", "Mantener las actuales", "Manter as atuais"},
    {"Pick with the D-pad or a tap", "Elige con la cruceta o tocando", "Escolha com o direcional ou tocando"},
    {"Other games", "Otros juegos", "Outros jogos"},
    {"Details", "Detalles", "Detalhes"},
    {"Options", "Opciones", "Opções"},
    {"Play", "Jugar", "Jogar"},
    {"Engine", "Motor", "Motor"},
    {"Search", "Buscar", "Buscar"},
    {"Next", "Siguiente", "Próximo"},
    {"Skip", "Saltar", "Pular"},
    {"Start playing", "Empezar a jugar", "Começar a jogar"},
    {"Language", "Idioma", "Idioma"},

    // Engines and the library.
    {"Unknown engine", "Motor desconocido", "Motor desconhecido"},
    {"All", "Todos", "Todos"},
    {"Collection", "Colección", "Coleção"},
    {"Game", "Juego", "Jogo"},
    {"Add games", "Añadir juegos", "Adicionar jogos"},
    {"Add", "Añadir", "Adicionar"},
    {"Scan a folder on your memory card", "Busca juegos en una carpeta de tu tarjeta", "Procure jogos em uma pasta do seu cartão"},
    {"Not played yet", "Sin jugar", "Ainda não jogado"},
    {"{} played", "Jugado: {}", "Jogado: {}"},
    {"Never", "Nunca", "Nunca"},
    {"Today", "Hoy", "Hoje"},
    {"Yesterday", "Ayer", "Ontem"},
    {"{} days ago", "Hace {} días", "Há {} dias"},
    {"Last week", "La semana pasada", "Semana passada"},
    {"{} weeks ago", "Hace {} semanas", "Há {} semanas"},
    {"{} months ago", "Hace {} meses", "Há {} meses"},
    {"Favourite", "Favorito", "Favorito"},
    {"Published by {}", "Publicado por {}", "Publicado por {}"},
    {"Play time", "Tiempo", "Tempo"},
    {"Last played", "Última vez", "Última vez"},
    {"Source", "Origen", "Origem"},
    {"Folder", "Carpeta", "Pasta"},
    {"details from {}", "detalles de {}", "detalhes de {}"},
    {"Measuring...", "Midiendo...", "Medindo..."},
    {"Starting {}", "Iniciando {}", "Iniciando {}"},
    {"Loading", "Cargando", "Carregando"},
    {"Choose artwork", "Elegir imágenes", "Escolher imagens"},
    {"Cover", "Portada", "Capa"},
    {"Background", "Fondo", "Fundo"},
    {"Logo", "Logo", "Logo"},
    {"Nothing found", "No se encontró nada", "Nada encontrado"},
    {"Current", "Actual", "Atual"},
    {"Title screen", "Pantalla de título", "Tela de título"},
    {"Remove from favourites", "Quitar de favoritos", "Remover dos favoritos"},
    {"Add to favourites", "Añadir a favoritos", "Adicionar aos favoritos"},
    {"Download game info", "Descargar info del juego", "Baixar info do jogo"},
    {"Game info for all games", "Info de todos los juegos", "Info de todos os jogos"},
    {"Game settings", "Ajustes del juego", "Ajustes do jogo"},
    {"Rename", "Cambiar nombre", "Renomear"},
    {"Filter", "Filtro", "Filtro"},
    {"Showing {} of {} games", "Mostrando {} de {} juegos", "Mostrando {} de {} jogos"},
    {"Order by", "Ordenar por", "Ordenar por"},
    {"Name", "Nombre", "Nome"},
    {"Newest first", "Recientes", "Recentes"},
    {"A to Z", "A a Z", "A a Z"},
    {"Most first", "Más primero", "Mais primeiro"},
    {"Oldest first", "Antiguos", "Antigos"},
    {"Z to A", "Z a A", "Z a A"},
    {"Least first", "Menos primero", "Menos primeiro"},
    {"Engines", "Motores", "Motores"},
    {"1 game", "1 juego", "1 jogo"},
    {"{} games", "{} juegos", "{} jogos"},
    {"Only show", "Mostrar solo", "Mostrar só"},
    {"Favourites", "Favoritos", "Favoritos"},
    {"Not played", "Sin jugar", "Não jogados"},
    {"Search games", "Buscar juegos", "Buscar jogos"},
    {"Rename game", "Cambiar nombre del juego", "Renomear jogo"},

    // Settings.
    {"Settings", "Ajustes", "Ajustes"},
    {"Game display", "Pantalla de los juegos", "Tela dos jogos"},
    {"RTP locations", "Ubicaciones del RTP", "Locais do RTP"},
    {"Game folders", "Carpetas de juegos", "Pastas de jogos"},
    {"Game error history", "Historial de errores", "Histórico de erros"},
    {"Performance and speed", "Rendimiento y velocidad", "Desempenho e velocidade"},
    {"Release checks", "Buscar versiones", "Verificar versões"},
    {"Game info and artwork", "Info e imágenes de juegos", "Info e imagens dos jogos"},
    {"Welcome guide", "Guía de bienvenida", "Guia de boas-vindas"},
    {"Exit HardRPG", "Salir de HardRPG", "Sair do HardRPG"},
    {"{} added", "{} añadidas", "{} adicionadas"},
    {"At startup", "Al iniciar", "Ao iniciar"},
    {"{} sources", "{} fuentes", "{} fontes"},
    {"Display", "Pantalla", "Tela"},
    {"Default for every game", "Predeterminado para todos los juegos", "Padrão para todos os jogos"},
    {"Aspect ratio", "Proporción", "Proporção"},
    {"Scaling", "Escala", "Escala"},
    {"Filtering", "Filtro", "Filtro"},
    {"Fill screen", "Llenar pantalla", "Preencher tela"},
    {"Original", "Original", "Original"},
    {"Fit screen", "Ajustar a pantalla", "Ajustar à tela"},
    {"Whole pixels", "Píxeles enteros", "Pixels inteiros"},
    {"Custom", "Personalizado", "Personalizado"},
    {"Saved for this game. <square> Square goes back to the default for every game.",
     "Guardado para este juego. <square> Cuadrado vuelve al ajuste de todos los juegos.",
     "Salvo para este jogo. <square> Quadrado volta ao padrão de todos os jogos."},
    {"Used by games without settings of their own.", "Lo usan los juegos que no tienen ajustes propios.",
     "Usado pelos jogos sem ajustes próprios."},
    {"Off", "Desactivado", "Desligado"},
    {"On", "Activado", "Ligado"},
    {"FPS and frame timing", "FPS y tiempo por cuadro", "FPS e tempo por quadro"},
    {"Overlay in game", "Datos en pantalla", "Dados na tela"},
    {"Fast forward with <l>+<r>", "Avance rápido con <l>+<r>", "Avanço rápido com <l>+<r>"},
    {"<l>+<r> switches up to twice the speed; »» shows in the corner while it is on. Busy maps run as fast as they can.",
     "<l>+<r> sube hasta el doble de velocidad; »» aparece en la esquina mientras está activo. Los mapas pesados van tan rápido como "
     "pueden.",
     "<l>+<r> acelera até o dobro da velocidade; »» aparece no canto enquanto está ligado. Mapas pesados rodam o mais rápido que "
     "conseguem."},
    {"Any button can also be mapped to Fast forward in Controls. Applies the next time a game starts.",
     "También puedes asignar el avance rápido a cualquier botón en Controles. Se aplica la próxima vez que inicies un juego.",
     "Você também pode atribuir o avanço rápido a qualquer botão em Controles. Vale na próxima vez que um jogo iniciar."},
    {"FPS: frames shown each second. MS: average time between frames, waits included. Peak: the longest frame in about the last "
     "five seconds.",
     "FPS: cuadros por segundo. MS: tiempo medio entre cuadros, esperas incluidas. Pico: el cuadro más largo de los últimos cinco "
     "segundos.",
     "FPS: quadros por segundo. MS: tempo médio entre quadros, esperas incluídas. Pico: o quadro mais longo dos últimos cinco "
     "segundos."},
    {"Drawn in an empty border when there is one, otherwise in a corner. Applies the next time a game starts.",
     "Se muestra en un borde vacío si lo hay; si no, en una esquina. Se aplica la próxima vez que inicies un juego.",
     "Aparece numa borda vazia se houver; senão, num canto. Vale na próxima vez que um jogo iniciar."},
    {"Shared by every game", "Compartidas por todos los juegos", "Compartilhados por todos os jogos"},
    {"(automatic)", "(automática)", "(automático)"},
    {"An RTP in a ZIP is unpacked once; the original pack is kept.",
     "Un RTP en ZIP se descomprime una vez; el paquete original se conserva.",
     "Um RTP em ZIP é descompactado uma vez; o pacote original é mantido."},
    {"Checking...", "Comprobando...", "Verificando..."},
    {"Detected", "Encontrado", "Encontrado"},
    {"Missing", "Falta", "Faltando"},
    {"Unavailable", "No disponible", "Indisponível"},
    {"Update", "Actualización", "Atualização"},
    {"A newer release is out", "Hay una versión nueva", "Saiu uma versão nova"},
    {"Check for new releases at startup", "Buscar versiones nuevas al iniciar", "Verificar versões novas ao iniciar"},
    {"Check now", "Buscar ahora", "Verificar agora"},
    {"Newer release notes", "Notas de la versión nueva", "Notas da versão nova"},
    {"None found", "Ninguna", "Nenhuma"},
    {"Checks published Alpha releases. Nothing is shown when you are up to date or offline.",
     "Busca las versiones Alpha publicadas. No muestra nada si ya estás al día o sin conexión.",
     "Verifica as versões Alpha publicadas. Nada aparece se você estiver em dia ou sem conexão."},
    {"Updates are downloaded and installed by hand.", "Las actualizaciones se descargan e instalan a mano.",
     "As atualizações são baixadas e instaladas manualmente."},
    {"Default: {}", "Predeterminada: {}", "Padrão: {}"},
    {"Add another folder", "Añadir otra carpeta", "Adicionar outra pasta"},
    {"Folders are searched for games each time HardRPG starts. Removing a folder here keeps its games and saves.",
     "HardRPG busca juegos en estas carpetas cada vez que se inicia. Quitar una carpeta aquí conserva sus juegos y partidas.",
     "O HardRPG procura jogos nestas pastas sempre que inicia. Remover uma pasta aqui mantém seus jogos e saves."},
    {"Covers, backgrounds, logos and details such as the year, developer and description",
     "Portadas, fondos, logos y detalles como el año, el desarrollador y la descripción",
     "Capas, fundos, logos e detalhes como o ano, o desenvolvedor e a descrição"},
    {"Add a key", "Añadir clave", "Adicionar chave"},
    {"Saved", "Guardada", "Salva"},
    {"Blur adult artwork", "Difuminar imágenes para adultos", "Desfocar imagens adultas"},
    {"No key needed", "Sin clave", "Sem chave"},
    {"Covers and backgrounds a source marks as adult are shown blurred in the library and the artwork chooser.",
     "Las portadas y fondos que una fuente marca para adultos se ven difuminados en la biblioteca y al elegir imágenes.",
     "Capas e fundos que uma fonte marca como adultos aparecem desfocados na biblioteca e na escolha de imagens."},
    {"SteamGridDB: covers, backgrounds and logos for almost any game. Free key: steamgriddb.com, Preferences, API.",
     "SteamGridDB: portadas, fondos y logos de casi cualquier juego. Clave gratis: steamgriddb.com, Preferences, API.",
     "SteamGridDB: capas, fundos e logos de quase qualquer jogo. Chave grátis: steamgriddb.com, Preferences, API."},
    {"IGDB client ID", "ID de cliente de IGDB", "ID de cliente do IGDB"},
    {"IGDB client secret", "Secreto de cliente de IGDB", "Segredo de cliente do IGDB"},
    {"SteamGridDB API key", "Clave API de SteamGridDB", "Chave de API do SteamGridDB"},
    {"IGDB (Twitch) client ID", "ID de cliente de IGDB (Twitch)", "ID de cliente do IGDB (Twitch)"},
    {"IGDB (Twitch) client secret", "Secreto de cliente de IGDB (Twitch)", "Segredo de cliente do IGDB (Twitch)"},
    {"RAWG API key", "Clave API de RAWG", "Chave de API do RAWG"},
    {"IGDB: release details, covers and screenshots. Free: sign in at dev.twitch.tv/console, register an application (any name, "
     "redirect http://localhost, category Application Integration), then copy its Client ID and make a New Secret.",
     "IGDB: datos de lanzamiento, portadas y capturas. Gratis: entra en dev.twitch.tv/console, registra una aplicación (cualquier "
     "nombre, redirect http://localhost, categoría Application Integration), copia su Client ID y crea un New Secret.",
     "IGDB: dados de lançamento, capas e capturas de tela. Grátis: entre em dev.twitch.tv/console, registre um aplicativo "
     "(qualquer nome, redirect http://localhost, categoria Application Integration), copie o Client ID e crie um New Secret."},
    {"Both are needed. A wrong pair is skipped, and the other sources still work.",
     "Hacen falta los dos. Si el par es incorrecto se omite, y las demás fuentes siguen funcionando.",
     "Os dois são necessários. Um par errado é ignorado, e as outras fontes continuam funcionando."},
    {"RAWG: release dates, developers and genres. Free key: rawg.io/apidocs.",
     "RAWG: fechas de lanzamiento, desarrolladores y géneros. Clave gratis: rawg.io/apidocs.",
     "RAWG: datas de lançamento, desenvolvedores e gêneros. Chave grátis: rawg.io/apidocs."},
    {"With no source knowing a game, its own title screen is used.",
     "Si ninguna fuente conoce un juego, se usa su propia pantalla de título.",
     "Se nenhuma fonte conhece um jogo, a tela de título dele é usada."},
    {"No game has ended with an error.", "Ningún juego ha terminado con un error.", "Nenhum jogo terminou com erro."},
    {"Earlier game error", "Error anterior de un juego", "Erro anterior de um jogo"},
    {"Earlier exported report", "Informe exportado antes", "Relatório exportado antes"},
    {"Version {}", "Versión {}", "Versão {}"},
    {"Classic RPG games on the PS Vita. Made by Asukate.", "Juegos RPG clásicos en PS Vita. Hecho por Asukate.",
     "Jogos de RPG clássicos no PS Vita. Feito por Asukate."},
    {"powered by mkxp-z and EasyRPG Player", "con mkxp-z y EasyRPG Player", "com mkxp-z e EasyRPG Player"},
    {"Vita drawing by Tokyoship, CC BY 3.0", "dibujo de la Vita de Tokyoship, CC BY 3.0", "desenho do Vita por Tokyoship, CC BY 3.0"},

    // Game settings.
    {"Controls", "Controles", "Controles"},
    {"Audio and MIDI", "Audio y MIDI", "Áudio e MIDI"},
    {"Game information", "Información del juego", "Informações do jogo"},
    {"Remove from library", "Quitar de la biblioteca", "Remover da biblioteca"},
    {"Delete the ZIP, keep the game", "Borrar el ZIP y conservar el juego", "Apagar o ZIP e manter o jogo"},
    {"Sound", "Sonido", "Som"},
    {"MIDI reverb", "Reverberación MIDI", "Reverberação MIDI"},
    {"MIDI chorus", "Coro MIDI", "Coro MIDI"},
    {"Saved for this game; applies the next time it starts.", "Guardado para este juego; se aplica la próxima vez que se inicie.",
     "Salvo para este jogo; vale na próxima vez que ele iniciar."},
    {"MIDI effects cost CPU time and are off by default.", "Los efectos MIDI consumen CPU y vienen desactivados.",
     "Os efeitos MIDI usam CPU e vêm desligados."},
    {"Remove {}", "Quitar {}", "Remover {}"},
    {"Delete the ZIP to free its space?", "¿Borrar el ZIP para liberar espacio?", "Apagar o ZIP para liberar espaço?"},
    {"Delete its unpacked copy too?", "¿Borrar también su copia descomprimida?", "Apagar também a cópia descompactada?"},
    {"Delete its game files too?", "¿Borrar también los archivos del juego?", "Apagar também os arquivos do jogo?"},
    {"The game stays listed and plays from its unpacked copy.", "El juego sigue en la lista y se juega desde su copia descomprimida.",
     "O jogo continua na lista e roda a partir da cópia descompactada."},
    {"It will be removed from the library.", "Se quitará de la biblioteca.", "Ele será removido da biblioteca."},
    {"Game files: will be deleted.", "Archivos del juego: se borrarán.", "Arquivos do jogo: serão apagados."},
    {"Game files: kept.", "Archivos del juego: se conservan.", "Arquivos do jogo: mantidos."},
    {"No, keep it", "No, conservarla", "Não, manter"},
    {"No, keep them", "No, conservarlos", "Não, manter"},
    {"Yes, delete it", "Sí, borrarla", "Sim, apagar"},
    {"Yes, delete them", "Sí, borrarlos", "Sim, apagar"},
    {"Saves and settings stay: they are in the unpacked copy.", "Las partidas y ajustes se quedan: están en la copia descomprimida.",
     "Saves e ajustes ficam: estão na cópia descompactada."},
    {"The unpacked copy holds the game's saves. This cannot be undone.",
     "La copia descomprimida guarda las partidas del juego. No se puede deshacer.",
     "A cópia descompactada guarda os saves do jogo. Não dá para desfazer."},
    {"Saves kept inside the game files go with them. This cannot be undone.",
     "Las partidas guardadas dentro de los archivos del juego se borran con ellos. No se puede deshacer.",
     "Saves guardados dentro dos arquivos do jogo vão junto. Não dá para desfazer."},
    {"ZIP archive", "Archivo ZIP", "Arquivo ZIP"},
    {"Game folder", "Carpeta del juego", "Pasta do jogo"},
    {"Location", "Ubicación", "Local"},
    {"Unpacked copy", "Copia descomprimida", "Cópia descompactada"},
    {"Unpacks to", "Se descomprime en", "Descompacta em"},
    {"MIDI bank", "Banco MIDI", "Banco MIDI"},
    {"Bundled General MIDI bank", "Banco General MIDI incluido", "Banco General MIDI incluído"},
    {"Developer", "Desarrollador", "Desenvolvedor"},
    {"Publisher", "Editora", "Editora"},
    {"Released", "Lanzamiento", "Lançamento"},
    {"Genres", "Géneros", "Gêneros"},
    {"Details from {}.", "Detalles de {}.", "Detalhes de {}."},

    // Controls.
    {"Test controls", "Probar controles", "Testar controles"},
    {"Press buttons, move the sticks, touch the screen or the back", "Pulsa botones, mueve los sticks, toca la pantalla o la parte trasera",
     "Aperte botões, mova os analógicos, toque na tela ou atrás"},
    {"now does: {}", "ahora hace: {}", "agora faz: {}"},
    {"Top row: RPG Maker actions (X=A: RPG Maker's X, key A on PC). Below: keys. Dimmed: in use.",
     "Arriba: acciones RPG Maker (X=A: X de RPG Maker, tecla A en PC). Abajo: teclas. Tenues: en uso.",
     "Acima: ações do RPG Maker (X=A: X do RPG Maker, tecla A no PC). Abaixo: teclas. Escuras: em uso."},
    {"Rear touch pad", "Panel táctil trasero", "Touchpad traseiro"},
    {"Front touch screen", "Pantalla táctil", "Tela sensível ao toque"},
    {"Two zones along the top edge, clear of your grip.", "Dos zonas en el borde superior, lejos de donde sujetas la consola.",
     "Duas zonas na borda de cima, longe de onde você segura."},
    {"Fingers already on the pad when the game starts are ignored.", "Se ignoran los dedos que ya están en el panel al iniciar el juego.",
     "Dedos que já estão no painel quando o jogo inicia são ignorados."},
    {"These buttons are drawn on screen. Whole screen, halves and corners are invisible.",
     "Estos botones se dibujan en pantalla. Pantalla completa, mitades y esquinas son invisibles.",
     "Estes botões aparecem na tela. Tela inteira, metades e cantos são invisíveis."},
    {"Invisible zones: nothing is drawn. Several fingers press several zones.",
     "Zonas invisibles: no se dibuja nada. Varios dedos pulsan varias zonas.",
     "Zonas invisíveis: nada aparece. Vários dedos apertam várias zonas."},
    {"Whole screen, halves and corners are invisible touch zones.", "Pantalla completa, mitades y esquinas son zonas táctiles invisibles.",
     "Tela inteira, metades e cantos são zonas de toque invisíveis."},
    {"Sticks and pointer", "Sticks y puntero", "Analógicos e ponteiro"},
    {"Pointer options need a game that reads the mouse.", "Las opciones del puntero necesitan un juego que use el ratón.",
     "As opções do ponteiro precisam de um jogo que use o mouse."},
    {"<cross> Cross gives this button a keyboard key or an RPG Maker action.", "<cross> X asigna a este botón una tecla o una acción de RPG Maker.",
     "<cross> X atribui a este botão uma tecla ou uma ação do RPG Maker."},
    {"Each button does what is shown. Saved for this game.", "Cada botón hace lo que se muestra. Guardado para este juego.",
     "Cada botão faz o que aparece. Salvo para este jogo."},
    {"Pick a button and press <cross> Cross to map it to any keyboard key.", "Elige un botón y pulsa <cross> X para asignarle cualquier tecla.",
     "Escolha um botão e aperte <cross> X para atribuir qualquer tecla."},
    {"Mode", "Modo", "Modo"},
    {"The back of the console, left and right as you hold it", "La parte trasera, izquierda y derecha tal como sujetas la consola",
     "A parte de trás, esquerda e direita como você segura o console"},
    {"The screen", "La pantalla", "A tela"},
    {"Trackpad: hold <l> and slide to move the pointer.", "Trackpad: mantén <l> y desliza para mover el puntero.",
     "Trackpad: segure <l> e deslize para mover o ponteiro."},
    {"Mouse pointer: touches move and click the pointer.", "Puntero: tocar mueve el puntero y hace clic.",
     "Ponteiro: tocar move o ponteiro e clica."},
    {"Off. Choose a mode with Left and Right to map zones.", "Desactivado. Elige un modo con izquierda y derecha para asignar zonas.",
     "Desligado. Escolha um modo com esquerda e direita para mapear zonas."},
    {"Key {}", "Tecla {}", "Tecla {}"},
    {"Back touch pad", "Panel trasero", "Painel traseiro"},
    {"Held", "Pulsado", "Apertado"},
    {"Press any button", "Pulsa cualquier botón", "Aperte qualquer botão"},
    {"Touch the screen", "Toca la pantalla", "Toque na tela"},
    {"Nothing this game's profile does is held.", "No se está pulsando nada del perfil de este juego.",
     "Nada do perfil deste jogo está apertado."},
    {"Custom controls are off: this shows the raw buttons.", "Los controles personalizados están desactivados: se ven los botones tal cual.",
     "Os controles personalizados estão desligados: aparecem os botões puros."},
    {"In game this does: {}", "En el juego hace: {}", "No jogo faz: {}"},
    {"Two drawn corner buttons", "Dos botones en las esquinas", "Dois botões nos cantos"},
    {"Mouse pointer", "Puntero", "Ponteiro"},
    {"D-pad + drawn button", "Cruceta + botón", "Direcional + botão"},
    {"Whole screen", "Toda la pantalla", "Tela inteira"},
    {"Left / right halves", "Mitades izquierda / derecha", "Metades esquerda / direita"},
    {"Four corners", "Cuatro esquinas", "Quatro cantos"},
    {"Two upper zones", "Dos zonas superiores", "Duas zonas de cima"},
    {"Trackpad (hold <l>)", "Trackpad (mantén <l>)", "Trackpad (segure <l>)"},
    {"Whole pad", "Todo el panel", "Painel inteiro"},
    {"Top left", "Arriba izquierda", "Em cima à esquerda"},
    {"Top right", "Arriba derecha", "Em cima à direita"},
    {"Bottom left", "Abajo izquierda", "Embaixo à esquerda"},
    {"Bottom right", "Abajo derecha", "Embaixo à direita"},
    {"Button", "Botón", "Botão"},
    {"Rear pad", "Panel trasero", "Painel traseiro"},
    {"Front screen", "Pantalla", "Tela"},
    {"Rear pad: {}", "Panel trasero: {}", "Painel traseiro: {}"},
    {"Front screen: {}", "Pantalla: {}", "Tela: {}"},
    {"{} button", "Botón {}", "Botão {}"},
    {"Custom controls", "Controles personalizados", "Controles personalizados"},
    {"Tap to walk", "Tocar para caminar", "Tocar para andar"},
    {"Test inputs", "Probar botones", "Testar botões"},
    {"Reset everything", "Restablecer todo", "Redefinir tudo"},
    {"Stick deadzone", "Zona muerta del stick", "Zona morta do analógico"},
    {"Right stick moves pointer", "Stick derecho mueve el puntero", "Analógico direito move o ponteiro"},
    {"Pointer speed", "Velocidad del puntero", "Velocidade do ponteiro"},
    {"<square> Square toggles its action", "<square> Cuadrado alterna su acción", "<square> Quadrado alterna sua ação"},
    {"Off (game's own controls)", "Desactivado (controles del juego)", "Desligado (controles do jogo)"},
    {"On (tap the map to walk there)", "Activado (toca el mapa para ir allí)", "Ligado (toque no mapa para ir até lá)"},
    {"Back to defaults, off", "Volver a lo predeterminado", "Voltar ao padrão"},
    // Buttons and RPG Maker actions.
    {"Cross", "X", "X"},
    {"Circle", "Círculo", "Círculo"},
    {"Square", "Cuadrado", "Quadrado"},
    {"Triangle", "Triángulo", "Triângulo"},
    {"Up", "Arriba", "Cima"},
    {"Down", "Abajo", "Baixo"},
    {"Left", "Izquierda", "Esquerda"},
    {"Right", "Derecha", "Direita"},
    {"Run", "Correr", "Correr"},
    {"Confirm", "Confirmar", "Confirmar"},
    {"A / Run", "A / Correr", "A / Correr"},
    {"B / Cancel", "B / Cancelar", "B / Cancelar"},
    {"C / Confirm", "C / Confirmar", "C / Confirmar"},
    {"RPG X (PC key A)", "RPG X (tecla A en PC)", "RPG X (tecla A no PC)"},
    {"RPG Y (PC key S)", "RPG Y (tecla S en PC)", "RPG Y (tecla S no PC)"},
    {"RPG Z (PC key D)", "RPG Z (tecla D en PC)", "RPG Z (tecla D no PC)"},
    {"RPG L (PC key Q)", "RPG L (tecla Q en PC)", "RPG L (tecla Q no PC)"},
    {"RPG R (PC key W)", "RPG R (tecla W en PC)", "RPG R (tecla W no PC)"},
    {"Mouse left", "Clic izquierdo", "Clique esquerdo"},
    {"Mouse right", "Clic derecho", "Clique direito"},
    {"Mouse L", "Clic I", "Clique E"},
    {"Mouse R", "Clic D", "Clique D"},
    {"Fast forward", "Avance rápido", "Avanço rápido"},
    {"Fast fwd", "Acelerar", "Acelerar"},
    // The key picker's one-key-wide "Off".
    {"picker|Off", "No", "Não"},

    // Messages.
    {"Could not start game (LoadExec {}, {}; {}).", "No se pudo iniciar el juego (LoadExec {}, {}; {}).",
     "Não foi possível iniciar o jogo (LoadExec {}, {}; {})."},
    {"RTP check failed.", "Falló la comprobación del RTP.", "A verificação do RTP falhou."},
    {"Cannot start RTP location check.", "No se puede comprobar la ubicación del RTP.", "Não dá para verificar o local do RTP."},
    {"Installed: {}", "Instalada: {}", "Instalada: {}"},
    {"Available: {}", "Disponible: {}", "Disponível: {}"},
    {"Download on your PC:", "Descárgala en tu PC:", "Baixe no seu PC:"},
    {"Open this collection and highlight a game first.", "Abre esta colección y elige primero un juego.",
     "Abra esta coleção e escolha um jogo primeiro."},
    {"ZIP deleted. {} plays from its unpacked copy.", "ZIP borrado. {} se juega desde su copia descomprimida.",
     "ZIP apagado. {} roda a partir da cópia descompactada."},
    {"{} removed and its files deleted.", "{} quitado y sus archivos borrados.", "{} removido e seus arquivos apagados."},
    {"{} removed from the list. Files kept; <start> adds it again.", "{} quitado de la lista. Los archivos se conservan; <start> lo vuelve a añadir.",
     "{} removido da lista. Arquivos mantidos; <start> adiciona de novo."},
    {"Game name keyboard is unavailable.", "El teclado no está disponible.", "O teclado não está disponível."},
    {"Original game name restored.", "Nombre original restaurado.", "Nome original restaurado."},
    {"Display name saved.", "Nombre guardado.", "Nome salvo."},
    {"Checking RTP locations...", "Comprobando las ubicaciones del RTP...", "Verificando os locais do RTP..."},
    {"Automatic RTP location restored.", "Ubicación automática del RTP restaurada.", "Local automático do RTP restaurado."},
    {"{} now does {}. Custom controls are on; applies on next launch.",
     "{} ahora hace {}. Controles personalizados activados; se aplica al iniciar el juego.",
     "{} agora faz {}. Controles personalizados ligados; vale ao iniciar o jogo."},
    {"Tap to walk turned off: the screen is touch zones now.", "Tocar para caminar desactivado: la pantalla ahora tiene zonas táctiles.",
     "Tocar para andar desligado: a tela agora tem zonas de toque."},
    {"Tap to walk on; front touch zones turned off. Next launch.", "Tocar para caminar activado; zonas táctiles desactivadas. Al iniciar el juego.",
     "Tocar para andar ligado; zonas de toque desligadas. Ao iniciar o jogo."},
    {"Tap to walk on: works in games with standard tile movement. Next launch.",
     "Tocar para caminar activado: funciona en juegos con movimiento normal por casillas. Al iniciar el juego.",
     "Tocar para andar ligado: funciona em jogos com movimento normal por quadrados. Ao iniciar o jogo."},
    {"Saved for {}. Applies on next launch.", "Guardado para {}. Se aplica al iniciar el juego.", "Salvo para {}. Vale ao iniciar o jogo."},
    {"Key removed.", "Clave quitada.", "Chave removida."},
    {"Key saved. Artwork downloads use it from now on.", "Clave guardada. Las descargas de imágenes la usarán desde ahora.",
     "Chave salva. Os downloads de imagens vão usá-la a partir de agora."},
    {"Display settings saved for the next game launch.", "Ajustes de pantalla guardados para el próximo juego.",
     "Ajustes de tela salvos para o próximo jogo."},
    {"Fast forward saved for the next game launch.", "Avance rápido guardado para el próximo juego.",
     "Avanço rápido salvo para o próximo jogo."},
    {"Performance metrics saved for the next game launch.", "Datos de rendimiento guardados para el próximo juego.",
     "Dados de desempenho salvos para o próximo jogo."},
    {"{} - <circle> Circle: cancel", "{} - <circle> Círculo: cancelar", "{} - <circle> Círculo: cancelar"},
    {"Preparing ZIP", "Preparando ZIP", "Preparando ZIP"},
    {"{}: {}% - <circle> Circle: cancel", "{}: {}% - <circle> Círculo: cancelar", "{}: {}% - <circle> Círculo: cancelar"},
    {"{}: reading contents - <circle> Circle: cancel", "{}: leyendo contenido - <circle> Círculo: cancelar", "{}: lendo conteúdo - <circle> Círculo: cancelar"},
    {"Scanning folder {} - <circle> Circle: cancel", "Buscando en la carpeta {} - <circle> Círculo: cancelar", "Procurando na pasta {} - <circle> Círculo: cancelar"},
    {"{} RTP location saved. Files stay in place.", "Ubicación del RTP {} guardada. Los archivos no se mueven.",
     "Local do RTP {} salvo. Os arquivos ficam onde estão."},
    {"Choose an RTP folder or ZIP first", "Primero elige una carpeta o ZIP del RTP", "Escolha antes uma pasta ou ZIP do RTP"},
    {"Choose a game folder or collection first", "Primero elige una carpeta de juego o colección",
     "Escolha antes uma pasta de jogo ou coleção"},
    {"Extra game folder saved. Found {} games; files stay in place.",
     "Carpeta de juegos guardada. Se encontraron {} juegos; los archivos no se mueven.",
     "Pasta de jogos salva. {} jogos encontrados; os arquivos ficam onde estão."},
    {"Added 1 game. Files stay in their original folders.", "1 juego añadido. Los archivos se quedan en sus carpetas.",
     "1 jogo adicionado. Os arquivos ficam nas pastas originais."},
    {"Added {} games. Files stay in their original folders.", "{} juegos añadidos. Los archivos se quedan en sus carpetas.",
     "{} jogos adicionados. Os arquivos ficam nas pastas originais."},
    {"Checking releases...", "Buscando versiones...", "Verificando versões..."},
    {"No newer release has been found.", "No se ha encontrado una versión más nueva.", "Nenhuma versão mais nova foi encontrada."},
    {"New release: {}", "Versión nueva: {}", "Versão nova: {}"},
    {"No newer release found.", "No hay una versión más nueva.", "Não há versão mais nova."},
    {"Could not check releases. Offline or request failed.", "No se pudieron buscar versiones. Sin conexión o falló la consulta.",
     "Não foi possível verificar versões. Sem conexão ou a consulta falhou."},
    {"Adult artwork is blurred.", "Las imágenes para adultos se difuminan.", "Imagens adultas ficam desfocadas."},
    {"Adult artwork is shown as is.", "Las imágenes para adultos se muestran tal cual.", "Imagens adultas aparecem normalmente."},
    {"This report could not be read.", "No se pudo leer este informe.", "Não foi possível ler este relatório."},
    {"Reading game archive", "Leyendo el ZIP del juego", "Lendo o ZIP do jogo"},
    {"Reading game folder", "Leyendo la carpeta del juego", "Lendo a pasta do jogo"},
    {"Saved: {}", "Guardado: {}", "Salvo: {}"},
    {"Automatic RTP location restored. Pack files were not removed.",
     "Ubicación automática del RTP restaurada. No se borró ningún archivo.",
     "Local automático do RTP restaurado. Nenhum arquivo foi apagado."},
    {"Game folder reference removed. Games and saves stay in place.",
     "Carpeta quitada de la lista. Los juegos y partidas no se mueven.",
     "Pasta removida da lista. Jogos e saves ficam onde estão."},
    {"Game info is already downloading.", "La info de juegos ya se está descargando.", "A info dos jogos já está sendo baixada."},
    {"Collections get game info for each game inside them.", "Las colecciones obtienen la info de cada juego que contienen.",
     "Coleções recebem a info de cada jogo que têm dentro."},
    {"Looking for game info...", "Buscando info de juegos...", "Procurando info dos jogos..."},
    {"Looking for game info for {}...", "Buscando info de {}...", "Procurando info de {}..."},
    {"Game info and artwork saved.", "Info e imágenes guardadas.", "Info e imagens salvas."},
    {"No game info found for this game.", "No se encontró info para este juego.", "Nenhuma info encontrada para este jogo."},
    {"Game info found for {} of {} games.", "Info encontrada para {} de {} juegos.", "Info encontrada para {} de {} jogos."},
    {"Downloading game info {} of {}: {}", "Descargando info {} de {}: {}", "Baixando info {} de {}: {}"},
    {"Saving...", "Guardando...", "Salvando..."},
    {"Kept the current artwork and details.", "Se mantienen las imágenes y detalles actuales.", "Imagens e detalhes atuais mantidos."},
    {"Added to favourites.", "Añadido a favoritos.", "Adicionado aos favoritos."},
    {"Removed from favourites.", "Quitado de favoritos.", "Removido dos favoritos."},
    {"Open this collection to rename its games.", "Abre esta colección para cambiar el nombre de sus juegos.",
     "Abra esta coleção para renomear seus jogos."},
    {"Not connected to the internet.", "Sin conexión a internet.", "Sem conexão com a internet."},
    {"Could not start the download.", "No se pudo iniciar la descarga.", "Não foi possível iniciar o download."},
    {"Preparation cancelled.", "Preparación cancelada.", "Preparação cancelada."},
    // Compiling the engine's shaders once after an update; the steps are
    // named in display/gl/shader-warmup.cpp.
    {"Preparing graphics for this HardRPG version (only once): {} of {}, {}",
     "Preparando los gráficos de esta versión de HardRPG (solo una vez): {} de {}, {}",
     "Preparando os gráficos desta versão do HardRPG (só uma vez): {} de {}, {}"},
    {"Graphics ready", "Gráficos listos", "Gráficos prontos"},
    {"pictures", "imágenes", "imagens"},
    {"sprites", "sprites", "sprites"},
    {"sprite effects", "efectos de sprites", "efeitos de sprites"},
    {"sprites with opacity", "sprites con opacidad", "sprites com opacidade"},
    {"fades", "fundidos", "transições suaves"},
    {"solid colors", "colores sólidos", "cores sólidas"},
    {"tinted pictures", "imágenes teñidas", "imagens tingidas"},
    {"screen fades", "fundidos de pantalla", "escurecimento da tela"},
    {"image transitions", "transiciones de imagen", "transições de imagem"},
    {"backgrounds and fogs", "fondos y nieblas", "fundos e névoas"},
    {"VX map tiles", "casillas de mapa VX", "blocos de mapa VX"},
    {"XP map tiles", "casillas de mapa XP", "blocos de mapa XP"},
    {"map flashes", "destellos del mapa", "flashes do mapa"},
    {"Sharp filter", "filtro Sharp", "filtro Sharp"},
    {"blur", "desenfoque", "desfoque"},
    {"color shifts", "cambios de color", "mudanças de cor"},
    {"gray effects", "efectos en gris", "efeitos em cinza"},
    {"picture copies", "copias de imágenes", "cópias de imagens"},
    {"rotated pictures", "imágenes giradas", "imagens giradas"},

    // Errors the library can report.
    {"Cannot create folder: {}", "No se puede crear la carpeta: {}", "Não dá para criar a pasta: {}"},
    {"Cannot read folder: {}", "No se puede leer la carpeta: {}", "Não dá para ler a pasta: {}"},
    {"Cannot open ZIP archive", "No se puede abrir el ZIP", "Não dá para abrir o ZIP"},
    {"Truncated ZIP file", "El ZIP está incompleto", "O ZIP está incompleto"},
    {"Cannot write ZIP cache (storage may be full)", "No se puede escribir la caché del ZIP (puede que no haya espacio)",
     "Não dá para gravar o cache do ZIP (o espaço pode ter acabado)"},
    {"Missing {} RTP", "Falta el RTP {}", "Falta o RTP {}"},
    {"Missing {} RTP. Choose its folder or ZIP in Settings.", "Falta el RTP {}. Elige su carpeta o ZIP en Ajustes.",
     "Falta o RTP {}. Escolha a pasta ou ZIP dele em Ajustes."},
    {"Saved game names could not be read; original names are shown.",
     "No se pudieron leer los nombres guardados; se muestran los originales.",
     "Não foi possível ler os nomes salvos; aparecem os originais."},
    {"Select a game or ZIP to rename.", "Elige un juego o ZIP para cambiarle el nombre.", "Escolha um jogo ou ZIP para renomear."},
    {"Use a single-line game name of at most 128 characters.", "Usa un nombre de una línea y 128 caracteres como máximo.",
     "Use um nome de uma linha com até 128 caracteres."},
    {"Game names cannot contain control characters.", "Los nombres no pueden tener caracteres de control.",
     "Os nomes não podem ter caracteres de controle."},
    {"Use at most 128 characters.", "Usa 128 caracteres como máximo.", "Use até 128 caracteres."},
    {"Folder scan exceeds 16 levels; choose a closer folder", "La búsqueda supera 16 niveles; elige una carpeta más cercana",
     "A busca passa de 16 níveis; escolha uma pasta mais próxima"},
    {"Folder scan exceeds 2048 folders; choose a closer folder", "La búsqueda supera 2048 carpetas; elige una carpeta más cercana",
     "A busca passa de 2048 pastas; escolha uma pasta mais próxima"},
    {"This game has no unpacked copy to play from", "Este juego no tiene una copia descomprimida",
     "Este jogo não tem uma cópia descompactada"},
    {"Cannot delete the ZIP", "No se puede borrar el ZIP", "Não dá para apagar o ZIP"},
    {"No supported RPG Maker games found", "No se encontraron juegos de RPG Maker compatibles",
     "Nenhum jogo de RPG Maker compatível encontrado"},
    {"Library has more than 2048 added games", "La biblioteca tiene más de 2048 juegos añadidos",
     "A biblioteca tem mais de 2048 jogos adicionados"},
    {"Choose an existing game folder", "Elige una carpeta de juegos que exista", "Escolha uma pasta de jogos que exista"},
    {"Choose at most 32 extra game folders", "Elige 32 carpetas de juegos como máximo", "Escolha até 32 pastas de jogos"},
    {"Select a game to configure controls.", "Elige un juego para configurar sus controles.",
     "Escolha um jogo para configurar os controles."},
    {"Select a game to change its settings.", "Elige un juego para cambiar sus ajustes.", "Escolha um jogo para mudar os ajustes."},
    {"Choose a pack containing Graphics/Audio or System/Music folders.",
     "Elige un paquete con carpetas Graphics/Audio o System/Music.",
     "Escolha um pacote com pastas Graphics/Audio ou System/Music."},
    {"Preparing game ZIP", "Preparando el ZIP del juego", "Preparando o ZIP do jogo"},
    {"Starting game", "Iniciando el juego", "Iniciando o jogo"},
    {"The {} runtime is missing from this package.", "Falta el motor {} en este paquete.", "Falta o motor {} neste pacote."},
    {"Checking {} RTP", "Comprobando el RTP {}", "Verificando o RTP {}"},
    {"Preparing {} RTP ZIP", "Preparando el ZIP del RTP {}", "Preparando o ZIP do RTP {}"},
    {"Launch paths are too long; choose a closer game or RTP folder.",
     "Las rutas son demasiado largas; elige una carpeta de juego o RTP más cercana.",
     "Os caminhos são longos demais; escolha uma pasta de jogo ou RTP mais próxima."},
    {"Error history could not be saved: {}", "No se pudo guardar el historial de errores: {}",
     "Não foi possível salvar o histórico de erros: {}"},
    {"No retained game error to export.", "No hay ningún error guardado para exportar.", "Não há erro guardado para exportar."},
    {"Cannot create exported report. Check free space.", "No se puede crear el informe. Revisa el espacio libre.",
     "Não dá para criar o relatório. Verifique o espaço livre."},
    {"Cannot write exported report. Check free space.", "No se puede escribir el informe. Revisa el espacio libre.",
     "Não dá para gravar o relatório. Verifique o espaço livre."},
    {"Report folder is full. Move older exported reports.", "La carpeta de informes está llena. Mueve los informes antiguos.",
     "A pasta de relatórios está cheia. Mova os relatórios antigos."},

    // Sheets around errors, the RTP and the browser.
    {"Error report", "Informe de error", "Relatório de erro"},
    {"The game ended with an error", "El juego terminó con un error", "O jogo terminou com um erro"},
    {"Can't start the game", "No se puede iniciar el juego", "Não dá para iniciar o jogo"},
    {"This game needs the RTP's shared graphics and sounds.", "Este juego necesita los gráficos y sonidos compartidos del RTP.",
     "Este jogo precisa dos gráficos e sons compartilhados do RTP."},
    {"Choose an RTP folder or ZIP", "Elegir una carpeta o ZIP del RTP", "Escolher uma pasta ou ZIP do RTP"},
    {"Back to games", "Volver a los juegos", "Voltar aos jogos"},
    {"Looked in: {}", "Se buscó en: {}", "Procurado em: {}"},
    {"Library", "Biblioteca", "Biblioteca"},
    {"Choose an RTP", "Elige un RTP", "Escolha um RTP"},
    {"Choose a game folder", "Elige una carpeta de juegos", "Escolha uma pasta de jogos"},
    {"Choose a storage device", "Elige un almacenamiento", "Escolha um armazenamento"},
    {"No storage found.", "No se encontró almacenamiento.", "Nenhum armazenamento encontrado."},
    {"No folders here. <square> Square uses this folder.", "Aquí no hay carpetas. <square> Cuadrado usa esta carpeta.",
     "Não há pastas aqui. <square> Quadrado usa esta pasta."},
    {"No folders here. <square> Square looks for games in this folder.", "Aquí no hay carpetas. <square> Cuadrado busca juegos en esta carpeta.",
     "Não há pastas aqui. <square> Quadrado procura jogos nesta pasta."},
    {"<square> Square looks for games in the folder shown above, including the folders inside it. Games stay where they are.",
     "<square> Cuadrado busca juegos en la carpeta de arriba y en las que contiene. Los juegos no se mueven.",
     "<square> Quadrado procura jogos na pasta acima e nas pastas dentro dela. Os jogos ficam onde estão."},

    // The welcome guide.
    {"Welcome", "Bienvenida", "Boas-vindas"},
    {"Welcome to HardRPG", "Te damos la bienvenida a HardRPG", "Boas-vindas ao HardRPG"},
    {"Choose your language. You can change it later in Settings.", "Elige tu idioma. Puedes cambiarlo luego en Ajustes.",
     "Escolha seu idioma. Você pode mudar depois em Ajustes."},
    {"Your games", "Tus juegos", "Seus jogos"},
    {"Where your games go", "Dónde van tus juegos", "Onde ficam seus jogos"},
    {"Copy each game into this folder, as a folder or a ZIP file:", "Copia cada juego en esta carpeta, como carpeta o como archivo ZIP:",
     "Copie cada jogo para esta pasta, como pasta ou arquivo ZIP:"},
    {"The RTP", "El RTP", "O RTP"},
    {"Many games need the RTP", "Muchos juegos necesitan el RTP", "Muitos jogos precisam do RTP"},
    {"The RTP is RPG Maker's shared graphics and music. A game that does not include it will not start without it.",
     "El RTP son los gráficos y la música que comparten los juegos de RPG Maker. Un juego que no lo incluye no arranca sin él.",
     "O RTP são os gráficos e as músicas que os jogos de RPG Maker compartilham. Um jogo que não o inclui não abre sem ele."},
    {"unpacked on its first start", "se descomprime al iniciarlo", "descompactado ao iniciar"},
    {"A ZIP game is unpacked the first time it starts: that start takes longer, and the ZIP and its unpacked copy both use space. "
     "Folders start right away.",
     "Un juego en ZIP se descomprime la primera vez que se inicia: ese inicio tarda más, y el ZIP y su copia descomprimida ocupan "
     "espacio los dos. Las carpetas arrancan enseguida.",
     "Um jogo em ZIP é descompactado na primeira vez que inicia: essa vez demora mais, e o ZIP e a cópia descompactada ocupam "
     "espaço. Pastas iniciam na hora."},
    {"Games kept elsewhere can stay there: add their folder in Settings, Game folders, or press <start> on the home screen.",
     "Los juegos guardados en otro sitio pueden quedarse ahí: añade su carpeta en Ajustes, Carpetas de juegos, o pulsa <start> en la "
     "pantalla principal.",
     "Jogos guardados em outro lugar podem ficar lá: adicione a pasta em Ajustes, Pastas de jogos, ou aperte <start> na tela "
     "inicial."},
    {"Use these names: HardRPG looks for them. XP, VX and VXAce work too, and so does a folder inside them. An RTP kept anywhere "
     "else can be chosen in Settings, RTP locations.",
     "Usa estos nombres: HardRPG los busca. También valen XP, VX y VXAce, y una carpeta dentro de ellos. Un RTP guardado en otro "
     "sitio se elige en Ajustes, Ubicaciones del RTP.",
     "Use estes nomes: o HardRPG procura por eles. XP, VX e VXAce também valem, assim como uma pasta dentro deles. Um RTP guardado "
     "em outro lugar pode ser escolhido em Ajustes, Locais do RTP."},
    {"How your games are shown", "Cómo se ven tus juegos", "Como seus jogos aparecem"},
    {"Choose the view the library opens in.", "Elige la vista con la que se abre la biblioteca.",
     "Escolha como a biblioteca abre."},
    {"Carousel", "Carrusel", "Carrossel"},
    {"Grid", "Cuadrícula", "Grade"},
    {"You can switch any time with the first button on the library's toolbar; the library keeps the view you used last.",
     "Puedes cambiarla cuando quieras con el primer botón de la barra de la biblioteca; se recuerda la última que usaste.",
     "Você pode trocar quando quiser no primeiro botão da barra da biblioteca; a última usada fica salva."},
    {"Good to know", "Para saber", "Bom saber"},
    {"A few defaults", "Algunos ajustes de inicio", "Alguns ajustes padrão"},
    {"Screen", "Pantalla", "Tela"},
    {"Games keep their shape and fill the screen as far as they can, with the Sharp filter. Change it in Settings, Game display.",
     "Los juegos mantienen su forma y llenan la pantalla todo lo posible, con el filtro Sharp. Cámbialo en Ajustes, Pantalla de "
     "los juegos.",
     "Os jogos mantêm o formato e ocupam a tela o máximo possível, com o filtro Sharp. Mude em Ajustes, Tela dos jogos."},
    {"Each game", "Cada juego", "Cada jogo"},
    {"Press <triangle> Triangle on a game for favourites, game info and its own settings and controls.",
     "Pulsa <triangle> Triángulo sobre un juego para favoritos, su info y sus propios ajustes y controles.",
     "Aperte <triangle> Triângulo num jogo para favoritos, info e os ajustes e controles dele."},
    {"Press <l>+<r> while playing to run up to twice as fast.", "Pulsa <l>+<r> mientras juegas para ir hasta el doble de rápido.",
     "Aperte <l>+<r> durante o jogo para rodar até duas vezes mais rápido."},
    {"Back to HardRPG", "Volver a HardRPG", "Voltar ao HardRPG"},
    {"Hold <l>+<r>+<select> in a game to come back here. Progress you have not saved is lost.",
     "Mantén <l>+<r>+<select> en un juego para volver aquí. Se pierde lo que no hayas guardado.",
     "Segure <l>+<r>+<select> num jogo para voltar aqui. O que não foi salvo é perdido."},
    {"Game info", "Info de juegos", "Info dos jogos"},
    {"Covers and game details", "Portadas y detalles", "Capas e detalhes"},
    {"HardRPG can download covers, backgrounds, logos and details such as the year and developer. Press <triangle> Triangle on a game and "
     "choose Download game info.",
     "HardRPG puede descargar portadas, fondos, logos y detalles como el año y el desarrollador. Pulsa <triangle> Triángulo sobre un juego y "
     "elige Descargar info del juego.",
     "O HardRPG pode baixar capas, fundos, logos e detalhes como o ano e o desenvolvedor. Aperte <triangle> Triângulo num jogo e escolha "
     "Baixar info do jogo."},
    {"Some sources work right away. Others need a free API key, which you can add in Settings, Game info and artwork. Each source "
     "there says where to get its key.",
     "Algunas fuentes funcionan enseguida. Otras necesitan una clave API gratuita, que puedes añadir en Ajustes, Info e imágenes de "
     "juegos. Cada fuente explica ahí dónde conseguir su clave.",
     "Algumas fontes funcionam na hora. Outras precisam de uma chave de API grátis, que você adiciona em Ajustes, Info e imagens "
     "dos jogos. Cada fonte diz ali onde conseguir a chave."},
    {"You can always keep the pictures you have: the artwork chooser shows what each source found first.",
     "Siempre puedes quedarte con las imágenes que tienes: primero verás lo que encontró cada fuente.",
     "Você sempre pode manter as imagens que já tem: primeiro você vê o que cada fonte encontrou."},
    // Launcher sound (Settings, Sound).
    {"Default", "Predeterminado", "Padrão"},
    {"Menu sounds only", "Solo sonidos del menú", "Só sons do menu"},
    {"Title music only", "Solo música de título", "Só música de título"},
    {"The launcher's own sounds. Games keep their audio settings.",
     "Los sonidos del lanzador. Los juegos conservan sus ajustes de audio.",
     "Os sons do launcher. Os jogos mantêm seus ajustes de áudio."},
    {"Menu sounds", "Sonidos del menú", "Sons do menu"},
    {"Menu sound volume", "Volumen de los sonidos", "Volume dos sons"},
    {"Title music", "Música de título", "Música de título"},
    {"Title music volume", "Volumen de la música", "Volume da música"},
    {"Sounds for moving, opening, going back and starting a game.",
     "Sonidos al moverte, abrir, volver e iniciar un juego.",
     "Sons ao mover, abrir, voltar e iniciar um jogo."},
    {"Plays the highlighted game's own title music, read from its files. Nothing is downloaded.",
     "Reproduce la música de título del juego seleccionado, leída de sus archivos. No se descarga nada.",
     "Toca a música de título do jogo selecionado, lida dos arquivos dele. Nada é baixado."},
    {"A ZIP game plays it once it has been unpacked. Put a hardrpg-theme.ogg (or .mp3 or .wav) in a game's folder to choose "
     "its music. MIDI titles stay silent.",
     "Un juego en ZIP la reproduce cuando ya se ha descomprimido. Pon un hardrpg-theme.ogg (o .mp3 o .wav) en la carpeta de un "
     "juego para elegir su música. Los títulos MIDI no suenan.",
     "Um jogo em ZIP toca depois de descompactado. Coloque um hardrpg-theme.ogg (ou .mp3 ou .wav) na pasta de um jogo para "
     "escolher a música. Títulos em MIDI ficam em silêncio."},
};

const std::map<std::string, const Text *> &index() {
  static std::map<std::string, const Text *> map = [] {
    std::map<std::string, const Text *> m;
    for (auto &t : table)
      m[t.en] = &t;
    return m;
  }();
  return map;
}
const char *pick(const Text &t, int lang) {
  const char *s = lang == Spanish ? t.es : lang == Portuguese ? t.pt : t.en;
  return s && *s ? s : t.en;
}
} // namespace

int language() { return current; }
void setLanguage(int lang) { current = lang >= 0 && lang < languageCount ? lang : English; }
const char *languageName(int lang) { return names[lang >= 0 && lang < languageCount ? lang : English]; }
const char *languageCode(int lang) { return codes[lang >= 0 && lang < languageCount ? lang : English]; }
int languageFromCode(const std::string &code) {
  for (int i = 0; i < languageCount; ++i)
    if (code == codes[i])
      return i;
  // "pt" and "pt-PT" read as the Portuguese there is.
  if (code.compare(0, 2, "pt") == 0)
    return Portuguese;
  if (code.compare(0, 2, "es") == 0)
    return Spanish;
  return -1;
}
int systemLanguage() {
#ifdef __vita__
  int lang = SCE_SYSTEM_PARAM_LANG_ENGLISH_US;
  if (sceAppUtilSystemParamGetInt(SCE_SYSTEM_PARAM_ID_LANG, &lang) < 0)
    return English;
  if (lang == SCE_SYSTEM_PARAM_LANG_SPANISH)
    return Spanish;
  if (lang == SCE_SYSTEM_PARAM_LANG_PORTUGUESE_BR || lang == SCE_SYSTEM_PARAM_LANG_PORTUGUESE_PT)
    return Portuguese;
#endif
  return English;
}

const char *trc(const char *context, const char *english) {
  if (current != English) {
    auto found = index().find(std::string(context) + "|" + english);
    if (found != index().end())
      return pick(*found->second, current);
  }
  return tr(english);
}
const char *tr(const char *english) {
  if (current == English || !english)
    return english;
  auto &m = index();
  auto found = m.find(english);
  return found == m.end() ? english : pick(*found->second, current);
}
std::string tr(const std::string &english) {
  if (current == English)
    return english;
  auto &m = index();
  auto found = m.find(english);
  return found == m.end() ? english : pick(*found->second, current);
}
std::string trf(const char *english, std::initializer_list<std::string> args) {
  std::string out = tr(english);
  size_t at = 0;
  for (auto &a : args) {
    at = out.find("{}", at);
    if (at == std::string::npos)
      break;
    out.replace(at, 2, a);
    at += a.size();
  }
  return out;
}
std::vector<std::pair<std::string, std::string>> translations(int lang) {
  std::vector<std::pair<std::string, std::string>> out;
  for (auto &t : table) {
    const char *s = lang == Spanish ? t.es : lang == Portuguese ? t.pt : t.en;
    out.push_back({t.en, s ? s : ""});
  }
  return out;
}
} // namespace hardrpg
