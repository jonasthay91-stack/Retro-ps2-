/*
  RetroHub PS2 — tela "Estante"
  Licenciado sob a Academic Free License version 3.0, como o restante do OPL.
*/

#ifndef __RH_SHELF_H
#define __RH_SHELF_H

/// Inicializa a estante. Chamada uma vez, junto com o resto da GUI.
void rhShelfInit(void);

/// Libera a textura da capa em uso. Chamada no encerramento.
void rhShelfEnd(void);

/// Entrada e desenho da tela, no formato que o gui_screen_handler_t espera.
void rhShelfHandleInput(void);
void rhShelfRender(void);

#endif
