#include "py_runner_view.h"
#include "../py_runner_script.h"
#include <toolbox/path.h>
#include <gui/elements.h>

#define MAX_NAME_LEN 64

struct PyRunner {
    View* view;
    PyRunnerButtonCallback callback;
    void* context;
};

typedef struct {
    char file_name[MAX_NAME_LEN];
    char layout[MAX_NAME_LEN];
    PyRunnerState state;
    uint8_t anim_frame;
} PyRunnerModel;

static void py_runner_draw_callback(Canvas* canvas, void* _model) {
    PyRunnerModel* model = _model;

    string_t disp_str;
    string_init_set_str(disp_str, model->file_name);
    elements_string_fit_width(canvas, disp_str, 128 - 2);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 2, 8, string_get_cstr(disp_str));

    if(strlen(model->layout) == 0) {
        string_set(disp_str, "(default)");
    } else {
        string_reset(disp_str);
        string_push_back(disp_str, '(');
        for(size_t i = 0; i < strlen(model->layout); i++)
            string_push_back(disp_str, model->layout[i]);
        string_push_back(disp_str, ')');
    }
    elements_string_fit_width(canvas, disp_str, 128 - 2);
    canvas_draw_str(canvas, 2, 8 + canvas_current_font_height(canvas), string_get_cstr(disp_str));

    string_reset(disp_str);

    canvas_draw_icon(canvas, 22, 24, &I_UsbTree_48x22);

    if((model->state.state == PyRunnerStateIdle) || (model->state.state == PyRunnerStateDone)) {
        elements_button_center(canvas, "Run");
    } else if((model->state.state == PyRunnerStateRunning) || (model->state.state == PyRunnerStateDelay)) {
        elements_button_center(canvas, "Stop");
    }

    if((model->state.state == PyRunnerStateNotConnected) ||
       (model->state.state == PyRunnerStateIdle) || (model->state.state == PyRunnerStateDone)) {
        elements_button_left(canvas, "Config");
    }

    if(model->state.state == PyRunnerStateNotConnected) {
        canvas_draw_icon(canvas, 4, 26, &I_Clock_18x18);
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str_aligned(canvas, 127, 31, AlignRight, AlignBottom, "Connect");
        canvas_draw_str_aligned(canvas, 127, 43, AlignRight, AlignBottom, "to USB");
    } else if(model->state.state == PyRunnerStateFileError) {
        canvas_draw_icon(canvas, 4, 26, &I_Error_18x18);
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str_aligned(canvas, 127, 31, AlignRight, AlignBottom, "File");
        canvas_draw_str_aligned(canvas, 127, 43, AlignRight, AlignBottom, "ERROR");
    } else if(model->state.state == PyRunnerStateScriptError) {
        canvas_draw_icon(canvas, 4, 26, &I_Error_18x18);
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str_aligned(canvas, 127, 33, AlignRight, AlignBottom, "ERROR:");
        canvas_set_font(canvas, FontSecondary);
        string_printf(disp_str, "line %u", model->state.error_line);
        canvas_draw_str_aligned(
            canvas, 127, 46, AlignRight, AlignBottom, string_get_cstr(disp_str));
        string_reset(disp_str);
    } else if(model->state.state == PyRunnerStateIdle) {
        canvas_draw_icon(canvas, 4, 26, &I_Smile_18x18);
        canvas_set_font(canvas, FontBigNumbers);
        canvas_draw_str_aligned(canvas, 114, 40, AlignRight, AlignBottom, "0");
        canvas_draw_icon(canvas, 117, 26, &I_Percent_10x14);
    } else if(model->state.state == PyRunnerStateRunning) {
        if(model->anim_frame == 0) {
            canvas_draw_icon(canvas, 4, 23, &I_EviSmile1_18x21);
        } else {
            canvas_draw_icon(canvas, 4, 23, &I_EviSmile2_18x21);
        }
        canvas_set_font(canvas, FontBigNumbers);
        string_printf(disp_str, "%u", ((model->state.line_cur - 1) * 100) / model->state.line_nb);
        canvas_draw_str_aligned(
            canvas, 114, 40, AlignRight, AlignBottom, string_get_cstr(disp_str));
        string_reset(disp_str);
        canvas_draw_icon(canvas, 117, 26, &I_Percent_10x14);
    } else if(model->state.state == PyRunnerStateDone) {
        canvas_draw_icon(canvas, 4, 23, &I_EviSmile1_18x21);
        canvas_set_font(canvas, FontBigNumbers);
        canvas_draw_str_aligned(canvas, 114, 40, AlignRight, AlignBottom, "100");
        string_reset(disp_str);
        canvas_draw_icon(canvas, 117, 26, &I_Percent_10x14);
    } else if(model->state.state == PyRunnerStateDelay) {
        if(model->anim_frame == 0) {
            canvas_draw_icon(canvas, 4, 23, &I_EviWaiting1_18x21);
        } else {
            canvas_draw_icon(canvas, 4, 23, &I_EviWaiting2_18x21);
        }
        canvas_set_font(canvas, FontBigNumbers);
        string_printf(disp_str, "%u", ((model->state.line_cur - 1) * 100) / model->state.line_nb);
        canvas_draw_str_aligned(
            canvas, 114, 40, AlignRight, AlignBottom, string_get_cstr(disp_str));
        string_reset(disp_str);
        canvas_draw_icon(canvas, 117, 26, &I_Percent_10x14);
        canvas_set_font(canvas, FontSecondary);
        string_printf(disp_str, "delay %us", model->state.delay_remain);
        canvas_draw_str_aligned(
            canvas, 127, 50, AlignRight, AlignBottom, string_get_cstr(disp_str));
        string_reset(disp_str);
    } else {
        canvas_draw_icon(canvas, 4, 26, &I_Clock_18x18);
    }

    string_clear(disp_str);
}

