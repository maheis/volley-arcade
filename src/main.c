#include <SDL2/SDL.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define WINDOW_WIDTH 1024
#define WINDOW_HEIGHT 600

#define COURT_MIN_X 0.0f
#define COURT_MAX_X ((float)WINDOW_WIDTH)
#define NET_X 512.0f
#define NET_TOP_Y 320.0f
#define FLOOR_Y 560.0f

#define PLAYER_W 26.0f
#define PLAYER_H 160.0f
#define HEAD_RADIUS 20.0f
#define HAND_HIT_RADIUS 10.0f
#define ARM_THICKNESS 12
#define HAND_OVAL_RX 12
#define HAND_OVAL_RY 8
#define PLAYER_SPEED 300.0f
#define PLAYER_BLOCK_TIME 0.16f
#define PLAYER_BLOCK_TIME_MIN 0.08f
#define PLAYER_JUMP_SPEED -525.0f
#define PLAYER_GRAVITY 1080.0f
#define PLAYER_JUMP_HOLD_TIME 0.16f
#define PLAYER_JUMP_HOLD_GRAVITY_MULT 0.46f
#define PLAYER_JUMP_RELEASE_GRAVITY_MULT 1.72f
#define BLOCK_JUMP_NET_DISTANCE 50.0f
#define CPU_BLOCK_TIME 0.14f
#define CPU_JUMP_SPEED -430.0f
#define CPU_SPEED 260.0f
#define CPU_HIT_COOLDOWN 0.12f

#define BALL_RADIUS 20.0f
#define GRAVITY 860.0f
#define RETURN_SPEED_X 270.0f
#define RETURN_SPEED_Y -520.0f
#define POINTS_TO_WIN_SET 25
#define MIN_LEAD_TO_WIN_SET 2
#define MAX_TOUCHES_PER_SIDE 3
#define TOUCH_MIN_INTERVAL 0.5f
#define TOUCH_MIN_RISE_PX 30.0f
#define SERVE_CHARGE_MAX_TIME 1.2f
#define START_BANNER_FADE_TIME 1.2f

#define FIXED_DT (1.0f / 120.0f)
#define MAX_ACCUMULATED_TIME 0.25f

#define AUDIO_SAMPLE_RATE 22050
#define AUDIO_AMPLITUDE 1800
#define MAX_HIGHSCORES 7
#define HIGHSCORE_NAME_LEN 10
#define HIGHSCORE_REL_DIR "volley-arcade"
#define HIGHSCORE_FILENAME "highscores.dat"

typedef struct Vec2
{
    float x;
    float y;
} Vec2;

typedef struct Ball
{
    Vec2 pos;
    Vec2 vel;
} Ball;

typedef struct Player
{
    float x;
    float y;
    float vy;
    float jumpHoldTimer;
    bool onGround;
    bool isBlocking;
    float blockTimer;
} Player;

typedef struct Audio
{
    SDL_AudioDeviceID device;
    SDL_AudioSpec spec;
    bool muted;
} Audio;

typedef struct HighscoreEntry
{
    char name[HIGHSCORE_NAME_LEN + 1];
    int setsFor;
    int setsAgainst;
    int pointsFor;
    int pointsAgainst;
} HighscoreEntry;

typedef enum Scene
{
    SCENE_START = 0,
    SCENE_PLAYING = 1,
    SCENE_PAUSED = 2,
    SCENE_GAME_OVER = 3,
    SCENE_NAME_ENTRY = 4
} Scene;

enum GameEvent
{
    EVENT_NONE = 0,
    EVENT_WALL_BOUNCE = 1 << 0,
    EVENT_BLOCK_SUCCESS = 1 << 1,
    EVENT_PLAYER_RECEIVE = 1 << 2,
    EVENT_CPU_RETURN = 1 << 3,
    EVENT_MISS = 1 << 4,
    EVENT_POINT = 1 << 5,
    EVENT_GAME_OVER = 1 << 6
};

typedef struct Game
{
    Ball ball;
    Player player;
    Player cpu;
    float cpuHitCooldown;
    float elapsedSeconds;
    float blockWindow;
    float cpuSpeedScale;
    int level;
    int playerPoints;
    int cpuPoints;
    int playerSets;
    int cpuSets;
    int highscoreSets;
    int touchesLeft;
    int touchesRight;
    int cpuServeVariant;
    int lastTouchSide;
    int ballSide;
    int serverSide;
    int difficulty;
    bool twoPlayerMode;
    bool waitingServe;
    float cpuServeTimer;
    bool serveCharging;
    float serveCharge;
    bool serveOutOnMax;
    bool touchGateActive;
    bool touchRiseReached;
    float touchTimer;
    float touchStartY;
    float startBannerFade;
} Game;

static void draw_filled_circle(SDL_Renderer *renderer, int cx, int cy, int radius);

static float clampf(float value, float min, float max)
{
    if (value < min)
    {
        return min;
    }
    if (value > max)
    {
        return max;
    }
    return value;
}

static bool predict_ball_x_at_y(const Ball *ball, float gravity, float targetY, float *outX)
{
    float a = 0.5f * gravity;
    float b = ball->vel.y;
    float c = ball->pos.y - targetY;
    float disc = b * b - 4.0f * a * c;
    float t = -1.0f;

    if (disc < 0.0f || fabsf(a) < 0.0001f)
    {
        return false;
    }

    {
        float sqrtDisc = sqrtf(disc);
        float t1 = (-b - sqrtDisc) / (2.0f * a);
        float t2 = (-b + sqrtDisc) / (2.0f * a);

        if (t1 > 0.0f && t2 > 0.0f)
        {
            t = t1 < t2 ? t1 : t2;
        }
        else if (t1 > 0.0f)
        {
            t = t1;
        }
        else if (t2 > 0.0f)
        {
            t = t2;
        }
    }

    if (t <= 0.0f)
    {
        return false;
    }

    *outX = ball->pos.x + ball->vel.x * t;
    return true;
}

static bool predict_ball_y_at_x(const Ball *ball, float gravity, float targetX, float *outY)
{
    if (fabsf(ball->vel.x) < 0.001f)
    {
        return false;
    }

    {
        float t = (targetX - ball->pos.x) / ball->vel.x;
        if (t <= 0.0f)
        {
            return false;
        }

        *outY = ball->pos.y + ball->vel.y * t + 0.5f * gravity * t * t;
        return true;
    }
}

static void get_highscore_dir_path(char *out, size_t outSize)
{
    const char *xdgConfigHome = getenv("XDG_CONFIG_HOME");
    const char *home = getenv("HOME");

    if (outSize == 0)
    {
        return;
    }

    if (xdgConfigHome && xdgConfigHome[0] != '\0')
    {
        snprintf(out, outSize, "%s/%s", xdgConfigHome, HIGHSCORE_REL_DIR);
        return;
    }

    if (home && home[0] != '\0')
    {
        snprintf(out, outSize, "%s/.config/%s", home, HIGHSCORE_REL_DIR);
        return;
    }

    out[0] = '\0';
}

static void get_highscore_file_path(char *out, size_t outSize)
{
    char dirPath[512];

    if (outSize == 0)
    {
        return;
    }

    get_highscore_dir_path(dirPath, sizeof(dirPath));
    if (dirPath[0] == '\0')
    {
        snprintf(out, outSize, "%s", HIGHSCORE_FILENAME);
        return;
    }

    snprintf(out, outSize, "%s/%s", dirPath, HIGHSCORE_FILENAME);
}

static void ensure_directory_recursive(const char *path)
{
    char tmp[512];
    size_t len;

    if (!path || path[0] == '\0')
    {
        return;
    }

    len = strlen(path);
    if (len == 0 || len >= sizeof(tmp))
    {
        return;
    }

    snprintf(tmp, sizeof(tmp), "%s", path);

    for (char *p = tmp + 1; *p != '\0'; ++p)
    {
        if (*p == '/')
        {
            *p = '\0';
            (void)mkdir(tmp, 0700);
            *p = '/';
        }
    }

    (void)mkdir(tmp, 0700);
}

static void ensure_highscore_directory(void)
{
    char dirPath[512];
    get_highscore_dir_path(dirPath, sizeof(dirPath));
    ensure_directory_recursive(dirPath);
}

static int load_highscores(HighscoreEntry *entries, int maxEntries)
{
    char path[640];
    get_highscore_file_path(path, sizeof(path));
    FILE *f = fopen(path, "r");
    int count = 0;

    if (!f)
    {
        return 0;
    }

    while (count < maxEntries)
    {
        HighscoreEntry e;
        int parsed = fscanf(f, "%10s %d %d %d %d", e.name, &e.setsFor, &e.setsAgainst, &e.pointsFor, &e.pointsAgainst);
        if (parsed < 2)
        {
            break;
        }
        if (parsed == 2)
        {
            e.setsAgainst = 0;
            e.pointsFor = 0;
            e.pointsAgainst = 0;
        }
        else if (parsed == 3)
        {
            e.setsAgainst = 0;
            e.pointsAgainst = 0;
        }
        else if (parsed == 4)
        {
            e.pointsAgainst = 0;
        }
        if (e.setsFor < 0)
        {
            e.setsFor = 0;
        }
        if (e.setsAgainst < 0)
        {
            e.setsAgainst = 0;
        }
        if (e.pointsFor < 0)
        {
            e.pointsFor = 0;
        }
        if (e.pointsAgainst < 0)
        {
            e.pointsAgainst = 0;
        }
        entries[count++] = e;
    }

    fclose(f);
    return count;
}

static void save_highscores(const HighscoreEntry *entries, int count)
{
    char path[640];

    ensure_highscore_directory();
    get_highscore_file_path(path, sizeof(path));

    FILE *f = fopen(path, "w");
    if (!f)
    {
        return;
    }

    for (int i = 0; i < count; ++i)
    {
        fprintf(
            f,
            "%s %d %d %d %d\n",
            entries[i].name,
            entries[i].setsFor,
            entries[i].setsAgainst,
            entries[i].pointsFor,
            entries[i].pointsAgainst);
    }
    fclose(f);
}

static bool highscore_better_than(
    int setsFor,
    int setsAgainst,
    int pointsFor,
    int pointsAgainst,
    const HighscoreEntry *entry)
{
    int setDiff = setsFor - setsAgainst;
    int entrySetDiff = entry->setsFor - entry->setsAgainst;
    int pointDiff = pointsFor - pointsAgainst;
    int entryPointDiff = entry->pointsFor - entry->pointsAgainst;

    if (setsFor != entry->setsFor)
    {
        return setsFor > entry->setsFor;
    }
    if (setDiff != entrySetDiff)
    {
        return setDiff > entrySetDiff;
    }
    if (pointsFor != entry->pointsFor)
    {
        return pointsFor > entry->pointsFor;
    }
    return pointDiff > entryPointDiff;
}

static void insert_highscore(
    HighscoreEntry *entries,
    int *count,
    const char *name,
    int setsFor,
    int setsAgainst,
    int pointsFor,
    int pointsAgainst)
{
    int n = *count;
    if (setsFor < 0)
    {
        setsFor = 0;
    }
    if (setsAgainst < 0)
    {
        setsAgainst = 0;
    }
    if (pointsFor < 0)
    {
        pointsFor = 0;
    }
    if (pointsAgainst < 0)
    {
        pointsAgainst = 0;
    }

    if (n < MAX_HIGHSCORES)
    {
        entries[n].setsFor = setsFor;
        entries[n].setsAgainst = setsAgainst;
        entries[n].pointsFor = pointsFor;
        entries[n].pointsAgainst = pointsAgainst;
        snprintf(entries[n].name, sizeof(entries[n].name), "%s", name);
        n += 1;
    }
    else if (highscore_better_than(setsFor, setsAgainst, pointsFor, pointsAgainst, &entries[n - 1]))
    {
        entries[n - 1].setsFor = setsFor;
        entries[n - 1].setsAgainst = setsAgainst;
        entries[n - 1].pointsFor = pointsFor;
        entries[n - 1].pointsAgainst = pointsAgainst;
        snprintf(entries[n - 1].name, sizeof(entries[n - 1].name), "%s", name);
    }
    else
    {
        *count = n;
        return;
    }

    for (int i = 0; i < n; ++i)
    {
        for (int j = i + 1; j < n; ++j)
        {
            if (highscore_better_than(
                    entries[j].setsFor,
                    entries[j].setsAgainst,
                    entries[j].pointsFor,
                    entries[j].pointsAgainst,
                    &entries[i]))
            {
                HighscoreEntry tmp = entries[i];
                entries[i] = entries[j];
                entries[j] = tmp;
            }
        }
    }

    if (n > MAX_HIGHSCORES)
    {
        n = MAX_HIGHSCORES;
    }
    *count = n;
}

