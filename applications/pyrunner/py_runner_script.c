#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <input/input.h>
#include <lib/toolbox/args.h>
#include <furi_hal_usb_hid.h>
#include <storage/storage.h>
#include "py_runner_script.h"
#include <dolphin/dolphin.h>

#define TAG "PyRunner"
#define WORKER_TAG TAG "Worker"
#define FILE_BUFFER_LEN 16

#define SCRIPT_STATE_ERROR (-1)
#define SCRIPT_STATE_END (-2)
#define SCRIPT_STATE_NEXT_LINE (-3)

#define PYRUNNER_ASCII_TO_KEY(script, x) \
    (((uint8_t)x < 128) ? (script->layout[(uint8_t)x]) : HID_KEYBOARD_NONE)

typedef enum {
    WorkerEvtToggle = (1 << 0),
    WorkerEvtEnd = (1 << 1),
    WorkerEvtConnect = (1 << 2),
    WorkerEvtDisconnect = (1 << 3),
} WorkerEvtFlags;

struct PyRunnerScript {
    FuriHalUsbHidConfig hid_cfg;
    PyRunnerState st;
    string_t file_path;
    uint32_t defdelay;
    uint16_t layout[128];
    FuriThread* thread;
    uint8_t file_buf[FILE_BUFFER_LEN + 1];
    uint8_t buf_start;
    uint8_t buf_len;
    bool file_end;
    string_t line;

    string_t line_prev;
    uint32_t repeat_cnt;
};

typedef struct {
    char* name;
    uint16_t keycode;
} DuckyKey;

static const DuckyKey ducky_keys[] = {
    {"CTRL-ALT", KEY_MOD_LEFT_CTRL | KEY_MOD_LEFT_ALT},
    {"CTRL-SHIFT", KEY_MOD_LEFT_CTRL | KEY_MOD_LEFT_SHIFT},
    {"ALT-SHIFT", KEY_MOD_LEFT_ALT | KEY_MOD_LEFT_SHIFT},
    {"ALT-GUI", KEY_MOD_LEFT_ALT | KEY_MOD_LEFT_GUI},
    {"GUI-SHIFT", KEY_MOD_LEFT_GUI | KEY_MOD_LEFT_SHIFT},

    {"CTRL", KEY_MOD_LEFT_CTRL},
    {"CONTROL", KEY_MOD_LEFT_CTRL},
    {"SHIFT", KEY_MOD_LEFT_SHIFT},
    {"ALT", KEY_MOD_LEFT_ALT},
    {"GUI", KEY_MOD_LEFT_GUI},
    {"WINDOWS", KEY_MOD_LEFT_GUI},

    {"DOWNARROW", HID_KEYBOARD_DOWN_ARROW},
    {"DOWN", HID_KEYBOARD_DOWN_ARROW},
    {"LEFTARROW", HID_KEYBOARD_LEFT_ARROW},
    {"LEFT", HID_KEYBOARD_LEFT_ARROW},
    {"RIGHTARROW", HID_KEYBOARD_RIGHT_ARROW},
    {"RIGHT", HID_KEYBOARD_RIGHT_ARROW},
    {"UPARROW", HID_KEYBOARD_UP_ARROW},
    {"UP", HID_KEYBOARD_UP_ARROW},

    {"ENTER", HID_KEYBOARD_RETURN},
    {"BREAK", HID_KEYBOARD_PAUSE},
    {"PAUSE", HID_KEYBOARD_PAUSE},
    {"CAPSLOCK", HID_KEYBOARD_CAPS_LOCK},
    {"DELETE", HID_KEYBOARD_DELETE},
    {"BACKSPACE", HID_KEYPAD_BACKSPACE},
    {"END", HID_KEYBOARD_END},
    {"ESC", HID_KEYBOARD_ESCAPE},
    {"ESCAPE", HID_KEYBOARD_ESCAPE},
    {"HOME", HID_KEYBOARD_HOME},
    {"INSERT", HID_KEYBOARD_INSERT},
    {"NUMLOCK", HID_KEYPAD_NUMLOCK},
    {"PAGEUP", HID_KEYBOARD_PAGE_UP},
    {"PAGEDOWN", HID_KEYBOARD_PAGE_DOWN},
    {"PRINTSCREEN", HID_KEYBOARD_PRINT_SCREEN},
    {"SCROLLOCK", HID_KEYBOARD_SCROLL_LOCK},
    {"SPACE", HID_KEYBOARD_SPACEBAR},
    {"TAB", HID_KEYBOARD_TAB},
    {"MENU", HID_KEYBOARD_APPLICATION},
    {"APP", HID_KEYBOARD_APPLICATION},

    {"F1", HID_KEYBOARD_F1},
    {"F2", HID_KEYBOARD_F2},
    {"F3", HID_KEYBOARD_F3},
    {"F4", HID_KEYBOARD_F4},
    {"F5", HID_KEYBOARD_F5},
    {"F6", HID_KEYBOARD_F6},
    {"F7", HID_KEYBOARD_F7},
    {"F8", HID_KEYBOARD_F8},
    {"F9", HID_KEYBOARD_F9},
    {"F10", HID_KEYBOARD_F10},
    {"F11", HID_KEYBOARD_F11},
    {"F12", HID_KEYBOARD_F12},
};

