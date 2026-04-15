
#include <furi.h>
#include <furi_hal.h>
#include <furi_hal_usb_hid.h>
#include <gui/gui.h>
#include <infrared_worker.h>
#include <input/input.h>

// -------------------------------------------------------
// Config
// -------------------------------------------------------
#define MAX_BINDINGS 16

// -------------------------------------------------------
// Key Binding Struct
// -------------------------------------------------------
typedef struct {
    uint32_t command;
    uint8_t hid_key;
    const char* label;
} KeyBinding;

// -------------------------------------------------------
// State
// -------------------------------------------------------
typedef struct {
    FuriMessageQueue* event_queue;
    const char* last_signal;
    bool running;

    // Bindings
    KeyBinding bindings[MAX_BINDINGS];
    uint8_t binding_count;

    // Learning mode
    bool learning_mode;
    uint8_t selected_key;
} AppState;

// -------------------------------------------------------
// GUI draw callback
// -------------------------------------------------------
static void draw_callback(Canvas* canvas, void* ctx) {
    AppState* state = (AppState*)ctx;

    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 12, "IR > USB HID");
    canvas_draw_str(canvas, 2, 24, "Built by @jasompalo");

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 2, 34, "github.com/jasompalo");
    canvas_draw_str(canvas, 2, 60, "In 1.0.0 the key is set to space");
    if(state->learning_mode) {
        canvas_draw_str(canvas, 2, 48, "LEARNING...");
    } else {
        canvas_draw_str(canvas, 2, 48, "Press OK to bind");
    }

    if(state->last_signal != NULL) {
        canvas_draw_str(canvas, 2, 72, state->last_signal);
    }
}

// -------------------------------------------------------
// Input callback
// -------------------------------------------------------
static void input_callback(InputEvent* event, void* ctx) {
    AppState* state = (AppState*)ctx;
    furi_message_queue_put(state->event_queue, event, 0);
}

// -------------------------------------------------------
// IR receive callback
// -------------------------------------------------------
static void ir_received_callback(void* ctx, InfraredWorkerSignal* signal) {
    AppState* state = (AppState*)ctx;

    if(!infrared_worker_signal_is_decoded(signal)) return;

    const InfraredMessage* msg = infrared_worker_get_decoded_signal(signal);

    // 🔴 Flash LED on signal
    furi_hal_light_set(LightRed, 255);
    furi_delay_ms(50);
    furi_hal_light_set(LightRed, 0);

    // 🧠 LEARNING MODE
    if(state->learning_mode) {
        if(state->binding_count < MAX_BINDINGS) {
            state->bindings[state->binding_count++] = (KeyBinding){
                .command = msg->command, .hid_key = state->selected_key, .label = "BOUND"};

            state->last_signal = "Bound!";
        } else {
            state->last_signal = "Full!";
        }

        state->learning_mode = false;
        return;
    }

    // 🎮 NORMAL MODE
    for(uint8_t i = 0; i < state->binding_count; i++) {
        if(state->bindings[i].command == msg->command) {
            state->last_signal = state->bindings[i].label;

            furi_hal_hid_kb_press(state->bindings[i].hid_key);
            furi_delay_ms(80);
            furi_hal_hid_kb_release_all();
            return;
        }
    }

    state->last_signal = "Unknown";
}

// -------------------------------------------------------
// App entry point
// -------------------------------------------------------
int32_t ir_to_hid_app(void* p) {
    UNUSED(p);

    // Allocate state
    AppState* state = malloc(sizeof(AppState));
    state->event_queue = furi_message_queue_alloc(8, sizeof(InputEvent));
    state->last_signal = NULL;
    state->running = true;

    state->binding_count = 0;
    state->learning_mode = false;
    state->selected_key = HID_KEYBOARD_SPACEBAR;

    // Switch USB to HID mode
    furi_hal_usb_set_config(&usb_hid, NULL);
    furi_delay_ms(200);

    // GUI setup
    ViewPort* view_port = view_port_alloc();
    view_port_draw_callback_set(view_port, draw_callback, state);
    view_port_input_callback_set(view_port, input_callback, state);

    Gui* gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(gui, view_port, GuiLayerFullscreen);

    // IR worker
    InfraredWorker* worker = infrared_worker_alloc();
    infrared_worker_rx_set_received_signal_callback(worker, ir_received_callback, state);
    infrared_worker_rx_start(worker);

    // Main loop
    InputEvent event;
    while(state->running) {
        if(furi_message_queue_get(state->event_queue, &event, 100) == FuriStatusOk) {
            if(event.type == InputTypeShort) {
                if(event.key == InputKeyBack) {
                    state->running = false;
                }

                else if(event.key == InputKeyOk) {
                    state->learning_mode = true;
                    state->last_signal = "Press remote...";
                }

                else if(event.key == InputKeyUp) {
                    state->selected_key = HID_KEYBOARD_UP_ARROW;
                    state->last_signal = "Key: UP";
                }

                else if(event.key == InputKeyDown) {
                    state->selected_key = HID_KEYBOARD_DOWN_ARROW;
                    state->last_signal = "Key: DOWN";
                }

                else if(event.key == InputKeyLeft) {
                    state->selected_key = HID_KEYBOARD_LEFT_ARROW;
                    state->last_signal = "Key: LEFT";
                }

                else if(event.key == InputKeyRight) {
                    state->selected_key = HID_KEYBOARD_RIGHT_ARROW;
                    state->last_signal = "Key: RIGHT";
                }
            }
        }

        view_port_update(view_port);
    }

    // Cleanup
    infrared_worker_rx_stop(worker);
    infrared_worker_free(worker);

    gui_remove_view_port(gui, view_port);
    view_port_free(view_port);
    furi_record_close(RECORD_GUI);

    furi_message_queue_free(state->event_queue);
    free(state);

    // Restore USB
    furi_hal_usb_set_config(&usb_cdc_single, NULL);

    return 0;
}
