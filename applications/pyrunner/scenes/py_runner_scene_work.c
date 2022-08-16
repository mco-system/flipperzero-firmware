#include "../py_runner_script.h"
#include "../py_runner_app_i.h"
#include "../views/py_runner_view.h"
#include "furi_hal.h"
#include "m-string.h"
#include "toolbox/path.h"

void py_runner_scene_work_button_callback(InputKey key, void* context) {
    furi_assert(context);
    PyRunnerApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, key);
}

bool py_runner_scene_work_on_event(void* context, SceneManagerEvent event) {
    PyRunnerApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == InputKeyLeft) {
            scene_manager_next_scene(app->scene_manager, PyRunnerSceneConfig);
            consumed = true;
        } else if(event.event == InputKeyOk) {
            py_runner_script_toggle(app->py_runner_script);
            consumed = true;
        }
    } else if(event.type == SceneManagerEventTypeTick) {
        py_runner_set_state(app->py_runner_view, py_runner_script_get_state(app->py_runner_script));
    }
    return consumed;
}

void py_runner_scene_work_on_enter(void* context) {
    PyRunnerApp* app = context;

    string_t file_name;
    string_init(file_name);
    path_extract_filename(app->file_path, file_name, true);
    py_runner_set_file_name(app->py_runner_view, string_get_cstr(file_name));
    string_clear(file_name);

    string_t layout;
    string_init(layout);
    path_extract_filename(app->keyboard_layout, layout, true);
    py_runner_set_layout(app->py_runner_view, string_get_cstr(layout));
    string_clear(layout);

    py_runner_set_state(app->py_runner_view, py_runner_script_get_state(app->py_runner_script));

    py_runner_set_button_callback(app->py_runner_view, py_runner_scene_work_button_callback, app);
    view_dispatcher_switch_to_view(app->view_dispatcher, PyRunnerAppViewWork);
}

void py_runner_scene_work_on_exit(void* context) {
    UNUSED(context);
}
