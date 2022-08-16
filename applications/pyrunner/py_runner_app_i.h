#pragma once

#include "py_runner_app.h"
#include "scenes/py_runner_scene.h"
#include "py_runner_script.h"

#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/scene_manager.h>
#include <gui/modules/submenu.h>
#include <dialogs/dialogs.h>
#include <notification/notification_messages.h>
#include <gui/modules/variable_item_list.h>
#include <gui/modules/widget.h>
#include "views/py_runner_view.h"

#define PY_RUNNER_APP_BASE_FOLDER ANY_PATH("pyrunner")
#define PY_RUNNER_APP_PATH_SCRIPT_FOLDER PY_RUNNER_APP_BASE_FOLDER
#define PY_RUNNER_APP_PATH_LAYOUT_FOLDER PY_RUNNER_APP_BASE_FOLDER
#define PY_RUNNER_APP_SCRIPT_EXTENSION ".py"
#define PY_RUNNER_APP_LAYOUT_EXTENSION ".kl"

typedef enum {
    PyRunnerAppErrorNoFiles,
    PyRunnerAppErrorCloseRpc,
} PyRunnerAppError;

struct PyRunnerApp {
    Gui* gui;
    ViewDispatcher* view_dispatcher;
    SceneManager* scene_manager;
    NotificationApp* notifications;
    DialogsApp* dialogs;
    Widget* widget;
    Submenu* submenu;

    PyRunnerAppError error;
    string_t file_path;
    string_t keyboard_layout;
    PyRunner* py_runner_view;
    PyRunnerScript* py_runner_script;
};

typedef enum {
    PyRunnerAppViewError,
    PyRunnerAppViewWork,
    PyRunnerAppViewConfig,
    PyRunnerAppViewConfigLayout,
} PyRunnerAppView;