static const char ducky_cmd_comment[] = {"REM"};
static const char ducky_cmd_id[] = {"ID"};
static const char ducky_cmd_delay[] = {"DELAY "};
static const char ducky_cmd_string[] = {"STRING "};
static const char ducky_cmd_defdelay_1[] = {"DEFAULT_DELAY "};
static const char ducky_cmd_defdelay_2[] = {"DEFAULTDELAY "};
static const char ducky_cmd_repeat[] = {"REPEAT "};

static const char ducky_cmd_altchar[] = {"ALTCHAR "};
static const char ducky_cmd_altstr_1[] = {"ALTSTRING "};
static const char ducky_cmd_altstr_2[] = {"ALTCODE "};

static const uint8_t numpad_keys[10] = {
    HID_KEYPAD_0,
    HID_KEYPAD_1,
    HID_KEYPAD_2,
    HID_KEYPAD_3,
    HID_KEYPAD_4,
    HID_KEYPAD_5,
    HID_KEYPAD_6,
    HID_KEYPAD_7,
    HID_KEYPAD_8,
    HID_KEYPAD_9,
};

static bool ducky_get_number(const char* param, uint32_t* val) {
    uint32_t value = 0;
    if(sscanf(param, "%lu", &value) == 1) {
        *val = value;
        return true;
    }
    return false;
}

static uint32_t ducky_get_command_len(const char* line) {
    uint32_t len = strlen(line);
    for(uint32_t i = 0; i < len; i++) {
        if(line[i] == ' ') return i;
    }
    return 0;
}

static bool ducky_is_line_end(const char chr) {
    return ((chr == ' ') || (chr == '\0') || (chr == '\r') || (chr == '\n'));
}

static void ducky_numlock_on() {
    if((furi_hal_hid_get_led_state() & HID_KB_LED_NUM) == 0) {
        furi_hal_hid_kb_press(HID_KEYBOARD_LOCK_NUM_LOCK);
        furi_hal_hid_kb_release(HID_KEYBOARD_LOCK_NUM_LOCK);
    }
}

static bool ducky_numpad_press(const char num) {
    if((num < '0') || (num > '9')) return false;

    uint16_t key = numpad_keys[num - '0'];
    furi_hal_hid_kb_press(key);
    furi_hal_hid_kb_release(key);

    return true;
}

static bool ducky_altchar(const char* charcode) {
    uint8_t i = 0;
    bool state = false;

    FURI_LOG_I(WORKER_TAG, "char %s", charcode);

    furi_hal_hid_kb_press(KEY_MOD_LEFT_ALT);

    while(!ducky_is_line_end(charcode[i])) {
        state = ducky_numpad_press(charcode[i]);
        if(state == false) break;
        i++;
    }

    furi_hal_hid_kb_release(KEY_MOD_LEFT_ALT);
    return state;
}