static char scancode_to_name_char(SDL_Scancode sc)
{
    if (sc >= SDL_SCANCODE_A && sc <= SDL_SCANCODE_Z)
    {
        return (char)('A' + (sc - SDL_SCANCODE_A));
    }
    if (sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_9)
    {
        return (char)('1' + (sc - SDL_SCANCODE_1));
    }
    if (sc == SDL_SCANCODE_0)
    {
        return '0';
    }
    return '\0';
}

static void refresh_difficulty(Game *game)
{
    int level = 1 + (int)(game->elapsedSeconds / 60.0f);
    if (level > 8)
    {
        level = 8;
    }

    game->level = level;
    game->cpuSpeedScale = 1.0f * (1.0f + 0.08f * (float)(level - 1));

    {
        float tightened = PLAYER_BLOCK_TIME - 0.01f * (float)(level - 1);
        game->blockWindow = clampf(tightened, PLAYER_BLOCK_TIME_MIN, PLAYER_BLOCK_TIME);
    }
}

static void queue_tone(Audio *audio, float frequency, int durationMs, float volumeScale)
{
    if (audio->device == 0)
    {
        return;
    }

    int samples = (AUDIO_SAMPLE_RATE * durationMs) / 1000;
    if (samples <= 0)
    {
        return;
    }

    int16_t *buffer = (int16_t *)malloc((size_t)samples * sizeof(int16_t));
    if (!buffer)
    {
        return;
    }

    float phase = 0.0f;
    float step = frequency / (float)AUDIO_SAMPLE_RATE;
    float amp = (float)AUDIO_AMPLITUDE * volumeScale;

    for (int i = 0; i < samples; ++i)
    {
        float env = 1.0f - ((float)i / (float)samples);
        float sample = (phase < 0.5f ? 1.0f : -1.0f) * amp * env;
        buffer[i] = (int16_t)sample;
        phase += step;
        if (phase >= 1.0f)
        {
            phase -= 1.0f;
        }
    }

    SDL_QueueAudio(audio->device, buffer, (Uint32)((size_t)samples * sizeof(int16_t)));
    free(buffer);
}

static void queue_whistle(Audio *audio, int durationMs, float volumeScale)
{
    if (audio->device == 0)
    {
        return;
    }

    int samples = (AUDIO_SAMPLE_RATE * durationMs) / 1000;
    if (samples <= 0)
    {
        return;
    }

    int16_t *buffer = (int16_t *)malloc((size_t)samples * sizeof(int16_t));
    if (!buffer)
    {
        return;
    }

    float phaseMain = 0.0f;
    float phaseHarm = 0.0f;
    float phaseAir = 0.0f;
    float amp = (float)AUDIO_AMPLITUDE * volumeScale;

    for (int i = 0; i < samples; ++i)
    {
        float t = (float)i / (float)AUDIO_SAMPLE_RATE;
        float norm = (float)i / (float)samples;
        float attack = clampf(norm / 0.08f, 0.0f, 1.0f);
        float release = clampf((1.0f - norm) / 0.25f, 0.0f, 1.0f);
        float envelope = attack * release;
        float vibrato = sinf(2.0f * 3.14159265f * 9.0f * t);
        float freq = 2150.0f + 140.0f * vibrato;
        float stepMain = freq / (float)AUDIO_SAMPLE_RATE;
        float stepHarm = (freq * 2.0f) / (float)AUDIO_SAMPLE_RATE;
        float stepAir = 4800.0f / (float)AUDIO_SAMPLE_RATE;
        float main = sinf(2.0f * 3.14159265f * phaseMain);
        float harm = sinf(2.0f * 3.14159265f * phaseHarm);
        float air = sinf(2.0f * 3.14159265f * phaseAir + 1.7f * phaseMain);
        float sample = (main * 0.78f + harm * 0.16f + air * 0.06f) * amp * envelope;

        if (sample > 32767.0f)
        {
            sample = 32767.0f;
        }
        else if (sample < -32768.0f)
        {
            sample = -32768.0f;
        }
        buffer[i] = (int16_t)sample;

        phaseMain += stepMain;
        phaseHarm += stepHarm;
        phaseAir += stepAir;

        if (phaseMain >= 1.0f)
        {
            phaseMain -= 1.0f;
        }
        if (phaseHarm >= 1.0f)
        {
            phaseHarm -= 1.0f;
        }
        if (phaseAir >= 1.0f)
        {
            phaseAir -= 1.0f;
        }
    }

    SDL_QueueAudio(audio->device, buffer, (Uint32)((size_t)samples * sizeof(int16_t)));
    free(buffer);
}

static void play_events(Audio *audio, unsigned events)
{
    if (audio->muted)
    {
        return;
    }

    if (events & EVENT_BLOCK_SUCCESS)
    {
        queue_tone(audio, 880.0f, 55, 1.0f);
        queue_tone(audio, 1040.0f, 45, 0.9f);
    }
    if (events & EVENT_CPU_RETURN)
    {
        queue_tone(audio, 660.0f, 45, 0.75f);
    }
    if (events & EVENT_PLAYER_RECEIVE)
    {
        queue_tone(audio, 420.0f, 40, 0.65f);
    }
    if (events & EVENT_WALL_BOUNCE)
    {
        queue_tone(audio, 320.0f, 20, 0.5f);
    }
    if (events & EVENT_MISS)
    {
        queue_tone(audio, 170.0f, 100, 1.0f);
    }
    if (events & EVENT_POINT)
    {
        queue_whistle(audio, 160, 1.35f);
    }
}

static float point_segment_distance_sq(float px, float py, float ax, float ay, float bx, float by)
{
    float abx = bx - ax;
    float aby = by - ay;
    float apx = px - ax;
    float apy = py - ay;
    float abLen2 = abx * abx + aby * aby;
    float t;

    if (abLen2 <= 0.0001f)
    {
        float dx = px - ax;
        float dy = py - ay;
        return dx * dx + dy * dy;
    }

    t = (apx * abx + apy * aby) / abLen2;
    t = clampf(t, 0.0f, 1.0f);

    {
        float cx = ax + t * abx;
        float cy = ay + t * aby;
        float dx = px - cx;
        float dy = py - cy;
        return dx * dx + dy * dy;
    }
}

static void closest_point_on_segment(float px, float py, float ax, float ay, float bx, float by, float *outX, float *outY)
{
    float abx = bx - ax;
    float aby = by - ay;
    float apx = px - ax;
    float apy = py - ay;
    float abLen2 = abx * abx + aby * aby;
    float t = 0.0f;

    if (abLen2 > 0.0001f)
    {
        t = (apx * abx + apy * aby) / abLen2;
        t = clampf(t, 0.0f, 1.0f);
    }

    *outX = ax + t * abx;
    *outY = ay + t * aby;
}

static void compute_arm_pose(
    float bodyX,
    float bodyY,
    bool towardRight,
    bool armsUp,
    bool nearNet,
    float *shoulderX,
    float *shoulderY,
    float *handX,
    float *handY)
{
    float dir = towardRight ? 1.0f : -1.0f;
    float sx = bodyX + PLAYER_W * 0.5f + dir * 3.0f;
    float sy = bodyY + 50.0f;
    float hx;
    float hy;

    if (armsUp)
    {
        if (nearNet)
        {
            /* Blocksprung at the net: hands fully extended above the head. */
            hx = sx + dir * 4.0f;
            hy = sy - 100.0f;
        }
        else
        {
            /* Pritschen farther back: raised, but not full block reach. */
            hx = sx + dir * 16.0f;
            hy = sy - 72.0f;
        }
    }
    else
    {
        hx = sx + dir * 34.0f;
        hy = sy + 36.0f;
    }

    *shoulderX = sx;
    *shoulderY = sy;
    *handX = hx;
    *handY = hy;
}

static void draw_rotated_filled_ellipse(SDL_Renderer *renderer, int cx, int cy, int rx, int ry, float angleRad)
{
    if (rx <= 0 || ry <= 0)
    {
        return;
    }

    {
        int r = rx > ry ? rx : ry;
        float c = cosf(angleRad);
        float s = sinf(angleRad);
        float invRx2 = 1.0f / ((float)rx * (float)rx);
        float invRy2 = 1.0f / ((float)ry * (float)ry);

        for (int dy = -r; dy <= r; ++dy)
        {
            for (int dx = -r; dx <= r; ++dx)
            {
                float ux = c * (float)dx + s * (float)dy;
                float uy = -s * (float)dx + c * (float)dy;
                float eq = ux * ux * invRx2 + uy * uy * invRy2;
                if (eq <= 1.0f)
                {
                    SDL_RenderDrawPoint(renderer, cx + dx, cy + dy);
                }
            }
        }
    }
}

static void draw_thick_segment(SDL_Renderer *renderer, float ax, float ay, float bx, float by, int thickness)
{
    float dx = bx - ax;
    float dy = by - ay;
    float len = sqrtf(dx * dx + dy * dy);
    int radius = thickness / 2;
    int steps;

    if (len < 0.001f)
    {
        draw_filled_circle(renderer, (int)ax, (int)ay, radius);
        return;
    }

    steps = (int)len;
    if (steps < 1)
    {
        steps = 1;
    }

    for (int i = 0; i <= steps; ++i)
    {
        float t = (float)i / (float)steps;
        float px = ax + dx * t;
        float py = ay + dy * t;
        draw_filled_circle(renderer, (int)px, (int)py, radius);
    }
}

static void draw_arm_with_hand(
    SDL_Renderer *renderer,
    float shoulderX,
    float shoulderY,
    float handX,
    float handY,
    bool towardRight,
    SDL_Color armColor,
    SDL_Color handColor)
{
    float armAngle = atan2f(handY - shoulderY, handX - shoulderX);
    float handAngle = armAngle + (towardRight ? 0.4363323f : -0.4363323f);
    float handCx = handX + cosf(armAngle) * 4.0f;
    float handCy = handY + sinf(armAngle) * 4.0f;

    SDL_SetRenderDrawColor(renderer, armColor.r, armColor.g, armColor.b, armColor.a);
    draw_thick_segment(renderer, shoulderX, shoulderY, handX, handY, ARM_THICKNESS);

    SDL_SetRenderDrawColor(renderer, handColor.r, handColor.g, handColor.b, handColor.a);
    draw_rotated_filled_ellipse(renderer, (int)handCx, (int)handCy, HAND_OVAL_RX, HAND_OVAL_RY, handAngle);
}

static bool reflect_ball_on_head_zone(
    Ball *ball,
    float prevX,
    float prevY,
    float headX,
    float headY,
    bool towardRight,
    float hitterVy,
    bool hitterOnGround,
    bool allowBackHeadHit)
{
    float sumR = BALL_RADIUS + HEAD_RADIUS;
    float sumR2 = sumR * sumR;
    float dist2 = point_segment_distance_sq(headX, headY, prevX, prevY, ball->pos.x, ball->pos.y);

    if (dist2 > sumR2)
    {
        return false;
    }

    {
        float nx = ball->pos.x - headX;
        float ny = ball->pos.y - headY;
        float nLen2 = nx * nx + ny * ny;
        float nLen;

        if (nLen2 < 0.0001f)
        {
            nx = towardRight ? 1.0f : -1.0f;
            ny = -0.1f;
            nLen = sqrtf(nx * nx + ny * ny);
        }
        else
        {
            nLen = sqrtf(nLen2);
        }

        nx /= nLen;
        ny /= nLen;

        ball->pos.x = headX + nx * (sumR + 1.0f);
        ball->pos.y = headY + ny * (sumR + 1.0f);
    }

    {
        float hitT = (ball->pos.x - (headX - HEAD_RADIUS)) / (2.0f * HEAD_RADIUS);
        float frontness;
        float vxMag;
        float vyMag;
        float jumpBoost;
        float upwardFactor;
        bool backHeadHit;

        hitT = clampf(hitT, 0.0f, 1.0f);

        /* Front edge toward opponent: flatter/faster; back edge: shorter/higher. */
        frontness = towardRight ? hitT : (1.0f - hitT);
        vxMag = 250.0f + 215.0f * frontness;
        vyMag = 375.0f + 245.0f * frontness;

        /* Airborne contacts are stronger; rising jump gives an extra punch. */
        jumpBoost = 1.0f;
        if (!hitterOnGround)
        {
            upwardFactor = clampf((-hitterVy) / 560.0f, 0.0f, 1.0f);
            jumpBoost = 1.02f + 0.07f * upwardFactor;
            vxMag *= jumpBoost;
            vyMag *= jumpBoost;
        }

        /* Rear quarter of the head sends the ball backwards. */
        backHeadHit = allowBackHeadHit && (frontness < 0.25f);

        if (backHeadHit)
        {
            ball->vel.x = towardRight ? -vxMag : vxMag;
        }
        else
        {
            ball->vel.x = towardRight ? vxMag : -vxMag;
        }
        ball->vel.y = -vyMag;
    }

    return true;
}

