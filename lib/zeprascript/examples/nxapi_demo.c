/*
 * ZebraScript Example Application
 * 
 * Demonstrates using the shared JS engine with NXAPI bindings.
 * 
 * Copyright (c) 2025 KetiveeAI. All Rights Reserved.
 */

#include <stdio.h>
#include "../include/zeprascript.h"
#include "../bindings/nxapi/nxapi.h"

/* Example JS code that uses NXAPI */
static const char *example_code = 
    "// Get system info\n"
    "var info = NX.System.getInfo();\n"
    "console.log('OS: ' + info.osName + ' ' + info.osVersion);\n"
    "console.log('Hostname: ' + info.hostname);\n"
    "console.log('CPUs: ' + info.cpuCount);\n"
    "\n"
    "// Generate UUID\n"
    "var uuid = NX.Crypto.uuid();\n"
    "console.log('Generated UUID: ' + uuid);\n"
    "\n"
    "// Check if file exists\n"
    "var exists = NX.FS.exists('/System/Library/Fonts');\n"
    "console.log('Font dir exists: ' + exists);\n";

/* Native console.log implementation */
static zs_value *js_console_log(zs_context *ctx, zs_value *this_val,
                                 int argc, zs_value **argv) {
    (void)this_val;
    
    for (int i = 0; i < argc; i++) {
        const char *str = zs_to_string(ctx, argv[i]);
        printf("%s", str);
        if (i < argc - 1) printf(" ");
    }
    printf("\n");
    
    return zs_undefined(ctx);
}

int main(void) {
    printf("ZebraScript Example Application\n");
    printf("================================\n\n");
    
    /* Create engine */
    zs_engine *engine = zs_engine_create();
    if (!engine) {
        fprintf(stderr, "Failed to create JS engine\n");
        return 1;
    }
    
    printf("Engine version: %s\n", zs_engine_version());
    
    /* Create context */
    zs_context *ctx = zs_context_create(engine);
    if (!ctx) {
        fprintf(stderr, "Failed to create JS context\n");
        zs_engine_destroy(engine);
        return 1;
    }
    
    /* Create console object */
    zs_object *console = zs_object_create(ctx);
    zs_set_property(ctx, console, "log", 
        zs_function_create(ctx, js_console_log, "log", 1));
    zs_set_property(ctx, zs_context_global(ctx), "console", (zs_value *)console);
    
    /* Initialize NXAPI */
    nxapi_init(ctx);
    printf("NXAPI initialized\n\n");
    
    /* Show what we're running */
    printf("Running JS code:\n");
    printf("----------------\n");
    printf("%s\n", example_code);
    printf("----------------\n");
    printf("Output:\n\n");
    
    /* Execute code */
    zs_value *result = zs_eval(ctx, example_code);
    
    /* Check for errors */
    if (zs_has_exception(ctx)) {
        zs_value *ex = zs_get_exception(ctx);
        printf("Error: %s\n", zs_to_string(ctx, ex));
    }
    
    (void)result;
    
    /* Cleanup */
    printf("\n");
    zs_context_destroy(ctx);
    zs_engine_destroy(engine);
    
    printf("Example completed successfully!\n");
    return 0;
}