static bool ducky_altstring(const char* param) {
    uint32_t i = 0;
    bool state = false;

    while(param[i] != '\0') {
        if((param[i] < ' ') || (param[i] > '~')) {
            i++;
            continue; // Skip non-printable chars
        }

        char temp_str[4];
        snprintf(temp_str, 4, "%u", param[i]);

        state = ducky_altchar(temp_str);
        if(state == false) break;
        i++;
    }
    return state;
}

static bool ducky_string(PyRunnerScript* py_runner, const char* param) {
    uint32_t i = 0;
    while(param[i] != '\0') {
        uint16_t keycode = PYRUNNER_ASCII_TO_KEY(py_runner, param[i]);
        if(keycode != HID_KEYBOARD_NONE) {
            furi_hal_hid_kb_press(keycode);
            furi_hal_hid_kb_release(keycode);
        }
        i++;
    }
    return true;
}

static uint16_t ducky_get_keycode(PyRunnerScript* py_runner, const char* param, bool accept_chars) {
    for(uint8_t i = 0; i < (sizeof(ducky_keys) / sizeof(ducky_keys[0])); i++) {
        uint8_t key_cmd_len = strlen(ducky_keys[i].name);
        if((strncmp(param, ducky_keys[i].name, key_cmd_len) == 0) &&
           (ducky_is_line_end(param[key_cmd_len]))) {
            return ducky_keys[i].keycode;
        }
    }
    if((accept_chars) && (strlen(param) > 0)) {
        return (PYRUNNER_ASCII_TO_KEY(py_runner, param[0]) & 0xFF);
    }
    return 0;
}

static int32_t ducky_parse_line(PyRunnerScript* py_runner, string_t line) {
    uint32_t line_len = string_size(line);
    const char* line_tmp = string_get_cstr(line);
    bool state = false;

    for(uint32_t i = 0; i < line_len; i++) {
        if((line_tmp[i] != ' ') && (line_tmp[i] != '\t') && (line_tmp[i] != '\n')) {
            line_tmp = &line_tmp[i];
            break; // Skip spaces and tabs
        }
        if(i == line_len - 1) return SCRIPT_STATE_NEXT_LINE; // Skip empty lines
    }

    FURI_LOG_D(WORKER_TAG, "line:%s", line_tmp);

    // General commands
    if(strncmp(line_tmp, ducky_cmd_comment, strlen(ducky_cmd_comment)) == 0) {
        // REM - comment line
        return (0);
    } else if(strncmp(line_tmp, ducky_cmd_id, strlen(ducky_cmd_id)) == 0) {
        // ID - executed in ducky_script_preload
        return (0);
    } else if(strncmp(line_tmp, ducky_cmd_delay, strlen(ducky_cmd_delay)) == 0) {
        // DELAY
        line_tmp = &line_tmp[ducky_get_command_len(line_tmp) + 1];
        uint32_t delay_val = 0;
        state = ducky_get_number(line_tmp, &delay_val);
        if((state) && (delay_val > 0)) {
            return (int32_t)delay_val;
        }
        return SCRIPT_STATE_ERROR;
    } else if(
        (strncmp(line_tmp, ducky_cmd_defdelay_1, strlen(ducky_cmd_defdelay_1)) == 0) ||
        (strncmp(line_tmp, ducky_cmd_defdelay_2, strlen(ducky_cmd_defdelay_2)) == 0)) {
        // DEFAULT_DELAY
        line_tmp = &line_tmp[ducky_get_command_len(line_tmp) + 1];
        state = ducky_get_number(line_tmp, &py_runner->defdelay);
        return (state) ? (0) : SCRIPT_STATE_ERROR;
    } else if(strncmp(line_tmp, ducky_cmd_string, strlen(ducky_cmd_string)) == 0) {
        // STRING
        line_tmp = &line_tmp[ducky_get_command_len(line_tmp) + 1];
        state = ducky_string(py_runner, line_tmp);
        return (state) ? (0) : SCRIPT_STATE_ERROR;
    } else if(strncmp(line_tmp, ducky_cmd_altchar, strlen(ducky_cmd_altchar)) == 0) {
        // ALTCHAR
        line_tmp = &line_tmp[ducky_get_command_len(line_tmp) + 1];
        ducky_numlock_on();
        state = ducky_altchar(line_tmp);
        return (state) ? (0) : SCRIPT_STATE_ERROR;
    } else if(
        (strncmp(line_tmp, ducky_cmd_altstr_1, strlen(ducky_cmd_altstr_1)) == 0) ||
        (strncmp(line_tmp, ducky_cmd_altstr_2, strlen(ducky_cmd_altstr_2)) == 0)) {
        // ALTSTRING
        line_tmp = &line_tmp[ducky_get_command_len(line_tmp) + 1];
        ducky_numlock_on();
        state = ducky_altstring(line_tmp);
        return (state) ? (0) : SCRIPT_STATE_ERROR;
    } else if(strncmp(line_tmp, ducky_cmd_repeat, strlen(ducky_cmd_repeat)) == 0) {
        // REPEAT
        line_tmp = &line_tmp[ducky_get_command_len(line_tmp) + 1];
        state = ducky_get_number(line_tmp, &py_runner->repeat_cnt);
        return (state) ? (0) : SCRIPT_STATE_ERROR;
    } else {
        // Special keys + modifiers
        uint16_t key = ducky_get_keycode(py_runner, line_tmp, false);
        if(key == HID_KEYBOARD_NONE) return SCRIPT_STATE_ERROR;
        if((key & 0xFF00) != 0) {
            // It's a modifier key
            line_tmp = &line_tmp[ducky_get_command_len(line_tmp) + 1];
            key |= ducky_get_keycode(py_runner, line_tmp, true);
        }
        furi_hal_hid_kb_press(key);
        furi_hal_hid_kb_release(key);
        return (0);
    }
    return SCRIPT_STATE_ERROR;
}

