#pragma once

#include <gui/view.h>
#include "../py_runner_script.h"

typedef struct PyRunner PyRunner;
typedef void (*PyRunnerButtonCallback)(InputKey key, void* context);

PyRunner* py_runner_alloc();

void py_runner_free(PyRunner* py_runner);

View* py_runner_get_view(PyRunner* py_runner);

void py_runner_set_button_callback(PyRunner* py_runner, PyRunnerButtonCallback callback, void* context);

void py_runner_set_file_name(PyRunner* py_runner, const char* name);

void py_runner_set_layout(PyRunner* py_runner, const char* layout);

void py_runner_set_state(PyRunner* py_runner, PyRunnerState* st);
