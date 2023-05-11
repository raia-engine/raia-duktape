#include "raia_duktape.h"

#ifdef __WINDOWS__
#define RAIA_EXPORT __declspec(dllexport)
#else
#define RAIA_EXPORT
#endif

#ifdef __WINDOWS__
#define _CRT_SECURE_NO_WARNINGS
#endif

static duk_ret_t raia_core_print(duk_context *ctx) {
    int ret = puts(duk_to_string(ctx, 0));
    duk_push_number(ctx, ret);
    return 1;
}

static duk_ret_t raia_core_exit(duk_context *ctx) {
    int status = duk_to_int(ctx, 0);
    exit(status);
    return 0;
}

// lib
static duk_ret_t raia_lib_open(duk_context *ctx) {
    const char *dll_file = duk_to_string(ctx, 0);
#ifdef __WINDOWS__
    const char *extension = "dll";
#endif
#ifdef __MACOS__
    const char *extension = "dylib";
#endif
#ifdef __LINUX__
    const char *extension = "so";
#endif
    char dll_file_extension[500];
    SPRINTF(dll_file_extension, "%s.%s", dll_file, extension);
    open_plugin(dll_file_extension);
    return 0;
}

static duk_ret_t raia_lib_close(duk_context *ctx) {
    close_plugin();
    return 0;
}

static duk_ret_t raia_lib_close_all(duk_context *ctx) {
    close_all_plugin();
    return 0;
}

static duk_ret_t raia_lib_func(duk_context *ctx) {
    const char *dll_func_name = duk_to_string(ctx, 0);
    int nargs = (int) duk_to_number(ctx, 1);
    duk_ret_t (*p_func)(duk_context *ctx) = add_plugin_func(ctx, dll_func_name);
    duk_push_c_function(ctx, p_func, nargs);
    return 1;
}

static duk_ret_t raia_lib_add(duk_context *ctx) { // func_hash
    const char *dll_func_name = duk_to_string(ctx, 0);
    add_plugin_func_hash(dll_func_name);
    return 0;
}

static duk_ret_t raia_lib_call(duk_context *ctx) { // func_hash
    const char *dll_func_name = duk_to_string(ctx, 0);
    char *src; //json string
    void *data;
    duk_size_t size;
    if (duk_is_string(ctx, 1)) {
        src = (char *)duk_require_string(ctx, 1);
    } else {
        src = NULL;
    }
    if (duk_is_buffer(ctx, 2)) {
        data = (void *)duk_require_buffer_data(ctx, 2, &size);
    } else if(duk_is_pointer(ctx, 2)) {
        data = (void *)duk_require_pointer(ctx, 2);
    } else {
        data = NULL;
        size = 0;
    }
    if (duk_is_number(ctx, 3)) {
        size = (duk_size_t)duk_require_number(ctx, 3);
    }
    JSON_Value *root_value = json_parse_string(src);
    if (root_value == ((void *) 0)) {
        fprintf(__stderrp, "Error: Unable to parse JSON string.\n");
        return 0;
    }
    if (src != NULL) {
        JSON_Object *root_object = json_value_get_object(root_value);
        const char *return_type = json_object_get_string(root_object, "@return");
        if (strcmp(return_type, "pointer") == 0 && return_type != NULL) {
            void *dest = call_func_hash(dll_func_name, src, data, (int)size);
            duk_push_pointer(ctx, dest);
            return 1;
        }
    }
    char *dest = (char *)call_func_hash(dll_func_name, src, data, (int)size);
    duk_push_string(ctx, dest);
    if(dest != NULL) {
        free(dest);
    }
    return 1;
}

static void register_function(duk_context *ctx, const char *name, duk_c_function func, int nargs) {
    duk_push_c_function(ctx, func, nargs);
    duk_put_prop_string(ctx, -2, name);
}

static void register_int(duk_context *ctx, const char *name, duk_int_t val) {
    duk_push_int(ctx, val);
    duk_put_prop_string(ctx, -2, name);
}