static bool ducky_set_usb_id(PyRunnerScript* py_runner, const char* line) {
    if(sscanf(line, "%lX:%lX", &py_runner->hid_cfg.vid, &py_runner->hid_cfg.pid) == 2) {
        py_runner->hid_cfg.manuf[0] = '\0';
        py_runner->hid_cfg.product[0] = '\0';

        uint8_t id_len = ducky_get_command_len(line);
        if(!ducky_is_line_end(line[id_len + 1])) {
            sscanf(
                &line[id_len + 1],
                "%31[^\r\n:]:%31[^\r\n]",
                py_runner->hid_cfg.manuf,
                py_runner->hid_cfg.product);
        }
        FURI_LOG_D(
            WORKER_TAG,
            "set id: %04X:%04X mfr:%s product:%s",
            py_runner->hid_cfg.vid,
            py_runner->hid_cfg.pid,
            py_runner->hid_cfg.manuf,
            py_runner->hid_cfg.product);
        return true;
    }
    return false;
}

static bool ducky_script_preload(PyRunnerScript* py_runner, File* script_file) {
    uint8_t ret = 0;
    uint32_t line_len = 0;

    string_reset(py_runner->line);

    do {
        ret = storage_file_read(script_file, py_runner->file_buf, FILE_BUFFER_LEN);
        for(uint16_t i = 0; i < ret; i++) {
            if(py_runner->file_buf[i] == '\n' && line_len > 0) {
                py_runner->st.line_nb++;
                line_len = 0;
            } else {
                if(py_runner->st.line_nb == 0) { // Save first line
                    string_push_back(py_runner->line, py_runner->file_buf[i]);
                }
                line_len++;
            }
        }
        if(storage_file_eof(script_file)) {
            if(line_len > 0) {
                py_runner->st.line_nb++;
                break;
            }
        }
    } while(ret > 0);

    const char* line_tmp = string_get_cstr(py_runner->line);
    bool id_set = false; // Looking for ID command at first line
    if(strncmp(line_tmp, ducky_cmd_id, strlen(ducky_cmd_id)) == 0) {
        id_set = ducky_set_usb_id(py_runner, &line_tmp[strlen(ducky_cmd_id) + 1]);
    }

    if(id_set) {
        furi_check(furi_hal_usb_set_config(&usb_hid, &py_runner->hid_cfg));
    } else {
        furi_check(furi_hal_usb_set_config(&usb_hid, NULL));
    }

    storage_file_seek(script_file, 0, true);
    string_reset(py_runner->line);

    return true;
}

