#include <hardware/dmabits.h>
#include <graphics/gfxbase.h>
#include <devices/input.h>

#include <clib/exec_protos.h>
#include <clib/graphics_protos.h>
#include <clib/intuition_protos.h>
#include <clib/alib_protos.h>
#include <stdio.h>

/*
 * This example demonstrates playing a simple sound.
 */
extern struct GfxBase *GfxBase;
extern struct Custom custom;

static volatile ULONG *custom_vposr = (volatile ULONG *) 0xdff004;

// Wait for this position for vertical blank
// translated from http://eab.abime.net/showthread.php?t=51928
static vb_waitpos;
static void wait_vblank(void)
{
    while (((*custom_vposr) & 0x1ff00) != (vb_waitpos<<8)) ;
}

// To handle input
static struct MsgPort *input_mp;
static struct IOStdReq *input_io;
static struct Interrupt handler_info;
static int should_exit;

static struct InputEvent *my_input_handler(__reg("a0") struct InputEvent *event,
                                           __reg("a1") APTR handler_data)
{
    struct InputEvent *result = event, *prev = NULL;

    Forbid();
    // Intercept all raw mouse events before they reach Intuition, ignore
    // everything else
    if (result->ie_Class == IECLASS_RAWMOUSE) {
        if (result->ie_Code == IECODE_LBUTTON) {
            should_exit = 1;
        }
        return NULL;
    }
    Permit();
    return result;
}

static void cleanup_input_handler(void)
{
    if (input_io) {
        // remove our input handler from the chain
        input_io->io_Command = IND_REMHANDLER;
        input_io->io_Data = (APTR) &handler_info;
        DoIO((struct IORequest *) input_io);

        if (!(CheckIO((struct IORequest *) input_io))) AbortIO((struct IORequest *) input_io);
        WaitIO((struct IORequest *) input_io);
        CloseDevice((struct IORequest *) input_io);
        DeleteExtIO((struct IORequest *) input_io);
    }
    if (input_mp) DeletePort(input_mp);
}

static int setup_input_handler(void)
{
    input_mp = CreatePort(0, 0);
    input_io = (struct IOStdReq *) CreateExtIO(input_mp, sizeof(struct IOStdReq));
    BYTE error = OpenDevice("input.device", 0L, (struct IORequest *) input_io, 0);

    handler_info.is_Code = (void (*)(void)) my_input_handler;
    handler_info.is_Data = NULL;
    handler_info.is_Node.ln_Pri = 100;
    handler_info.is_Node.ln_Name = "ex01";
    input_io->io_Command = IND_ADDHANDLER;
    input_io->io_Data = (APTR) &handler_info;
    DoIO((struct IORequest *) input_io);
    return 1;
}

// These are 22.05k samples
#define SOUND_FILE "sr22.05k/cowbell.raw8"
#define SOUND_DATA_BYTES (10747)
#define SAMPLE_PERIOD_NTSC (162)
#define SAMPLE_PERIOD_PAL (161)
#define SOUND_DATA_SIZE (SOUND_DATA_BYTES / 2)
#define MAX_VOLUME (64)

static UBYTE __chip sound_data[SOUND_DATA_BYTES];

int main(int argc, char **argv)
{
    if (!setup_input_handler()) {
        puts("Could not initialize input handler");
        return 1;
    }
    BOOL is_pal = (((struct GfxBase *) GfxBase)->DisplayFlags & PAL) == PAL;
    FILE *fp = fopen(SOUND_FILE, "rb");
    int bytes_read = fread(sound_data, sizeof(UBYTE), SOUND_DATA_BYTES, fp);
    fclose(fp);

    custom.aud[0].ac_ptr = (UWORD *) sound_data;
    custom.aud[0].ac_len = SOUND_DATA_SIZE;
    custom.aud[0].ac_vol = MAX_VOLUME;
    custom.aud[0].ac_per = is_pal ? SAMPLE_PERIOD_PAL : SAMPLE_PERIOD_NTSC;
    custom.dmacon = (DMAF_SETCLR | DMAF_AUD0);

    // the event loop
    while (!should_exit) {
        wait_vblank();
    }
    custom.dmacon = DMAF_AUD0;
    cleanup_input_handler();
    return 0;
}