static bool reflect_ball_on_hand_zone(
    Ball *ball,
    float prevX,
    float prevY,
    float shoulderX,
    float shoulderY,
    float handX,
    float handY,
    bool towardRight,
    float hitterVy,
    bool hitterOnGround)
{
    float sumR = BALL_RADIUS + HAND_HIT_RADIUS;
    float sumR2 = sumR * sumR;
    float handSweepDist2 = point_segment_distance_sq(handX, handY, prevX, prevY, ball->pos.x, ball->pos.y);
    float armDistNow2 = point_segment_distance_sq(ball->pos.x, ball->pos.y, shoulderX, shoulderY, handX, handY);
    float armDistPrev2 = point_segment_distance_sq(prevX, prevY, shoulderX, shoulderY, handX, handY);
    float handNowDx = ball->pos.x - handX;
    float handNowDy = ball->pos.y - handY;
    float handNowDist2 = handNowDx * handNowDx + handNowDy * handNowDy;
    bool handHit = handSweepDist2 <= sumR2;
    bool armHit = armDistNow2 <= sumR2 || armDistPrev2 <= sumR2;
    float contactX = handX;
    float contactY = handY;

    if (!handHit && !armHit)
    {
        return false;
    }

    if (armHit && armDistNow2 < handNowDist2)
    {
        closest_point_on_segment(ball->pos.x, ball->pos.y, shoulderX, shoulderY, handX, handY, &contactX, &contactY);
    }

    {
        float nx = ball->pos.x - contactX;
        float ny = ball->pos.y - contactY;
        float nLen2 = nx * nx + ny * ny;
        float nLen;

        if (nLen2 < 0.0001f)
        {
            nx = towardRight ? 1.0f : -1.0f;
            ny = -0.18f;
            nLen = sqrtf(nx * nx + ny * ny);
        }
        else
        {
            nLen = sqrtf(nLen2);
        }

        nx /= nLen;
        ny /= nLen;
        ball->pos.x = contactX + nx * (sumR + 1.0f);
        ball->pos.y = contactY + ny * (sumR + 1.0f);
    }

    {
        float handHit = (ball->pos.y - (contactY - HAND_HIT_RADIUS)) / (2.0f * HAND_HIT_RADIUS);
        float topHit;
        float vxMag;
        float vyMag;
        float jumpBoost = 1.0f;

        handHit = clampf(handHit, 0.0f, 1.0f);
        topHit = 1.0f - handHit;

        vxMag = 250.0f + 215.0f * topHit;
        vyMag = 375.0f + 245.0f * topHit;

        if (!hitterOnGround)
        {
            float upwardFactor = clampf((-hitterVy) / 560.0f, 0.0f, 1.0f);
            jumpBoost = 1.02f + 0.07f * upwardFactor;
            vxMag *= jumpBoost;
            vyMag *= jumpBoost;
        }

        ball->vel.x = towardRight ? vxMag : -vxMag;
        ball->vel.y = -vyMag;
    }

    return true;
}

static void place_ball_in_server_hand(Game *game)
{
    if (game->serverSide < 0)
    {
        float hx = game->player.x + PLAYER_W * 0.5f;
        float hy = game->player.y;
        game->ball.pos.x = hx + HEAD_RADIUS + BALL_RADIUS - 3.0f;
        game->ball.pos.y = hy - (HEAD_RADIUS + BALL_RADIUS - 3.0f);
    }
    else
    {
        float hx = game->cpu.x + PLAYER_W * 0.5f;
        float hy = game->cpu.y;
        game->ball.pos.x = hx - (HEAD_RADIUS + BALL_RADIUS - 3.0f);
        game->ball.pos.y = hy - (HEAD_RADIUS + BALL_RADIUS - 3.0f);
    }
    game->ball.vel.x = 0.0f;
    game->ball.vel.y = 0.0f;
}

static void start_serve(Game *game, int side)
{
    int v;
    float serveScale = 1.0f;
    float xScale = 1.0f;
    float yScale = 1.0f;

    if (!game->waitingServe || game->serverSide != side)
    {
        return;
    }

    game->waitingServe = false;
    game->touchesLeft = 0;
    game->touchesRight = 0;
    game->ballSide = side;
    game->lastTouchSide = side;

    if (side < 0)
    {
        game->ball.vel.x = 360.0f;
        game->ball.vel.y = -430.0f;
    }
    else
    {
        serveScale = clampf(game->cpuSpeedScale, 0.90f, 1.12f);
        xScale = 0.90f;
        yScale = 1.02f;

        /* Cycle through a few serve trajectories so CPU serves are varied and net-safe. */
        v = game->cpuServeVariant % 4;
        if (v == 0)
        {
            game->ball.vel.x = -560.0f * serveScale * xScale;
            game->ball.vel.y = -430.0f * serveScale * yScale;
        }
        else if (v == 1)
        {
            game->ball.vel.x = -520.0f * serveScale * xScale;
            game->ball.vel.y = -500.0f * serveScale * yScale;
        }
        else if (v == 2)
        {
            game->ball.vel.x = -620.0f * serveScale * xScale;
            game->ball.vel.y = -390.0f * serveScale * yScale;
        }
        else
        {
            game->ball.vel.x = -540.0f * serveScale * xScale;
            game->ball.vel.y = -460.0f * serveScale * yScale;
        }
        game->cpuServeVariant = (game->cpuServeVariant + 1) % 4;
    }

    game->touchGateActive = false;
    game->touchRiseReached = true;
    game->touchTimer = TOUCH_MIN_INTERVAL;
    game->serveCharging = false;
    game->serveCharge = 0.0f;
    game->serveOutOnMax = false;
}

static void start_human_charged_serve(Game *game, int side)
{
    float c;
    float jumpHeight;
    float jumpInfluence;
    float hitterY;
    float hitterVy;
    float vx;

    if (!game->waitingServe || game->serverSide != side)
    {
        return;
    }

    c = clampf(game->serveCharge, 0.0f, 1.0f);

    game->waitingServe = false;
    game->touchesLeft = 0;
    game->touchesRight = 0;
    game->ballSide = side;
    game->lastTouchSide = side;

    if (side < 0)
    {
        hitterY = game->player.y;
        hitterVy = game->player.vy;
    }
    else
    {
        hitterY = game->cpu.y;
        hitterVy = game->cpu.vy;
    }

    game->serveOutOnMax = (c >= 0.999f);
    if (game->serveOutOnMax)
    {
        game->ball.vel.x = (side < 0) ? 760.0f : -760.0f;
        game->ball.vel.y = -120.0f;
    }
    else
    {
        jumpHeight = (FLOOR_Y - PLAYER_H) - hitterY;
        jumpInfluence = clampf(jumpHeight / 90.0f, 0.0f, 1.0f);

        /* CTRL sets distance; optional jump timing nudges range and arc shape. */
        vx = 300.0f + 340.0f * c + 90.0f * jumpInfluence;
        game->ball.vel.x = (side < 0) ? vx : -vx;
        game->ball.vel.y = -(560.0f - 170.0f * c) + 0.25f * hitterVy - 45.0f * jumpInfluence;
    }

    game->touchGateActive = false;
    game->touchRiseReached = true;
    game->touchTimer = TOUCH_MIN_INTERVAL;
    game->serveCharging = false;
    game->serveCharge = 0.0f;
}

static void reset_rally(Game *game)
{
    game->player.x = COURT_MIN_X + 40.0f;
    game->player.y = FLOOR_Y - PLAYER_H;
    game->player.vy = 0.0f;
    game->player.jumpHoldTimer = 0.0f;
    game->player.onGround = true;
    game->player.isBlocking = false;
    game->player.blockTimer = 0.0f;

    game->cpu.x = COURT_MAX_X - PLAYER_W - 24.0f;
    game->cpu.y = FLOOR_Y - PLAYER_H;
    game->cpu.vy = 0.0f;
    game->cpu.jumpHoldTimer = 0.0f;
    game->cpu.onGround = true;
    game->cpu.isBlocking = false;
    game->cpu.blockTimer = 0.0f;
    game->cpuHitCooldown = 0.0f;

    game->touchesLeft = 0;
    game->touchesRight = 0;
    game->ballSide = game->serverSide;
    game->lastTouchSide = 0;
    game->waitingServe = true;
    game->cpuServeTimer = 0.55f;
    game->serveCharging = false;
    game->serveCharge = 0.0f;
    game->serveOutOnMax = false;
    game->touchGateActive = false;
    game->touchRiseReached = true;
    game->touchTimer = TOUCH_MIN_INTERVAL;

    place_ball_in_server_hand(game);
    game->touchStartY = game->ball.pos.y;
}

static bool can_touch_ball(const Game *game)
{
    if (!game->touchGateActive)
    {
        return true;
    }
    return game->touchTimer >= TOUCH_MIN_INTERVAL && game->touchRiseReached;
}

static bool should_rebound_on_back_wall(const Game *game, int wallSide, float ballY)
{
    float courtTopY = 40.0f;
    float middleY = courtTopY + (FLOOR_Y - courtTopY) * 0.5f;
    float lowerQuarterY = FLOOR_Y - (FLOOR_Y - courtTopY) * 0.25f;
    bool ownSide = (game->lastTouchSide == wallSide);
    bool opponentSide = (game->lastTouchSide == -wallSide);

    if (game->difficulty <= 0)
    {
        /* Easy: all back-wall contacts rebound, never out. */
        return true;
    }

    if (game->difficulty >= 2)
    {
        /* Hard: no back-wall rebounds, always out. */
        return false;
    }

    /* Normal: own side rebounds only in upper half. */
    if (ownSide && ballY < middleY)
    {
        return true;
    }

    /* Normal: opponent blasting into lower quarter is always out. */
    if (opponentSide && ballY >= lowerQuarterY)
    {
        return false;
    }

    return false;
}

static void register_ball_touch(Game *game)
{
    game->touchGateActive = true;
    game->touchRiseReached = false;
    game->touchTimer = 0.0f;
    game->touchStartY = game->ball.pos.y;
}

static void award_point(Game *game, bool playerWon, unsigned *events)
{
    *events |= EVENT_POINT;

    if (playerWon)
    {
        game->playerPoints += 1;
        game->serverSide = -1;
    }
    else
    {
        game->cpuPoints += 1;
        game->serverSide = 1;
        *events |= EVENT_MISS;
    }

    if ((game->playerPoints >= POINTS_TO_WIN_SET || game->cpuPoints >= POINTS_TO_WIN_SET) &&
        abs(game->playerPoints - game->cpuPoints) >= MIN_LEAD_TO_WIN_SET)
    {
        if (game->playerPoints > game->cpuPoints)
        {
            game->playerSets += 1;
            if (game->playerSets > game->highscoreSets)
            {
                game->highscoreSets = game->playerSets;
            }
        }
        else
        {
            game->cpuSets += 1;
        }

        game->playerPoints = 0;
        game->cpuPoints = 0;
    }

    reset_rally(game);
}

static void init_game(Game *game, bool twoPlayerMode, int difficulty)
{
    game->twoPlayerMode = twoPlayerMode;
    if (difficulty < 0)
    {
        game->difficulty = 0;
    }
    else if (difficulty > 2)
    {
        game->difficulty = 2;
    }
    else
    {
        game->difficulty = difficulty;
    }
    game->playerPoints = 0;
    game->cpuPoints = 0;
    game->playerSets = 0;
    game->cpuSets = 0;
    game->cpuServeVariant = 0;
    game->elapsedSeconds = 0.0f;
    game->level = 1;
    game->blockWindow = PLAYER_BLOCK_TIME;
    game->cpuSpeedScale = 1.0f;
    game->serverSide = 1;
    game->startBannerFade = START_BANNER_FADE_TIME;
    refresh_difficulty(game);
    reset_rally(game);
}

