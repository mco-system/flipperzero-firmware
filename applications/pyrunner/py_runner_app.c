#include "py_runner_app_i.h"
#include "py_runner_settings_filename.h"
#include "m-string.h"
#include <furi.h>
#include <furi_hal.h>
#include <storage/storage.h>
#include <lib/toolbox/path.h>

#define PY_RUNNER_SETTINGS_PATH PY_RUNNER_APP_BASE_FOLDER "/" PY_RUNNER_SETTINGS_FILE_NAME

static bool py_runner_app_custom_event_callback(void* context, uint32_t event) {
    furi_assert(context);
    PyRunnerApp* app = context;
    return scene_manager_handle_custom_event(app->scene_manager, event);
}

static bool py_runner_app_back_event_callback(void* context) {
    furi_assert(context);
    PyRunnerApp* app = context;
    return scene_manager_handle_back_event(app->scene_manager);
}

static void py_runner_app_tick_event_callback(void* context) {
    furi_assert(context);
    PyRunnerApp* app = context;
    scene_manager_handle_tick_event(app->scene_manager);
}

static void py_runner_load_settings(PyRunnerApp* app) {
    File* settings_file = storage_file_alloc(furi_record_open(RECORD_STORAGE));
    if(storage_file_open(settings_file, PY_RUNNER_SETTINGS_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
        char chr;
        while((storage_file_read(settings_file, &chr, 1) == 1) &&
              !storage_file_eof(settings_file) && !isspace(chr)) {
            string_push_back(app->keyboard_layout, chr);
        }
    }
    storage_file_close(settings_file);
    storage_file_free(settings_file);
}

static void py_runner_save_settings(PyRunnerApp* app) {
    File* settings_file = storage_file_alloc(furi_record_open(RECORD_STORAGE));
    if(storage_file_open(settings_file, PY_RUNNER_SETTINGS_PATH, FSAM_WRITE, FSOM_OPEN_ALWAYS)) {
        storage_file_write(
            settings_file,
            string_get_cstr(app->keyboard_layout),
            string_size(app->keyboard_layout));
        storage_file_write(settings_file, "\n", 1);
    }
    storage_file_close(settings_file);
    storage_file_free(settings_file);
}

PyRunnerApp* py_runner_app_alloc(char* arg) {
    PyRunnerApp* app = malloc(sizeof(PyRunnerApp));

    app->py_runner_script = NULL;

    string_init(app->file_path);
    string_init(app->keyboard_layout);
    if(arg && strlen(arg)) {
        string_set_str(app->file_path, arg);
    }

    py_runner_load_settings(app);

    app->gui = furi_record_open(RECORD_GUI);
    app->notifications = furi_record_open(RECORD_NOTIFICATION);
    app->dialogs = furi_record_open(RECORD_DIALOGS);

    app->view_dispatcher = view_dispatcher_alloc();
    view_dispatcher_enable_queue(app->view_dispatcher);

    app->scene_manager = scene_manager_alloc(&py_runner_scene_handlers, app);

    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_tick_event_callback(
        app->view_dispatcher, py_runner_app_tick_event_callback, 500);
    view_dispatcher_set_custom_event_callback(
        app->view_dispatcher, py_runner_app_custom_event_callback);
    view_dispatcher_set_navigation_event_callback(
        app->view_dispatcher, py_runner_app_back_event_callback);

    // Custom Widget
    app->widget = widget_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, PyRunnerAppViewError, widget_get_view(app->widget));

    app->submenu = submenu_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, PyRunnerAppViewConfig, submenu_get_view(app->submenu));

    app->py_runner_view = py_runner_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, PyRunnerAppViewWork, py_runner_get_view(app->py_runner_view));

    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);

    if(furi_hal_usb_is_locked()) {
        app->error = PyRunnerAppErrorCloseRpc;
        scene_manager_next_scene(app->scene_manager, PyRunnerSceneError);
    } else {
        if(!string_empty_p(app->file_path)) {
            app->py_runner_script = py_runner_script_open(app->file_path);
            py_runner_script_set_keyboard_layout(app->py_runner_script, app->keyboard_layout);
            scene_manager_next_scene(app->scene_manager, PyRunnerSceneWork);
        } else {
            string_set_str(app->file_path, PY_RUNNER_APP_PATH_SCRIPT_FOLDER);
            scene_manager_next_scene(app->scene_manager, PyRunnerSceneFileSelect);
        }
    }

    return app;
}

void py_runner_app_free(PyRunnerApp* app) {
    furi_assert(app);

    if(app->py_runner_script) {
        py_runner_script_close(app->py_runner_script);
        app->py_runner_script = NULL;
    }

    // Views
    view_dispatcher_remove_view(app->view_dispatcher, PyRunnerAppViewWork);
    py_runner_free(app->py_runner_view);

    // Custom Widget
    view_dispatcher_remove_view(app->view_dispatcher, PyRunnerAppViewError);
    widget_free(app->widget);

    // Submenu
    view_dispatcher_remove_view(app->view_dispatcher, PyRunnerAppViewConfig);
    submenu_free(app->submenu);

    // View dispatcher
    view_dispatcher_free(app->view_dispatcher);
    scene_manager_free(app->scene_manager);

    // Close records
    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_NOTIFICATION);
    furi_record_close(RECORD_DIALOGS);

    py_runner_save_settings(app);

    string_clear(app->file_path);
    string_clear(app->keyboard_layout);

    free(app);
}

int32_t py_runner_app(void* p) {
    PyRunnerApp* py_runner_app = py_runner_app_alloc((char*)p);

    view_dispatcher_run(py_runner_app->view_dispatcher);

    py_runner_app_free(py_runner_app);
    return 0;
}
