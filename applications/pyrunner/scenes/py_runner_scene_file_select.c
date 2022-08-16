#include "../py_runner_app_i.h"
#include "furi_hal_power.h"
#include "furi_hal_usb.h"

static bool py_runner_file_select(PyRunnerApp* py_runner) {
    furi_assert(py_runner);

    // Input events and views are managed by file_browser
    bool res = dialog_file_browser_show(
        py_runner->dialogs,
        py_runner->file_path,
        py_runner->file_path,
        PY_RUNNER_APP_SCRIPT_EXTENSION,
        true,
        &I_pyrunner_10px,
        true);

    return res;
}

void py_runner_scene_file_select_on_enter(void* context) {
    PyRunnerApp* py_runner = context;

    furi_hal_usb_disable();
    if(py_runner->py_runner_script) {
        py_runner_script_close(py_runner->py_runner_script);
        py_runner->py_runner_script = NULL;
    }

    if(py_runner_file_select(py_runner)) {
        py_runner->py_runner_script = py_runner_script_open(py_runner->file_path);
        py_runner_script_set_keyboard_layout(py_runner->py_runner_script, py_runner->keyboard_layout);

        scene_manager_next_scene(py_runner->scene_manager, PyRunnerSceneWork);
    } else {
        furi_hal_usb_enable();
        view_dispatcher_stop(py_runner->view_dispatcher);
    }
}

bool py_runner_scene_file_select_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    // PyRunnerApp* py_runner = context;
    return false;
}

void py_runner_scene_file_select_on_exit(void* context) {
    UNUSED(context);
    // PyRunnerApp* py_runner = context;
}
