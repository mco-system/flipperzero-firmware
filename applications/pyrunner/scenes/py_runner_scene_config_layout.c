#include "../py_runner_app_i.h"
#include "furi_hal_power.h"
#include "furi_hal_usb.h"
#include <storage/storage.h>

static bool py_runner_layout_select(PyRunnerApp* py_runner) {
    furi_assert(py_runner);

    string_t predefined_path;
    string_init(predefined_path);
    if(!string_empty_p(py_runner->keyboard_layout)) {
        string_set(predefined_path, py_runner->keyboard_layout);
    } else {
        string_set_str(predefined_path, PY_RUNNER_APP_PATH_LAYOUT_FOLDER);
    }

    // Input events and views are managed by file_browser
    bool res = dialog_file_browser_show(
        py_runner->dialogs,
        py_runner->keyboard_layout,
        predefined_path,
        PY_RUNNER_APP_LAYOUT_EXTENSION,
        true,
        &I_keyboard_10px,
        true);

    string_clear(predefined_path);
    return res;
}

void py_runner_scene_config_layout_on_enter(void* context) {
    PyRunnerApp* py_runner = context;

    if(py_runner_layout_select(py_runner)) {
        py_runner_script_set_keyboard_layout(py_runner->py_runner_script, py_runner->keyboard_layout);
    }
    scene_manager_previous_scene(py_runner->scene_manager);
}

bool py_runner_scene_config_layout_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    // PyRunnerApp* py_runner = context;
    return false;
}

void py_runner_scene_config_layout_on_exit(void* context) {
    UNUSED(context);
    // PyRunnerApp* py_runner = context;
}
