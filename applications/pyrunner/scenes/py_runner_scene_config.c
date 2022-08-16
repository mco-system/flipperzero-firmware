#include "../py_runner_app_i.h"
#include "furi_hal_power.h"
#include "furi_hal_usb.h"

enum SubmenuIndex {
    SubmenuIndexKeyboardLayout,
};

void py_runner_scene_config_submenu_callback(void* context, uint32_t index) {
    PyRunnerApp* py_runner = context;
    view_dispatcher_send_custom_event(py_runner->view_dispatcher, index);
}

void py_runner_scene_config_on_enter(void* context) {
    PyRunnerApp* py_runner = context;
    Submenu* submenu = py_runner->submenu;

    submenu_add_item(
        submenu,
        "Keyboard layout",
        SubmenuIndexKeyboardLayout,
        py_runner_scene_config_submenu_callback,
        py_runner);

    submenu_set_selected_item(
        submenu, scene_manager_get_scene_state(py_runner->scene_manager, PyRunnerSceneConfig));

    view_dispatcher_switch_to_view(py_runner->view_dispatcher, PyRunnerAppViewConfig);
}

bool py_runner_scene_config_on_event(void* context, SceneManagerEvent event) {
    PyRunnerApp* py_runner = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        scene_manager_set_scene_state(py_runner->scene_manager, PyRunnerSceneConfig, event.event);
        consumed = true;
        if(event.event == SubmenuIndexKeyboardLayout) {
            scene_manager_next_scene(py_runner->scene_manager, PyRunnerSceneConfigLayout);
        } else {
            furi_crash("Unknown key type");
        }
    }

    return consumed;
}

void py_runner_scene_config_on_exit(void* context) {
    PyRunnerApp* py_runner = context;
    Submenu* submenu = py_runner->submenu;

    submenu_reset(submenu);
}