static unsigned update_game(Game *game, float dt, const Uint8 *keys)
{
    unsigned events = EVENT_NONE;
    float aiFactor = 1.0f;
    bool shouldCpuBlock = false;
    bool shouldCpuJump = false;
    float cpuHeadX;
    float cpuHeadY;
    float playerHeadX;
    float playerHeadY;
    float playerShoulderX;
    float playerShoulderY;
    float playerHandX;
    float playerHandY;
    bool playerNearNet;
    float cpuShoulderX;
    float cpuShoulderY;
    float cpuHandX;
    float cpuHandY;
    bool cpuNearNet;
    bool playerJumpHeld;
    bool rightJumpHeld;
    float playerGravityScale;
    float rightGravityScale;

    if (game->startBannerFade > 0.0f)
    {
        game->startBannerFade -= dt;
        if (game->startBannerFade < 0.0f)
        {
            game->startBannerFade = 0.0f;
        }
    }

    game->elapsedSeconds += dt;

    game->touchTimer += dt;
    if (game->touchGateActive && !game->touchRiseReached)
    {
        if ((game->touchStartY - game->ball.pos.y) >= TOUCH_MIN_RISE_PX)
        {
            game->touchRiseReached = true;
        }
    }
    refresh_difficulty(game);

    {
        bool playerMoveLeft = keys[SDL_SCANCODE_A] || (!game->twoPlayerMode && keys[SDL_SCANCODE_LEFT]);
        bool playerMoveRight = keys[SDL_SCANCODE_D] || (!game->twoPlayerMode && keys[SDL_SCANCODE_RIGHT]);
        bool rightMoveLeft = game->twoPlayerMode && keys[SDL_SCANCODE_LEFT];
        bool rightMoveRight = game->twoPlayerMode && keys[SDL_SCANCODE_RIGHT];

        if (game->waitingServe)
        {
            if (game->serverSide < 0)
            {
                /* Left server cannot move forward toward the net (to the right). */
                playerMoveRight = false;
            }
            else if (game->serverSide > 0)
            {
                /* Right server cannot move forward toward the net (to the left). */
                rightMoveLeft = false;
            }
        }

        if (playerMoveLeft)
        {
            game->player.x -= PLAYER_SPEED * dt;
        }
        if (playerMoveRight)
        {
            game->player.x += PLAYER_SPEED * dt;
        }

        if (rightMoveLeft)
        {
            game->cpu.x -= PLAYER_SPEED * dt;
        }
        if (rightMoveRight)
        {
            game->cpu.x += PLAYER_SPEED * dt;
        }
    }

    game->player.x = clampf(game->player.x, COURT_MIN_X, NET_X - PLAYER_W - 6.0f);
    game->cpu.x = clampf(game->cpu.x, NET_X + 8.0f, COURT_MAX_X - PLAYER_W);

    playerJumpHeld = keys[SDL_SCANCODE_W] || (!game->twoPlayerMode && keys[SDL_SCANCODE_UP]);
    playerGravityScale = 1.0f;
    if (!game->player.onGround && game->player.vy < 0.0f)
    {
        if (playerJumpHeld && game->player.jumpHoldTimer > 0.0f)
        {
            playerGravityScale = PLAYER_JUMP_HOLD_GRAVITY_MULT;
            game->player.jumpHoldTimer -= dt;
            if (game->player.jumpHoldTimer < 0.0f)
            {
                game->player.jumpHoldTimer = 0.0f;
            }
        }
        else
        {
            playerGravityScale = PLAYER_JUMP_RELEASE_GRAVITY_MULT;
        }
    }

    game->player.vy += PLAYER_GRAVITY * playerGravityScale * dt;
    game->player.y += game->player.vy * dt;
    if (game->player.y >= FLOOR_Y - PLAYER_H)
    {
        game->player.y = FLOOR_Y - PLAYER_H;
        game->player.vy = 0.0f;
        game->player.jumpHoldTimer = 0.0f;
        game->player.onGround = true;
    }

    rightJumpHeld = game->twoPlayerMode && keys[SDL_SCANCODE_UP];
    rightGravityScale = 1.0f;
    if (game->twoPlayerMode && !game->cpu.onGround && game->cpu.vy < 0.0f)
    {
        if (rightJumpHeld && game->cpu.jumpHoldTimer > 0.0f)
        {
            rightGravityScale = PLAYER_JUMP_HOLD_GRAVITY_MULT;
            game->cpu.jumpHoldTimer -= dt;
            if (game->cpu.jumpHoldTimer < 0.0f)
            {
                game->cpu.jumpHoldTimer = 0.0f;
            }
        }
        else
        {
            rightGravityScale = PLAYER_JUMP_RELEASE_GRAVITY_MULT;
        }
    }

    game->cpu.vy += PLAYER_GRAVITY * rightGravityScale * dt;
    game->cpu.y += game->cpu.vy * dt;
    if (game->cpu.y >= FLOOR_Y - PLAYER_H)
    {
        game->cpu.y = FLOOR_Y - PLAYER_H;
        game->cpu.vy = 0.0f;
        game->cpu.jumpHoldTimer = 0.0f;
        game->cpu.onGround = true;
    }

    playerHeadX = game->player.x + PLAYER_W * 0.5f;
    playerHeadY = game->player.y;
    cpuHeadX = game->cpu.x + PLAYER_W * 0.5f;
    cpuHeadY = game->cpu.y;
    playerNearNet = (NET_X - playerHeadX) <= BLOCK_JUMP_NET_DISTANCE;
    cpuNearNet = (cpuHeadX - NET_X) <= BLOCK_JUMP_NET_DISTANCE;

    compute_arm_pose(game->player.x, game->player.y, true, !game->player.onGround, playerNearNet, &playerShoulderX, &playerShoulderY, &playerHandX, &playerHandY);
    compute_arm_pose(game->cpu.x, game->cpu.y, false, !game->cpu.onGround, cpuNearNet, &cpuShoulderX, &cpuShoulderY, &cpuHandX, &cpuHandY);

    if (game->waitingServe)
    {
        place_ball_in_server_hand(game);
        if (game->serveCharging)
        {
            game->serveCharge += dt / SERVE_CHARGE_MAX_TIME;
            game->serveCharge = clampf(game->serveCharge, 0.0f, 1.0f);
        }
        if (!game->twoPlayerMode && game->serverSide > 0)
        {
            game->cpuServeTimer -= dt;
            if (game->cpuServeTimer <= 0.0f)
            {
                start_serve(game, 1);
                events |= EVENT_CPU_RETURN;
            }
        }
        return events;
    }

    if (!game->twoPlayerMode)
    {
        float cpuTargetX;
        float cpuMoveSpeed;
        float predictedX = 0.0f;
        float predictedLandingX = 0.0f;
        float predictedNetY = 0.0f;
        float interceptY = cpuHeadY + 18.0f;
        bool predicted = false;
        bool predictedLanding = false;
        bool predictedAtNet = false;
        bool urgentDefend;
        float tacticalOffset = 10.0f;

        if (game->ball.pos.x > NET_X - 14.0f)
        {
            predicted = predict_ball_x_at_y(&game->ball, GRAVITY, interceptY, &predictedX);
            predictedLanding = predict_ball_x_at_y(&game->ball, GRAVITY, FLOOR_Y - BALL_RADIUS - 4.0f, &predictedLandingX);
        }
        predictedAtNet = predict_ball_y_at_x(&game->ball, GRAVITY, NET_X + BALL_RADIUS + 6.0f, &predictedNetY);

        if (game->touchesRight >= 1)
        {
            tacticalOffset = 14.0f;
        }
        if (game->touchesRight >= 2)
        {
            tacticalOffset = 6.0f;
        }

        if (game->ball.pos.x > NET_X + 8.0f && predicted)
        {
            cpuTargetX = predictedX - (PLAYER_W * 0.5f) + tacticalOffset;

            if (predictedLanding && predictedLandingX > NET_X + 8.0f &&
                fabsf(predictedLandingX - cpuHeadX) < 96.0f && game->ball.vel.y > 160.0f)
            {
                /* For low emergency saves, get a bit more centered under the ball. */
                cpuTargetX = predictedX - (PLAYER_W * 0.5f) + 4.0f;
            }
        }
        else if (game->ball.pos.x > NET_X + 8.0f)
        {
            cpuTargetX = game->ball.pos.x - (PLAYER_W * 0.5f) + tacticalOffset;
        }
        else
        {
            /* Hold a smarter standby lane near net or midcourt based on rally flow. */
            cpuTargetX = (game->lastTouchSide > 0) ? (NET_X + 74.0f) : (NET_X + 122.0f);

            if (predictedAtNet && predictedNetY > NET_TOP_Y - 20.0f && predictedNetY < NET_TOP_Y + 120.0f)
            {
                cpuTargetX = NET_X + 42.0f;
            }
        }

        cpuTargetX = clampf(cpuTargetX, NET_X + 8.0f, COURT_MAX_X - PLAYER_W);
        cpuMoveSpeed = CPU_SPEED * game->cpuSpeedScale * aiFactor;
        urgentDefend = (game->ball.pos.x > NET_X + 8.0f && game->ball.vel.y > 130.0f);
        if (urgentDefend)
        {
            cpuMoveSpeed *= 1.28f;
        }

        if (game->cpu.x < cpuTargetX)
        {
            game->cpu.x += cpuMoveSpeed * dt;
            if (game->cpu.x > cpuTargetX)
            {
                game->cpu.x = cpuTargetX;
            }
        }
        else if (game->cpu.x > cpuTargetX)
        {
            game->cpu.x -= cpuMoveSpeed * dt;
            if (game->cpu.x < cpuTargetX)
            {
                game->cpu.x = cpuTargetX;
            }
        }
    }

    if (!game->twoPlayerMode)
    {
        float predictedNetY = 0.0f;
        float predictedLandingX = 0.0f;
        bool netPredicted = predict_ball_y_at_x(&game->ball, GRAVITY, NET_X + BALL_RADIUS + 4.0f, &predictedNetY);
        bool landingPredicted = predict_ball_x_at_y(&game->ball, GRAVITY, FLOOR_Y - BALL_RADIUS - 4.0f, &predictedLandingX);

        bool ballNearNetAttack =
            game->ball.vel.x < -20.0f &&
            netPredicted &&
            predictedNetY > NET_TOP_Y - (40.0f * aiFactor) &&
            predictedNetY < NET_TOP_Y + (120.0f * aiFactor);

        bool defensiveJumpWindow =
            game->ball.pos.x > NET_X + 18.0f &&
            fabsf(game->ball.pos.x - cpuHeadX) < 74.0f * aiFactor &&
            game->ball.pos.y < cpuHeadY + 20.0f &&
            game->ball.pos.y > cpuHeadY - 130.0f &&
            game->ball.vel.y > -35.0f;

        bool emergencySave =
            landingPredicted &&
            predictedLandingX > NET_X + 8.0f &&
            fabsf(predictedLandingX - cpuHeadX) < 98.0f &&
            game->ball.pos.y > FLOOR_Y - 138.0f &&
            game->ball.vel.y > 150.0f;

        shouldCpuBlock = ballNearNetAttack;
        shouldCpuJump = defensiveJumpWindow || emergencySave;
    }

    if (game->player.isBlocking)
    {
        game->player.blockTimer -= dt;
        if (game->player.blockTimer <= 0.0f)
        {
            game->player.isBlocking = false;
            game->player.blockTimer = 0.0f;
        }
    }

    if (game->cpu.isBlocking)
    {
        game->cpu.blockTimer -= dt;
        if (game->cpu.blockTimer <= 0.0f)
        {
            game->cpu.isBlocking = false;
            game->cpu.blockTimer = 0.0f;
        }
    }
    else if (shouldCpuBlock)
    {
        game->cpu.isBlocking = true;
        game->cpu.blockTimer = CPU_BLOCK_TIME * clampf(aiFactor, 0.8f, 1.35f);
    }

    if (game->cpu.onGround && (shouldCpuJump || shouldCpuBlock))
    {
        game->cpu.vy = CPU_JUMP_SPEED;
        game->cpu.jumpHoldTimer = 0.0f;
        game->cpu.onGround = false;
    }

    if (game->cpuHitCooldown > 0.0f)
    {
        game->cpuHitCooldown -= dt;
        if (game->cpuHitCooldown < 0.0f)
        {
            game->cpuHitCooldown = 0.0f;
        }
    }

    {
        float prevBallX = game->ball.pos.x;
        float prevBallY = game->ball.pos.y;
        bool leftRebound;
        bool rightRebound;

        game->ball.vel.y += GRAVITY * dt;
        game->ball.pos.x += game->ball.vel.x * dt;
        game->ball.pos.y += game->ball.vel.y * dt;

        if (game->serveOutOnMax && game->difficulty >= 2 && game->ball.pos.x - BALL_RADIUS > COURT_MAX_X)
        {
            award_point(game, game->lastTouchSide > 0, &events);
            return events;
        }

        leftRebound = should_rebound_on_back_wall(game, -1, game->ball.pos.y);
        rightRebound = should_rebound_on_back_wall(game, 1, game->ball.pos.y);

        if (game->ball.pos.x + BALL_RADIUS < COURT_MIN_X)
        {
            if (leftRebound)
            {
                game->ball.pos.x = COURT_MIN_X + BALL_RADIUS;
                game->ball.vel.x = fabsf(game->ball.vel.x) * 0.86f;
                events |= EVENT_WALL_BOUNCE;
            }
            else
            {
                award_point(game, game->lastTouchSide > 0, &events);
                return events;
            }
        }
        if (game->ball.pos.x - BALL_RADIUS > COURT_MAX_X)
        {
            if (rightRebound)
            {
                game->ball.pos.x = COURT_MAX_X - BALL_RADIUS;
                game->ball.vel.x = -fabsf(game->ball.vel.x) * 0.86f;
                events |= EVENT_WALL_BOUNCE;
            }
            else
            {
                award_point(game, game->lastTouchSide > 0, &events);
                return events;
            }
        }

        if (game->ball.pos.x - BALL_RADIUS < NET_X + 4.0f && game->ball.pos.x + BALL_RADIUS > NET_X - 4.0f && game->ball.pos.y + BALL_RADIUS > NET_TOP_Y)
        {
            if (game->ball.pos.x < NET_X)
            {
                game->ball.pos.x = NET_X - BALL_RADIUS - 4.0f;
                game->ball.vel.x = -fabsf(game->ball.vel.x) * 0.86f;
            }
            else
            {
                game->ball.pos.x = NET_X + BALL_RADIUS + 4.0f;
                game->ball.vel.x = fabsf(game->ball.vel.x) * 0.86f;
            }
            events |= EVENT_WALL_BOUNCE;
        }

        if (!game->serveOutOnMax && can_touch_ball(game))
        {
            bool playerTouched = false;
            if (reflect_ball_on_hand_zone(
                    &game->ball,
                    prevBallX,
                    prevBallY,
                    playerShoulderX,
                    playerShoulderY,
                    playerHandX,
                    playerHandY,
                    true,
                    game->player.vy,
                    game->player.onGround))
            {
                playerTouched = true;
            }
            else if (reflect_ball_on_head_zone(&game->ball, prevBallX, prevBallY, playerHeadX, playerHeadY, true, game->player.vy, game->player.onGround, true))
            {
                playerTouched = true;
            }

            if (playerTouched)
            {
                register_ball_touch(game);
                game->lastTouchSide = -1;
                game->touchesLeft += 1;
                if (game->touchesLeft > MAX_TOUCHES_PER_SIDE)
                {
                    award_point(game, false, &events);
                    return events;
                }
                events |= EVENT_PLAYER_RECEIVE;
            }
        }

        if (!game->serveOutOnMax && game->ball.pos.x > NET_X + 10.0f)
        {
            if (can_touch_ball(game))
            {
                bool cpuTouched = false;
                if (reflect_ball_on_hand_zone(
                        &game->ball,
                        prevBallX,
                        prevBallY,
                        cpuShoulderX,
                        cpuShoulderY,
                        cpuHandX,
                        cpuHandY,
                        false,
                        game->cpu.vy,
                        game->cpu.onGround))
                {
                    cpuTouched = true;
                }
                else if (reflect_ball_on_head_zone(&game->ball, prevBallX, prevBallY, cpuHeadX, cpuHeadY, false, game->cpu.vy, game->cpu.onGround, false))
                {
                    cpuTouched = true;
                }

                if (cpuTouched)
                {
                    if (game->ball.vel.x > -170.0f)
                    {
                        /* Keep CPU returns flowing toward the opponent side. */
                        game->ball.vel.x = -170.0f;
                    }

                    register_ball_touch(game);
                    game->lastTouchSide = 1;
                    game->touchesRight += 1;
                    if (game->touchesRight > MAX_TOUCHES_PER_SIDE)
                    {
                        award_point(game, true, &events);
                        return events;
                    }
                    if (game->cpuHitCooldown <= 0.0f)
                    {
                        game->cpuHitCooldown = CPU_HIT_COOLDOWN / clampf(game->cpuSpeedScale * aiFactor, 0.6f, 2.8f);
                    }
                    events |= EVENT_CPU_RETURN;
                }
            }
        }

        {
            int currentSide = game->ball.pos.x < NET_X ? -1 : 1;
            if (currentSide != game->ballSide)
            {
                game->ballSide = currentSide;
                if (currentSide < 0)
                {
                    game->touchesLeft = 0;
                }
                else
                {
                    game->touchesRight = 0;
                }
            }
        }
    }

    if (game->ball.pos.y + BALL_RADIUS >= FLOOR_Y)
    {
        if (game->ball.pos.x < NET_X)
        {
            award_point(game, false, &events);
        }
        else
        {
            award_point(game, true, &events);
        }
    }

    return events;
}