static bool py_runner_input_callback(InputEvent* event, void* context) {
    furi_assert(context);
    PyRunner* py_runner = context;
    bool consumed = false;

    if(event->type == InputTypeShort) {
        if((event->key == InputKeyLeft) || (event->key == InputKeyOk)) {
            consumed = true;
            furi_assert(py_runner->callback);
            py_runner->callback(event->key, py_runner->context);
        }
    }

    return consumed;
}

PyRunner* py_runner_alloc() {
    PyRunner* py_runner = malloc(sizeof(PyRunner));

    py_runner->view = view_alloc();
    view_allocate_model(py_runner->view, ViewModelTypeLocking, sizeof(PyRunnerModel));
    view_set_context(py_runner->view, py_runner);
    view_set_draw_callback(py_runner->view, py_runner_draw_callback);
    view_set_input_callback(py_runner->view, py_runner_input_callback);

    return py_runner;
}

void py_runner_free(PyRunner* py_runner) {
    furi_assert(py_runner);
    view_free(py_runner->view);
    free(py_runner);
}

View* py_runner_get_view(PyRunner* py_runner) {
    furi_assert(py_runner);
    return py_runner->view;
}

void py_runner_set_button_callback(PyRunner* py_runner, PyRunnerButtonCallback callback, void* context) {
    furi_assert(py_runner);
    furi_assert(callback);
    with_view_model(
        py_runner->view, (PyRunnerModel * model) {
            UNUSED(model);
            py_runner->callback = callback;
            py_runner->context = context;
            return true;
        });
}

void py_runner_set_file_name(PyRunner* py_runner, const char* name) {
    furi_assert(name);
    with_view_model(
        py_runner->view, (PyRunnerModel * model) {
            strlcpy(model->file_name, name, MAX_NAME_LEN);
            return true;
        });
}

void py_runner_set_layout(PyRunner* py_runner, const char* layout) {
    furi_assert(layout);
    with_view_model(
        py_runner->view, (PyRunnerModel * model) {
            strlcpy(model->layout, layout, MAX_NAME_LEN);
            return true;
        });
}
void py_runner_set_state(PyRunner* py_runner, PyRunnerState* st) {
    furi_assert(st);
    with_view_model(
        py_runner->view, (PyRunnerModel * model) {
            memcpy(&(model->state), st, sizeof(PyRunnerState));
            model->anim_frame ^= 1;
            return true;
        });
}
