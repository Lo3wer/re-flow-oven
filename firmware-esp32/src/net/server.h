#pragma once

#include "controller.h"
#include "history.h"

// Starts the embedded web server: serves a control/monitoring page, a REST
// status/control API and a WebSocket stream of temperature samples.
void server_init(controller_t *ctrl, history_t *hist);