static void draw_filled_circle(SDL_Renderer *renderer, int cx, int cy, int radius)
{
    for (int dy = -radius; dy <= radius; ++dy)
    {
        int dx = (int)sqrtf((float)(radius * radius - dy * dy));
        SDL_RenderDrawLine(renderer, cx - dx, cy + dy, cx + dx, cy + dy);
    }
}

static void draw_volleyball(SDL_Renderer *renderer, int cx, int cy, int radius)
{
    int r2 = radius * radius;

    for (int y = -radius; y <= radius; ++y)
    {
        for (int x = -radius; x <= radius; ++x)
        {
            if (x * x + y * y > r2)
            {
                continue;
            }

            float xf = (float)x / (float)radius;
            float yf = (float)y / (float)radius;

            {
                float seamA = yf - (0.10f + 0.22f * sinf((xf + 0.04f) * 2.4f));
                float seamB = xf - (-0.28f + 0.14f * sinf((yf + 0.07f) * 2.5f));
                float seamC = xf - (0.27f + 0.13f * sinf((yf - 0.03f) * 2.3f));
                bool bluePanel = fabsf(seamA) < 0.15f || fabsf(seamB) < 0.13f || fabsf(seamC) < 0.12f;
                bool bayer = (((x & 1) == 0) && ((y & 1) == 0)) || (((x & 1) != 0) && ((y & 1) != 0));

                if (bluePanel)
                {
                    if (bayer)
                    {
                        SDL_SetRenderDrawColor(renderer, 62, 114, 188, 255);
                    }
                    else
                    {
                        SDL_SetRenderDrawColor(renderer, 48, 92, 160, 255);
                    }
                }
                else
                {
                    if (bayer)
                    {
                        SDL_SetRenderDrawColor(renderer, 236, 194, 108, 255);
                    }
                    else
                    {
                        SDL_SetRenderDrawColor(renderer, 210, 166, 86, 255);
                    }
                }
            }
            SDL_RenderDrawPoint(renderer, cx + x, cy + y);
        }
    }

    SDL_SetRenderDrawColor(renderer, 248, 216, 132, 255);
    for (int a = 0; a < 360; ++a)
    {
        float rad = (float)a * 3.14159265f / 180.0f;
        int px = cx + (int)((float)radius * cosf(rad));
        int py = cy + (int)((float)radius * sinf(rad));
        SDL_RenderDrawPoint(renderer, px, py);
    }
}

static const uint8_t *glyph_5x7(char c)
{
    static const uint8_t SPACE[7] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    static const uint8_t COLON[7] = {0x00, 0x04, 0x04, 0x00, 0x04, 0x04, 0x00};
    static const uint8_t UNDERSCORE[7] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F};
    static const uint8_t A[7] = {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11};
    static const uint8_t B[7] = {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E};
    static const uint8_t C[7] = {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E};
    static const uint8_t D[7] = {0x1C, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1C};
    static const uint8_t E[7] = {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F};
    static const uint8_t F[7] = {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10};
    static const uint8_t G[7] = {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0E};
    static const uint8_t H[7] = {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11};
    static const uint8_t I[7] = {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x1F};
    static const uint8_t J[7] = {0x1F, 0x02, 0x02, 0x02, 0x12, 0x12, 0x0C};
    static const uint8_t L[7] = {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F};
    static const uint8_t K[7] = {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11};
    static const uint8_t M[7] = {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11};
    static const uint8_t N[7] = {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11};
    static const uint8_t O[7] = {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E};
    static const uint8_t P[7] = {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10};
    static const uint8_t Q[7] = {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D};
    static const uint8_t R[7] = {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11};
    static const uint8_t S[7] = {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E};
    static const uint8_t T[7] = {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04};
    static const uint8_t U[7] = {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E};
    static const uint8_t V[7] = {0x11, 0x11, 0x11, 0x11, 0x0A, 0x0A, 0x04};
    static const uint8_t W[7] = {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A};
    static const uint8_t X[7] = {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11};
    static const uint8_t Y[7] = {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04};
    static const uint8_t Z[7] = {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F};
    static const uint8_t ZERO[7] = {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E};
    static const uint8_t ONE[7] = {0x04, 0x0C, 0x14, 0x04, 0x04, 0x04, 0x1F};
    static const uint8_t TWO[7] = {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F};
    static const uint8_t THREE[7] = {0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E};
    static const uint8_t FOUR[7] = {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02};
    static const uint8_t FIVE[7] = {0x1F, 0x10, 0x10, 0x1E, 0x01, 0x01, 0x1E};
    static const uint8_t SIX[7] = {0x0E, 0x10, 0x10, 0x1E, 0x11, 0x11, 0x0E};
    static const uint8_t SEVEN[7] = {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08};
    static const uint8_t EIGHT[7] = {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E};
    static const uint8_t NINE[7] = {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x01, 0x0E};

    switch (c)
    {
    case 'A':
        return A;
    case 'B':
        return B;
    case 'C':
        return C;
    case 'D':
        return D;
    case 'E':
        return E;
    case 'F':
        return F;
    case 'G':
        return G;
    case 'H':
        return H;
    case 'I':
        return I;
    case 'J':
        return J;
    case 'K':
        return K;
    case 'L':
        return L;
    case 'M':
        return M;
    case 'N':
        return N;
    case 'O':
        return O;
    case 'P':
        return P;
    case 'Q':
        return Q;
    case 'R':
        return R;
    case 'S':
        return S;
    case 'T':
        return T;
    case 'U':
        return U;
    case 'V':
        return V;
    case 'W':
        return W;
    case 'X':
        return X;
    case 'Y':
        return Y;
    case 'Z':
        return Z;
    case '0':
        return ZERO;
    case '1':
        return ONE;
    case '2':
        return TWO;
    case '3':
        return THREE;
    case '4':
        return FOUR;
    case '5':
        return FIVE;
    case '6':
        return SIX;
    case '7':
        return SEVEN;
    case '8':
        return EIGHT;
    case '9':
        return NINE;
    case ':':
        return COLON;
    case '_':
        return UNDERSCORE;
    default:
        return SPACE;
    }
}

static void draw_text_5x7(SDL_Renderer *renderer, int x, int y, const char *text, int scale, SDL_Color color)
{
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);

    for (int i = 0; text[i] != '\0'; ++i)
    {
        const uint8_t *g = glyph_5x7(text[i]);
        for (int row = 0; row < 7; ++row)
        {
            for (int col = 0; col < 5; ++col)
            {
                if ((g[row] >> (4 - col)) & 1)
                {
                    SDL_Rect px = {
                        x + i * (6 * scale) + col * scale,
                        y + row * scale,
                        scale,
                        scale};
                    SDL_RenderFillRect(renderer, &px);
                }
            }
        }
    }
}

static bool dither_keep_pixel(int x, int y, float visibility, int pixelSize)
{
    uint32_t h;
    uint32_t n;

    if (visibility >= 1.0f)
    {
        return true;
    }
    if (visibility <= 0.0f)
    {
        return false;
    }

    if (pixelSize < 1)
    {
        pixelSize = 1;
    }

    x /= pixelSize;
    y /= pixelSize;

    h = (uint32_t)(x * 73856093u) ^ (uint32_t)(y * 19349663u);
    h ^= h >> 13;
    h *= 1274126177u;
    n = h & 1023u;
    return ((float)n / 1023.0f) < visibility;
}