static int32_t ducky_script_execute_next(PyRunnerScript* py_runner, File* script_file) {
    int32_t delay_val = 0;

    if(py_runner->repeat_cnt > 0) {
        py_runner->repeat_cnt--;
        delay_val = ducky_parse_line(py_runner, py_runner->line_prev);
        if(delay_val == SCRIPT_STATE_NEXT_LINE) { // Empty line
            return 0;
        } else if(delay_val < 0) { // Script error
            py_runner->st.error_line = py_runner->st.line_cur - 1;
            FURI_LOG_E(WORKER_TAG, "Unknown command at line %lu", py_runner->st.line_cur - 1);
            return SCRIPT_STATE_ERROR;
        } else {
            return (delay_val + py_runner->defdelay);
        }
    }

    string_set(py_runner->line_prev, py_runner->line);
    string_reset(py_runner->line);

    while(1) {
        if(py_runner->buf_len == 0) {
            py_runner->buf_len = storage_file_read(script_file, py_runner->file_buf, FILE_BUFFER_LEN);
            if(storage_file_eof(script_file)) {
                if((py_runner->buf_len < FILE_BUFFER_LEN) && (py_runner->file_end == false)) {
                    py_runner->file_buf[py_runner->buf_len] = '\n';
                    py_runner->buf_len++;
                    py_runner->file_end = true;
                }
            }

            py_runner->buf_start = 0;
            if(py_runner->buf_len == 0) return SCRIPT_STATE_END;
        }
        for(uint8_t i = py_runner->buf_start; i < (py_runner->buf_start + py_runner->buf_len); i++) {
            if(py_runner->file_buf[i] == '\n' && string_size(py_runner->line) > 0) {
                py_runner->st.line_cur++;
                py_runner->buf_len = py_runner->buf_len + py_runner->buf_start - (i + 1);
                py_runner->buf_start = i + 1;
                delay_val = ducky_parse_line(py_runner, py_runner->line);
                if(delay_val < 0) {
                    py_runner->st.error_line = py_runner->st.line_cur;
                    FURI_LOG_E(WORKER_TAG, "Unknown command at line %lu", py_runner->st.line_cur);
                    return SCRIPT_STATE_ERROR;
                } else {
                    return (delay_val + py_runner->defdelay);
                }
            } else {
                string_push_back(py_runner->line, py_runner->file_buf[i]);
            }
        }
        py_runner->buf_len = 0;
        if(py_runner->file_end) return SCRIPT_STATE_END;
    }

    return 0;
}

static void py_runner_hid_state_callback(bool state, void* context) {
    furi_assert(context);
    PyRunnerScript* py_runner = context;

    if(state == true)
        furi_thread_flags_set(furi_thread_get_id(py_runner->thread), WorkerEvtConnect);
    else
        furi_thread_flags_set(furi_thread_get_id(py_runner->thread), WorkerEvtDisconnect);
}

