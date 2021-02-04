#include <hardware/dmabits.h>
#include <graphics/gfxbase.h>
#include <devices/input.h>

#include <clib/exec_protos.h>
#include <clib/graphics_protos.h>
#include <clib/intuition_protos.h>
#include <clib/alib_protos.h>

#include <stdio.h>

/*
 * This example demonstrates switching between sounds and interrupting the
 * previously playing sound
 */
extern struct GfxBase *GfxBase;
extern struct Custom custom;

// To handle input
static struct MsgPort *input_mp;
static struct IOStdReq *input_io;
static struct Interrupt handler_info;
static int should_exit;

// These are 22.05k samples
#define SOUND1_FILE "sr22.05k/strat_powerchord.raw8"
#define SOUND1_DATA_BYTES (14715)
#define SOUND2_FILE "sr22.05k/only_amiga.raw8"
#define SOUND2_DATA_BYTES (21264)
#define SOUND3_FILE "sr22.05k/cowbell.raw8"
#define SOUND3_DATA_BYTES (10747)
#define SOUND4_FILE "sr7k/bass.raw8"
#define SOUND4_DATA_BYTES (2551)
#define SOUND5_FILE "sr22.05k/otomatone.raw8"
#define SOUND5_DATA_BYTES (18132)
#define SOUND6_FILE "sr22.05k/welcome.raw8"
#define SOUND6_DATA_BYTES (51325)

// NTSC: 1 / (sample rate * 2.79365 * 10^-7)
// PAL 1 / (sample rate * 2.81937 * 10^-7)
#define SAMPLE_PERIOD_22_05K_NTSC (162)
#define SAMPLE_PERIOD_22_05K_PAL (161)
#define SAMPLE_PERIOD_14K_NTSC (256)
#define SAMPLE_PERIOD_14K_PAL (253)
#define SAMPLE_PERIOD_7K_NTSC (511)
#define SAMPLE_PERIOD_7K_PAL (507)
#define MAX_VOLUME (64)

static UBYTE __chip sound1_data[SOUND1_DATA_BYTES];
static UBYTE __chip sound2_data[SOUND2_DATA_BYTES];
static UBYTE __chip sound3_data[SOUND3_DATA_BYTES];
static UBYTE __chip sound4_data[SOUND4_DATA_BYTES];
static UBYTE __chip sound5_data[SOUND5_DATA_BYTES];
static UBYTE __chip sound6_data[SOUND6_DATA_BYTES];

UWORD sample_periods_pal[] = {SAMPLE_PERIOD_22_05K_PAL, SAMPLE_PERIOD_14K_PAL, SAMPLE_PERIOD_7K_PAL};
UWORD sample_periods_ntsc[] = {SAMPLE_PERIOD_22_05K_NTSC, SAMPLE_PERIOD_14K_NTSC,
    SAMPLE_PERIOD_7K_NTSC};

enum SampleRate {SAMPLE_RATE_22_05K = 0, SAMPLE_RATE_14K, SAMPLE_RATE_7K};

struct SoundData {
    const char *path;
    UBYTE __chip *data;
    UWORD num_bytes;
    int sample_rate;
} sounds[] = {
    { SOUND1_FILE, sound1_data, SOUND1_DATA_BYTES, SAMPLE_RATE_22_05K },
    { SOUND2_FILE, sound2_data, SOUND2_DATA_BYTES, SAMPLE_RATE_22_05K },
    { SOUND3_FILE, sound3_data, SOUND3_DATA_BYTES, SAMPLE_RATE_22_05K },
    { SOUND4_FILE, sound4_data, SOUND4_DATA_BYTES, SAMPLE_RATE_7K },
    { SOUND5_FILE, sound5_data, SOUND5_DATA_BYTES, SAMPLE_RATE_22_05K },
    { SOUND6_FILE, sound6_data, SOUND6_DATA_BYTES, SAMPLE_RATE_22_05K }
};

#define NUM_SOUNDS (6)
static int next_sound = 1;
static BOOL go_next_sound = FALSE;

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
        } else if (result->ie_Code == IECODE_RBUTTON) {
            go_next_sound = TRUE; // indicate we want to switch sounds
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
    handler_info.is_Node.ln_Name = "ex03";
    input_io->io_Command = IND_ADDHANDLER;
    input_io->io_Data = (APTR) &handler_info;
    DoIO((struct IORequest *) input_io);
    return 1;
}

void play_next_sound(BOOL is_pal)
{
    // stop DMA, shorten sample period and set volume to 0
    custom.dmacon = DMAF_AUD0;
    custom.aud[0].ac_per = 1;
    custom.aud[0].ac_vol = 0;

    // wait at least 2 sample periods, we wait for 6 lines here
    LONG vpos = VBeamPos();
    while (VBeamPos() - vpos < 6) ;

    // now we switch to the next sound
    custom.aud[0].ac_ptr = (UWORD *) sounds[next_sound].data;
    custom.aud[0].ac_len = sounds[next_sound].num_bytes / 2;
    custom.aud[0].ac_per = is_pal ? sample_periods_pal[sounds[next_sound].sample_rate] :
        sample_periods_ntsc[sounds[next_sound].sample_rate];
    custom.aud[0].ac_vol = MAX_VOLUME;

    // start fetching data again
    custom.dmacon = DMAF_SETCLR | DMAF_AUD0;

    next_sound++;
    next_sound %= NUM_SOUNDS;
    go_next_sound = FALSE;
}

int main(int argc, char **argv)
{
    if (!setup_input_handler()) {
        puts("Could not initialize input handler");
        return 1;
    }
    custom.dmacon = DMAF_AUD0;
    BOOL is_pal = (((struct GfxBase *) GfxBase)->DisplayFlags & PAL) == PAL;
    FILE *fp;
    int bytes_read;
    for (int i = 0; i < NUM_SOUNDS; i++) {
        fp = fopen(sounds[i].path, "rb");
        bytes_read = fread(sounds[i].data, sizeof(UBYTE), sounds[i].num_bytes, fp);
        fclose(fp);
    }
    custom.aud[0].ac_ptr = (UWORD *) sounds[0].data;
    custom.aud[0].ac_len = sounds[0].num_bytes / 2;
    custom.aud[0].ac_per = is_pal ? sample_periods_pal[sounds[0].sample_rate] :
       sample_periods_ntsc[sounds[0].sample_rate];
    custom.aud[0].ac_vol = MAX_VOLUME;

    custom.dmacon = DMAF_SETCLR | DMAF_AUD0;

    // the event loop
    while (!should_exit) {
        if (go_next_sound) {
          play_next_sound(is_pal);
        }
        WaitTOF();
    }
    // stop audio channel 0
    custom.dmacon = DMAF_AUD0;
    cleanup_input_handler();
    return 0;
}