static void draw_dithered_fill_rect(SDL_Renderer *renderer, SDL_Rect rect, SDL_Color color, float visibility, int pixelSize)
{
    if (rect.w <= 0 || rect.h <= 0 || visibility <= 0.0f)
    {
        return;
    }

    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    for (int y = rect.y; y < rect.y + rect.h; y += pixelSize)
    {
        for (int x = rect.x; x < rect.x + rect.w; x += pixelSize)
        {
            SDL_Rect px = {x, y, pixelSize, pixelSize};
            if (!dither_keep_pixel(x, y, visibility, pixelSize))
            {
                continue;
            }
            SDL_RenderFillRect(renderer, &px);
        }
    }
}

static void draw_text_5x7_dithered(SDL_Renderer *renderer, int x, int y, const char *text, int scale, SDL_Color color, float visibility, int pixelSize)
{
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);

    for (int i = 0; text[i] != '\0'; ++i)
    {
        const uint8_t *g = glyph_5x7(text[i]);
        for (int row = 0; row < 7; ++row)
        {
            for (int col = 0; col < 5; ++col)
            {
                if (((g[row] >> (4 - col)) & 1) == 0)
                {
                    continue;
                }

                for (int sy = 0; sy < scale; sy += pixelSize)
                {
                    for (int sx = 0; sx < scale; sx += pixelSize)
                    {
                        int pxX = x + i * (6 * scale) + col * scale + sx;
                        int pxY = y + row * scale + sy;
                        SDL_Rect px = {pxX, pxY, pixelSize, pixelSize};
                        if (!dither_keep_pixel(pxX, pxY, visibility, pixelSize))
                        {
                            continue;
                        }
                        SDL_RenderFillRect(renderer, &px);
                    }
                }
            }
        }
    }
}

static void draw_digit_7seg(SDL_Renderer *renderer, int x, int y, int digit, int scale, SDL_Color color)
{
    static const uint8_t DIGIT_MASK[10] = {
        0x3F, 0x06, 0x5B, 0x4F, 0x66,
        0x6D, 0x7D, 0x07, 0x7F, 0x6F};
    int thickness = scale;
    int length = 4 * scale;
    uint8_t mask;

    if (digit < 0 || digit > 9)
    {
        return;
    }

    mask = DIGIT_MASK[digit];
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);

    if (mask & (1 << 0))
    {
        SDL_Rect seg = {x + thickness, y, length, thickness};
        SDL_RenderFillRect(renderer, &seg);
    }
    if (mask & (1 << 1))
    {
        SDL_Rect seg = {x + thickness + length, y + thickness, thickness, length};
        SDL_RenderFillRect(renderer, &seg);
    }
    if (mask & (1 << 2))
    {
        SDL_Rect seg = {x + thickness + length, y + 2 * thickness + length, thickness, length};
        SDL_RenderFillRect(renderer, &seg);
    }
    if (mask & (1 << 3))
    {
        SDL_Rect seg = {x + thickness, y + 2 * (thickness + length), length, thickness};
        SDL_RenderFillRect(renderer, &seg);
    }
    if (mask & (1 << 4))
    {
        SDL_Rect seg = {x, y + 2 * thickness + length, thickness, length};
        SDL_RenderFillRect(renderer, &seg);
    }
    if (mask & (1 << 5))
    {
        SDL_Rect seg = {x, y + thickness, thickness, length};
        SDL_RenderFillRect(renderer, &seg);
    }
    if (mask & (1 << 6))
    {
        SDL_Rect seg = {x + thickness, y + thickness + length, length, thickness};
        SDL_RenderFillRect(renderer, &seg);
    }
}

static int draw_two_digits_7seg(SDL_Renderer *renderer, int x, int y, int value, int scale, SDL_Color color)
{
    int tens;
    int ones;
    int digitWidth;

    if (value < 0)
    {
        value = 0;
    }
    if (value > 99)
    {
        value = 99;
    }

    tens = value / 10;
    ones = value % 10;
    digitWidth = 6 * scale;

    draw_digit_7seg(renderer, x, y, tens, scale, color);
    draw_digit_7seg(renderer, x + digitWidth + scale, y, ones, scale, color);
    return digitWidth * 2 + scale;
}

static void render_hud(SDL_Renderer *renderer, const Game *game)
{
    int boardW = 300;
    int boardH = 94;
    int boardX = (WINDOW_WIDTH - boardW) / 2;
    int boardY = 12;
    int midX = boardX + boardW / 2;
    int pointY = boardY + 14;
    int setY = boardY + 62;
    int pointGroupW = 52;
    int setGroupW = 26;
    int leftPointX = midX - 88;
    int rightPointX = midX + 40;
    int leftSetX = leftPointX + (pointGroupW - setGroupW) / 2;
    int rightSetX = rightPointX + (pointGroupW - setGroupW) / 2;
    SDL_Color pointColor = {246, 244, 195, 255};
    SDL_Color setColor = {171, 229, 248, 255};
    SDL_Color labelColor = {154, 198, 220, 255};

    SDL_Rect board = {boardX, boardY, boardW, boardH};
    SDL_SetRenderDrawColor(renderer, 8, 18, 28, 220);
    SDL_RenderFillRect(renderer, &board);
    SDL_SetRenderDrawColor(renderer, 62, 120, 156, 255);
    SDL_RenderDrawRect(renderer, &board);

    draw_two_digits_7seg(renderer, leftPointX, pointY, game->playerPoints, 4, pointColor);
    draw_two_digits_7seg(renderer, rightPointX, pointY, game->cpuPoints, 4, pointColor);

    SDL_SetRenderDrawColor(renderer, 154, 198, 220, 255);
    SDL_Rect dotTop = {midX - 2, boardY + 34, 4, 4};
    SDL_Rect dotBottom = {midX - 2, boardY + 46, 4, 4};
    SDL_RenderFillRect(renderer, &dotTop);
    SDL_RenderFillRect(renderer, &dotBottom);

    draw_two_digits_7seg(renderer, leftSetX, setY, game->playerSets, 2, setColor);
    draw_two_digits_7seg(renderer, rightSetX, setY, game->cpuSets, 2, setColor);
    draw_text_5x7(renderer, midX - 12, boardY + 80, "SETS", 1, labelColor);
}

static void render_scene_overlay(
    SDL_Renderer *renderer,
    Scene scene,
    float uiTimeSeconds,
    bool startTwoPlayer,
    bool audioMuted,
    int startDifficulty,
    const HighscoreEntry *highscores,
    int highscoreCount,
    const char *nameInput,
    int nameSetsFor,
    int nameSetsAgainst,
    int namePointsFor,
    int namePointsAgainst)
{
    if (scene == SCENE_PLAYING)
    {
        return;
    }

    SDL_Rect box = {200, 182, 560, 280};
    if (scene == SCENE_START)
    {
        box.x = 96;
        box.y = 238;
    }
    SDL_SetRenderDrawColor(renderer, 6, 12, 20, 230);
    SDL_RenderFillRect(renderer, &box);
    SDL_SetRenderDrawColor(renderer, 71, 132, 163, 255);
    SDL_RenderDrawRect(renderer, &box);

    SDL_Color title = {245, 230, 165, 255};
    SDL_Color text = {176, 214, 230, 255};

    if (scene == SCENE_START)
    {
        bool blink = fmodf(uiTimeSeconds, 1.0f) < 0.58f;
        const char *difficultyLabel = "NORMAL";

        if (startDifficulty <= 0)
        {
            difficultyLabel = "EASY";
        }
        else if (startDifficulty >= 2)
        {
            difficultyLabel = "HARD";
        }

        SDL_SetRenderDrawColor(renderer, 54, 96, 124, 255);
        SDL_RenderDrawLine(renderer, box.x + 28, box.y + 92, box.x + 212, box.y + 92);

        draw_text_5x7(renderer, box.x + 22, box.y + 42, "START", 4, title);
        if (blink)
        {
            draw_text_5x7(renderer, box.x + 78, box.y + 112, "PRESS ENTER", 2, text);
        }
        if (startTwoPlayer)
        {
            draw_text_5x7(renderer, box.x + 78, box.y + 136, "PLAYER TWO", 2, text);
        }
        else
        {
            draw_text_5x7(renderer, box.x + 78, box.y + 136, "PLAYER ONE", 2, text);
        }
        draw_text_5x7(renderer, box.x + 102, box.y + 152, "UP DOWN", 1, text);

        draw_text_5x7(renderer, box.x + 78, box.y + 164, "DIFF", 2, text);
        draw_text_5x7(renderer, box.x + 156, box.y + 164, difficultyLabel, 2, text);
        draw_text_5x7(renderer, box.x + 92, box.y + 180, "LEFT RIGHT", 1, text);

        if (audioMuted)
        {
            draw_text_5x7(renderer, box.x + 78, box.y + 192, "SOUND MUTE", 2, text);
        }
        else
        {
            draw_text_5x7(renderer, box.x + 78, box.y + 192, "SOUND LIVE", 2, text);
        }
        draw_text_5x7(renderer, box.x + 102, box.y + 208, "SPACE", 1, text);
        draw_text_5x7(renderer, box.x + 78, box.y + 220, "ENTER PLAY  ESC", 1, text);

        if (!startTwoPlayer)
        {
            draw_text_5x7(renderer, box.x + 336, box.y + 134, "HIGHSCORES", 1, text);
            for (int i = 0; i < highscoreCount && i < MAX_HIGHSCORES; ++i)
            {
                char line[64];
                snprintf(
                    line,
                    sizeof(line),
                    "%d %.10s S%d:%d P%d:%d",
                    i + 1,
                    highscores[i].name,
                    highscores[i].setsFor,
                    highscores[i].setsAgainst,
                    highscores[i].pointsFor,
                    highscores[i].pointsAgainst);
                draw_text_5x7(renderer, box.x + 336, box.y + 146 + i * 12, line, 1, text);
            }
        }
    }
    else if (scene == SCENE_PAUSED)
    {
        draw_text_5x7(renderer, box.x + 126, box.y + 42, "PAUSE", 4, title);
        draw_text_5x7(renderer, box.x + 84, box.y + 122, "P OR ENTER TO RESUME", 2, text);
        draw_text_5x7(renderer, box.x + 88, box.y + 162, "W BLOCK  R RESET", 1, text);
    }
    else if (scene == SCENE_GAME_OVER)
    {
        SDL_SetRenderDrawColor(renderer, 128, 48, 58, 255);
        SDL_RenderDrawRect(renderer, &box);
        draw_text_5x7(renderer, box.x + 74, box.y + 42, "GAME OVER", 4, title);
        draw_text_5x7(renderer, box.x + 84, box.y + 122, "PRESS ENTER RETRY", 2, text);
        draw_text_5x7(renderer, box.x + 84, box.y + 162, "R TO TITLE  M SOUND", 1, text);
    }
    else if (scene == SCENE_NAME_ENTRY)
    {
        char scoreLine[32];
        char nameLine[48];
        bool blink = fmodf(uiTimeSeconds, 1.0f) < 0.58f;

        snprintf(
            scoreLine,
            sizeof(scoreLine),
            "SETS %d:%d POINTS %d:%d",
            nameSetsFor,
            nameSetsAgainst,
            namePointsFor,
            namePointsAgainst);
        if (blink)
        {
            snprintf(nameLine, sizeof(nameLine), "NAME %s_", nameInput);
        }
        else
        {
            snprintf(nameLine, sizeof(nameLine), "NAME %s", nameInput);
        }

        draw_text_5x7(renderer, box.x + 66, box.y + 42, "NEW HIGHSCORE", 3, title);
        draw_text_5x7(renderer, box.x + 88, box.y + 122, scoreLine, 2, text);
        draw_text_5x7(renderer, box.x + 88, box.y + 154, nameLine, 2, text);
        draw_text_5x7(renderer, box.x + 90, box.y + 186, "TYPE NAME ENTER SAVE", 1, text);
        draw_text_5x7(renderer, box.x + 90, box.y + 202, "BACKSPACE DELETE", 1, text);
        draw_text_5x7(renderer, box.x + 90, box.y + 218, "ESC CANCEL", 1, text);
    }
}

