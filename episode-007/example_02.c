#include <hardware/dmabits.h>
#include <hardware/intbits.h>
#include <graphics/gfxbase.h>
#include <devices/input.h>

#include <clib/exec_protos.h>
#include <clib/graphics_protos.h>
#include <clib/intuition_protos.h>
#include <clib/alib_protos.h>

#include <stdio.h>

/*
 * This example demonstrates playing sounds on all 4 channels and queuing up
 * sounds using audio interrupts
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
    handler_info.is_Node.ln_Name = "ex02";
    input_io->io_Command = IND_ADDHANDLER;
    input_io->io_Data = (APTR) &handler_info;
    DoIO((struct IORequest *) input_io);
    return 1;
}

// just an empty buffer to point the sound hardware to for silence
#define SILENCE_DATA_SIZE (32)
static UBYTE __chip silence_data[SILENCE_DATA_SIZE * 2];

/*
 * Interrupt handlers.
 */
void audio0_int_handler(void)
{
    custom.aud[0].ac_ptr = (UWORD *) silence_data;
    custom.aud[0].ac_len = SILENCE_DATA_SIZE;
    custom.intreq = INTF_AUD0;
}
#define COWBELL_FILE "sr22.05k/cowbell.raw8"
#define COWBELL_DATA_BYTES (10747)
static UBYTE __chip cowbell_data[COWBELL_DATA_BYTES];
static int audio1_counter = 0;
void audio1_int_handler(void)
{
    if (audio1_counter == 0) {
        custom.aud[1].ac_ptr = (UWORD *) cowbell_data;
        custom.aud[1].ac_len = COWBELL_DATA_BYTES / 2;
    }
    if (audio1_counter >= 4) {
        custom.aud[1].ac_ptr = (UWORD *) silence_data;
        custom.aud[1].ac_len = SILENCE_DATA_SIZE;
    }
    audio1_counter++;
    custom.intreq = INTF_AUD1;
}

void audio2_int_handler(void)
{
    custom.aud[2].ac_ptr = (UWORD *) silence_data;
    custom.aud[2].ac_len = SILENCE_DATA_SIZE;
    custom.intreq = INTF_AUD2;
}

void audio3_int_handler(void)
{
    // we need to reset the interrupt request bits in our handler !!!
    custom.aud[3].ac_ptr = (UWORD *) silence_data;
    custom.aud[3].ac_len = SILENCE_DATA_SIZE;
    custom.intreq = INTF_AUD3;
}

static struct Interrupt *old_audio0_interrupt, *old_audio1_interrupt,
    *old_audio2_interrupt, *old_audio3_interrupt;
UWORD old_intena;

void install_audio_interrupts(void)
{
    old_intena = custom.intenar;
    // disable and clear any outstanding audio interrupts
    custom.intena = INTF_AUD0 | INTF_AUD1 | INTF_AUD2 | INTF_AUD3;
    custom.intreq = INTF_AUD0 | INTF_AUD1 | INTF_AUD2 | INTF_AUD3;

    struct Interrupt audio_interrupt;
    audio_interrupt.is_Node.ln_Type = NT_INTERRUPT;
    audio_interrupt.is_Node.ln_Pri = 0;
    audio_interrupt.is_Data = 0;

    audio_interrupt.is_Code = (APTR) audio0_int_handler;
    old_audio0_interrupt = SetIntVector(INTB_AUD0, &audio_interrupt);
    audio_interrupt.is_Code = (APTR) audio1_int_handler;
    old_audio1_interrupt = SetIntVector(INTB_AUD1, &audio_interrupt);
    audio_interrupt.is_Code = (APTR) audio2_int_handler;
    old_audio2_interrupt = SetIntVector(INTB_AUD2, &audio_interrupt);
    audio_interrupt.is_Code = (APTR) audio3_int_handler;
    old_audio3_interrupt = SetIntVector(INTB_AUD3, &audio_interrupt);

    // enable audio interrupts
    custom.intena = INTF_SETCLR | INTF_AUD0 | INTF_AUD1 | INTF_AUD2 | INTF_AUD3;
}

static void uninstall_audio_interrupts(void)
{
    // Clear interrupts again and reset to old handler
    custom.intena = INTF_AUD0 | INTF_AUD1 | INTF_AUD2 | INTF_AUD3;
    custom.intreq = INTF_AUD0 | INTF_AUD1 | INTF_AUD2 | INTF_AUD3;

    SetIntVector(INTB_AUD0, old_audio0_interrupt);
    SetIntVector(INTB_AUD1, old_audio1_interrupt);
    SetIntVector(INTB_AUD2, old_audio2_interrupt);
    SetIntVector(INTB_AUD3, old_audio3_interrupt);

    custom.intena = INTF_SETCLR | old_intena;
}

// Assuming a 22.05k sample rate
#define SOUND_DATA_BYTES (35041)
#define SOUND_DATA_SIZE (SOUND_DATA_BYTES / 2)
#define MAX_VOLUME (64)
#define SAMPLE_PERIOD_NTSC (162)
#define SAMPLE_PERIOD_PAL (161)

static UBYTE __chip sound0_data[SOUND_DATA_BYTES];
static UBYTE __chip sound1_data[SOUND_DATA_BYTES];
static UBYTE __chip sound2_data[SOUND_DATA_BYTES];
static UBYTE __chip sound3_data[SOUND_DATA_BYTES];
static const char * sound_files[] = {
    "sr22.05k/ac_track0.raw8", "sr22.05k/ac_track1.raw8", "sr22.05k/ac_track2.raw8",
    "sr22.05k/ac_track3.raw8"
};
static UBYTE *sound_data[] = { sound0_data, sound1_data, sound2_data, sound3_data };

int main(int argc, char **argv)
{
    if (!setup_input_handler()) {
        puts("Could not initialize input handler");
        return 1;
    }
    install_audio_interrupts();

    BOOL is_pal = (((struct GfxBase *) GfxBase)->DisplayFlags & PAL) == PAL;
    FILE *fp;
    int bytes_read;
    fp = fopen(COWBELL_FILE, "rb");
    bytes_read = fread(cowbell_data, sizeof(UBYTE), COWBELL_DATA_BYTES, fp);
    fclose(fp);
    for (int i = 0; i < 4; i++) {
        fp = fopen(sound_files[i], "rb");
        bytes_read = fread(sound_data[i], sizeof(UBYTE), SOUND_DATA_BYTES, fp);
        fclose(fp);

        custom.aud[i].ac_ptr = (UWORD *) sound_data[i];
        custom.aud[i].ac_len = SOUND_DATA_SIZE;
        custom.aud[i].ac_vol = MAX_VOLUME;
        custom.aud[i].ac_per = is_pal ? SAMPLE_PERIOD_PAL : SAMPLE_PERIOD_NTSC;
    }
    custom.dmacon = DMAF_SETCLR | DMAF_AUD0 | DMAF_AUD1 | DMAF_AUD2 | DMAF_AUD3;

    // the event loop
    while (!should_exit) {
        wait_vblank();
    }
    uninstall_audio_interrupts();
    // deactivate sound DMA for all channels
    custom.dmacon = DMAF_AUD0 | DMAF_AUD1 | DMAF_AUD2 | DMAF_AUD3;
    cleanup_input_handler();
    return 0;
}