static int32_t py_runner_worker(void* context) {
    PyRunnerScript* py_runner = context;

    PyRunnerWorkerState worker_state = PyRunnerStateInit;
    int32_t delay_val = 0;

    FuriHalUsbInterface* usb_mode_prev = furi_hal_usb_get_config();

    FURI_LOG_I(WORKER_TAG, "Init");
    File* script_file = storage_file_alloc(furi_record_open(RECORD_STORAGE));
    string_init(py_runner->line);
    string_init(py_runner->line_prev);

    furi_hal_hid_set_state_callback(py_runner_hid_state_callback, py_runner);

    while(1) {
        if(worker_state == PyRunnerStateInit) { // State: initialization
            if(storage_file_open(
                   script_file,
                   string_get_cstr(py_runner->file_path),
                   FSAM_READ,
                   FSOM_OPEN_EXISTING)) {
                if((ducky_script_preload(py_runner, script_file)) && (py_runner->st.line_nb > 0)) {
                    if(furi_hal_hid_is_connected()) {
                        worker_state = PyRunnerStateIdle; // Ready to run
                    } else {
                        worker_state = PyRunnerStateNotConnected; // USB not connected
                    }
                } else {
                    worker_state = PyRunnerStateScriptError; // Script preload error
                }
            } else {
                FURI_LOG_E(WORKER_TAG, "File open error");
                worker_state = PyRunnerStateFileError; // File open error
            }
            py_runner->st.state = worker_state;

        } else if(worker_state == PyRunnerStateNotConnected) { // State: USB not connected
            uint32_t flags = furi_thread_flags_wait(
                WorkerEvtEnd | WorkerEvtConnect, FuriFlagWaitAny, FuriWaitForever);
            furi_check((flags & FuriFlagError) == 0);
            if(flags & WorkerEvtEnd) {
                break;
            } else if(flags & WorkerEvtConnect) {
                worker_state = PyRunnerStateIdle; // Ready to run
            }
            py_runner->st.state = worker_state;

        } else if(worker_state == PyRunnerStateIdle) { // State: ready to start
            uint32_t flags = furi_thread_flags_wait(
                WorkerEvtEnd | WorkerEvtToggle | WorkerEvtDisconnect,
                FuriFlagWaitAny,
                FuriWaitForever);
            furi_check((flags & FuriFlagError) == 0);
            if(flags & WorkerEvtEnd) {
                break;
            } else if(flags & WorkerEvtToggle) { // Start executing script
                DOLPHIN_DEED(DolphinDeedPyRunnerPlayScript);
                delay_val = 0;
                py_runner->buf_len = 0;
                py_runner->st.line_cur = 0;
                py_runner->defdelay = 0;
                py_runner->repeat_cnt = 0;
                py_runner->file_end = false;
                storage_file_seek(script_file, 0, true);
                worker_state = PyRunnerStateRunning;
            } else if(flags & WorkerEvtDisconnect) {
                worker_state = PyRunnerStateNotConnected; // USB disconnected
            }
            py_runner->st.state = worker_state;

        } else if(worker_state == PyRunnerStateRunning) { // State: running
            uint16_t delay_cur = (delay_val > 1000) ? (1000) : (delay_val);
            uint32_t flags = furi_thread_flags_wait(
                WorkerEvtEnd | WorkerEvtToggle | WorkerEvtDisconnect, FuriFlagWaitAny, delay_cur);
            delay_val -= delay_cur;
            if(!(flags & FuriFlagError)) {
                if(flags & WorkerEvtEnd) {
                    break;
                } else if(flags & WorkerEvtToggle) {
                    worker_state = PyRunnerStateIdle; // Stop executing script
                    furi_hal_hid_kb_release_all();
                } else if(flags & WorkerEvtDisconnect) {
                    worker_state = PyRunnerStateNotConnected; // USB disconnected
                    furi_hal_hid_kb_release_all();
                }
                py_runner->st.state = worker_state;
                continue;
            } else if((flags == FuriFlagErrorTimeout) || (flags == FuriFlagErrorResource)) {
                if(delay_val > 0) {
                    py_runner->st.delay_remain--;
                    continue;
                }
                py_runner->st.state = PyRunnerStateRunning;
                delay_val = ducky_script_execute_next(py_runner, script_file);
                if(delay_val == SCRIPT_STATE_ERROR) { // Script error
                    delay_val = 0;
                    worker_state = PyRunnerStateScriptError;
                    py_runner->st.state = worker_state;
                } else if(delay_val == SCRIPT_STATE_END) { // End of script
                    delay_val = 0;
                    worker_state = PyRunnerStateIdle;
                    py_runner->st.state = PyRunnerStateDone;
                    furi_hal_hid_kb_release_all();
                    continue;
                } else if(delay_val > 1000) {
                    py_runner->st.state = PyRunnerStateDelay; // Show long delays
                    py_runner->st.delay_remain = delay_val / 1000;
                }
            } else {
                furi_check((flags & FuriFlagError) == 0);
            }

        } else if(
            (worker_state == PyRunnerStateFileError) ||
            (worker_state == PyRunnerStateScriptError)) { // State: error
            uint32_t flags = furi_thread_flags_wait(
                WorkerEvtEnd, FuriFlagWaitAny, FuriWaitForever); // Waiting for exit command
            furi_check((flags & FuriFlagError) == 0);
            if(flags & WorkerEvtEnd) {
                break;
            }
        }
    }

    furi_hal_hid_set_state_callback(NULL, NULL);

    furi_hal_usb_set_config(usb_mode_prev, NULL);

    storage_file_close(script_file);
    storage_file_free(script_file);
    string_clear(py_runner->line);
    string_clear(py_runner->line_prev);

    FURI_LOG_I(WORKER_TAG, "End");

    return 0;
}