static void render_game(
    SDL_Renderer *renderer,
    const Game *game,
    Scene scene,
    float uiTimeSeconds,
    bool startTwoPlayer,
    bool audioMuted,
    int startDifficulty,
    const HighscoreEntry *highscores,
    int highscoreCount,
    const char *nameInput,
    int nameSetsFor,
    int nameSetsAgainst,
    int namePointsFor,
    int namePointsAgainst)
{
    SDL_SetRenderDrawColor(renderer, 14, 25, 38, 255);
    SDL_RenderClear(renderer);

    SDL_Rect court = {(int)COURT_MIN_X, 70, (int)(COURT_MAX_X - COURT_MIN_X), (int)(FLOOR_Y - 70.0f)};
    SDL_SetRenderDrawColor(renderer, 21, 48, 70, 255);
    SDL_RenderFillRect(renderer, &court);

    if (scene != SCENE_PLAYING || game->startBannerFade > 0.0f)
    {
        SDL_Rect banner = {(WINDOW_WIDTH - 520) / 2, 164, 520, 58};
        int tipW = 24;
        int midY = banner.y + banner.h / 2;
        float fade = 1.0f;
        float visibility = 1.0f;
        int fadePixelSize = 2;
        const char *bannerLabel = "VOLLEY ARCADE";
        int textScale = 3;
        int textWidth = ((int)strlen(bannerLabel) * 6 - 1) * textScale;
        int textHeight = 7 * textScale;
        int textX = banner.x + (banner.w - textWidth) / 2;
        int textY = banner.y + (banner.h - textHeight) / 2;
        SDL_Color bannerText;
        SDL_Color bannerShadow;
        SDL_Color bannerBase = {24, 86, 122, 255};
        SDL_Color wallColor = {21, 48, 70, 255};
        SDL_Color borderColor = {236, 196, 106, 255};

        if (scene == SCENE_PLAYING)
        {
            fade = clampf(game->startBannerFade / START_BANNER_FADE_TIME, 0.0f, 1.0f);
            visibility = fade;
        }

        bannerText = (SDL_Color){250, 238, 170, 255};
        bannerShadow = (SDL_Color){24, 44, 66, 255};

        draw_dithered_fill_rect(renderer, banner, bannerBase, visibility, fadePixelSize);

        /* Cut inward arrow notches so the tips point into the banner. */
        SDL_SetRenderDrawColor(renderer, wallColor.r, wallColor.g, wallColor.b, wallColor.a);
        for (int y = 0; y < banner.h; ++y)
        {
            int dy = abs(y - banner.h / 2);
            int depth = tipW - (dy * tipW) / (banner.h / 2);
            int leftEnd = banner.x + depth;
            int rightStart = banner.x + banner.w - 1 - depth;

            SDL_RenderDrawLine(renderer, banner.x, banner.y + y, leftEnd, banner.y + y);
            SDL_RenderDrawLine(renderer, rightStart, banner.y + y, banner.x + banner.w - 1, banner.y + y);
        }

        draw_dithered_fill_rect(renderer, (SDL_Rect){banner.x, banner.y, banner.w, 1}, borderColor, visibility, fadePixelSize);
        draw_dithered_fill_rect(renderer, (SDL_Rect){banner.x, banner.y + banner.h - 1, banner.w, 1}, borderColor, visibility, fadePixelSize);
        for (int i = 0; i <= tipW; ++i)
        {
            int yA = banner.y + (i * (midY - banner.y)) / tipW;
            int yB = banner.y + banner.h - 1 - (i * (banner.y + banner.h - 1 - midY)) / tipW;
            draw_dithered_fill_rect(renderer, (SDL_Rect){banner.x + i, yA, 1, 1}, borderColor, visibility, fadePixelSize);
            draw_dithered_fill_rect(renderer, (SDL_Rect){banner.x + i, yB, 1, 1}, borderColor, visibility, fadePixelSize);
            draw_dithered_fill_rect(renderer, (SDL_Rect){banner.x + banner.w - 1 - i, yA, 1, 1}, borderColor, visibility, fadePixelSize);
            draw_dithered_fill_rect(renderer, (SDL_Rect){banner.x + banner.w - 1 - i, yB, 1, 1}, borderColor, visibility, fadePixelSize);
        }

        draw_text_5x7_dithered(renderer, textX + 1, textY + 1, bannerLabel, textScale, bannerShadow, visibility, fadePixelSize);
        draw_text_5x7_dithered(renderer, textX, textY, bannerLabel, textScale, bannerText, visibility, fadePixelSize);
    }

    SDL_SetRenderDrawColor(renderer, 224, 196, 122, 255);
    SDL_RenderDrawLine(renderer, (int)COURT_MIN_X, (int)FLOOR_Y - 1, (int)COURT_MAX_X, (int)FLOOR_Y - 1);
    SDL_RenderDrawLine(renderer, (int)COURT_MIN_X, (int)FLOOR_Y, (int)COURT_MAX_X, (int)FLOOR_Y);
    SDL_RenderDrawLine(renderer, (int)COURT_MIN_X, (int)FLOOR_Y + 1, (int)COURT_MAX_X, (int)FLOOR_Y + 1);

    {
        int leftPoleX = (int)NET_X - 14;
        int rightPoleX = (int)NET_X + 24;
        int meshLeftTopX = leftPoleX + 2;
        int meshLeftTopY = (int)NET_TOP_Y + 8;
        int meshRightTopX = rightPoleX - 2;
        int meshRightTopY = (int)NET_TOP_Y + 14;

        int meshLeftBottomX = leftPoleX + 8;
        int meshLeftBottomY = meshLeftTopY + 64;
        int meshRightBottomX = rightPoleX - 6;
        int meshRightBottomY = meshRightTopY + 68;

        int leftTopY = meshLeftTopY - 8;
        int rightTopY = meshRightTopY - 8;
        int leftBottomY = (int)FLOOR_Y;
        int rightBottomY = (int)FLOOR_Y;

        SDL_SetRenderDrawColor(renderer, 176, 176, 176, 255);
        SDL_Rect leftPole = {leftPoleX - 1, leftTopY, 3, leftBottomY - leftTopY + 1};
        SDL_Rect rightPole = {rightPoleX - 1, rightTopY, 3, rightBottomY - rightTopY + 1};
        SDL_RenderFillRect(renderer, &leftPole);
        SDL_RenderFillRect(renderer, &rightPole);

        SDL_SetRenderDrawColor(renderer, 245, 245, 245, 255);
        SDL_RenderDrawLine(renderer, meshLeftTopX, meshLeftTopY, meshRightTopX, meshRightTopY);
        SDL_RenderDrawLine(renderer, meshLeftTopX, meshLeftTopY + 1, meshRightTopX, meshRightTopY + 1);
        SDL_RenderDrawLine(renderer, meshLeftTopX, meshLeftTopY + 2, meshRightTopX, meshRightTopY + 2);
        SDL_RenderDrawLine(renderer, meshLeftBottomX, meshLeftBottomY, meshRightBottomX, meshRightBottomY);
        SDL_RenderDrawLine(renderer, meshLeftBottomX, meshLeftBottomY - 1, meshRightBottomX, meshRightBottomY - 1);
        SDL_RenderDrawLine(renderer, meshLeftBottomX, meshLeftBottomY - 2, meshRightBottomX, meshRightBottomY - 2);

        SDL_SetRenderDrawColor(renderer, 48, 48, 48, 255);
        for (int i = 0; i <= 8; ++i)
        {
            float t = (float)i / 8.0f;
            int xTop = (int)((1.0f - t) * (float)meshLeftTopX + t * (float)meshRightTopX);
            int yTop = (int)((1.0f - t) * (float)meshLeftTopY + t * (float)meshRightTopY);
            int xBottom = (int)((1.0f - t) * (float)meshLeftBottomX + t * (float)meshRightBottomX);
            int yBottom = (int)((1.0f - t) * (float)meshLeftBottomY + t * (float)meshRightBottomY);
            SDL_RenderDrawLine(renderer, xTop, yTop, xBottom, yBottom);
        }

        for (int j = 0; j <= 6; ++j)
        {
            float t = (float)j / 6.0f;
            int xLeft = (int)((1.0f - t) * (float)meshLeftTopX + t * (float)meshLeftBottomX);
            int yLeft = (int)((1.0f - t) * (float)meshLeftTopY + t * (float)meshLeftBottomY);
            int xRight = (int)((1.0f - t) * (float)meshRightTopX + t * (float)meshRightBottomX);
            int yRight = (int)((1.0f - t) * (float)meshRightTopY + t * (float)meshRightBottomY);
            SDL_RenderDrawLine(renderer, xLeft, yLeft, xRight, yRight);
        }
    }

    SDL_Rect player = {(int)game->player.x, (int)game->player.y, (int)PLAYER_W, (int)PLAYER_H};
    float playerShoulderX;
    float playerShoulderY;
    float playerHandX;
    float playerHandY;
    bool playerNearNet;
    float cpuShoulderX;
    float cpuShoulderY;
    float cpuHandX;
    float cpuHandY;
    bool cpuNearNet;
    bool playerArmInFront;
    bool cpuArmInFront;
    SDL_Color playerBodyColor;
    SDL_Color cpuBodyColor;
    SDL_Color handColor = {255, 188, 110, 255};

    if (game->player.isBlocking)
    {
        playerBodyColor = (SDL_Color){255, 206, 84, 255};
        SDL_SetRenderDrawColor(renderer, 255, 206, 84, 255);
    }
    else
    {
        playerBodyColor = (SDL_Color){228, 96, 132, 255};
        SDL_SetRenderDrawColor(renderer, 228, 96, 132, 255);
    }
    SDL_RenderFillRect(renderer, &player);

    SDL_Rect cpu = {(int)game->cpu.x, (int)game->cpu.y, (int)PLAYER_W, (int)PLAYER_H};
    if (game->cpu.isBlocking)
    {
        cpuBodyColor = (SDL_Color){255, 206, 84, 255};
        SDL_SetRenderDrawColor(renderer, 255, 206, 84, 255);
    }
    else
    {
        cpuBodyColor = (SDL_Color){130, 240, 152, 255};
        SDL_SetRenderDrawColor(renderer, 130, 240, 152, 255);
    }
    SDL_RenderFillRect(renderer, &cpu);

    playerNearNet = (NET_X - (game->player.x + PLAYER_W * 0.5f)) <= BLOCK_JUMP_NET_DISTANCE;
    cpuNearNet = ((game->cpu.x + PLAYER_W * 0.5f) - NET_X) <= BLOCK_JUMP_NET_DISTANCE;

    compute_arm_pose(game->player.x, game->player.y, true, !game->player.onGround, playerNearNet, &playerShoulderX, &playerShoulderY, &playerHandX, &playerHandY);
    compute_arm_pose(game->cpu.x, game->cpu.y, false, !game->cpu.onGround, cpuNearNet, &cpuShoulderX, &cpuShoulderY, &cpuHandX, &cpuHandY);
    playerArmInFront = !game->player.onGround;
    cpuArmInFront = !game->cpu.onGround;

    if (!playerArmInFront)
    {
        draw_arm_with_hand(renderer, playerShoulderX, playerShoulderY, playerHandX, playerHandY, true, playerBodyColor, handColor);
    }
    if (!cpuArmInFront)
    {
        draw_arm_with_hand(renderer, cpuShoulderX, cpuShoulderY, cpuHandX, cpuHandY, false, cpuBodyColor, handColor);
    }

    SDL_SetRenderDrawColor(renderer, 255, 188, 110, 255);
    draw_filled_circle(
        renderer,
        (int)(game->player.x + PLAYER_W * 0.5f),
        (int)game->player.y,
        (int)HEAD_RADIUS);
    draw_filled_circle(
        renderer,
        (int)(game->cpu.x + PLAYER_W * 0.5f),
        (int)game->cpu.y,
        (int)HEAD_RADIUS);

    if (playerArmInFront)
    {
        draw_arm_with_hand(renderer, playerShoulderX, playerShoulderY, playerHandX, playerHandY, true, playerBodyColor, handColor);
    }
    if (cpuArmInFront)
    {
        draw_arm_with_hand(renderer, cpuShoulderX, cpuShoulderY, cpuHandX, cpuHandY, false, cpuBodyColor, handColor);
    }

    if (game->waitingServe && (game->serverSide < 0 || game->twoPlayerMode))
    {
        int bx;
        int by;
        int bw = 56;
        int bh = 8;
        int fill = (int)((float)(bw - 2) * game->serveCharge);
        SDL_Rect bg;
        SDL_Rect fg;

        if (game->serverSide < 0)
        {
            bx = (int)(game->player.x + PLAYER_W * 0.5f) - 28;
            by = (int)(game->player.y - HEAD_RADIUS - BALL_RADIUS - 20.0f);
        }
        else
        {
            bx = (int)(game->cpu.x + PLAYER_W * 0.5f) - 28;
            by = (int)(game->cpu.y - HEAD_RADIUS - BALL_RADIUS - 20.0f);
        }

        bg.x = bx;
        bg.y = by;
        bg.w = bw;
        bg.h = bh;
        fg.x = bx + 1;
        fg.y = by + 1;
        fg.w = fill;
        fg.h = bh - 2;

        SDL_SetRenderDrawColor(renderer, 20, 32, 44, 255);
        SDL_RenderFillRect(renderer, &bg);

        if (game->serveCharge >= 0.999f && (fmodf(uiTimeSeconds * 10.0f, 2.0f) < 1.0f))
        {
            SDL_SetRenderDrawColor(renderer, 240, 96, 82, 255);
        }
        else
        {
            SDL_SetRenderDrawColor(renderer, 86, 214, 176, 255);
        }
        SDL_RenderFillRect(renderer, &fg);
        SDL_SetRenderDrawColor(renderer, 145, 189, 212, 255);
        SDL_RenderDrawRect(renderer, &bg);
    }

    render_hud(renderer, game);
    draw_volleyball(renderer, (int)game->ball.pos.x, (int)game->ball.pos.y, (int)BALL_RADIUS);
    render_scene_overlay(
        renderer,
        scene,
        uiTimeSeconds,
        startTwoPlayer,
        audioMuted,
        startDifficulty,
        highscores,
        highscoreCount,
        nameInput,
        nameSetsFor,
        nameSetsAgainst,
        namePointsFor,
        namePointsAgainst);

    SDL_RenderPresent(renderer);
}

