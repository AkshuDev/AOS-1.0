#include <inttypes.h>
#include <asm.h>

#include <inc/drivers/gpu/apis/pyrion.h>
#include <inc/core/vshell.h>
#include <inc/drivers/io/io.h>

static struct pyrion_ctx* vshell_ctx = NULL;
static struct pyrion_rect vshell_viewport = (struct pyrion_rect){.x=0,.y=0,.width=500,.height=300,.color=0x121212FF};
static struct pyrion_ctx* gdisplay_ctx = NULL;

void start_vshell(struct pyrion_ctx* display_ctx) {
    serial_print("Starting VShell...\n");
    gdisplay_ctx = display_ctx;
    vshell_ctx = pyrion_create_ctx();
    if (!vshell_ctx) return;

    pyrion_viewport(vshell_ctx, &vshell_viewport);
    pyrion_builtin_print(vshell_ctx, "Welcome to AOS++ Visible Shell!");
    pyrion_flush(vshell_ctx);
}