static void py_runner_script_set_default_keyboard_layout(PyRunnerScript* py_runner) {
    furi_assert(py_runner);
    memset(py_runner->layout, HID_KEYBOARD_NONE, sizeof(py_runner->layout));
    memcpy(py_runner->layout, hid_asciimap, MIN(sizeof(hid_asciimap), sizeof(py_runner->layout)));
}

PyRunnerScript* py_runner_script_open(string_t file_path) {
    furi_assert(file_path);

    PyRunnerScript* py_runner = malloc(sizeof(PyRunnerScript));
    string_init(py_runner->file_path);
    string_set(py_runner->file_path, file_path);
    py_runner_script_set_default_keyboard_layout(py_runner);

    py_runner->st.state = PyRunnerStateInit;

    py_runner->thread = furi_thread_alloc();
    furi_thread_set_name(py_runner->thread, "PyRunnerWorker");
    furi_thread_set_stack_size(py_runner->thread, 2048);
    furi_thread_set_context(py_runner->thread, py_runner);
    furi_thread_set_callback(py_runner->thread, py_runner_worker);

    furi_thread_start(py_runner->thread);
    return py_runner;
}

void py_runner_script_close(PyRunnerScript* py_runner) {
    furi_assert(py_runner);
    furi_thread_flags_set(furi_thread_get_id(py_runner->thread), WorkerEvtEnd);
    furi_thread_join(py_runner->thread);
    furi_thread_free(py_runner->thread);
    string_clear(py_runner->file_path);
    free(py_runner);
}

void py_runner_script_set_keyboard_layout(PyRunnerScript* py_runner, string_t layout_path) {
    furi_assert(py_runner);

    if((py_runner->st.state == PyRunnerStateRunning) || (py_runner->st.state == PyRunnerStateDelay)) {
        // do not update keyboard layout while a script is running
        return;
    }

    File* layout_file = storage_file_alloc(furi_record_open(RECORD_STORAGE));
    if(!string_empty_p(layout_path)) {
        if(storage_file_open(
               layout_file, string_get_cstr(layout_path), FSAM_READ, FSOM_OPEN_EXISTING)) {
            uint16_t layout[128];
            if(storage_file_read(layout_file, layout, sizeof(layout)) == sizeof(layout)) {
                memcpy(py_runner->layout, layout, sizeof(layout));
            }
        }
        storage_file_close(layout_file);
    } else {
        py_runner_script_set_default_keyboard_layout(py_runner);
    }
    storage_file_free(layout_file);
}

void py_runner_script_toggle(PyRunnerScript* py_runner) {
    furi_assert(py_runner);
    furi_thread_flags_set(furi_thread_get_id(py_runner->thread), WorkerEvtToggle);
}

PyRunnerState* py_runner_script_get_state(PyRunnerScript* py_runner) {
    furi_assert(py_runner);
    return &(py_runner->st);
}