static void register_boolean(duk_context *ctx, const char *name, duk_bool_t val) {
    duk_push_boolean(ctx, val);
    duk_put_prop_string(ctx, -2, name);
}

static void register_string(duk_context *ctx, const char *name, const char *str) {
    duk_push_string(ctx, str);
    duk_put_prop_string(ctx, -2, name);
}

typedef struct {
    int debug_mode;
    int typescript_mode;
    int es2015_mode;
    char startup_script[512];
    int preprocess;
    char preprocess_script[512];
} raia_config_t;

static raia_config_t raia_load_config(const char *json_file_name) {
    char *json_string = file_load_string(json_file_name);

    if (!json_string) {
        fprintf(stderr, "Failed to read JSON file: %s\n", json_file_name);
        exit(1);
    }

    JSON_Value *root_value = json_parse_string(json_string);
    if (!root_value) {
        fprintf(stderr, "Failed to parse JSON\n");
        free(json_string);
        exit(1);
    }

    JSON_Object *root_object = json_value_get_object(root_value);

    // JSONオブジェクトから値を取得
    raia_config_t config;
    config.debug_mode = json_object_get_boolean(root_object, "debug_mode");
    config.typescript_mode = json_object_get_boolean(root_object, "typescript_mode");
    config.es2015_mode = json_object_get_boolean(root_object, "es2015_mode");
    config.preprocess = json_object_get_boolean(root_object, "preprocess");

    const char *startup_script = json_object_get_string(root_object, "startup_script");
    STRNCPY(config.startup_script, startup_script, sizeof(config.startup_script) - 1);
    config.startup_script[sizeof(config.startup_script) - 1] = '\0';

    const char *preprocess_script = json_object_get_string(root_object, "preprocess_script");
    STRNCPY(config.preprocess_script, preprocess_script, sizeof(config.preprocess_script) - 1);
    config.preprocess_script[sizeof(config.preprocess_script) - 1] = '\0';

    json_value_free(root_value);
    free(json_string);
    return config;
}

static raia_config_t raia_set_functions(duk_context *ctx) {
    raia_config_t config = raia_load_config("raia_config.json");

    duk_idx_t raia_idx = duk_push_object(ctx);
    duk_idx_t core_idx = duk_push_object(ctx);

    duk_idx_t lib_idx = duk_push_object(ctx);
    register_function(ctx, "open", raia_lib_open, 1);
    register_function(ctx, "close", raia_lib_close, 0);
    register_function(ctx, "closeAll", raia_lib_close_all, 0);
    register_function(ctx, "func", raia_lib_func, 2);
    // func_hash
    register_function(ctx, "add", raia_lib_add, 1);
    register_function(ctx, "call", raia_lib_call, 4);

    duk_put_prop_string(ctx, core_idx, "Lib");

    register_function(ctx, "print", raia_core_print, 1);
    register_function(ctx, "exit", raia_core_exit, 1);
    duk_put_prop_string(ctx, raia_idx, "Core");

    register_boolean(ctx, "debug_mode", config.debug_mode);
    register_boolean(ctx, "typescript_mode", config.typescript_mode);
    register_boolean(ctx, "es2015_mode", config.es2015_mode);
    register_boolean(ctx, "preprocess", config.preprocess);
    register_string(ctx, "startup_script", config.startup_script);
    register_string(ctx, "preprocess_script", config.preprocess_script);

    duk_put_global_string(ctx, "__Raia__");
    return config;
}

RAIA_EXPORT int run(int argc, char *argv[]) {
    init_plugin_loader();
    init_func_hash();
    duk_context *ctx = duk_create_heap_default();
    duk_module_duktape_init(ctx);
    raia_config_t config = raia_set_functions(ctx);
    if (config.preprocess == 1) {
        char *preprocess_script = file_load_string(config.preprocess_script);
        duk_eval_string(ctx, preprocess_script);
    }
    char *startup_script = file_load_string(config.startup_script); //ファイルを読み込む
    duk_eval_string(ctx, startup_script);
    duk_destroy_heap(ctx);
    return 0;
}