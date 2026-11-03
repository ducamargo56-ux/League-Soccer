
#define SDL_MAIN_HANDLED
#include <SDL.h>

#include <cstdio>
#include <cstdarg>
#include <cstring>

static FILE* arquivo = nullptr;

static void Registrar(const char* formato, ...) {
    va_list args;
    va_start(args, formato);

    va_list copia;
    va_copy(copia, args);

    vprintf(formato, args);
    printf("\n");

    if (arquivo) {
        vfprintf(arquivo, formato, copia);
        fprintf(arquivo, "\n");
        fflush(arquivo);
    }

    va_end(copia);
    va_end(args);
}

static void DescreverControle(int indice) {
    const char* nome = SDL_JoystickNameForIndex(indice);
    SDL_JoystickGUID guid = SDL_JoystickGetDeviceGUID(indice);
    char guidTexto[64] = {};

    SDL_JoystickGetGUIDString(guid, guidTexto, sizeof(guidTexto));

    Registrar("CONTROLE ENCONTRADO: indice=%d nome=%s",
              indice, nome ? nome : "(desconhecido)");
    Registrar("GUID: %s", guidTexto);
    Registrar("Instance ID: %d",
              static_cast<int>(SDL_JoystickGetDeviceInstanceID(indice)));
    Registrar("GameController reconhecido: %s",
              SDL_IsGameController(indice) ? "SIM" : "NAO");
}

int main(int, char**) {
    arquivo = fopen("teste_8bitdo.log", "w");

    Registrar("========================================");
    Registrar("DIAGNOSTICO SDL2 - 8BITDO");
    Registrar("========================================");
    Registrar("SDL compilado: %d.%d.%d",
              SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_PATCHLEVEL);

    SDL_version versao;
    SDL_GetVersion(&versao);
    Registrar("SDL em execucao: %d.%d.%d",
              versao.major, versao.minor, versao.patch);

    if (SDL_Init(SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER |
                 SDL_INIT_EVENTS) != 0) {
        Registrar("ERRO SDL_Init: %s", SDL_GetError());
        if (arquivo) fclose(arquivo);
        return 1;
    }

    SDL_JoystickEventState(SDL_ENABLE);
    SDL_GameControllerEventState(SDL_ENABLE);

    const int quantidade = SDL_NumJoysticks();
    Registrar("Quantidade de controles: %d", quantidade);

    for (int i = 0; i < quantidade; ++i) {
        DescreverControle(i);
    }

    SDL_Joystick* joystick = nullptr;
    SDL_GameController* controller = nullptr;

    if (quantidade > 0) {
        joystick = SDL_JoystickOpen(0);

        if (joystick) {
            Registrar("JOYSTICK ABERTO COM SUCESSO");
            Registrar("Botoes: %d", SDL_JoystickNumButtons(joystick));
            Registrar("Eixos: %d", SDL_JoystickNumAxes(joystick));
            Registrar("Hats: %d", SDL_JoystickNumHats(joystick));
        } else {
            Registrar("ERRO AO ABRIR JOYSTICK: %s", SDL_GetError());
        }

        if (SDL_IsGameController(0)) {
            controller = SDL_GameControllerOpen(0);
            Registrar("GameControllerOpen: %s",
                      controller ? "SUCESSO" : SDL_GetError());

            if (controller) {
                const char* mapping = SDL_GameControllerMapping(controller);
                Registrar("MAPEAMENTO: %s",
                          mapping ? mapping : "(nenhum)");
                SDL_free((void*)mapping);
            }
        }
    }

    Registrar("----------------------------------------");
    Registrar("PRESSIONE BOTOES, DPAD E ANALOGICOS.");
    Registrar("PRESSIONE ESC NO TECLADO PARA ENCERRAR.");
    Registrar("----------------------------------------");

    SDL_Window* janela = SDL_CreateWindow(
        "Diagnostico SDL2 - 8BitDo",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        640, 240,
        SDL_WINDOW_SHOWN
    );

    if (!janela) {
        Registrar("ERRO AO CRIAR JANELA: %s", SDL_GetError());
    }

    bool executando = true;

    while (executando) {
        SDL_Event evento;

        while (SDL_PollEvent(&evento)) {
            switch (evento.type) {
                case SDL_QUIT:
                    executando = false;
                    break;

                case SDL_KEYDOWN:
                    if (evento.key.keysym.sym == SDLK_ESCAPE) {
                        executando = false;
                    }
                    break;

                case SDL_JOYBUTTONDOWN:
                    Registrar("JOY BUTTON DOWN: id=%d botao=%d",
                              evento.jbutton.which,
                              evento.jbutton.button);
                    break;

                case SDL_JOYBUTTONUP:
                    Registrar("JOY BUTTON UP: id=%d botao=%d",
                              evento.jbutton.which,
                              evento.jbutton.button);
                    break;

                case SDL_JOYAXISMOTION:
                    if (evento.jaxis.value > 12000 ||
                        evento.jaxis.value < -12000) {
                        Registrar("JOY AXIS: id=%d eixo=%d valor=%d",
                                  evento.jaxis.which,
                                  evento.jaxis.axis,
                                  evento.jaxis.value);
                    }
                    break;

                case SDL_JOYHATMOTION:
                    Registrar("JOY HAT: id=%d hat=%d valor=%d",
                              evento.jhat.which,
                              evento.jhat.hat,
                              evento.jhat.value);
                    break;

                case SDL_CONTROLLERBUTTONDOWN:
                    Registrar("CONTROLLER BUTTON DOWN: id=%d botao=%d",
                              evento.cbutton.which,
                              evento.cbutton.button);
                    break;

                case SDL_CONTROLLERBUTTONUP:
                    Registrar("CONTROLLER BUTTON UP: id=%d botao=%d",
                              evento.cbutton.which,
                              evento.cbutton.button);
                    break;

                case SDL_CONTROLLERAXISMOTION:
                    if (evento.caxis.value > 12000 ||
                        evento.caxis.value < -12000) {
                        Registrar("CONTROLLER AXIS: id=%d eixo=%d valor=%d",
                                  evento.caxis.which,
                                  evento.caxis.axis,
                                  evento.caxis.value);
                    }
                    break;

                case SDL_JOYDEVICEADDED:
                    Registrar("JOY DEVICE ADDED: indice=%d",
                              evento.jdevice.which);
                    break;

                case SDL_JOYDEVICEREMOVED:
                    Registrar("JOY DEVICE REMOVED: id=%d",
                              evento.jdevice.which);
                    break;

                default:
                    break;
            }
        }

        SDL_Delay(10);
    }

    Registrar("FIM DO DIAGNOSTICO");

    if (controller) SDL_GameControllerClose(controller);
    if (joystick) SDL_JoystickClose(joystick);
    if (janela) SDL_DestroyWindow(janela);

    SDL_Quit();

    if (arquivo) fclose(arquivo);
    return 0;
}