int main(void)
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_AUDIO) != 0)
    {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    Audio audio = {0};
    {
        SDL_AudioSpec wanted;
        SDL_zero(wanted);
        wanted.freq = AUDIO_SAMPLE_RATE;
        wanted.format = AUDIO_S16SYS;
        wanted.channels = 1;
        wanted.samples = 512;
        wanted.callback = NULL;

        audio.device = SDL_OpenAudioDevice(NULL, 0, &wanted, &audio.spec, 0);
        if (audio.device != 0)
        {
            SDL_PauseAudioDevice(audio.device, 0);
        }
    }

    SDL_Window *window = SDL_CreateWindow(
        "Volley-Arcade",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        WINDOW_WIDTH,
        WINDOW_HEIGHT,
        SDL_WINDOW_SHOWN);
    if (!window)
    {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer)
    {
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    }
    if (!renderer)
    {
        fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        if (audio.device != 0)
        {
            SDL_CloseAudioDevice(audio.device);
        }
        SDL_Quit();
        return 1;
    }

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    bool startTwoPlayer = false;
    int startDifficulty = 1;
    HighscoreEntry highscores[MAX_HIGHSCORES];
    int highscoreCount = load_highscores(highscores, MAX_HIGHSCORES);
    char lastPlayerName[HIGHSCORE_NAME_LEN + 1] = "PLAYER";
    char nameInput[HIGHSCORE_NAME_LEN + 1] = "PLAYER";
    int nameInputLen = 6;
    int nameSetsFor = 0;
    int nameSetsAgainst = 0;
    int namePointsFor = 0;
    int namePointsAgainst = 0;
    Game game;
    init_game(&game, startTwoPlayer, startDifficulty);
    Scene scene = SCENE_START;

    bool running = true;
    Uint64 lastCounter = SDL_GetPerformanceCounter();
    float accumulator = 0.0f;
    float uiTimeSeconds = 0.0f;

    while (running)
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_QUIT)
            {
                running = false;
            }
            if (event.type == SDL_KEYDOWN)
            {
                SDL_Scancode sc = event.key.keysym.scancode;

                if (scene == SCENE_NAME_ENTRY)
                {
                    if (sc == SDL_SCANCODE_BACKSPACE)
                    {
                        if (nameInputLen > 0)
                        {
                            nameInputLen -= 1;
                            nameInput[nameInputLen] = '\0';
                        }
                        continue;
                    }
                    if (sc == SDL_SCANCODE_RETURN || sc == SDL_SCANCODE_KP_ENTER)
                    {
                        if (nameInputLen == 0)
                        {
                            snprintf(nameInput, sizeof(nameInput), "%s", lastPlayerName);
                            nameInputLen = (int)strlen(nameInput);
                        }
                        snprintf(lastPlayerName, sizeof(lastPlayerName), "%s", nameInput);
                        insert_highscore(
                            highscores,
                            &highscoreCount,
                            nameInput,
                            nameSetsFor,
                            nameSetsAgainst,
                            namePointsFor,
                            namePointsAgainst);
                        save_highscores(highscores, highscoreCount);
                        scene = SCENE_START;
                        continue;
                    }
                    if (sc == SDL_SCANCODE_ESCAPE)
                    {
                        scene = SCENE_START;
                        continue;
                    }

                    {
                        char ch = scancode_to_name_char(sc);
                        if (ch != '\0' && nameInputLen < HIGHSCORE_NAME_LEN)
                        {
                            nameInput[nameInputLen++] = ch;
                            nameInput[nameInputLen] = '\0';
                        }
                    }
                    continue;
                }

                if (scene == SCENE_START)
                {
                    if (sc == SDL_SCANCODE_UP)
                    {
                        startTwoPlayer = false;
                    }
                    if (sc == SDL_SCANCODE_DOWN)
                    {
                        startTwoPlayer = true;
                    }
                    if (sc == SDL_SCANCODE_LEFT)
                    {
                        if (startDifficulty > 0)
                        {
                            startDifficulty -= 1;
                        }
                    }
                    if (sc == SDL_SCANCODE_RIGHT)
                    {
                        if (startDifficulty < 2)
                        {
                            startDifficulty += 1;
                        }
                    }
                    if (sc == SDL_SCANCODE_SPACE)
                    {
                        audio.muted = !audio.muted;
                        if (audio.muted && audio.device != 0)
                        {
                            SDL_ClearQueuedAudio(audio.device);
                        }
                    }
                }

                if ((sc == SDL_SCANCODE_W || (!game.twoPlayerMode && (sc == SDL_SCANCODE_UP || sc == SDL_SCANCODE_SPACE))) && scene == SCENE_PLAYING)
                {
                    if (game.player.onGround)
                    {
                        game.player.vy = PLAYER_JUMP_SPEED;
                        game.player.jumpHoldTimer = PLAYER_JUMP_HOLD_TIME;
                        game.player.onGround = false;
                    }
                    if (!game.waitingServe)
                    {
                        game.player.isBlocking = true;
                        game.player.blockTimer = game.blockWindow;
                    }
                }

                if (sc == SDL_SCANCODE_UP && scene == SCENE_PLAYING && game.twoPlayerMode)
                {
                    if (game.cpu.onGround)
                    {
                        game.cpu.vy = PLAYER_JUMP_SPEED;
                        game.cpu.jumpHoldTimer = PLAYER_JUMP_HOLD_TIME;
                        game.cpu.onGround = false;
                    }
                    if (!game.waitingServe)
                    {
                        game.cpu.isBlocking = true;
                        game.cpu.blockTimer = game.blockWindow;
                    }
                }

                if ((sc == SDL_SCANCODE_LCTRL || (!game.twoPlayerMode && sc == SDL_SCANCODE_RCTRL)) && scene == SCENE_PLAYING)
                {
                    if (game.waitingServe && game.serverSide < 0)
                    {
                        game.serveCharging = true;
                    }
                }

                if ((sc == SDL_SCANCODE_RALT || sc == SDL_SCANCODE_MODE) && scene == SCENE_PLAYING && game.twoPlayerMode)
                {
                    if (game.waitingServe && game.serverSide > 0)
                    {
                        game.serveCharging = true;
                    }
                }

                if (sc == SDL_SCANCODE_R)
                {
                    init_game(&game, startTwoPlayer, startDifficulty);
                    scene = SCENE_START;
                }

                if (sc == SDL_SCANCODE_M)
                {
                    audio.muted = !audio.muted;
                    if (audio.muted && audio.device != 0)
                    {
                        SDL_ClearQueuedAudio(audio.device);
                    }
                }

                if (sc == SDL_SCANCODE_P)
                {
                    if (scene == SCENE_PLAYING)
                    {
                        scene = SCENE_PAUSED;
                    }
                    else if (scene == SCENE_PAUSED)
                    {
                        scene = SCENE_PLAYING;
                    }
                }

                if (sc == SDL_SCANCODE_RETURN || sc == SDL_SCANCODE_KP_ENTER)
                {
                    if (scene == SCENE_START)
                    {
                        init_game(&game, startTwoPlayer, startDifficulty);
                        scene = SCENE_PLAYING;
                    }
                    else if (scene == SCENE_PAUSED)
                    {
                        scene = SCENE_PLAYING;
                    }
                    else if (scene == SCENE_GAME_OVER)
                    {
                        init_game(&game, startTwoPlayer, startDifficulty);
                        scene = SCENE_PLAYING;
                    }
                }

                if (sc == SDL_SCANCODE_ESCAPE)
                {
                    if (scene == SCENE_PLAYING && !game.twoPlayerMode)
                    {
                        scene = SCENE_NAME_ENTRY;
                        nameSetsFor = game.playerSets;
                        nameSetsAgainst = game.cpuSets;
                        namePointsFor = game.playerPoints;
                        namePointsAgainst = game.cpuPoints;
                        snprintf(nameInput, sizeof(nameInput), "%s", lastPlayerName);
                        nameInputLen = (int)strlen(nameInput);
                    }
                    else if (scene == SCENE_START)
                    {
                        running = false;
                    }
                    else
                    {
                        scene = SCENE_START;
                    }
                }
            }

            if (event.type == SDL_KEYUP)
            {
                SDL_Scancode sc = event.key.keysym.scancode;
                if (sc == SDL_SCANCODE_W || (!game.twoPlayerMode && (sc == SDL_SCANCODE_UP || sc == SDL_SCANCODE_SPACE)))
                {
                    game.player.jumpHoldTimer = 0.0f;
                }
                if (game.twoPlayerMode && sc == SDL_SCANCODE_UP)
                {
                    game.cpu.jumpHoldTimer = 0.0f;
                }
                if ((sc == SDL_SCANCODE_LCTRL || (!game.twoPlayerMode && sc == SDL_SCANCODE_RCTRL)) && scene == SCENE_PLAYING)
                {
                    if (game.waitingServe && game.serverSide < 0 && game.serveCharging)
                    {
                        start_human_charged_serve(&game, -1);
                    }
                }
                if ((sc == SDL_SCANCODE_RALT || sc == SDL_SCANCODE_MODE) && scene == SCENE_PLAYING && game.twoPlayerMode)
                {
                    if (game.waitingServe && game.serverSide > 0 && game.serveCharging)
                    {
                        start_human_charged_serve(&game, 1);
                    }
                }
            }
        }

        Uint64 currentCounter = SDL_GetPerformanceCounter();
        float frameTime = (float)(currentCounter - lastCounter) / (float)SDL_GetPerformanceFrequency();
        if (frameTime > MAX_ACCUMULATED_TIME)
        {
            frameTime = MAX_ACCUMULATED_TIME;
        }
        lastCounter = currentCounter;
        accumulator += frameTime;
        uiTimeSeconds += frameTime;

        const Uint8 *keys = SDL_GetKeyboardState(NULL);

        while (accumulator >= FIXED_DT)
        {
            if (scene == SCENE_PLAYING)
            {
                unsigned events = update_game(&game, FIXED_DT, keys);
                play_events(&audio, events);
                if ((events & EVENT_GAME_OVER) && !game.twoPlayerMode)
                {
                    scene = SCENE_NAME_ENTRY;
                    nameSetsFor = game.playerSets;
                    nameSetsAgainst = game.cpuSets;
                    namePointsFor = game.playerPoints;
                    namePointsAgainst = game.cpuPoints;
                    snprintf(nameInput, sizeof(nameInput), "%s", lastPlayerName);
                    nameInputLen = (int)strlen(nameInput);
                    break;
                }
            }
            else
            {
                accumulator = 0.0f;
                break;
            }
            accumulator -= FIXED_DT;
        }

        render_game(
            renderer,
            &game,
            scene,
            uiTimeSeconds,
            startTwoPlayer,
            audio.muted,
            startDifficulty,
            highscores,
            highscoreCount,
            nameInput,
            nameSetsFor,
            nameSetsAgainst,
            namePointsFor,
            namePointsAgainst);

        SDL_SetWindowTitle(window, "Volley-Arcade");
    }

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    if (audio.device != 0)
    {
        SDL_ClearQueuedAudio(audio.device);
        SDL_CloseAudioDevice(audio.device);
    }
    SDL_Quit();
    return 0;
}
