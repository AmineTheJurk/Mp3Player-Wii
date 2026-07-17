#include <grrlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>
#include <time.h>
#include <math.h>
#include <gccore.h>
#include <wiiuse/wpad.h>
#include <fat.h>
#include <asndlib.h>
#include <mp3player.h>

// Assets intégrés dans le .dol
#include "background_png.h"
#include "note_png.h"
#include "bubble_png.h"
#include "comicsansms_ttf.h"

#define MAX_SONGS 100
#define MUSIC_DIR "sd:/Music"
#define BUBBLE_COUNT 25

typedef struct {
    float x, y;
    float speed;
    float scale;
} Bubble;

char song_list[MAX_SONGS][256];
int song_count = 0;
int selected_song = 0;
bool is_playing = false;
u32 start_time = 0;
u32 total_duration = 0;
Bubble bubbles[BUBBLE_COUNT];

struct mad_stream;
struct mad_frame;
void dummy_filter(struct mad_stream *stream, struct mad_frame *frame) {}

static s32 mp3_reader(void *cb_data, void *buffer, s32 length) {
    return fread(buffer, 1, length, (FILE *)cb_data);
}

// Estimation de la durée
u32 estimate_duration(const char* filename) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", MUSIC_DIR, filename);
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fclose(f);
    return (u32)(size / 16000);
}

void scan_music_folder() {
    DIR *dir = opendir(MUSIC_DIR);
    song_count = 0;
    if (dir != NULL) {
        struct dirent *ent;
        while ((ent = readdir(dir)) != NULL && song_count < MAX_SONGS) {
            if (strstr(ent->d_name, ".mp3") || strstr(ent->d_name, ".MP3")) {
                snprintf(song_list[song_count], sizeof(song_list[song_count]), "%s", ent->d_name);
                song_count++;
            }
        }
        closedir(dir);
    }
}

void init_bubbles() {
    for (int i = 0; i < BUBBLE_COUNT; i++) {
        bubbles[i].x = rand() % 640;
        bubbles[i].y = rand() % 480;
        bubbles[i].speed = (rand() % 100) / 80.0f + 0.5f;
        bubbles[i].scale = (rand() % 100) / 250.0f + 0.1f;
    }
}

void update_bubbles() {
    for (int i = 0; i < BUBBLE_COUNT; i++) {
        bubbles[i].y -= bubbles[i].speed;
        bubbles[i].x += sin(bubbles[i].y * 0.03f) * 0.3f;
        if (bubbles[i].y < -64) {
            bubbles[i].y = 480 + 64;
            bubbles[i].x = rand() % 640;
        }
    }
}

int main(int argc, char **argv) {
    GRRLIB_Init();
    WPAD_Init();
    srand(time(NULL));

    if (!fatInitDefault()) {
        GRRLIB_Exit();
        return 0;
    }

    ASND_Init();
    ASND_Pause(0);
    MP3Player_Init();

    // Chargement des textures
    GRRLIB_texImg *tex_bg = GRRLIB_LoadTexture(background_png);
    GRRLIB_texImg *tex_note = GRRLIB_LoadTexture(note_png);
    GRRLIB_texImg *tex_bubble = GRRLIB_LoadTexture(bubble_png);

    // On centre les images
    if(tex_note) GRRLIB_SetMidHandle(tex_note, true);
    if(tex_bubble) GRRLIB_SetMidHandle(tex_bubble, true);

    GRRLIB_ttfFont *font_comic = GRRLIB_LoadTTF(comicsansms_ttf, comicsansms_ttf_size);

    scan_music_folder();
    init_bubbles();

    while (1) {
        WPAD_ScanPads();
        u32 pressed = WPAD_ButtonsDown(0);

        if (pressed & WPAD_BUTTON_HOME) break;

        if (song_count > 0) {
            if (pressed & (WPAD_BUTTON_RIGHT | WPAD_BUTTON_LEFT)) {
                if (pressed & WPAD_BUTTON_RIGHT) selected_song = (selected_song + 1) % song_count;
                if (pressed & WPAD_BUTTON_LEFT) selected_song = (selected_song - 1 + song_count) % song_count;
            }

            if (pressed & WPAD_BUTTON_A) {
                if (is_playing) MP3Player_Stop();
                char path[512];
                snprintf(path, sizeof(path), "%s/%s", MUSIC_DIR, song_list[selected_song]);
                FILE *f = fopen(path, "rb");
                if (f) {
                    MP3Player_PlayFile(f, mp3_reader, dummy_filter);
                    is_playing = true;
                    start_time = time(NULL);
                    total_duration = estimate_duration(song_list[selected_song]);
                }
            }
            if (pressed & WPAD_BUTTON_B) {
                MP3Player_Stop();
                is_playing = false;
            }
        }

        update_bubbles();

        // --- RENDERING ---

        // 1. Fond d'écran (Dessiné en 0,0 sans échelle si 640x480)
        if (tex_bg) {
            GRRLIB_DrawImg(0, 0, tex_bg, 0, 1, 1, 0xFFFFFFFF);
        } else {
            GRRLIB_FillScreen(0x0055FFFF);
        }

        // 2. Bulles animées
        if (tex_bubble) {
            for (int i = 0; i < BUBBLE_COUNT; i++) {
                GRRLIB_DrawImg(bubbles[i].x, bubbles[i].y, tex_bubble, 0, bubbles[i].scale, bubbles[i].scale, 0xFFFFFF88);
            }
        }

        if (song_count > 0) {
            // 3. Icône de Note au centre
            if (tex_note) {
                GRRLIB_DrawImg(320, 200, tex_note, 0, 1, 1, 0xFFFFFFFF);
            }

            // 4. Titre de la chanson
            char track_text[300];
            snprintf(track_text, sizeof(track_text), "%s", song_list[selected_song]);
            f32 tw = GRRLIB_WidthTTF(font_comic, track_text, 24);
            GRRLIB_PrintfTTF((640 - tw) / 2, 320, font_comic, track_text, 24, 0xFFFFFFFF);

            // 5. Chronomètre (0:00 / 0:00)
            if (is_playing) {
                u32 elapsed = time(NULL) - start_time;
                char time_text[100];
                snprintf(time_text, sizeof(time_text), "%d:%02d / %d:%02d",
                        (int)(elapsed / 60), (int)(elapsed % 60),
                        (int)(total_duration / 60), (int)(total_duration % 60));
                f32 timew = GRRLIB_WidthTTF(font_comic, time_text, 32);
                GRRLIB_PrintfTTF((640 - timew) / 2, 360, font_comic, time_text, 32, 0xFFFFFFFF);
            }
        }

        // HUD
        GRRLIB_PrintfTTF(60, 440, font_comic, "(A) Play  (B) Stop  (Left/Right) Select  (Home) Exit", 18, 0xFFFFFFFF);

        GRRLIB_Render();
    }

    MP3Player_Stop();
    if(tex_bg) GRRLIB_FreeTexture(tex_bg);
    if(tex_note) GRRLIB_FreeTexture(tex_note);
    if(tex_bubble) GRRLIB_FreeTexture(tex_bubble);
    if(font_comic) GRRLIB_FreeTTF(font_comic);
    GRRLIB_Exit();
    return 0;
}
