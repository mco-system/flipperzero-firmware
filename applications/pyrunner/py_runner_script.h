#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <furi.h>
#include <m-string.h>

typedef struct PyRunnerScript PyRunnerScript;

typedef enum {
    PyRunnerStateInit,
    PyRunnerStateNotConnected,
    PyRunnerStateIdle,
    PyRunnerStateRunning,
    PyRunnerStateDelay,
    PyRunnerStateDone,
    PyRunnerStateScriptError,
    PyRunnerStateFileError,
} PyRunnerWorkerState;

typedef struct {
    PyRunnerWorkerState state;
    uint16_t line_cur;
    uint16_t line_nb;
    uint32_t delay_remain;
    uint16_t error_line;
} PyRunnerState;

PyRunnerScript* py_runner_script_open(string_t file_path);

void py_runner_script_close(PyRunnerScript* py_runner);

void py_runner_script_set_keyboard_layout(PyRunnerScript* py_runner, string_t layout_path);

void py_runner_script_start(PyRunnerScript* py_runner);

void py_runner_script_stop(PyRunnerScript* py_runner);

void py_runner_script_toggle(PyRunnerScript* py_runner);

PyRunnerState* py_runner_script_get_state(PyRunnerScript* py_runner);

#ifdef __cplusplus
}
#endif
